/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** joao.lima@inf.ufsm.br
**
** This software is a computer program whose purpose is to execute
** blas subroutines on multi-GPUs system.
**
** This software is governed by the CeCILL-C license under French law and
** abiding by the rules of distribution of free software.  You can  use,
** modify and/ or redistribute the software under the terms of the CeCILL-C
** license as circulated by CEA, CNRS and INRIA at the following URL
** "http://www.cecill.info".

** As a counterpart to the access to the source code and  rights to copy,
** modify and redistribute granted by the license, users are provided only
** with a limited warranty  and the software's author,  the holder of the
** economic rights,  and the successive licensors  have only  limited
** liability.

** In this respect, the user's attention is drawn to the risks associated
** with loading,  using,  modifying and/or developing or reproducing the
** software by the user in light of its specific status of free software,
** that may mean  that it is complicated to manipulate,  and  that  also
** therefore means  that it is reserved for developers  and  experienced
** professionals having in-depth computer knowledge. Users are therefore
** encouraged to load and test the software's suitability as regards their
** requirements in conditions enabling the security of their systems and/or
** data to be ensured and,  more generally, to use and operate it in the
** same conditions as regards security.

** The fact that you are presently reading this means that you have had
** knowledge of the CeCILL-C license and that you accept its terms.
**/

#ifndef _KAAPI_MEMORY_H
#define _KAAPI_MEMORY_H 1

#include <stdbool.h>
    
//#define _MEMORY_DEBUG   1

/* ============================ Address space ================================= */

/* Representation of address space id over 64bits integer
   - 16bits: local identifier
   - 32bits: ?
   - 12bits: reserved
   - 4bits : 16 values for coding architecture
   On a multicore, each address space has a local identifier. The global identifier
   was used to identified process in the same way as MPI rank.
*/
#define KAAPI_ASID_MASK_LID       0x000000000000FFFFULL /* size = 16 */
#define KAAPI_ASID_SHIFT_LID      0UL                   /* shift = 0 */
#define KAAPI_ASID_MASK_GID       0x0000FFFFFFFF0000ULL /* size = 32 */
#define KAAPI_ASID_SHIFT_GID      16UL                  /* shift = 16 */
#define KAAPI_ASID_MASK_RESERVED  0x0FFF000000000000ULL /* size = 12 */
#define KAAPI_ASID_MASK_ARCH      0xF000000000000000ULL /* size = 4 */
#define KAAPI_ASID_SHIFT_ARCH     60UL                  /* shift = 0 */

static inline kaapi_address_space_id_t kaapi_memory_create_asid(
    kaapi_globalid_t gid,
    uint16_t lid,
    uint8_t arch
)
{
  kaapi_address_space_id_t asid = (((uint64_t)lid) & KAAPI_ASID_MASK_LID)
      | ((((uint64_t)gid) << KAAPI_ASID_SHIFT_GID) & KAAPI_ASID_MASK_GID)
      | ((((uint64_t)arch) << KAAPI_ASID_SHIFT_ARCH) & KAAPI_ASID_MASK_ARCH);
  return asid;
}

static inline kaapi_globalid_t kaapi_memory_get_current_globalid(void)
{ return 0;}

/** Return the gid of the address space
    \param kasid [IN] an address space identifier
    \return the gid encoded into the address space identifier
*/
static inline kaapi_globalid_t kaapi_memory_asid_get_gid( kaapi_address_space_id_t kasid )
{ return (kaapi_globalid_t)((kasid & KAAPI_ASID_MASK_GID)>> KAAPI_ASID_SHIFT_GID); }

/** Return the arch type of the address space location.
    \param kasid [IN] an address space identifier
    \return the type encoded into the address space identifier
*/
static inline uint8_t kaapi_memory_asid_get_arch( kaapi_address_space_id_t kasid )
{ return (uint8_t)((kasid & KAAPI_ASID_MASK_ARCH) >> KAAPI_ASID_SHIFT_ARCH); }

/** Return the local identifier of the address space location.
    \param kasid [IN] an address space identifier
    \return the local id encoded into the address space identifier
*/
static inline uint16_t kaapi_memory_asid_get_lid( kaapi_address_space_id_t kasid )
{ return (uint16_t)((kasid & KAAPI_ASID_MASK_LID) >> KAAPI_ASID_SHIFT_LID); }



/* ====================== Management of replicated data ============================== */

/* callback for replica
   By default one callback is stored in the replica data structure. Further callbacks may
   have to be dynamically allocated and link together.
*/
typedef struct kaapi_data_replica_cbk {
    struct kaapi_io_cbk            iocbk;      /* called on completion of async comm */
#if KAAPI_DEBUG
    int                            idx;
#endif
    struct kaapi_data_replica_cbk* next;       /* chained if required */
} kaapi_data_replica_cbk_t;

/* one replica
   - pinned is a counter that allows 255 reservations of the data before allowing it
   to be evicted from the device memory.
   Each replicat is part of LRU cache with respect to the device.
   - lock procted update between different driver threads of different GPUs.
*/
typedef struct kaapi_data_replica {
    kaapi_lock_t             lock;
    kaapi_atomic8_t          pinned;       /* counter: cannot be evicted if>0 */
    kaapi_pointer_t          ptr;
    kaapi_memory_view_t      view;
    int                      count;        /* debug, length of cbk */
    kaapi_data_replica_cbk_t cbk;
    void*                    cachelist;
    void*                    cacheentry;
#if KAAPI_DEBUG
    kaapi_thread_t*          thread;       /* thread putting replica */
#endif
} kaapi_data_replica_t;
    
/* Metadata information for each address
   - replicas[lid] == information about replica of data on address space lid (locality domain id)
   - alloc[lid] == 1 iff data allocated (and replica[lid].ptr != 0)
   - valid[lid] == 1 iff data in replica[lid] is the last recent write data
   - whish[lid] == 1 iff data has interested to be on localitydomain lid. Used to implement distribution
                     attribute before real distribution, i.e. may be whish !=0 with valid ==0 or xfer ==0.
   - xfer[lid]  == 1 iff data in replica[lid] is going to be transfered to lid.
                      upon reception the state will be updated to valid and xfer unset.
   - xferb[lid bit] == 1 to be used for routing inside NVLink
*/
/* static limitation of the current implementation */
#define KAAPI_MEMORY_MAX_NODES 16
#define KAAPI_MEMORY_BITFIELD_TYPE kaapi_atomic16_t
#define KAAPI_MEMORY_VALUE_TYPE uint16_t
#define KAAPI_MEMORY_FFS __builtin_ffs
#define KAAPI_MEMORY_POPCNT __builtin_popcount
struct kaapi_metadata_info {
    kaapi_data_replica_t*      replicas[KAAPI_MEMORY_MAX_NODES];
    KAAPI_MEMORY_BITFIELD_TYPE alloc;
    KAAPI_MEMORY_BITFIELD_TYPE valid;
    KAAPI_MEMORY_BITFIELD_TYPE xfer;
    KAAPI_MEMORY_BITFIELD_TYPE whish;
    KAAPI_MEMORY_BITFIELD_TYPE xferb; /* used ??? */
#if defined(KAAPI_DEBUG)
    const char*                debug_info;
    pthread_t                  owner;      /* current xkblas model => the user thread that call
                                             xkblas have its own context and data */
#endif
};

/* utility: return 1+the index of random select bit set to 1*/
static inline int _kaapi_get_random_bit1(KAAPI_MEMORY_VALUE_TYPE value, unsigned int* seed )
{
  int idx = 0;
  if (value ==0) return 0;
  int nb = KAAPI_MEMORY_POPCNT(value);
  int k = rand_r(seed)%nb;
  for (int i=0; i<=k; ++i) 
  {
    idx = KAAPI_MEMORY_FFS(value);
    value &= ~(1UL << idx);
  }
  return idx;
}


/* ============================ Memory Node device ================================= */
struct kaapi_alloc_data;
typedef struct kaapi_alloc_data kaapi_alloc_data_t;

#if KAAPI_USE_OWN_HEAP_ALLOCATOR
struct kaapi_alloc_chunk;
typedef struct kaapi_alloc_chunk kaapi_alloc_chunk_t;
#endif

#define KAAPI_MEMORY_DEVICE_FLAG_NONE         0
#define KAAPI_MEMORY_DEVICE_FLAG_MOSTLY_FULL  0x1
#define KAAPI_MEMORY_DEVICE_FLAG_FULL         0x2

struct kaapi_memory_device {
    kaapi_address_space_id_t asid;
    kaapi_device_t* device;
    kaapi_lock_t mem_lock;
    kaapi_alloc_data_t* freelist_bloc;
    kaapi_alloc_data_t* freelist_metabloc;
#if KAAPI_USE_OWN_HEAP_ALLOCATOR
    kaapi_alloc_chunk_t* free_chunk_list;
    kaapi_alloc_chunk_t* main_chunk;
#endif

    /* Virtualization of alloc/free on the offload memory device */
    uintptr_t (*f_alloc)(struct kaapi_memory_device*,  size_t, int* );
    void  (*f_free)(struct kaapi_memory_device*, uintptr_t, size_t);

    /* returns:
       0: success
       EINPROGRESS : pending operations on the device
       else error
    */
    int   (*f_copy)(struct kaapi_memory_device*,
                    kaapi_pointer_t /* dest*/,
                    const kaapi_memory_view_t* /*view_dest*/,
                    kaapi_pointer_t /*src*/,
                    const kaapi_memory_view_t* /*view_src*/,
                    int flags, /* 0, 1, 2 */
                    kaapi_io_cbk_fnc_t cbk,
                    void* arg0, void* arg1, void* arg2
    );
    int  (*f_memsync)(struct kaapi_memory_device*, int begend);

    /* to help to manage cache */
    size_t (*f_get_mem_info)(struct kaapi_memory_device*, size_t*, size_t*);
    size_t (*f_get_free_mem)(struct kaapi_memory_device*);

    /* return source lid to reach lid_dest knowing valid_bit and xfer_bit for the data */
    uint16_t (*f_get_source)( struct kaapi_memory_device*, uint16_t, KAAPI_MEMORY_VALUE_TYPE, KAAPI_MEMORY_VALUE_TYPE );

#if KAAPI_DEBUG
    size_t size_alloc;     /* size alloc by the memory device */
    size_t size_free;      /* size free */
    size_t size_dev_alloc; /* size alloc / free to low level device driver */
    size_t size_dev_free;
#endif
};


/* Return the memory device with the given asid
*/
extern kaapi_memory_device_t* kaapi_memory_device_get(
    kaapi_address_space_id_t asid
);


/* ============================ Management of cache ================================= */
struct kaapi_memory_cache;
typedef struct kaapi_memory_cache kaapi_memory_cache_t;


/* ============================ DSM ================================= */
/* DSM structure in Kaapi.
   - set of standard operation to allocate, deallocate, bind, unbind, copy memory
   between address space.
   - current implementation valid only for a small number of adress space (global hash table)
   used to retreive metadata. A true distributed caching mecanism should be implemented at
   lowest cost as possible.
   - caches are always accessed without concurrency between different threads
*/
struct kaapi_dsm_node;
typedef struct kaapi_dsm_node kaapi_dsm_node_t;

typedef struct kaapi_dsm {
  int               mask_level;  // topology level: size of mask_nodes[]
  uint64_t          *mask_nodes;
  kaapi_dsm_node_t* nodes[KAAPI_MEMORY_MAX_NODES];
} kaapi_dsm_t;

extern kaapi_dsm_t kaapi_the_dsm;

/* Called before any method on dsm
*/
extern int kaapi_dsm_init( void );


/* Called after initialization of localitydomain attached to the dsm
*/
extern int kaapi_dsm_commit( void );

/*
*/
extern int kaapi_dsm_finalize( void );

/* Register a device to the dsm
   Each device has distinc id used as the local identifier for the dsm object
   Return values are:
   - 0 in case of success
   - EINPROGRESS if an device with same local identifier is already registered
*/
extern int kaapi_dsm_register_device( kaapi_dsm_t* dsm, kaapi_memory_device_t* device, int arch );

/* Unregister a device to the dsm
   Unregister the device.
   The device should has been stop. No thread will give to the device work.
   The call to kaapi_dsm_unregister_device, first synchronise pending operation.
   Then for all data store in the device (view from its cache), the call make
   copy back to the host for data that does not have valid value on the local host.
*/
extern int kaapi_dsm_unregister_device( kaapi_dsm_t* dsm, kaapi_memory_device_t* device );

/* Allocate memory on asid
*/
extern kaapi_pointer_t kaapi_dsm_allocate( kaapi_address_space_id_t asid, size_t size );

/* Deallocate memory on asid
*/
extern int kaapi_dsm_deallocate( kaapi_address_space_id_t asid, kaapi_pointer_t ptr );

/* Find in dsm object the object with access ptr and view.
   If the object does exist, then new entry is created (if createflag is set)
   and the local memory stores a valid, allocated and pinned replica to (a->ptr,view).
   On return the metadata pointer is returned and access->mdi points to it.
*/
extern kaapi_metadata_info_t* kaapi_dsm_findaccess_on_node(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      int createflag,
      kaapi_access_t* a,
      const kaapi_memory_view_t* view
);
#define KAAPI_DSM_NOCREATE     0x0
#define KAAPI_DSM_CREATE_DATA  0x1
#define KAAPI_DSM_CREATE_MDI   0x2

/* Display information about mdi
*/
extern void kaapi_dsm_print_mdi( const char* fname, const kaapi_metadata_info_t* mdi );

/* Debug: add a name to the meta data associated to an address
  The name should be valid until the entry in the hashtable remains valid.
*/
extern int kaapi_dsm_debug_name( kaapi_dsm_t* dsm, void *ptr, const kaapi_memory_view_t* view, const char* name );


/* Call to the function returns true if the data on asid is valid and
   could be read immediatly. Else it returns false.
   There is not need to lock access to state information because if a reader exists
   and its activated, then the writer, that may change state information,
   was completed due to data flow constraints.
*/
extern int kaapi_dsm_is_valid_on(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_metadata_info_t* mdi
);


/* Annote that data whish to be (initially) distributed on the address space.
   The DSM will create the entry in the metadata that will be attached to 'a'
*/
extern int kaapi_dsm_whish_distribution(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_handle_t* h
);


/* Acquire the data for the task with the access_mode mp.
   If mp is a read mode, then ask to the dsm a copy on asid for the task.
   Upon reception of the communication, the callback cbk is called if not already valid on asid.
   If a valid copy of the data is alread on asid, the callback is not called.
   If mp is write mode, then invalidate all other copies and callback is not called.
   In case of necessary communication, the waiting counter of the task is incremented
   and it would be decremented upon reception of the data.
   Return values:
   - 0 if a valid copy of data is already stored on asid
   - EINPROGRESS if a communication has been initiated.
*/
extern int kaapi_dsm_acquire_data(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_task_t* task,
      kaapi_access_mode_t mp,
      kaapi_metadata_info_t* mdi,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
);


/* To be call when the data was consummed or produced and may be released
*/
extern int kaapi_dsm_release_data(
      kaapi_metadata_info_t* mdi,
      kaapi_address_space_id_t asid,
      kaapi_access_t* a
);


/* Prefetch request to store copy on asid
*/
extern int kaapi_dsm_prefetch_on(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_metadata_info_t* mdi,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
);

/* Debug
*/
extern void kaapi_memory_cache_print( kaapi_memory_device_t* device);

/* Initiate write through of non valid data on the device asid to the host
*/
extern uint64_t kaapi_memory_writeback_all(
  kaapi_dsm_t* dsm,
  kaapi_address_space_id_t asid,
  kaapi_io_cbk_fnc_t cbk,
  void* arg0, void* arg1, void* arg2
);


/* -------------------------------------------------------------------------- */
/*
*/
extern kaapi_pointer_t kaapi_memory_alloc(kaapi_address_space_id_t asid, size_t size);

/*
*/
extern void kaapi_memory_free(kaapi_pointer_t ptr, size_t size );

/*
*/
extern int kaapi_memory_freelist_destroy(kaapi_memory_device_t* device);

/*
*/
enum {
    KAAPI_MEMORY_VOID =0,
    KAAPI_MEMORY_EXPECTED_BLOCK =1 
};
extern int kaapi_memory_set_info( int kind, size_t value );


/* -------------------------------------------------------------------------- */
/*
*/
extern int kaapi_memory_invalidate_cache(kaapi_address_space_id_t asid);

/*
*/
int kaapi_memory_cache_invalidate_data(
  kaapi_metadata_info_t* mdi
);

/*
*/
extern int kaapi_memory_cache_invalidate_bloc(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  kaapi_metadata_info_t* mdi
);

/* High level memory copy
*/
extern int kaapi_memory_copy_async(
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src,
    int flags,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
);

/* Take the first. But always the same.
   If several capacity => take one of the less loaded or random
*/
extern uint16_t _kaapi_get_source_lid(
  kaapi_dsm_t* dsm,
  kaapi_metadata_info_t* mdi,
  kaapi_address_space_id_t dest_asid,
  uint16_t* sel_dest
);


static inline bool kaapi_memory_replica_is_valid(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->valid) & (1UL<<lid)) !=0);
}

static inline bool kaapi_memory_replica_is_xfer(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->xfer) & (1UL<<lid)) !=0);
}

static inline bool kaapi_memory_replica_is_xferb(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->xferb) & (1UL<<lid)) !=0);
}

#if KAAPI_DEBUG
extern void kaapi_memory_cache_print_all(void);
#endif

#endif
