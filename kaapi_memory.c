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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kaapi_impl.h"
#include "kaapi_offload.h"
#include "kaapi_offload_stream.h"

/* Priorities for fecthing or copying data
*/
#define KAAPI_FETCH_PRIORITY_LOW    0
#define KAAPI_FETCH_PRIORITY_NORMAL 1
#define KAAPI_FETCH_PRIORITY_HIGH   2


/* DSM node representation
*/
struct kaapi_dsm_node {
  kaapi_hashentries_t*   mapentries[1ULL<<KAAPI_SIZE_DSM_MAP];
  kaapi_hashmap_t        ht;
  kaapi_lock_t           lock;
  kaapi_memory_device_t* device;
  kaapi_memory_cache_t*  cache;
};


/* -------------------------------------------------------------------------- */
/* cache data structure for dsm
   here is an implementation of our IPDPS2013 paper cache with separate entry for ro an
   data that are writen.
   Each replica is managed into the cache of its corresponding memory device.
   replica has both cacheentry and cachelist where cachelist points to the ro or rw list
   and cache entry points to the entry in the list.
*/
#define KAAPI_CACHEENTRY_BLOCSIZE 1024
typedef struct kaapi_cache_entry {
  kaapi_metadata_info_t* mdi;
  struct kaapi_cache_entry *next;
  struct kaapi_cache_entry *prev;
} kaapi_cache_entry_t;

/* entry[0] is reserved for next and pos */
typedef struct kaapi_cache_blocentry {
  union {
    struct {
      struct kaapi_cache_blocentry* next;
      int pos;
    };
    kaapi_cache_entry_t entry[KAAPI_CACHEENTRY_BLOCSIZE];
  };
} kaapi_cache_blocentry_t;

typedef struct {
    kaapi_lock_t         lock;
    kaapi_cache_entry_t *beg;
    kaapi_cache_entry_t *end;
} kaapi_cache_list_t;

typedef struct kaapi_memory_cache {
  kaapi_address_space_id_t asid;
  size_t size_limit;
  size_t size_dev_alloc;            /* sum of all data allocated in the device and cached */
  kaapi_cache_list_t ro   __attribute__((aligned(64)));
  kaapi_cache_list_t rw   __attribute__((aligned(64)));
  kaapi_lock_t       lock __attribute__((aligned(64)));
  kaapi_cache_entry_t* freelist;
  kaapi_cache_blocentry_t* allocated_bloc;
} kaapi_cache_lru_double_fifo_t;

/*
*/
kaapi_dsm_t kaapi_the_dsm;
kaapi_atomic32_t kaapi_dsm_asid_lid = {0}; /* 0 is the host memory */

/* fwd decl */
static int _kaapi_dsm_deallocate_replica(
    kaapi_dsm_t* dsm,
    kaapi_metadata_info_t* mdi
);

static int kaapi_memory_cache_evict(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  size_t size,
  int flag
);

/* short cut */
kaapi_memory_device_t* kaapi_memory_device_get(
    kaapi_address_space_id_t asid
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (kaapi_the_dsm.nodes[lid] ==0) return 0;
  return kaapi_the_dsm.nodes[lid]->device;
}

static int kaapi_dsm_fetch_on(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_metadata_info_t* mdi,
      uint32_t gen,
      int flags,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
);

#if KAAPI_DEBUG
static void _kaapi_memory_cache_check( kaapi_memory_cache_t* cache);
#endif

/* -------------------------------------------------------------------------- */
static inline bool kaapi_memory_replica_is_allocated(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->alloc) & (1UL<<lid)) !=0);
}

static inline void kaapi_memory_replica_set_allocated(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_OR64(&mdi->alloc, (1UL<<lid));
}

static inline void kaapi_memory_replica_unset_allocated(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_AND64(&mdi->alloc, ~(uint64_t)(1UL<<lid));
}


static inline bool kaapi_memory_replica_is_valid_on(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->valid) & (uint64_t)(1UL<<lid)) !=0);
}

static inline bool kaapi_memory_replica_is_valid_excepton(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  ((KAAPI_ATOMIC_READ(&mdi->valid) & ~(uint64_t)(1UL<<lid)) !=0);
}

static inline bool kaapi_memory_replica_is_valid_somewhere(
    kaapi_metadata_info_t*   mdi
)
{
  kaapi_assert_debug(mdi != 0);
  return  (KAAPI_ATOMIC_READ(&mdi->valid) !=0);
}

static inline void kaapi_memory_replica_set_valid(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_OR64(&mdi->valid, (1UL<<lid));
}

static inline void kaapi_memory_replica_unset_valid(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_AND64(&mdi->valid, ~(uint64_t)(1UL<<lid));
}

static inline void kaapi_memory_replica_set_all_dirty_except(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_WRITE(&mdi->valid, (1UL<<lid));
}


static inline void kaapi_memory_replica_set_xfer(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_OR64(&mdi->xfer, (1UL<<lid));
}

static inline void kaapi_memory_replica_unset_xfer(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_AND64(&mdi->xfer, ~(uint64_t)(1UL<<lid));
}

static inline int kaapi_memory_replica_is_xfer_tosomewhere(
    kaapi_metadata_info_t*   mdi
)
{
  kaapi_assert_debug(mdi != 0);
  return KAAPI_ATOMIC_READ(&mdi->xfer) != 0;
}

static inline void kaapi_memory_replica_set_xferb(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_OR64(&mdi->xferb, (1UL<<lid));
}

static inline void kaapi_memory_replica_unset_xferb(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_AND64(&mdi->xferb, ~(uint64_t)(1UL<<lid));
}

static inline bool kaapi_memory_replica_is_notpinned(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  return  (mdi->replicas[lid] ==0) || (KAAPI_ATOMIC_READ(&mdi->replicas[lid]->pinned) ==0);
}

static inline void kaapi_memory_replica_mark_pinned(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
#if KAAPI_DEBUG
  int val = 
#endif
    KAAPI_ATOMIC_INCR(&mdi->replicas[lid]->pinned);
  kaapi_assert_debug( val <= 127 ); /* only debug because */
}

static inline void kaapi_memory_replica_unmark_pinned(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
#if KAAPI_DEBUG
  int value=  
#endif
    KAAPI_ATOMIC_DECR(&mdi->replicas[lid]->pinned);
#if KAAPI_DEBUG
   kaapi_assert( ((lid!=0)&&(value >=0)) || (value >0) );
#endif
}

static inline void kaapi_memory_replica_unset_pinned(
    kaapi_metadata_info_t*   mdi,
    uint16_t lid
)
{
  kaapi_assert_debug(mdi != 0);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  KAAPI_ATOMIC_WRITE(&mdi->replicas[lid]->pinned, 0);
}

/* -------------------------------------------------------------------------- */
static void bit2str( char* buffer, int bitmap )
{
  buffer[0] = 0;
  for (int i=7; i>=0; --i) 
  {
    if ( bitmap & (1<<i))
      buffer[7-i] = '1';
    else
      buffer[7-i] = '0';
  }
  buffer[8]=0;
}

/* -------------------------------------------------------------------------- */
struct kaapi_alloc_data {
  kaapi_pointer_t     ptr;
  size_t              size;
  kaapi_alloc_data_t* next;
};

/* If free list used for Tile matrix (blocsize always the same size)
   performance is good for the warmup run but poor for next runs
*/
#define MEM_ALLOC_FREELIST 1

#if MEM_ALLOC_FREELIST
static size_t DEFAULT_TILE_SIZE = 1*2048*2048*sizeof(double);
static size_t TILE_SIZE = 1*2048*2048*sizeof(double);
#endif
int kaapi_memory_set_info( int kind, size_t value )
{
  if (kind == KAAPI_MEMORY_VOID)
    value;
#if MEM_ALLOC_FREELIST
  else if (kind == KAAPI_MEMORY_EXPECTED_BLOCK)
  {
    if (value ==0)
    {
      if (getenv("KAAPI_BLOCK_SIZE"))
      {
        value = atoi(getenv("KAAPI_BLOCK_SIZE"));
        value = value*sizeof(double);
      }
    }
    TILE_SIZE = value;
    if (getenv("KAAPI_VERBOSE")) 
      printf("[xkaapi] preferred block size to %zu\n", value );
  }
#endif
  else
    return ENOENT;
  return 0;
}


#define KAAPI_SIZE_DBG_MAP_ALLOC 16
#if KAAPI_DEBUG_MEMORY_ALLOC
kaapi_hashmap_t      free_ptr_ht; /* to store task & data visited */
kaapi_hashentries_t* free_mapentries[1<<KAAPI_SIZE_DBG_MAP_ALLOC];
#endif

/* Return a device bloc of size at least size
*/
kaapi_pointer_t kaapi_memory_alloc(kaapi_address_space_id_t asid, size_t size)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  kaapi_memory_device_t* device = kaapi_the_dsm.nodes[lid]->device;
  kaapi_memory_cache_t* cache   = kaapi_the_dsm.nodes[lid]->cache;
  kaapi_assert(device !=0);
  kaapi_assert((cache !=0) || (lid ==0));

  kaapi_pointer_t ptr;
  int flag = KAAPI_MEMORY_DEVICE_FLAG_NONE;

  kaapi_atomic_lock(&device->mem_lock);

#if MEM_ALLOC_FREELIST
  if (size <=TILE_SIZE)
  {
    size = TILE_SIZE;
    kaapi_alloc_data_t* kad = device->freelist_bloc;
    if (kad ==0)
    {
      ptr = kaapi_make_pointer((void*)device->f_alloc(device,TILE_SIZE, &flag), asid);
#  if KAAPI_DEBUG
      if (!kaapi_pointer_isnull(ptr))
        device->size_dev_alloc += TILE_SIZE;
#  endif
    }
    else
    {
#  if KAAPI_DEBUG_MEMORY_ALLOC
      kaapi_hashentries_t* entry = kaapi_hashmap_findinsert(&free_ptr_ht, (void*)kad->ptr.ptr);
      void* ref = KAAPI_HASHENTRIES_GET(entry, void*);
      if (ref ==0)
         printf("*** Reuse freeed pointer not referenced in map???\n");
      KAAPI_HASHENTRIES_SET(entry, 0, void*);
#  endif
      ptr = kad->ptr;
      kaapi_assert_debug( !kaapi_pointer_isnull(ptr) );
      kaapi_assert_debug( size <= kad->size );
      kaapi_assert_debug( asid == kad->ptr.asid );
      device->freelist_bloc = kad->next;
      kad->next = device->freelist_metabloc;
      device->freelist_metabloc = kad;
#  if KAAPI_DEBUG
      device->size_alloc += TILE_SIZE;
#  endif
    }
  }
  else
#endif
  {
    ptr = kaapi_make_pointer((void*)device->f_alloc(device,size, &flag), asid);
#if KAAPI_DEBUG
    if (!kaapi_pointer_isnull(ptr))
      device->size_dev_alloc += size;
#endif
  }

retval:
  kaapi_atomic_unlock(&device->mem_lock);
#if defined(KAAPI_USE_PERFCOUNTER)
  if (!kaapi_pointer_isnull(ptr))
    kaapi_perthread_stat[device->device->ctxt->tid].counter[KAAPI_CNT_ALLOC] += size;
#endif
  if (flag)
  {
    int err = 9;
    if (cache->size_limit == (size_t)-1UL)
      kaapi_offload_get_mem_info(device->device, 0, &cache->size_limit );
      
    if (flag & KAAPI_MEMORY_DEVICE_FLAG_MOSTLY_FULL)
    {
      double p = 0.01; 
      //printf("Mostly full cache, try to evict: %.2f of %zu = %zu\n",p*100, cache->size_limit, (size_t)(cache->size_limit*p));
      err = kaapi_memory_cache_evict(device, cache, cache->size_limit*p, 1 );
    }
    if (flag & KAAPI_MEMORY_DEVICE_FLAG_FULL)
    {
      //printf("Full cache, try to evict: 10%% of %zu = %zu\n",cache->size_limit, (size_t)(cache->size_limit*0.1));
      err = kaapi_memory_cache_evict(device, cache, cache->size_limit*0.1, 1 );
    }
    kaapi_offload_poll_device( device->device );
  }
  return ptr;
}


/*
*/
void kaapi_memory_free(kaapi_pointer_t ptr, size_t size )
{
  if (kaapi_pointer_isnull(ptr)) return;

#if KAAPI_DEBUG_MEMORY_ALLOC
  kaapi_hashentries_t* entry = kaapi_hashmap_findinsert(&free_ptr_ht, (void*)ptr.ptr);
  void* ref = KAAPI_HASHENTRIES_GET(entry, void*);
  if (ref !=0)
    printf("Already freed pointer !\n");
  KAAPI_HASHENTRIES_SET(entry, (void*)ptr.ptr, void*);
#endif

  kaapi_memory_device_t* device = kaapi_memory_device_get(ptr.asid);
  kaapi_assert_debug(device !=0);
#if defined(KAAPI_USE_PERFCOUNTER)
  kaapi_perthread_stat[device->device->ctxt->tid].counter[KAAPI_CNT_FREE] += size;
#endif
  kaapi_atomic_lock(&device->mem_lock);

#if MEM_ALLOC_FREELIST
  if (size <=TILE_SIZE)
  {
#define KAD_BLOCSIZE 64
    kaapi_alloc_data_t* kad = device->freelist_metabloc;
    if (kad ==0)
    {
      kad = malloc(sizeof(kaapi_alloc_data_t)*KAD_BLOCSIZE);
      for (int i=0; i<KAD_BLOCSIZE; ++i)
      {
        kad[i].ptr = kaapi_make_nullpointer(ptr.asid);
        kad[i].next = &kad[i+1];
        kad[i].size = 0;
      }
      kad[KAD_BLOCSIZE-1].next = 0;
      device->freelist_metabloc = &kad[1];
    }
    device->freelist_metabloc = kad->next;
    kad->size = TILE_SIZE;
    kaapi_assert_debug( !kaapi_pointer_isnull(ptr) );
    kad->ptr  = ptr;
    kad->next = device->freelist_bloc;
    device->freelist_bloc = kad;
#if KAAPI_DEBUG
    device->size_free += TILE_SIZE;
    kaapi_assert_debug( device->size_free <= device->size_alloc+device->size_dev_alloc );
#endif
  }
  else
#endif
  {
    device->f_free(device,ptr.ptr, size);
#if KAAPI_DEBUG
    device->size_dev_free += size;
#endif
  }
  kaapi_atomic_unlock(&device->mem_lock);
}


/* todo: free metabloc
*/
int kaapi_memory_freelist_destroy(kaapi_memory_device_t* device )
{
  kaapi_atomic_lock(&device->mem_lock);
  while (device->freelist_bloc !=0)
  {
    kaapi_alloc_data_t* kad = device->freelist_bloc;
    device->f_free(device, kad->ptr.ptr, kad->size);
    device->freelist_bloc = kad->next;
  }
  kaapi_atomic_unlock(&device->mem_lock);
  return 0;
}


/** Allocates an empty software cache for the device.
 * All blocks are in a FIFO double-ended queue, LRU policy.
 */
static kaapi_memory_cache_t* kaapi_memory_cache_init(
  kaapi_memory_device_t* device,
  kaapi_address_space_id_t asid,
  int flags
)
{
  kaapi_memory_cache_t* cache = malloc(sizeof(kaapi_memory_cache_t));
  cache->asid  = asid;
  kaapi_atomic_initlock(&cache->ro.lock);
  cache->ro.beg = cache->ro.end = 0;
  kaapi_atomic_initlock(&cache->rw.lock);
  cache->rw.beg = cache->rw.end = 0;
  kaapi_atomic_initlock(&cache->lock);
  cache->freelist = 0;
  cache->allocated_bloc = 0;
  kaapi_offload_get_mem_info(device->device, 0, &cache->size_limit );
  return cache;
}

/** \ingroup Offload
 */
static inline int kaapi_memory_cache_destroy(kaapi_memory_cache_t* cache)
{
  if (cache ==0) return 0;
  while (cache->allocated_bloc !=0)
  {
    kaapi_cache_blocentry_t* bloc = cache->allocated_bloc;
    cache->allocated_bloc = bloc->next;
    free(bloc);
  }
  kaapi_atomic_destroylock(&cache->ro.lock);
  kaapi_atomic_destroylock(&cache->rw.lock);
  kaapi_atomic_destroylock(&cache->lock);
  free(cache);
  return 0;
}


/* New entry for a cache list
*/
static kaapi_cache_entry_t* kaapi_memory_cache_allocate_entry(kaapi_memory_cache_t* cache)
{
  kaapi_memory_device_t* device = kaapi_memory_device_get(cache->asid);
  kaapi_assert_debug(device !=0);
  kaapi_atomic_lock(&cache->lock);
  kaapi_cache_entry_t* entry = cache->freelist;
  if (entry ==0)
  {
    kaapi_cache_blocentry_t* bloc = cache->allocated_bloc;
    if ((bloc ==0) || (bloc->pos == KAAPI_CACHEENTRY_BLOCSIZE))
    {
      bloc = (kaapi_cache_blocentry_t*)malloc(sizeof(kaapi_cache_blocentry_t));
      bloc->next = cache->allocated_bloc;
      cache->allocated_bloc = bloc;
      bloc->pos = 1;
    }
    entry = &bloc->entry[bloc->pos++];
  } else
    cache->freelist = entry->next;
  kaapi_atomic_unlock(&cache->lock);
  return entry;
}


/* Remove entry from the list
*/
static void kaapi_memory_cache_remove_from_list( kaapi_cache_list_t* list, kaapi_cache_entry_t* entry)
{
  kaapi_atomic_lock(&list->lock);
  if (entry->next !=0) entry->next->prev = entry->prev;
  else list->end = entry->prev;
  if (entry->prev !=0) entry->prev->next = entry->next;
  else list->beg = entry->next;
  entry->prev = entry->next = 0;
  kaapi_atomic_unlock(&list->lock);

}


/* Push entry on front the list
*/
static void kaapi_memory_cache_push_front( kaapi_cache_list_t* list, kaapi_cache_entry_t* entry)
{
  kaapi_atomic_lock(&list->lock);
  entry->next = list->beg;
  entry->prev = 0;
  if (list->beg ==0)
    list->end = entry;
  else
    list->beg->prev = entry;
  list->beg = entry;
  kaapi_atomic_unlock(&list->lock);
}


/* Touch data: move cache entry to hot entries list
*/
static int kaapi_memory_cache_touch(
  kaapi_dsm_t*           dsm,
  uint16_t               lid,
  kaapi_access_mode_t    mode,  /* ro, rw, w, ... */
  kaapi_metadata_info_t* mdi
)
{
  if (lid ==0) return 0; /* host memory: no cache */

  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_data_replica_t* kdr  = mdi->replicas[lid];
  kaapi_memory_cache_t* cache = dsm->nodes[lid]->cache;
  kaapi_memory_device_t* device = dsm->nodes[lid]->device;

  kaapi_assert_debug( &kaapi_offload_self_device()->memdev == device );

  int tid = device->device->ctxt->tid;
  kaapi_cache_list_t* list = KAAPI_ACCESS_IS_WRITE(mode) ? &cache->rw : &cache->ro;

  /* previous entry ? */
  kaapi_cache_list_t* oldlist = (kaapi_cache_list_t*)kdr->cachelist;
  kaapi_assert_debug((oldlist==0)||(oldlist==&cache->rw)||(oldlist==&cache->ro));

  kaapi_atomic_lock(&kdr->lock);
  kaapi_cache_entry_t* entry  = (kaapi_cache_entry_t*)kdr->cacheentry;

  if (entry ==0) /* not in this cache */
  {
    entry = kaapi_memory_cache_allocate_entry(cache);
    entry->mdi = mdi;
    kdr->cacheentry = entry;

#if defined(KAAPI_USE_PERFCOUNTER)
    ++kaapi_perthread_stat[tid].counter[KAAPI_CNT_CACHE_MISS];
    kaapi_perthread_stat[tid].counter[KAAPI_CNT_CACHE_MISS_BYTES] += kaapi_memory_view_size( &kdr->view );
#endif
  }
  else 
  { /* to not change the accounting of size_used,
       because oldlist is either the cache's ro or rw
    */
    kaapi_memory_cache_remove_from_list( oldlist, entry );

#if defined(KAAPI_USE_PERFCOUNTER)
    ++kaapi_perthread_stat[tid].counter[KAAPI_CNT_CACHE_HIT];
    kaapi_perthread_stat[tid].counter[KAAPI_CNT_CACHE_HIT_BYTES] +=
      kaapi_memory_view_size( &kdr->view );
#endif
  }
  if ((oldlist == &cache->rw) && !kaapi_memory_replica_is_valid_on(mdi,lidhost))
  {
    list = oldlist;
  }

  kaapi_assert_debug((list==&cache->rw)||(list==&cache->ro));
  kaapi_memory_cache_push_front( list, entry );
  kdr->cachelist = list;
  kaapi_atomic_unlock(&kdr->lock);

#if KAAPI_DEBUG
  _kaapi_memory_cache_check( cache );
#endif
  return 0;
}


/*
 * */
static size_t _kaapi_memory_cache_sizelist( kaapi_memory_cache_t* cache, kaapi_cache_list_t* list)
{
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  kaapi_atomic_lock(&list->lock);
  kaapi_cache_entry_t* curr = list->beg;
  size_t size = 0;
  while (curr !=0)
  {
    if (curr->mdi)
      size += kaapi_memory_view_size(&curr->mdi->replicas[lid]->view);
    curr = curr->next;
  }
  kaapi_atomic_unlock(&list->lock);
  return size;
}



/*
*/
static void _kaapi_memory_cache_print( kaapi_memory_cache_t* cache)
{
  if (cache ==0) return;

  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  char valid[16];
  char alloc[16];
  char pin[16];
  char xfer[16];
  char xferb[16];

#if 0
  printf("%2i:: cache asid:%i  info: size_limit:%lu, size_used:%lu\n", (int)lid, (int)lid, cache->size_limit, cache->size_used);
#endif
  for (int it = 0; it < 2; ++it)
  {
    kaapi_cache_list_t* list;
    if (it ==0) list = &cache->ro;
    else list = &cache->rw;


    /* compute size of list before dumping each entry */
    size_t size = _kaapi_memory_cache_sizelist(cache, list);
    printf("%2i:: list %s: @:%p, size_cached:%lu\n", lid, (list  == &cache->ro ? "RO" : "RW"), (void*)list, size);

    kaapi_cache_entry_t* curr = list->beg;
    int count = 0;
    while (curr != 0)
    {
      bit2str( valid, KAAPI_ATOMIC_READ(&curr->mdi->valid) );
      bit2str( alloc, KAAPI_ATOMIC_READ(&curr->mdi->alloc) );
      int pinned = 0;
      for (int k=0;k<KAAPI_MEMORY_MAX_NODES; ++k)
        if ((curr->mdi->replicas[k] !=0) && (KAAPI_ATOMIC_READ(&curr->mdi->replicas[k]->pinned)>0))
          pinned |= (1<<k);
      bit2str( pin, pinned );
      bit2str( xfer, KAAPI_ATOMIC_READ(&curr->mdi->xfer) );
      bit2str( xferb, KAAPI_ATOMIC_READ(&curr->mdi->xferb) );
#if 1
      const char* name = kaapi_dbg_get_name((void*)curr->mdi->replicas[lidhost]->ptr.ptr);
#endif
      printf("%2i:: %i  curr: %p, next: %p, name: %s,  data: %p/ (0)%p, valid: %s, alloc: %s, pin: %s, xfer: %s, xferb: %s, size: %lu\n",
         lid,
         count++,
         (void*)curr,
         (void*)curr->next,
         (name == 0 ?  "" : name),
         (void*)curr->mdi->replicas[lid]->ptr.ptr,
         (void*)curr->mdi->replicas[lidhost]->ptr.ptr,
         valid, alloc, pin, xfer, xferb,
         kaapi_memory_view_size(&curr->mdi->replicas[lid]->view)
      );
      kaapi_assert(kaapi_memory_view_size(&curr->mdi->replicas[lid]->view) ==
         kaapi_memory_view_size(&curr->mdi->replicas[lidhost]->view)
      );
      curr = curr->next;
    }
  }
}

/*
*/
void kaapi_memory_cache_print( kaapi_memory_device_t* device)
{
  _kaapi_memory_cache_print( kaapi_the_dsm.nodes[kaapi_memory_asid_get_lid(device->asid)]->cache );
}


/*
*/
void kaapi_memory_cache_print_all(void)
{
  /* skip device 0: host */
  for (int i=1; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    _kaapi_memory_cache_print(kaapi_the_dsm.nodes[kaapi_memory_asid_get_lid(device->memdev.asid)]->cache);
  }
}

#if KAAPI_DEBUG==1
/*
 */
static void _kaapi_memory_cache_check( kaapi_memory_cache_t* cache)
{
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  int flagerr = 0;
  size_t size_ro = _kaapi_memory_cache_sizelist(cache, &cache->ro);
  size_t size_rw = _kaapi_memory_cache_sizelist(cache, &cache->rw);

  for (int it = 0; it < 2; ++it)
  {
    kaapi_cache_list_t* list;
    if (it ==0) list = &cache->ro;
    else list = &cache->rw;

    kaapi_atomic_lock(&list->lock);
    kaapi_cache_entry_t* curr = list->beg;

    while ((curr != 0) && (flagerr ==0))
    {
      kaapi_data_replica_t* kdr = curr->mdi->replicas[lid];
      if (kaapi_memory_view_size(&kdr->view) != kaapi_memory_view_size(&kdr->view))
      {
        abort();
      }
      if ((kdr->ptr.ptr ==0) && kaapi_memory_replica_is_allocated(curr->mdi,lid))
      {
        fprintf(stderr,"*** cache corrupted, null pointer cached\n");
        flagerr = 1;
      }
      if ((kdr->cacheentry != 0) && (kdr->cacheentry != curr))
      {
        fprintf(stderr,"*** cache corrupted, cacheentry differ: @%p ~ @%p\n",kdr->cacheentry,curr);
        flagerr = 1;
      }
      if ((kdr->cachelist != 0) && (kdr->cachelist != list))
      {
        fprintf(stderr,"*** cache corrupted, cachelist differ: @%p ~ @%p\n",kdr->cachelist,list);
        flagerr = 1;
      }

      if (flagerr)
      {
         kaapi_atomic_unlock(&list->lock);
        _kaapi_memory_cache_print(cache);
      }
      kaapi_assert( flagerr == 0);

      curr = curr->next;
    }
    kaapi_atomic_unlock(&list->lock);
  }
}


/*
*/
void _kaapi_memory_cache_verify_notself(void)
{
  /* here check all data in cache: it should not exist trace of data owned by the current thread */
  /* verify that their is no data with owner myself in all the caches */
  pthread_t myself = pthread_self();
  /* skip device 0: host: no cache */
  for (int i=1; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    kaapi_memory_cache_t* cache = kaapi_the_dsm.nodes[kaapi_memory_asid_get_lid(device->memdev.asid)]->cache;
    for (int it = 0; it < 2; ++it)
    {
      kaapi_cache_list_t* list;
      if (it ==0) list = &cache->ro;
      else list = &cache->rw;

      kaapi_atomic_lock(&list->lock);
      kaapi_cache_entry_t* curr = list->beg;
      while (curr != 0)
      {
        kaapi_assert( curr->mdi->owner != myself);
        curr = curr->next;
      }
      kaapi_atomic_unlock(&list->lock);
    }
  }
}
#endif



/* evict at least size bytes of object in the cache
*/
static size_t kaapi_memory_cache_evict_fromlist(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  size_t size,
  kaapi_cache_list_t* list,
  int flag
)
{
  /* */
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  kaapi_cache_entry_t* curr = list->end;
  kaapi_cache_entry_t* pcurr;

  size_t size2 = 2*size;
  kaapi_assert( &kaapi_offload_self_device()->memdev == device );

  while ((curr != 0) && (size2 >0))
  {
    pcurr = curr->prev;
    if (kaapi_memory_replica_is_notpinned(curr->mdi, lid)
     && kaapi_memory_replica_is_valid_excepton(curr->mdi, lid)
    )
    {
      kaapi_assert_debug( kaapi_memory_replica_is_allocated(curr->mdi, lid) );

      kaapi_memory_replica_unset_valid(curr->mdi, lid);
      kaapi_assert_debug( kaapi_memory_replica_is_valid_somewhere(curr->mdi) );

/* Remainder part of the protocol should be
      kaapi_mem_barrier();
      if (kaapi_memory_replica_under_xfer(curr->mdi)) abort eviction
  On the concurrent threads:
      kaapi_memory_replica_set_xfer(mdi, lidj); // lidj: lid that want to read value
      kaapi_mem_barrier();
      if (!kaapi_memory_replica_is_valid(curr->mdi,lid))
        redo transfer with new valid replica
*/
      kaapi_assert_debug( list == curr->mdi->replicas[lid]->cachelist );

      kaapi_memory_replica_unset_allocated(curr->mdi, lid);
      kaapi_assert( !kaapi_memory_replica_is_xfer(curr->mdi, lid) );

      size_t size_view = kaapi_memory_view_size( &curr->mdi->replicas[lid]->view );
      kaapi_assert_debug( curr->mdi->replicas[lid]->ptr.asid == device->asid );
      kaapi_memory_free(curr->mdi->replicas[lid]->ptr, size_view );
//printf("* evict data in list '%s' @:%p size:%lu\n", list == &cache->ro ? "ro" : "rw", curr->mdi->replicas[lid]->ptr.ptr, size_view );
      curr->mdi->replicas[lid]->ptr = kaapi_make_nullpointer(cache->asid);
      curr->mdi->replicas[lid]->cachelist = 0;
      curr->mdi->replicas[lid]->cacheentry = 0;
      if (size2 < size_view) size2 = 0;
      else size2 -= size_view;

      /* erase entry from cache list */
      if (curr->next !=0) curr->next->prev = pcurr;
      else list->end = pcurr;
      if (curr->prev !=0) curr->prev->next = curr->next;
      else list->beg = curr->next;
      curr->prev = 0;
      kaapi_atomic_lock(&cache->lock);
      curr->next = cache->freelist;
      cache->freelist = curr;
      kaapi_atomic_unlock(&cache->lock);
    }
    else if (0)//((flag ==1) && !kaapi_memory_replica_is_valid_excepton(curr->mdi, lid) && !kaapi_memory_replica_is_xfer(curr->mdi, kaapi_local_asid))
    //else if (!kaapi_memory_replica_is_valid_excepton(curr->mdi, lid))
    {
      printf("Evict data\n");
      int err = kaapi_dsm_prefetch_on( &kaapi_the_dsm, kaapi_local_asid,
        curr->mdi, (uint32_t)-1,
        0, 0, 0, 0
      );
      if (err == EINPROGRESS)
        kaapi_offload_poll_device(device->device);
    }

    curr = pcurr;
  }
  if (size2 <= size) return 0;
  return size2-size;
}



/* display basic stats about cache
*/
static void kaapi_memory_print_cache_stats(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  const char* msg,
  kaapi_cache_list_t* list
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  kaapi_cache_entry_t* curr  = list->end;

  size_t total_size   = 0;
  size_t xfer_size    = 0;
  size_t xferb_size    = 0;
  size_t pinned_size  = 0;
  size_t invalid_size = 0;

  while (curr != 0)
  {
    size_t size_view = kaapi_memory_view_size( &curr->mdi->replicas[lid]->view );
    total_size += size_view;

    if (!kaapi_memory_replica_is_notpinned(curr->mdi, lid))
      pinned_size += size_view;

    if (kaapi_memory_replica_is_valid_excepton(curr->mdi, lid))
      invalid_size += size_view;

    if (kaapi_memory_replica_is_xfer(curr->mdi, lid) )
      xfer_size += size_view;

    if (kaapi_memory_replica_is_xferb(curr->mdi, lid) )
      xferb_size += size_view;

    curr = curr->prev;
  }
  printf("Eviction from device: %li\n", lid );
#if KAAPI_DEBUG
  printf("  -- size_alloc: %li, size_dev_alloc: %li, size_free: %li\n", lid, device->size_alloc, device->size_dev_alloc, device->size_free);
#endif
  printf("  -- list: %s\n", msg);
  printf("Total size : %ul\n", (unsigned long int)total_size);
  printf("XValid size: %ul  (%.2f)\n", (unsigned long int)invalid_size, 100.0*((double)invalid_size)/(double)total_size);
  printf("Pinned size: %ul  (%.2f)\n", (unsigned long int)pinned_size, 100.0*((double)pinned_size)/(double)total_size);
  printf("Xfer  size : %ul  (%.2f)\n", (unsigned long int)xfer_size, 100.0*((double)xfer_size)/(double)total_size);
  printf("Xfer  size : %ul  (%.2f)\n", (unsigned long int)xferb_size, 100.0*((double)xferb_size)/(double)total_size);
}

/* Initiate communication from device to the host.
   Wait completion of the communications should be done.
   On return.
*/
uint64_t kaapi_memory_writeback_all(
  kaapi_dsm_t* dsm,
  kaapi_address_space_id_t asid,
  kaapi_io_cbk_fnc_t cbk,
  void* arg0, void* arg1, void* arg2
)
{
  uint64_t send_msg = 0;
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);

  /* Only iterates on RW list where touched data should be found */
  kaapi_cache_list_t* list = &dsm->nodes[lid]->cache->rw;
  kaapi_cache_entry_t* curr = list->beg;
  kaapi_cache_entry_t* ncurr;

  while (curr != 0)
  {
    ncurr = curr->next;
    kaapi_assert(kaapi_memory_replica_is_allocated(curr->mdi, lidhost) );
    if (!kaapi_memory_replica_is_valid( curr->mdi, lidhost )
      && kaapi_memory_replica_is_valid( curr->mdi, lid ))
    {
#if KAAPI_DEBUG
      if (kaapi_memory_replica_is_valid( curr->mdi, lid == 1 ? 2 : 1 ))
        printf("*** also valid on other cache\n");
#endif
      int err = kaapi_dsm_prefetch_on( dsm, kaapi_local_asid, curr->mdi, (uint32_t)-1, cbk, arg0, arg1, arg2 );
      kaapi_assert((err ==0) || (err ==EINPROGRESS));
      if (err == EINPROGRESS) ++send_msg;
    }
    curr = ncurr;
  }
  return send_msg;
}


/* Evict some memory to make room for more allocations (at least of size size).
 * Return 0 in case of success, else ENOMEM
 */
static int kaapi_memory_cache_evict(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  size_t size,
  int flag /* 1== RW first is already communicated, else RO first */
)
{
  kaapi_assert( &kaapi_offload_self_device()->memdev == device );
#if 1
static volatile int volatile prt = 0;
if (prt)
{
  kaapi_memory_cache_print( device );
  kaapi_memory_print_cache_stats( device, cache, "RO", &cache->ro);
  kaapi_memory_print_cache_stats( device, cache, "RW", &cache->rw);
}
#endif
  
  kaapi_cache_list_t* list_order[2];
  int flags[2];
  if (flag ==1)
  {
    list_order[0] =  &cache->rw; flags[0] = 1;
    list_order[1] =  &cache->ro; flags[1] = 0;
  }
  else 
  {
    list_order[0] =  &cache->ro; flags[0] = 0;
    list_order[1] =  &cache->rw; flags[1] = 0;
  }

  for (int l=0; l<2; ++l)
  {
    if (size >0)
    {
      int cnt = 0;
      do {
        size = kaapi_memory_cache_evict_fromlist(device, cache, size, list_order[l], flags[l]);
        if (size >0)
        {
          ++cnt;
          kaapi_offload_poll_device(device->device);
        }
      } while ((size >0) && (cnt <2));
    }
  }

  /* here if size >0 then it could be necessary to write back valid bloc on the cache
     but not pinned.
  */
#if 0//KAAPI_DEBUG
  if (size !=0)
   _kaapi_memory_cache_print(cache);
#endif

  return size ==0 ? 0 : ENOMEM;
}


/* Force to invalidate and free all data of a mdi but keep it into cache !
*/
int kaapi_memory_cache_invalidate_data(
  kaapi_metadata_info_t* mdi
)
{
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  kaapi_assert_debug(mdi->owner == pthread_self());

  /* unmask all replicas with generation less than requested gen
     due to few number of GPUs may be it is best to create mask of invalid
     replicas before unmasking them to valid_bit set.
  */
#if 1
  uint64_t alloc_bit = KAAPI_ATOMIC_READ(&mdi->alloc);
  size_t size = kaapi_memory_view_size(&mdi->replicas[lidhost]->view);
  while (alloc_bit !=0)
  {
    int lid= __builtin_ffsll( alloc_bit );
    --lid;
    kaapi_data_replica_t* kdr = mdi->replicas[lid];

    /* suppress replica from cache list */
    kaapi_memory_device_t* device = kaapi_the_dsm.nodes[lid]->device;
    kaapi_assert_debug((lid == lidhost) || (device !=0));
    if (lid != lidhost)
    {
      kaapi_memory_cache_t* cache = kaapi_the_dsm.nodes[lid]->cache;
      kaapi_cache_entry_t* entry = kdr->cacheentry;
      if (entry !=0)
      {
        kaapi_cache_list_t* list = kdr->cachelist;
        kaapi_memory_cache_remove_from_list( list, entry );
        entry->mdi  = 0;
        kaapi_atomic_lock(&cache->lock);
        entry->next = cache->freelist;
        cache->freelist = entry;
        kaapi_atomic_unlock(&cache->lock);
        kdr->cacheentry = 0;
      }
      kdr->cachelist = 0;
    }

    kaapi_atomic_lock(&kdr->lock);
    if (!kaapi_pointer_isnull(kdr->ptr))
    {
      if (lid != lidhost)
        kaapi_memory_free(kdr->ptr, size);
      kdr->ptr.ptr = 0;
    }
    kaapi_memory_replica_unset_allocated(mdi, lid );
    kaapi_memory_replica_unset_valid(mdi, lid );
    kaapi_memory_replica_unset_pinned(mdi, lid );
    kaapi_atomic_unlock(&kdr->lock);

    alloc_bit &= ~(1<<lid);
  }
#else
//  KAAPI_ATOMIC_WRITE(&mdi->alloc,  1ULL<<lid0);
  uint64_t alloc_bit = KAAPI_ATOMIC_READ(&mdi->alloc);
  size_t size = kaapi_memory_view_size(&mdi->replicas[lidhost]->view);
  while (alloc_bit !=0)
  {
    int lid= __builtin_ffsll( alloc_bit );
    --lid;
    kaapi_data_replica_t* kdr = mdi->replicas[lid];
    KAAPI_ATOMIC_WRITE(&kdr->pinned, 0);  /* one reference count to the application data */
    alloc_bit &= ~(1<<lid);
  }
  KAAPI_ATOMIC_WRITE(&mdi->valid,  0);
  KAAPI_ATOMIC_WRITE(&mdi->xfer,   0ULL);
  KAAPI_ATOMIC_WRITE(&mdi->xferb,  0ULL);
#endif

#if KAAPI_DEBUG 
  for (int i=0; i<KAAPI_MEMORY_MAX_NODES; ++i)
  {
    if (mdi->replicas[i])
    {
      kaapi_assert( mdi->replicas[i]->ptr.ptr ==0);
      kaapi_assert( !kaapi_memory_replica_is_allocated(mdi, i) );
      kaapi_assert( !kaapi_memory_replica_is_valid_on(mdi, i) );
    }
  }
  kaapi_assert(KAAPI_ATOMIC_READ(&mdi->valid) ==0);
  kaapi_assert(KAAPI_ATOMIC_READ(&mdi->alloc) ==0);
#endif
  return 0;
}


/* Force to invalidate all blocs of the cache
*/
int kaapi_memory_cache_invalidate_bloc(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t*  cache,
  kaapi_metadata_info_t* mdi
)
{
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(device->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (lid == lidhost) return 0;

  if (cache ==0)
    cache = kaapi_the_dsm.nodes[lid]->cache;

  kaapi_data_replica_t* kdr = mdi->replicas[lid];
  kaapi_atomic_lock(&kdr->lock);

  kaapi_assert( kaapi_memory_replica_is_allocated( mdi, lidhost ));
  kaapi_memory_replica_set_all_dirty_except(mdi, lidhost);

  size_t size = kaapi_memory_view_size(&kdr->view);

  if (kdr->ptr.ptr !=0)
  {
    kaapi_assert( kdr->ptr.asid == device->asid );
    kaapi_memory_free(kdr->ptr, size);
    kdr->ptr.ptr = 0;
  }
  kaapi_memory_replica_unset_allocated(mdi, lid );
  kaapi_memory_replica_unset_valid(mdi, lid );
  kaapi_memory_replica_unset_pinned(mdi, lid );
  kaapi_atomic_unlock(&kdr->lock);

  return 0;
}


/*
*/
static int kaapi_memory_cache_invalidate_fromlist(
  kaapi_memory_device_t* device,
  kaapi_memory_cache_t* cache,
  kaapi_cache_list_t* list
)
{
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(cache->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (lid == lidhost) return 0;

  kaapi_cache_entry_t* curr = list->beg;
  size_t size_cached = 0;

#if 0
  printf("%2i:: >>> [%s] invalidate all data. list: %p, size: %lu\n",
       lid, 
       __FUNCTION__, 
      (void*)list, _kaapi_memory_cache_sizelist(cache, list)
  );
#endif
  while (curr != 0)
  {
    size_t size = kaapi_memory_view_size(&curr->mdi->replicas[lid]->view);
    size_cached += size;
    kaapi_memory_cache_invalidate_bloc( device, cache, curr->mdi );

    kaapi_cache_entry_t* ncurr = curr->next;
    curr->mdi->replicas[lid]->cacheentry = 0;
    curr->mdi->replicas[lid]->cachelist = 0;
    kaapi_atomic_lock(&cache->lock);
    curr->next = cache->freelist;
    curr = ncurr;
    cache->freelist = curr;
    kaapi_atomic_unlock(&cache->lock);
  }
  list->beg = list->end = 0;
#if 0
  printf("%2i:: <<< [%s] invalidated\n", lid, __FUNCTION__, );
#endif
  return 0;
}


/* Invalidation of all entries in the cache asid.
   Memory blocs are reclaimed.
*/
int kaapi_memory_invalidate_cache( kaapi_address_space_id_t asid )
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  int err;
  kaapi_memory_cache_t* cache = kaapi_the_dsm.nodes[lid]->cache;
  kaapi_memory_device_t* device = kaapi_the_dsm.nodes[lid]->device;
  if (cache !=0)
  {
    err = kaapi_memory_cache_invalidate_fromlist(device, cache, &cache->ro);
    if (err) return err;
    cache->ro.beg = cache->ro.end = 0;
    err = kaapi_memory_cache_invalidate_fromlist(device, cache, &cache->rw);
    if (err) return err;
    cache->rw.beg = cache->rw.end = 0;
  }
  return 0;
}

/*
*/
static void kaapi_memory_freehashmap( kaapi_hashmap_t* ht, kaapi_address_space_id_t asid )
{
  int err;
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (lid==0)
  {
    kaapi_hashentries_t* entry;
    for (uint32_t i = 0; i< kaapi_hashmap_sizeentries(ht); ++i)
    {
      do {
        entry = _pop_hashmap_entry(ht, i);
        if (entry ==0) break;
        kaapi_metadata_info_t* mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
//#warning "How it possible that mdi ==0 here ?"
        if (mdi !=0)
        {
          _kaapi_dsm_deallocate_replica(&kaapi_the_dsm, mdi);
          free(mdi);
          KAAPI_HASHENTRIES_SET(entry, 0, kaapi_metadata_info_t*);
        }
      } while (1);
    }
  }
  err = kaapi_hashmap_clear(ht);
}

/* -------------------------------------------------------------------------- */
/* High level function: do memory copy
   Return 0 if the operation successfully completes before
   returning to the caller.
   It returns EINPROGRESS if the operation does not complete.
   Else it returns error code.
   Depending on the source and dest pointer, the function dispatch the call
   to the corresponding device that is not pure host device (host device
   cannot talk with true device while true device can transfer data between
   the device and the host).
*/
int kaapi_memory_copy_async(
    kaapi_memory_device_t* dev,
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src,
    int flags,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  if (1)//dev ==0)
  {
    kaapi_memory_device_t* dest_dev = kaapi_memory_device_get( dest.asid );
    kaapi_memory_device_t* src_dev = kaapi_memory_device_get( src.asid );
    dev= (
       (dest_dev ==0) || (kaapi_memory_asid_get_arch(dest_dev->asid) == KAAPI_PROC_TYPE_HOST) ?
         src_dev : dest_dev
    );
  }
  return dev->f_copy(
    dev,
    dest, view_dest,
    src, view_src,
    flags,
    cbk, arg0, arg1, arg2
  );
}


/*
*/
int kaapi_memory_copy(
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src
)
{
  int err = kaapi_memory_copy_async(
    0,
    dest, view_dest, src, view_src,
    KAAPI_FETCH_PRIORITY_NORMAL,
    0, 0, 0, 0 /* cbk etc */
  );
  if (err == EINPROGRESS)
  {
    kaapi_memory_device_t* dest_dev = kaapi_memory_device_get( dest.asid );
    kaapi_memory_device_t* src_dev __attribute__((unused)) = kaapi_memory_device_get( src.asid );
    dest_dev->f_memsync( dest_dev, 0);
  }

  return err;
}


/* -------------------------------------------------------------------------- */
/*
*/
void kaapi_dsm_print_mdi(
    const char* fname,
    const kaapi_metadata_info_t* mdi
)
{
 printf("[%s MDI] valid=[", fname);
 int valid = KAAPI_ATOMIC_READ(&mdi->valid);
 while (valid !=0)
 {
   int lid= __builtin_ffsll( valid);
   if (lid== 0 ) break;
   --lid;
   printf("%i/asid=%i, @=%p ", lid, 
     kaapi_memory_asid_get_lid(mdi->replicas[lid]->ptr.asid),
     (void*)mdi->replicas[lid]->ptr.ptr);
   valid &= ~(1<<lid);
 }

 printf("], alloc=[");
 int alloc = KAAPI_ATOMIC_READ(&mdi->alloc);
 while (alloc !=0)
 { 
   int lid= __builtin_ffsll( alloc);
   if (lid== 0 ) break;
   --lid;
   printf("%i/asid=%i @=%p ", lid, 
     kaapi_memory_asid_get_lid(mdi->replicas[lid]->ptr.asid),
     (void*)mdi->replicas[lid]->ptr.ptr);
   alloc &= ~(1<<lid);
 }
 printf("]\n");
}


/* Allocate a new main meta data information with replica on the host node.
   - init view as a rescaled view from given view if asid != localhost
*/
static inline kaapi_data_replica_t* _kaapi_new_replica(
    kaapi_metadata_info_t*     mdi,
    kaapi_data_replica_t*      kdr,
    kaapi_address_space_id_t   asid,
    const kaapi_memory_view_t* view
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  /* allocate entry in lid0 */
  if (kdr ==0)
  {
    kdr = (kaapi_data_replica_t*)malloc(sizeof(kaapi_data_replica_t));
    kaapi_atomic_initlock(&kdr->lock);
    kdr->cacheentry    = 0; /* to avoid conflict with debug section just below */
    kdr->ptr           = kaapi_make_nullpointer( asid );
    kdr->cbk.iocbk.fnc = 0;
    kdr->cbk.next      = 0;
    kdr->cacheentry    = 0;
    kdr->cachelist     = 0;
    KAAPI_ATOMIC_WRITE(&kdr->pinned, 0);  /* one reference count to the application data */
  }
  else 
  {
    kaapi_assert( kdr->cacheentry ==0);
    kaapi_assert( kdr->cachelist ==0);
    kaapi_assert(KAAPI_ATOMIC_READ(&kdr->pinned) == 0);
  }
#if LOG1
  else {
    printf("Reset cbk list: cbk: %p\n", &kdr->cbk);
  }
#endif
  kdr->count         = 0;
  if (!kaapi_pointer_isnull( kdr->ptr ))
  {
    size_t size = kaapi_memory_view_size(&kdr->view);
    if (kaapi_memory_view_size(view) != size)
      kaapi_memory_free(kdr->ptr, size);
    kdr->ptr           = kaapi_make_nullpointer( asid );
    kaapi_memory_replica_unset_allocated(mdi, lid );
    kaapi_memory_replica_unset_valid(mdi, lid );
    kaapi_memory_replica_unset_pinned(mdi, lid );
    kaapi_assert(kdr->cacheentry ==0);
    kaapi_assert(kdr->cachelist ==0);
  }
  kdr->view          = *view;
  if (asid != kaapi_local_asid)
  {
    /* Cuda may have alignment constraint for better communication and or kernel execution... */
    kaapi_memory_view_reallocated(&kdr->view);
  }

#if KAAPI_DEBUG
  extern __thread kaapi_thread_t* _xkblas_self_thread;
  kdr->thread = _xkblas_self_thread;
#endif
  return kdr;
}



/* Allocate a new meta data information with replica on the host node.
   Host ptr is used as the referent pointer - valid bit is set as well as alloc bit.
   The host replica is always marked as pined to avoid unallocation.
*/
static kaapi_metadata_info_t* _kaapi_new_mdi(
    kaapi_metadata_info_t* mdi,
    void* ptr,
    const kaapi_memory_view_t* view
)
{
  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lid0 < KAAPI_MEMORY_MAX_NODES);

  if (mdi ==0)
  {
    mdi = (kaapi_metadata_info_t*)malloc(sizeof(kaapi_metadata_info_t));
    memset( &mdi->replicas, 0, sizeof(mdi->replicas));
  }

  /* allocate replica and entry for lid0 */
  kaapi_data_replica_t* kdr = mdi->replicas[lid0];
  kdr = _kaapi_new_replica( mdi, kdr, kaapi_local_asid, view );
  mdi->replicas[lid0] =  kdr;
  
  kdr->ptr = kaapi_make_pointer(ptr, kaapi_local_asid);
  KAAPI_ATOMIC_WRITE(&kdr->pinned, 1);  /* one reference count to the application data */
  KAAPI_ATOMIC_WRITE(&mdi->alloc,  1ULL<<lid0);
  KAAPI_ATOMIC_WRITE(&mdi->valid,  1ULL<<lid0);
  KAAPI_ATOMIC_WRITE(&mdi->xfer,   0ULL);
  KAAPI_ATOMIC_WRITE(&mdi->xferb,  0ULL);
#if defined(KAAPI_DEBUG)
  mdi->debug_info = 0;
  mdi->owner = 0;
#endif
  return mdi;
}


/* If defined, then interact with findaccess_on node and
   results (dgemm test) are incorrect == should separate insertion
   of the mdi from the allocation of replica information with the view !!!
*/
int kaapi_dsm_debug_name(
    kaapi_dsm_t* dsm,
    void *ptr,
    const kaapi_memory_view_t* view,
    const char* name
)
{
printf("Do not use function. Abort\n");
abort();

  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lid0 < KAAPI_MEMORY_MAX_NODES);
  kaapi_metadata_info_t* mdi;
  kaapi_hashentries_t* entry;
  entry = kaapi_hashmap_find( &dsm->nodes[lid0]->ht, ptr );
  if (entry ==0)
  {
    mdi = _kaapi_new_mdi( 0, ptr, view );
    entry = kaapi_hashmap_insert( &dsm->nodes[lid0]->ht, ptr );
    KAAPI_HASHENTRIES_SET(entry, mdi, kaapi_metadata_info_t*);
  }
  else
    mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
#if defined(KAAPI_DEBUG)
  mdi->debug_info = strdup(name);
#endif
  return 0;
}


/* This function first look in the per device hashmap to retreive the data.
   If it does not exist, then it search in the global dsm hashmap.
   On return:
    - mdi and the replica for device lid are allocated
    - replica for host node points to the memory passed in a.
*/
kaapi_metadata_info_t* kaapi_dsm_findaccess_on_node(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      int createflag,
      kaapi_access_t* a,
      const kaapi_memory_view_t* view
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lid0 < KAAPI_MEMORY_MAX_NODES);

  kaapi_metadata_info_t* mdi = 0;
  kaapi_hashentries_t* entry;
  if (lid == (uint16_t)-1) lid = lid0;

  /* revoir ce point in case of multi-threaded code calling xkblas */

  /* big lock */
  kaapi_atomic_lock( &dsm->nodes[lid0]->lock );
  entry = kaapi_hashmap_find( &dsm->nodes[lid0]->ht, a->data );

#if 0 /* NOT IN USE */
  kaapi_atomic_lock( &dsm->nodes[lid0]->lock );
  entry = kaapi_hashmap_find( &dsm->nodes[lid]->ht, a->data );
  if ((entry ==0) && (lid != lid0))
    entry = kaapi_hashmap_find( &dsm->nodes[lid0]->ht, a->data );
  kaapi_atomic_unlock( &dsm->nodes[lid0]->lock );
#endif

  if ((entry ==0) && !createflag)
  {
    kaapi_atomic_unlock( &dsm->nodes[lid0]->lock );
    return 0;
  }

  if ((entry ==0) && createflag)
  {
    /* not entry but required to create it */
    kaapi_assert_debug( view );
    entry = kaapi_hashmap_findinsert( &dsm->nodes[lid0]->ht, a->data );
    mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
    if (mdi ==0)
    {
      mdi = _kaapi_new_mdi( 0, a->data, view );
      KAAPI_HASHENTRIES_SET(entry, mdi, kaapi_metadata_info_t*);
#if KAAPI_DEBUG
      mdi->owner = a->creator;
#endif
      goto return_value;
    }
  }
  if (mdi ==0) 
    mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
  //if (createflag && (mdi->replicas[0]->ptr.ptr !=0) && ((uintptr_t)a->data != (uintptr_t)mdi->replicas[0]->ptr.ptr))
  kaapi_assert( ((uintptr_t)mdi->replicas[0]->ptr.ptr ==0)
             || ((uintptr_t)a->data == (uintptr_t)mdi->replicas[0]->ptr.ptr) );
  if ((uintptr_t)a->data != (uintptr_t)mdi->replicas[0]->ptr.ptr)
  {
    /* reference point on the host differs and entry should be created */
    kaapi_assert_debug( !kaapi_memory_replica_is_valid(mdi,0));
    mdi = _kaapi_new_mdi( mdi, a->data, view );
#if KAAPI_DEBUG
    mdi->owner = a->creator;
#endif
    goto return_value;
  }
  kaapi_assert_debug(
    kaapi_memory_view_size(view) == kaapi_memory_view_size(&mdi->replicas[lid0]->view)
  );

  //
  {
    /* entry found. may be view differ : old entry.
     * here may be it would be best to have cache flush or cache invalidate that 
     * flush all entries in the cache    
     */
#if KAAPI_DEBUG
    if ((uintptr_t)a->data != mdi->replicas[0]->ptr.ptr)
    {
      printf("Cache------ 1\n");
      _kaapi_memory_cache_print(kaapi_the_dsm.nodes[1]->cache);
      printf("Cache------ 2\n");
      _kaapi_memory_cache_print(kaapi_the_dsm.nodes[2]->cache);

      kaapi_hashmap_t* ht = &kaapi_the_dsm.nodes[0]->ht;
      uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
      kaapi_hashentries_t* entry;
      for (uint32_t i = 0; i< kaapi_hashmap_sizeentries(ht); ++i)
      {
        entry = _get_hashmap_entry(ht, i);
        while (entry !=0)
        {
          kaapi_metadata_info_t*mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
          if (mdi !=0) kaapi_dsm_print_mdi("mdi", mdi);
          entry = entry->next;
        }
      }
    }
#endif
    kaapi_assert( (uintptr_t)a->data == (uintptr_t)mdi->replicas[0]->ptr.ptr );

    /* make sure view correspond */
    kaapi_assert_debug(
      kaapi_memory_view_size(view) == kaapi_memory_view_size(&mdi->replicas[lid0]->view)
    );
#if KAAPI_DEBUG
    if ((entry==0) && createflag)
      mdi->replicas[lid0]->view = *view;
    else
      kaapi_assert( kaapi_memory_view_size(view) == kaapi_memory_view_size(&mdi->replicas[lid0]->view) );
#endif

      //kaapi_assert_debug(kaapi_memory_view_size(view) <= TILE_SIZE);
// TODO here : what is data already in the cache ?
// update cache->size in order to pass assert
  }

return_value:
  kaapi_atomic_unlock( &dsm->nodes[lid0]->lock );

  /* */
  a->mdi = mdi;
  /* allocate replica but not the memory block */
  if (createflag && !kaapi_memory_replica_is_allocated(mdi,lid))
  //if (createflag) // && (mdi->replicas[lid] ==0))
  {
    mdi->replicas[lid] = _kaapi_new_replica( mdi, mdi->replicas[lid], asid, view);
  }

  return mdi;
}


/* Allocate a replica on lid:
   - if memory is already allocated and has the same size then try to reuse it
   - else new data is allocated from the memory allocator
   On return the alloc bit field of mdi is updated to reflect that data on asid is
   allocated.
   Return value is:
     - 0 in case of success
     - else possible error:
      - EINVAL: invalid argument
      - ENOMEM: cannot allocate memory on the device
*/
static int _kaapi_dsm_allocate_replica(
    kaapi_dsm_t* dsm,
    kaapi_metadata_info_t* mdi,
    uint32_t gen,
    kaapi_address_space_id_t asid
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  /* */
  kaapi_memory_cache_t* cache = kaapi_the_dsm.nodes[lid]->cache;
  kaapi_data_replica_t* kdr = mdi->replicas[lid];

  /* in this version, cache invalidation does not suppress replicas */
  kaapi_assert_debug(kdr !=0);
  if (kdr ==0) return EINVAL;

  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  size_t size = kaapi_memory_view_size(&kdr->view);

  /* already allocated ? */
  if (kaapi_memory_replica_is_allocated(mdi,lid))
  {
    kaapi_assert_debug( !kaapi_pointer_isnull( kdr->ptr ) );
    return 0;
  }

  /* replica cannot be valid if requested to allocate it */
  kaapi_assert_debug( !kaapi_memory_replica_is_valid(mdi,lid) );

  /* else allocate data */
  kaapi_assert_debug( kaapi_pointer_isnull( kdr->ptr ) );
  int retry_cnt = 0;

  kdr->ptr = kaapi_memory_alloc( asid, size );
  while (kaapi_pointer_isnull( kdr->ptr ))// && (++retry_cnt <32))
  {
    int err = kaapi_memory_cache_evict(dsm->nodes[lid]->device, cache, size, 0);
    if (err == 0)
      kdr->ptr = kaapi_memory_alloc( asid, size );
    if (err == ENOMEM || kaapi_pointer_isnull( kdr->ptr ))
      kaapi_offload_poll_device( dsm->nodes[lid]->device->device );
    retry_cnt++;
  }

  if (kaapi_pointer_isnull( kdr->ptr ))
    return ENOMEM;

  kaapi_memory_replica_set_allocated(mdi,lid);
  return 0;
}


/*
*/
static int _kaapi_dsm_deallocate_replica(
    kaapi_dsm_t* dsm,
    kaapi_metadata_info_t* mdi
)
{
  /* dispatch over all caches / memory devices */
  for (uint16_t lid=0; lid<KAAPI_MEMORY_MAX_NODES; ++lid)
  {
    kaapi_data_replica_t* kdr = mdi->replicas[lid];
    if (kdr ==0) continue;
    kaapi_atomic_lock(&kdr->lock);
    mdi->replicas[lid] = 0;
    if (kaapi_memory_replica_is_allocated(mdi, lid ))
    {
      kaapi_memory_replica_unset_allocated(mdi, lid);
      //kaapi_memory_device_t* device = kaapi_memory_device_get(kdr->ptr.asid);
      //if (device ==0) break;
      if (lid !=0)
      {
        size_t size_view = kaapi_memory_view_size( &kdr->view );
        kaapi_memory_free(kdr->ptr, size_view );
      }
      kdr->ptr = kaapi_make_nullpointer(0);
      kdr->cachelist = 0;
      kaapi_cache_entry_t* entry = (kaapi_cache_entry_t*)kdr->cacheentry;
      if (entry !=0)
      {
        kaapi_memory_cache_t* cache = dsm->nodes[lid]->cache;
        entry->next = cache->freelist;
        cache->freelist = entry;
        kdr->cacheentry = 0;
      }
    } 
    kaapi_atomic_unlock(&kdr->lock);
    free(kdr);
  }
  return 0;
}



/* Take the first valid lid to make copy to dest_asid.
   If several possibility should return the "best" lid.
*/
uint16_t _kaapi_get_source_lid(
  kaapi_dsm_t* dsm,
  kaapi_metadata_info_t* mdi,
  uint32_t gen,
  kaapi_address_space_id_t dest_asid,
  int mark
)
{
  uint16_t lid0, lid_dest;
  uint16_t lid_src;
  uint64_t valid_bit;
#if KAAPI_USE_FAVOR_D2D_1
  uint64_t xfer_bit;
#endif

  lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  lid_dest = kaapi_memory_asid_get_lid(dest_asid);

  /* choose valid copies to move data to device
     - warning: only replica with valid bit, not in transit are
     ensured to be send to the device
  */
  //#warning "Here store topology between GPUs through a better_peer() function"
  //#warning "Here protocol is not concurrent with eviction !!!! "
reload:
  valid_bit= KAAPI_ATOMIC_READ(&mdi->valid);
  uint64_t mask_valid_bit = valid_bit;

#if KAAPI_USE_FAVOR_D2D_1
  xfer_bit = KAAPI_ATOMIC_READ(&mdi->xfer);
#endif

  valid_bit &= ~(1<<lid0);
#if KAAPI_USE_FAVOR_D2D_1
  xfer_bit &= ~(1<<lid0);
#endif

  /* if ==0 => lid0 is the only valid ressource */
#if KAAPI_USE_FAVOR_D2D_1
  if ((valid_bit ==0) && (xfer_bit==0))
#else
  if (valid_bit ==0)
#endif
  {
    return lid0;
  }


#if 1
  /* return the best source pointer for the device lid_dest */
  kaapi_memory_device_t* memdev = kaapi_the_dsm.nodes[lid_dest]->device;
  if (memdev)
  {
    while (valid_bit !=0)
    {
      lid_src = memdev->f_get_source( memdev, lid0, valid_bit, xfer_bit );
      if (lid_src == (uint16_t)-1) break;
      valid_bit &= ~(1<<lid_src);
      if (kaapi_memory_replica_is_valid(mdi, lid_src))
        return lid_src;
    }

#if KAAPI_USE_FAVOR_D2D_1 // 1 to activate multi-OP
    /* not found valid lid. Check if xfer source exist */
    while (xfer_bit !=0)
    {
      lid_src = memdev->f_get_source( memdev, lid0, 0, xfer_bit );
      if (lid_src == (uint16_t)-1)
        goto reload;
      xfer_bit &= ~(1<<lid_src);

      if (kaapi_memory_replica_is_xfer(mdi, lid_src)
       || kaapi_memory_replica_is_valid(mdi, lid_src))
        return lid_src;
    }
    return lid0;
  }
#endif

#elif 0
/* REVOIR: topo en localitydomain & choix algorithmique
*/
#if 1 // for DGX1 or DGX1-MAXQ
// TOPO DGX1 or DGX1-MAXQ
/* 4 levels of affinity. LID=0 == CPU, so shift value +1*/
#define HLEVEL_AFFINITY 4
#define BIT(x) (1<<(1+(x)))
static int affinity[9][HLEVEL_AFFINITY] = {
  { 0, ~0,         0,                   0},
  { BIT(1), BIT(3)|BIT(4), BIT(1)|BIT(2), BIT(5)|BIT(6)|BIT(7)},
  { BIT(2), BIT(2)|BIT(5), BIT(0)|BIT(3), BIT(4)|BIT(6)|BIT(7)},
  { BIT(3), BIT(1)|BIT(3), BIT(0)|BIT(6), BIT(4)|BIT(5)|BIT(7)},
  { BIT(4), BIT(0)|BIT(2), BIT(1)|BIT(7), BIT(4)|BIT(5)|BIT(6)},
  { BIT(5), BIT(0)|BIT(7), BIT(5)|BIT(6), BIT(1)|BIT(2)|BIT(3)},
  { BIT(6), BIT(1)|BIT(6), BIT(4)|BIT(7), BIT(0)|BIT(2)|BIT(3)},
  { BIT(7), BIT(5)|BIT(7), BIT(2)|BIT(4), BIT(0)|BIT(1)|BIT(3)},
  { BIT(8), BIT(4)|BIT(6), BIT(3)|BIT(5), BIT(0)|BIT(1)|BIT(2)}
};
#else // for BLAISE
// TOPO BLAISE
/* 3 levels of affinity. LID=0 == CPU, so shift value +1*/
#define BIT(x) (1<<(1+(x)))
#define HLEVEL_AFFINITY 3
static int affinity[5][HLEVEL_AFFINITY] = {
  { 0, ~0,         0,       },
  { 0, BIT(3), BIT(1)|BIT(2)},
  { 1, BIT(2), BIT(0)|BIT(3)},
  { 2, BIT(1), BIT(0)|BIT(3)},
  { 3, BIT(0), BIT(1)|BIT(2)}
};
#endif // DGX1 of MAXQ

  for (int i=1; i<HLEVEL_AFFINITY; ++i)
  {
    if (valid_bit & affinity[lid_dest][i])
    {
redo1:
      lid_src = __builtin_ffsll( valid_bit & affinity[lid_dest][i] );
      if (lid_src ==0) { ++i; continue; }
      --lid_src;
      valid_bit &= ~(1<<lid_src);
      if (!kaapi_memory_replica_is_valid(mdi, lid_src))
      {
        goto redo1; // next valid at same level ?
      }
      kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
      kaapi_assert_debug(kaapi_memory_replica_is_valid(mdi, lid_src));

      return lid_src;
    }
  }
  valid_bit=0;

#if KAAPI_USE_FAVOR_D2D_1 // 1 to activate multi-OP
  /* not found valid lid. Check if xfer source exist */
  for (int i=1; i<HLEVEL_AFFINITY-1; ++i)
  {
    if (xfer_bit & affinity[lid_dest][i])
    {
redo2:
      lid_src = __builtin_ffsll( xfer_bit & affinity[lid_dest][i] );
      if (lid_src ==0) { ++i; continue; }
      --lid_src;
      xfer_bit &= ~(1<<lid_src);
      kaapi_assert_debug(kaapi_memory_replica_is_xfer(mdi, lid_src)||kaapi_memory_replica_is_valid(mdi,lid_src));
      if (!kaapi_memory_replica_is_xfer(mdi, lid_src)
       && !kaapi_memory_replica_is_valid(mdi, lid_src))
      {
        goto redo2; // next valid at same level ?
      }
      kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
      kaapi_assert_debug(kaapi_memory_replica_is_xfer(mdi, lid_src)
        ||kaapi_memory_replica_is_valid(mdi, lid_src));

      return lid_src;
    }
  }
  xfer_bit=0;
  return lid0;
#endif

  goto reload;
#else // if 1

#if 1 // RANDOM
{
  int rnd = rand() % __builtin_popcountll(valid_bit);
  for (int i=0; i<rnd; ++i)
  {
    lid_src = __builtin_ffsll( valid_bit );
    kaapi_assert_debug( lid_src != 0);
    --lid_src;
    valid_bit &= ~(1<<lid_src);
  }
}
#endif
#endif

redo:
  lid_src = __builtin_ffsll( valid_bit );
  if (lid_src == 0) goto reload;
  --lid_src;
  valid_bit &= ~(1<<lid_src);
  if (!kaapi_memory_replica_is_valid(mdi, lid_src))
  {
    goto redo;
  }
  kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
  return lid_src;
}


#if KAAPI_DEBUG_LOW
kaapi_atomic_t pending_xfer={0};
kaapi_atomic_t received_xfer={0};
kaapi_atomic_t count_cbk={0};
kaapi_atomic_t count_cbk_called={0};
typedef struct {
  kaapi_io_cbk_fnc_t    cbk;
  kaapi_io_cbk_fnc_t    cbk0;
  kaapi_metadata_info_t*mdi;
  kaapi_data_replica_t* r;
  int                   lid_dst;
  int                   lid_src;
  uint64_t              valid;
  uint64_t              xfer;
  int                   err;
  kaapi_device_t*       device_topost;
  kaapi_io_stream_t*    ios_topost;
} dbg_cbk_t;

dbg_cbk_t all_cbk[16384];
__thread dbg_cbk_t* current_all_cbk = 0;

void debug_low_set_info_cbk( kaapi_device_t* dev, kaapi_io_stream_t* ios)
{
  if (current_all_cbk ==0) return;
  current_all_cbk->device_topost = dev;
  current_all_cbk->ios_topost = ios;
}

static void callback_called(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{ }
#endif


/* Activate the list of callbacks for a given replica
*/
static void callback_activate_replica_on_receive_cbk(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  kaapi_metadata_info_t* mdi = (kaapi_metadata_info_t*)arg0;
  kaapi_address_space_id_t asid = (kaapi_address_space_id_t)arg1;
  uint16_t lid_src = (uint16_t)(uintptr_t)arg2;
  kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

//printf("*** CBK end of xfer data @%p: src:%i -> dest: %i\n", (void*)mdi, lid_src, lid); fflush(stdout);

  /* begin critical section */
  kaapi_atomic_lock(&mdi->replicas[lid]->lock);

  /* mark data received : unxfer source data and set validity */
  kaapi_memory_replica_set_valid(mdi, lid);
  kaapi_memory_replica_unset_xfer(mdi, lid);
#if KAAPI_DEBUG_LOW
  KAAPI_ATOMIC_INCR(&received_xfer);
#endif

#if KAAPI_DEBUG_LOW
  //printf("r:@%p -> cbk length: %i\n", mdi->replicas[lid], mdi->replicas[lid]->count);
  mdi->replicas[lid]->count = 0;
#endif

  /* activate cbk and unpin memory once per callback (see fetch_on) */
  kaapi_data_replica_cbk_t* drc0 = &mdi->replicas[lid]->cbk;
  kaapi_data_replica_cbk_t* drc = drc0;
  do {
#if KAAPI_DEBUG_LOW
    KAAPI_ATOMIC_INCR(&count_cbk_called);
#endif
#if LOG1
    printf("Call cbk: %p mdi: %p, lid: %i\n", drc, mdi, lid);
#endif
    kaapi_assert_debug( drc->iocbk.fnc !=0 );
    if (drc->iocbk.fnc !=0)
    {
      drc->iocbk.fnc( status, ios,
          drc->iocbk.arg[0], drc->iocbk.arg[1], drc->iocbk.arg[2]
      );
    }
#if KAAPI_DEBUG_LOW
    int idx = drc->idx % 16384;
    /* is assert becomes fails it could also be due to bounded size of all_cbk */
    kaapi_assert_debug( all_cbk[idx].cbk == drc->iocbk.fnc );
    all_cbk[idx].cbk = callback_called;
#endif
    /* reset callback  */
#if LOG1
    printf("Reset cbk: %p\n",drc);
#endif
    drc->iocbk.fnc = 0;

    kaapi_data_replica_cbk_t* drcn = drc->next;
    if (drc != drc0) free(drc);
    drc = drcn;
  } while (drc !=0);
  drc0->next = 0;
  kaapi_assert_debug(drc0->next == 0);
  kaapi_assert_debug(drc0->iocbk.fnc == 0);

  /* end critical section */
  kaapi_atomic_unlock(&mdi->replicas[lid]->lock);
}


/*
*/
static void callback_nop(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* _drc, void* _asid, void* _mdi
)
{
  //printf("*** CBK nop\n"); fflush(stdout);
}


/*
*/
static void callback_resend_replica_on_callback_cbk(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* _cbkarg, void* _asid, void* _mdi
)
{
  uint16_t lid_src = (uint16_t)(uintptr_t)_cbkarg;
  kaapi_address_space_id_t asid = (kaapi_address_space_id_t)_asid;
  kaapi_metadata_info_t* mdi = (kaapi_metadata_info_t*)_mdi;
  kaapi_data_replica_t* src_replica  = mdi->replicas[lid_src];
  kaapi_data_replica_cbk_t* drc = &src_replica->cbk;

  uint16_t lid = kaapi_memory_asid_get_lid(asid);

  /* if dest was already received: nothing to do */
  kaapi_assert( !kaapi_memory_replica_is_valid(mdi,lid) );
  if (kaapi_memory_replica_is_valid(mdi, lid))
  {
    if (drc->iocbk.fnc !=0)
    {
      drc->iocbk.fnc( status, ios, drc->iocbk.arg[0], drc->iocbk.arg[1], drc->iocbk.arg[2] );
      drc->iocbk.fnc = 0;
    }
    kaapi_abort(__LINE__, __FILE__, "*** should never occurs (I hop assert is true :-))");
  }
  else
  {
    kaapi_data_replica_t* dest_replica = mdi->replicas[lid];
    kaapi_assert_debug( src_replica->ptr.ptr != 0 );
    kaapi_assert_debug( dest_replica->ptr.ptr != 0 );

#if LOG1
    printf("* resend callback, drc: @%p, mdi: %p, src: %i, dest: %i\n",
        (void*)drc, (void*)mdi, lid_src, lid);
    fflush(stdout);
#endif
    int err = kaapi_memory_copy_async(
        &kaapi_offload_self_device()->memdev, //kaapi_memory_device_get(asid),
        dest_replica->ptr, &dest_replica->view,
        src_replica->ptr, &src_replica->view,
        KAAPI_FETCH_PRIORITY_NORMAL,
        callback_activate_replica_on_receive_cbk,
        mdi, (void*)asid, (void*)(uintptr_t)lid_src
    );
    kaapi_assert((err ==0) || (err ==EINPROGRESS));
  }
}

/* fetch data on node asid if data is not valid on it.
   Look if data is already present or not on asid. If present, then the call returns 0.
   If data is not present a message to get it back is sent except if already under transit.
   Flag is one of KAAPI_FETCH_XX values.
   Return 0: data is already present
   Return EINVAL: data is already valid
   Return EINPROGRESS if a message is send and data is under transfer
*/
static int kaapi_dsm_fetch_on(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_metadata_info_t* mdi,
      uint32_t gen,
      int flags,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  kaapi_assert_debug(
       (flags ==KAAPI_FETCH_PRIORITY_LOW)
    || (flags == KAAPI_FETCH_PRIORITY_NORMAL)
    || (flags == KAAPI_FETCH_PRIORITY_HIGH) );

  /* fast path */
  if (kaapi_memory_replica_is_valid(mdi, lid)) return EINVAL;

  /* get a replica with good locality to be used to send data to lid/asid.
     Note that on return, the replicas[lid_src] is pinned (counter incremented)
     until the call back callback_activate_replica_on_receive_cbk is called (due to
     flag value =1 in the call).
     lid_src may, if D2D heuristic is activated, return a lid with state xfer: a valid
     data is under transfer to lid_src.
  */
  uint16_t lid_src = _kaapi_get_source_lid(dsm, mdi, gen, asid, 1);

  /* emit the communication between lid_src to lid */
  kaapi_data_replica_t* src_replica  = mdi->replicas[lid_src];
  kaapi_data_replica_t* dest_replica = mdi->replicas[lid];
  kaapi_assert_debug( src_replica->ptr.ptr != 0 );
  kaapi_assert_debug( dest_replica->ptr.ptr != 0 );

  /* lock lid_src & lid state evolution. Lock in order < to avoid deadlock. */
  kaapi_lock_t* lock1;
  kaapi_lock_t* lock2;
  if (lid < lid_src) 
  {
    lock1 = &mdi->replicas[lid]->lock;
    lock2 = &mdi->replicas[lid_src]->lock;
  }
  else
  {
    lock1 = &mdi->replicas[lid_src]->lock;
    lock2 = &mdi->replicas[lid]->lock;
  }
  kaapi_atomic_lock(lock1);
  if (lock2 != lock1)
    kaapi_atomic_lock(lock2);

  if (kaapi_memory_replica_is_valid(mdi, lid))
  {
    if (lock2 != lock1)
      kaapi_atomic_unlock(lock2);
    kaapi_atomic_unlock(lock1);
    return EINVAL;
  }

  /* replica where to add callback */
  kaapi_data_replica_t* r = mdi->replicas[lid];

  kaapi_assert_debug( cbk !=0 );
  cbk = cbk == 0 ? callback_nop : cbk;

  /* If selected source lid_src is xfer, then attach callback that will transfer it to lid when
     lid_src will receive it.
  */
  int d2d_route = kaapi_memory_replica_is_xfer(mdi, lid_src) && !kaapi_memory_replica_is_xfer(mdi, lid);
  if (d2d_route)
  {
    /* add callback on lid */
    kaapi_data_replica_cbk_t* drc;
    if (r->cbk.iocbk.fnc == 0)
      drc = &r->cbk;
    else
      drc = malloc(sizeof(kaapi_data_replica_cbk_t));
    drc->iocbk.fnc    = cbk;
    kaapi_assert( drc->iocbk.fnc != 0);
    drc->iocbk.arg[0] = arg0;
    drc->iocbk.arg[1] = arg1;
    drc->iocbk.arg[2] = arg2;
    drc->next = 0;
    if (drc != &r->cbk)
    {
      drc->next   = r->cbk.next;
      r->cbk.next = drc;
    }

    /* Mark destination lid as under xfer to avoid multiple copy */
    kaapi_memory_replica_set_xfer( mdi, lid );

#if LOG1
    printf("Insert resend cbk: %p mdi: %p, lid_src: %i, lid_dest: %i, state: %i\n", drc, mdi, lid_src, lid, (int)KAAPI_ATOMIC_READ(&mdi->xfer));
#endif

    /* replace callback */
    cbk = callback_resend_replica_on_callback_cbk;
    arg0 = (void*)(uintptr_t)lid_src;
    arg1 = (void*)asid;
    arg2 = mdi;
    /* add next callback to replicas[lid_src] to forward data to lid (asid) upon reception */
    r = mdi->replicas[lid_src];
  }

  /* chain callbacks together.
     TODO: optimize memory allocation here
  */
  kaapi_data_replica_cbk_t* drc;
  if (r->cbk.iocbk.fnc == 0)
    drc = &r->cbk;
  else
    drc = malloc(sizeof(kaapi_data_replica_cbk_t));
  drc->iocbk.fnc    = cbk;
  kaapi_assert( drc->iocbk.fnc != 0);
  drc->iocbk.arg[0] = arg0;
  drc->iocbk.arg[1] = arg1;
  drc->iocbk.arg[2] = arg2;
  drc->next = 0;
  if (drc != &r->cbk)
  {
    drc->next   = r->cbk.next;
    r->cbk.next = drc;
  }
#if LOG1
  printf("Insert cbk: %p mdi: %p, lid: %i\n", drc, mdi, (d2d_route ? lid_src : lid));
#endif

#if KAAPI_DEBUG_LOW
  ++r->count;
  int idx = KAAPI_ATOMIC_INCR(&count_cbk);
  drc->idx = idx;
  idx = idx % 16384;
  all_cbk[idx].cbk     = drc->iocbk.fnc;
  all_cbk[idx].cbk0    = drc->iocbk.fnc;
  all_cbk[idx].mdi     = mdi;
  all_cbk[idx].r       = r;
  all_cbk[idx].lid_dst = lid;
  all_cbk[idx].lid_src = lid_src;
  all_cbk[idx].valid   = KAAPI_ATOMIC_READ(&mdi->valid);
  all_cbk[idx].xfer    = KAAPI_ATOMIC_READ(&mdi->xfer);
  current_all_cbk      = &all_cbk[idx]; /* to pass information to lower API */
  all_cbk[idx].err     = -1;
#endif

  /* Return if current fetch op is pending op on destination lid or if d2d_route is on:
     - callback was registered and action will be garantee to be executed (I hope so)
  */
  if  ((kaapi_memory_replica_is_xfer(mdi, lid)) ||  d2d_route)
  {
    /* callback was linked; nothing else to do */
    if (lock2 != lock1)
      kaapi_atomic_unlock(lock2);
    kaapi_atomic_unlock(lock1);
#if KAAPI_DEBUG_LOW
    all_cbk[idx].err = -(kaapi_memory_replica_is_xfer(mdi, lid) ? 1 : 2);
#endif
    return EINPROGRESS;
  }

  kaapi_assert_debug( !kaapi_memory_replica_is_valid(mdi, lid) );
  kaapi_assert_debug( !kaapi_memory_replica_is_xfer(mdi, lid) );

  /* Mark destination lid as under xfer to be valid */
  kaapi_memory_replica_set_xfer( mdi, lid );
#if KAAPI_DEBUG_LOW
  KAAPI_ATOMIC_INCR(&pending_xfer);
#endif

  if (lock2 != lock1)
    kaapi_atomic_unlock(lock2);
  kaapi_atomic_unlock(lock1);

  /* do not have concurrent access to device structure: one thread manages each device */
  kaapi_assert_debug( kaapi_memory_view_size(&dest_replica->view) == kaapi_memory_view_size(&src_replica->view) );

  int err = kaapi_memory_copy_async(
      &kaapi_offload_self_device()->memdev, //kaapi_memory_device_get(asid),
      dest_replica->ptr, &dest_replica->view,
      src_replica->ptr, &src_replica->view,
      flags,
      callback_activate_replica_on_receive_cbk,
      mdi, (void*)asid, (void*)(uintptr_t)lid_src
  );
  kaapi_assert((err ==0) || (err ==EINPROGRESS));
#if KAAPI_DEBUG_LOW
  all_cbk[idx].err     = err;
  current_all_cbk      = 0;
#endif
  return err;
}


/* ~ to pin/unpin memory location
   may return ENOMEM if no more memory on the device
*/
int kaapi_dsm_acquire_data(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_task_t* task,
      kaapi_access_mode_t mp,
      kaapi_metadata_info_t* mdi,
      uint32_t gen,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
)
{
  int err = 0;
  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  uint16_t lid  = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  /* pin the replica for the task */
  kaapi_memory_replica_mark_pinned( mdi, lid );

  /* do allocation if any. Use view on 0 to determine size and layout */
  err = _kaapi_dsm_allocate_replica( dsm, mdi, gen, asid );
  kaapi_assert_debug( (err ==0)||(err == ENOMEM)) ;

  /* here on return there is possible error if device memory is full (ENOMEM).
     TODO: process this error
  */
  if (err !=0) return err;

  kaapi_data_replica_t* r = mdi->replicas[lid];

  /* if read then make copy on device->memorydevice.asid */
  if (KAAPI_ACCESS_IS_READ(mp))
  {
    /* difficult to integrate this test into fetch_on.
       because we need to know before fetch if the data will be received after
       in order to inc the waiting counter of the task.
    */
    if (!kaapi_memory_replica_is_valid(mdi, lid))
    {
      /* need aync comm -> increment waiting counter */
      if (task)
        KAAPI_ATOMIC_INCR(&task->wc);

      err = kaapi_dsm_fetch_on( dsm, asid, mdi, gen, KAAPI_FETCH_PRIORITY_NORMAL, cbk, arg0, arg1, arg2 );
    }

    kaapi_memory_cache_touch( dsm, lid, KAAPI_ACCESS_MODE_R, mdi );
  }
  if (KAAPI_ACCESS_IS_WRITE(mp))
  {
    /* Putting here set_all_dirty is correct because there is no
       concurrency between the end of the tasks and reads thanks to data flow constraints.
       Nevertheless, putting all_dirty while the task is not yet executed will favor
       extra communication from valid data. Hence it would be preferable to delay the
       date where set_all_dirty is called even at the cost of an extra synchronisation to
       wait until the data on the on the fly data transfers.
    */
    kaapi_memory_replica_set_all_dirty_except( mdi, lid );

    /* touch entry in cache as write access */
    kaapi_memory_cache_touch( dsm, lid, KAAPI_ACCESS_MODE_W, mdi );
  }

  return err;
}


/*
*/
int kaapi_dsm_release_data(
      kaapi_metadata_info_t* mdi,
      kaapi_address_space_id_t asid,
      kaapi_access_t* a
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  kaapi_assert( a->mdi == mdi );
  kaapi_memory_replica_unmark_pinned( mdi, lid );

  if (lid ==0)
  {
    kaapi_assert_debug( KAAPI_ATOMIC_READ(&mdi->replicas[0]->pinned) >1 );
  }

  /* here is m == write and a->next ==0 => back to the host memory ? */
  return 0;
}


/* May returns:
     EINPROGRESS : if data not yet on asid
     ENOMEM : not possible to allocate replica
*/
int kaapi_dsm_prefetch_on(
      kaapi_dsm_t* dsm,
      kaapi_address_space_id_t asid,
      kaapi_metadata_info_t* mdi,
      uint32_t gen,
      kaapi_io_cbk_fnc_t cbk,
      void* arg0, void* arg1, void* arg2
)
{
  uint16_t lid = kaapi_memory_asid_get_lid(asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  /* if not allocated: do allocation. Use view on 0 to determine size and layout */
  int err = _kaapi_dsm_allocate_replica( dsm, mdi, gen, asid );
  kaapi_assert_debug( (err ==0)||(err == ENOMEM)) ;
  if (err !=0) return err;

  if (!kaapi_memory_replica_is_valid(mdi, lid))
    return kaapi_dsm_fetch_on( dsm, asid, mdi, gen, KAAPI_FETCH_PRIORITY_LOW,
        cbk, arg0, arg1, arg2
    );

  return 0;
}


/* -------------------------------------------------------------------------- */
/*
*/
void kaapi_memory_synchronize(void)
{
#if KAAPI_USE_OFFLOAD
  kaapi_offload_synchronize();
#endif
  kaapi_mem_barrier();
}

/*
*/
int kaapi_memory_invalidate_caches(void)
{
#if KAAPI_USE_OFFLOAD
  kaapi_offload_invalidate_caches();
#endif
  //kaapi_mem_barrier();
//TG BUG here: done in unregister ? 
kaapi_memory_freehashmap( &kaapi_the_dsm.nodes[0]->ht, kaapi_local_asid );
#if CHECK
{
    kaapi_hashmap_t* ht = &kaapi_the_dsm.nodes[0]->ht;
    uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
    kaapi_hashentries_t* entry;
    for (uint32_t i = 0; i<kaapi_hashmap_sizeentries(ht); ++i)
    {
      entry = _get_hashmap_entry(ht, i);
      if (entry !=0)
      {
        printf("Error: non null entry %i after clearing hashmap\n",i);
        while (entry !=0)
        {
          kaapi_metadata_info_t*mdi = KAAPI_HASHENTRIES_GET(entry, kaapi_metadata_info_t*);
          printf("\tmdi: %p\n", mdi);
          entry = entry->next;

        }
      }
    }
}
#endif

  return 0;
}

/*
*/
struct kaapi_memgroup {
  kaapi_atomic_t counter;
#if 0 // TODO for non active waiting loop
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
  int iswaiting;
#endif
};

/*
*/
int kaapi_memory_group_init( kaapi_memgroup_t* grp)
{
  KAAPI_ATOMIC_WRITE(&grp->counter, 0);
#if 0
  kaapi_assert(0 == pthread_mutex_init(&grp->mutex, 0));
  kaapi_assert(0 == pthread_cond_init(&grp->cond, 0));
  grp->iswaiting = 0;
#endif
  return 0;
}

/*
*/
static void callback_signal_grp_on_receive_cbk(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  kaapi_memgroup_t* grp    = (kaapi_memgroup_t*)arg0;
  kaapi_metadata_info_t* mdi __attribute__((unused)) = (kaapi_metadata_info_t*)arg1;
  kaapi_address_space_id_t asid  __attribute__((unused)) = (kaapi_address_space_id_t)arg2;
  KAAPI_ATOMIC_DECR(&grp->counter);
}

/*
*/
int kaapi_memory_sync_data(kaapi_memgroup_t* grp, void* ptr)
{
  uint16_t lid0 = kaapi_memory_asid_get_lid( kaapi_local_asid );
  kaapi_assert_debug(lid0 < KAAPI_MEMORY_MAX_NODES);
  kaapi_access_t a;
  kaapi_access_init(&a, ptr);

  kaapi_metadata_info_t* mdi = kaapi_dsm_findaccess_on_node(
      &kaapi_the_dsm,
      kaapi_local_asid,
      0, /* do not create if not existing */
      &a,
      0
  );
  if (mdi ==0)
    return EINVAL;

  if (kaapi_memory_replica_is_valid(mdi, lid0))
    return 0;

  KAAPI_ATOMIC_INCR(&grp->counter);
  int err = kaapi_dsm_fetch_on(
      &kaapi_the_dsm,
      kaapi_local_asid,
      mdi,
      (uint32_t)-1,
      KAAPI_FETCH_PRIORITY_NORMAL,
      callback_signal_grp_on_receive_cbk, (void*)grp, (void*)mdi, (void*)kaapi_local_asid
  );
  if (err != EINPROGRESS)
    KAAPI_ATOMIC_DECR(&grp->counter);
  return err;
}

/*
*/
int kaapi_memory_group_wait( kaapi_memgroup_t* grp )
{
  useconds_t d= 1;
  while (1)
  {
    if (KAAPI_ATOMIC_READ(&grp->counter) == 0) return 0;
    usleep(d);
    d *= 2;
    if (d > 1000) d = 1000;
  }

#if 0
  kaapi_assert(0 == pthread_mutex_lock(&grp->mutex));
  grp->iswaiting = 1;
  while (KAAPI_ATOMIC_READ(&grp->counter) != 0)
    kaapi_assert(0==pthread_cond_wait(&grp->cond, &grp->mutex));
  grp->iswaiting = 0;
  kaapi_assert(0 == pthread_mutex_unlock(&grp->mutex));
#endif
  return 0;
}

/*
*/
int kaapi_memory_group_destroy( kaapi_memgroup_t* grp )
{
#if 0
  kaapi_assert(0 == pthread_mutex_destroy(&grp->mutex));
  kaapi_assert(0 == pthread_cond_destroy(&grp->cond));
#endif
  return 0;
}



/* -------------------------------------------------------------------------- */
/* Allocate the asid for the newly registered device
*/
int kaapi_dsm_register_device(
    kaapi_dsm_t* dsm,
    kaapi_memory_device_t* device,
    int arch
)
{
  int err;
  if (arch == KAAPI_PROC_TYPE_HOST)
    device->asid = kaapi_local_asid;
  else
    device->asid = kaapi_memory_create_asid(
      0, /* global id */
      KAAPI_ATOMIC_INCR(&kaapi_dsm_asid_lid), /* lid */
      arch
    );

  kaapi_atomic_initlock(&device->mem_lock);
  device->freelist_bloc = 0;
  device->freelist_metabloc  = 0;

  uint16_t lid = kaapi_memory_asid_get_lid(device->asid);
  if (lid >= KAAPI_MEMORY_MAX_NODES)
  {
    printf("*** Number of requested memory nodes is upper than defined limit\n"
           "    Please augment 'KAAPI_MEMORY_MAX_NODES' in kaapi_memory.h\n"
    );
    return ENOMEM;
  }
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (dsm->nodes[lid] !=0) return 0; /* already initialized */

  kaapi_dsm_node_t* node = (kaapi_dsm_node_t*)malloc(sizeof(kaapi_dsm_node_t));
  if (node ==0) return ENOMEM;
  err = kaapi_hashmap_init(&node->ht, node->mapentries, KAAPI_SIZE_DSM_MAP, 0);
  if (err) return err;
  node->device = device;
  node->cache = kaapi_memory_cache_init(device, device->asid, 0);
  if (err) return ENOMEM;
  kaapi_the_dsm.nodes[lid] = node;

  return 0;
}


/* Unregister a device to the dsm
*/
int kaapi_dsm_unregister_device(
    kaapi_dsm_t* dsm,
    kaapi_memory_device_t* device
)
{
  int err;
  uint16_t lid = kaapi_memory_asid_get_lid(device->asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);

  /* node 0 is processed in dsm_finalize */
  if (lid ==0) return 0;

  /* suppress device */
  kaapi_dsm_node_t* node = dsm->nodes[lid];
  dsm->nodes[lid] = 0;

  /*
  */
  kaapi_memory_freelist_destroy(node->device);
  device->freelist_bloc = 0;
  device->freelist_metabloc  = 0;
  kaapi_atomic_destroylock(&device->mem_lock);

  err = kaapi_memory_cache_destroy( node->cache);
  if (err) return err;
  node->device = 0;
  node->cache = 0;
  err = kaapi_hashmap_clear(&node->ht);
  if (err) return err;
  err = kaapi_hashmap_destroy(&node->ht);
  if (err) return err;
  free(node);
  kaapi_the_dsm.nodes[lid] = 0;

  return 0;
}


int kaapi_dsm_init( void )
{
  int err = 0;
  memset( &kaapi_the_dsm.nodes, 0, sizeof(kaapi_dsm_node_t*)*KAAPI_MEMORY_MAX_NODES);
#if KAAPI_DEBUG_MEMORY_ALLOC
  kaapi_hashmap_init(&free_ptr_ht, free_mapentries, KAAPI_SIZE_DBG_MAP_ALLOC, 0);
#endif
#if MEM_ALLOC_FREELIST
  if (getenv("KAAPI_VERBOSE"))
    printf("[xkaapi] use free list allocator\n" );
#endif
#if KAAPI_DEBUG_LOW
  memset(all_cbk, 0, sizeof(all_cbk));
#endif

  /* fill virtual node 0 as the host node */
  kaapi_dsm_node_t* node = (kaapi_dsm_node_t*)malloc(sizeof(kaapi_dsm_node_t));
  if (node ==0) return ENOMEM;
  kaapi_atomic_initlock(&node->lock);
  err = kaapi_hashmap_init(&node->ht, node->mapentries, KAAPI_SIZE_DSM_MAP, 0);
  if (err) return err;
  node->device = 0;
  node->cache = 0;
  kaapi_the_dsm.nodes[0] = node;
  return err;
}


/*
*/
int kaapi_dsm_commit( void )
{
  int err = 0;
  kaapi_the_dsm.mask_level;  // topology level: size of mask_nodes[]
  uint64_t          *mask_nodes;

  return err;
}


/*
*/
int kaapi_dsm_finalize( void )
{
  int err;
  kaapi_memory_freehashmap( &kaapi_the_dsm.nodes[0]->ht, kaapi_local_asid );

  err = kaapi_memory_cache_destroy( kaapi_the_dsm.nodes[0]->cache);
  if (err) return err;

  err = kaapi_hashmap_clear(&kaapi_the_dsm.nodes[0]->ht);
  if (err) return err;
  err = kaapi_hashmap_destroy(&kaapi_the_dsm.nodes[0]->ht);
  if (err) return err;
  kaapi_atomic_destroylock(&kaapi_the_dsm.nodes[0]->lock);
  free(kaapi_the_dsm.nodes[0]);
  kaapi_the_dsm.nodes[0] = 0;

  /* Reset all device */
  KAAPI_ATOMIC_WRITE(&kaapi_dsm_asid_lid,0);

  return 0;
}



/* for debug */
static void print_info()
{
  size_t c1=0, c2=0, c3=0;
  for (int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    printf(" exec:%i   <-> spawn:%i + push:%i  = %i\n", 
      device->exec_count, 
      device->spawn_count, 
      device->ld->queue->push_count, 
      device->spawn_count + device->ld->queue->push_count );
    c1 += device->exec_count;
    c2 += device->spawn_count;
    c3 += device->ld->queue->push_count;
  }
  printf("Total: exec:%i <-> spawn:%i + push:%i = %i\n", c1, c2, c3, c2+c3);
#if KAAPI_DEBUG
  extern void kaapi_offload_print_stream_info(kaapi_offload_stream_t* stream);
  for (int i=1; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    printf("Device: %i\n", i);
    kaapi_offload_print_stream_info( &device->stream );
  }
  printf("  \n");
#endif

#if KAAPI_DEBUG_LOW
  extern kaapi_atomic_t count_queue_push;
  extern kaapi_atomic_t count_queue_pop;

  c1 = 0; c2 = 0;
  for (int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    if (device->ctxt ==0) continue;
    printf("Device:%i :: queue: push:%i   <-> pop:%i\n", 
      i,
      KAAPI_ATOMIC_READ(&device->ctxt->queue->cnt_push),
      KAAPI_ATOMIC_READ(&device->ctxt->queue->cnt_pop));
    c1 += KAAPI_ATOMIC_READ(&device->ctxt->queue->cnt_push);
    c2 += KAAPI_ATOMIC_READ(&device->ctxt->queue->cnt_pop);
  }
  printf("        sum: push: %i / pop: %i\n", c1, c2);
  printf("Total queue: push: %i / pop: %i\n", KAAPI_ATOMIC_READ(&count_queue_push), KAAPI_ATOMIC_READ(&count_queue_pop));

  c1 = 0; c2 = 0;
  for (int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    if (device->ctxt ==0) continue;
    printf("Device:%i :: mailbox: push:%i   <-> pop:%i\n", 
      i,
      device->ld->queue->push_count,
      device->ld->queue->pop_count 
    );
    c1 += device->ld->queue->push_count;
    c2 += device->ld->queue->pop_count;
  }
  printf("Total fifo queue: push: %i / pop: %i\n", c1, c2 );

  printf("Callback  (#/called): %i/%i\n", KAAPI_ATOMIC_READ(&count_cbk), KAAPI_ATOMIC_READ(&count_cbk_called));

  for (int i=1; i<16384; ++i)
  {
    if ((all_cbk[i].cbk !=0) && (all_cbk[i].cbk != callback_called))
    {
      printf("    -- callback: %i not called\n", i);
    }
  }
#endif
}
