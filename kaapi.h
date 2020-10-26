/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** KAAPI_TASK_FLAG_LD_BOUNDvincent.danjean@imag.fr
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
#ifndef _KAAPI_H
#define _KAAPI_H 1

#define KAAPI_USE_OFFLOAD 1

/* #define KAAPI_NO_TSO_ARCH */

/*!
    @header kaapi
    @abstract This is the public header for uKaapi
*/
#define KAAPI_H _KAAPI_H

/* .0: initial release
   .1: with merge of static part and new libkomp for supporting libgomp and libiomp5
   .2: next release
*/
#define __KAAPI__ 5
#define __KAAPI_MINOR__ 0

#if !defined(__SIZEOF_POINTER__)
#  if defined(__LP64__) || defined(__x86_64__)
#    define __SIZEOF_POINTER__ 8
#  elif defined(__i386__) || defined(__ppc__)
#    define __SIZEOF_POINTER__ 4
#  else
#    error KAAPI needs __SIZEOF_* macros. Use a recent version of gcc
#  endif
#endif

#if ((__SIZEOF_POINTER__ != 4) && (__SIZEOF_POINTER__ != 8)) 
#  error KAAPI cannot be compiled on this architecture due to strange size for __SIZEOF_POINTER__
#endif

#ifndef __BIGGEST_ALIGNMENT__
#  define __BIGGEST_ALIGNMENT__ 16
#endif


#include <stdint.h>
#include <stddef.h>
#include "kaapi_error.h"
#include "kaapi_atomic.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__linux__)
#  define KAAPI_HAVE_COMPILER_TLS_SUPPORT 1
#elif defined(__APPLE__)
#endif

#if !defined(KAAPI_CACHE_LINE)
#define KAAPI_CACHE_LINE 64
#endif


/* Kaapi types.
 */
typedef uint32_t  kaapi_globalid_t;
typedef uint32_t  kaapi_processor_id_t;
typedef uint8_t   kaapi_format_id_t;

/* processor type for format */
#define KAAPI_PROC_TYPE_DEFAULT 0x0
#define KAAPI_PROC_TYPE_HOST    0x0
#define KAAPI_PROC_TYPE_CUDA    0x1
#define KAAPI_PROC_TYPE_CPU     KAAPI_PROC_TYPE_HOST
#define KAAPI_PROC_TYPE_GPU     KAAPI_PROC_TYPE_CUDA
#define KAAPI_PROC_TYPE_MAX 2


/* Fwd decl
*/
struct kaapi_task;
typedef struct kaapi_task kaapi_task_t;
struct kaapi_thread;
typedef struct kaapi_thread kaapi_thread_t;
struct kaapi_team;
typedef struct kaapi_team kaapi_team_t;
struct kaapi_frame;
typedef struct kaapi_frame kaapi_frame_t;
struct kaapi_format;
typedef struct kaapi_format kaapi_format_t;
struct kaapi_queue;
typedef struct kaapi_queue kaapi_queue_t;
struct kaapi_fifo_queue;
typedef struct kaapi_fifo_queue kaapi_fifo_queue_t;
struct kaapi_device;
typedef struct kaapi_device kaapi_device_t;
struct kaapi_memgroup;
typedef struct kaapi_memgroup kaapi_memgroup_t;
struct kaapi_memgroup;
typedef struct kaapi_memgroup kaapi_memgroup_t;
struct kaapi_localitydomain;
typedef struct kaapi_localitydomain kaapi_localitydomain_t;

typedef struct kaapi_listrequest_iterator kaapi_listrequest_iterator_t;


/** \ingroup Kaapi
    Identifier to a locality domain 
*/
typedef uint64_t  kaapi_ldid_t;


/** \ingroup Kaapi
    Type of locality domain.
    For each type the associated functions with return the description
    in term of capability (#ressource, #memory etc)
*/
typedef enum
{
  KAAPI_LD_CORE    = 0x01, /* core type */
  KAAPI_LD_NUMA    = 0x02, /* NUMA type */
  KAAPI_LD_SOCKET  = 0x04, /* socket type */
  KAAPI_LD_BOARD   = 0x08, /* machine type */
  KAAPI_LD_GPU     = 0x10, /* GPUs type */
  KAAPI_LD_ALLTYPE = 0x1F  /* encode union of previous type */
} kaapi_ld_type_t;

#define KAAPI_LD_COUNTTYPE 5


/* ========================================================================= */
/* Initialization & environement functions                                   */
/* ========================================================================= */
/* Main function: initialization of the library; terminaison and abort        
   In case of normal terminaison, all internal objects are (I hope so !) deleted.
   The abort function is used in order to try to flush most of the internal buffer.
   kaapi_init should be called before any other kaapi function.
   \param flag [IN] if !=0 then start execution in parallel, else only the main thread is started
   \retval 0 in case of sucsess
   \retval EALREADY if already called
*/
extern int kaapi_init(void);

/* Kaapi finalization. 
   After call to this functions all other kaapi function calls may not success.
   \retval 0 in case of sucsess
   \retval EALREADY if already called
*/
extern int kaapi_finalize(void);


/* Get the id of the thread.
   The id set to the thread when attach to a team.
   \retval processor kid
*/
extern unsigned int kaapi_thread_kid( kaapi_thread_t* );


/** Get the workstealing concurrency number, i.e. the number of kernel
    activities to execute the user level thread. 
    This function is machine dependent.
    \retval the number of active threads to steal tasks
 */
extern int kaapi_get_concurrency (void);



/* ========================================================================= */
/* Kaapi Thread                                                              */
/* ========================================================================= */

/* stack of tasks is managed by bloc of given size */
#define KAAPI_STACKBLOCSIZE 1048576ULL

/* Kaapi thread ~ tasks and data allocator in its public interface.
   sp points to address aligned to KAAPI_STACKBLOCSIZE boundary.
*/
struct kaapi_thread {
  void*                  sp;
  uint64_t               cnt;
};


/* Thread registers to save/ restore thread stack of tasks
*/
typedef struct kaapi_thread_register_t {
  kaapi_task_t*          pc;
  char*                  sp;
  void*                  reserved1;
  void*                  reserved2;
  void*                  reserved3;
  void*                  reserved4;
} kaapi_thread_register_t __attribute__((aligned(8)));

/* returns the top task, only valid if their is no push of data between the
   last call to task_alloc and this function call
*/
static inline kaapi_task_t* kaapi_thread_top_task(kaapi_thread_t* thread )
{ return (kaapi_task_t*)thread->sp; }

extern kaapi_task_t* kaapi_thread_base_task(kaapi_thread_t* thread );
extern kaapi_task_t* kaapi_thread_parent_task(kaapi_thread_t* thread );
extern kaapi_task_t* kaapi_thread_current_task(kaapi_thread_t* thread );
extern int kaapi_thread_set_current_task(kaapi_thread_t* thread, kaapi_task_t* task );



/* ========================================================================= */
/* Kaapi Task                                                                */
/* ========================================================================= */
#ifndef KAAPI_NATIVE_FLAGS_T
#define KAAPI_NATIVE_FLAGS_T
#endif
#ifndef KAAPI_NATIVE_TASK_T
#define KAAPI_NATIVE_TASK_T
#endif
typedef uint32_t kaapi_task_flag_t;

/* Task body
*/
typedef uint16_t kaapi_task_body_t;
typedef void (*kaapi_task_bodyfnc_t)( kaapi_task_t*, kaapi_thread_t* );
typedef void (*kaapi_task_bodyfnc_cpu_t)( kaapi_task_t*, kaapi_thread_t* );
typedef void (*kaapi_task_bodyfnc_gpu_t)( kaapi_task_t*, kaapi_thread_t*, void* );

/* A Kaapi task is the basic unit of computation.
   It has a constant size including some task's specific values.
   The body field is the entry point in the task format definition
   In this implementation V0:
    - the native object (kmp task or other task from other runtime) is assume to be
    stored a (task+1)
    - task+1 is the argument of the task
   If ld is not enough (should be able to define system wide id for ressources), then
   it should be increased. Or (not exclusive) controling scheduling queue and locality
   should be redesigned.
*/
struct kaapi_task {
  uint16_t                     body;    // == id of the format describing entry point 
  uint16_t                     flags;
  unsigned int                 prio: 3; // iff flag KAAPI_TASK_FLAG_PRIORITY is set, else ignored
  unsigned int                 ld: 13;  // iff flag KAAPI_TASK_FLAG_LD_BOUND is set, else ignored
  kaapi_atomic16_t             wc;
  kaapi_frame_t*               frame;
};

typedef kaapi_task_t kaapi_task_withperfcnt_t;

/* Flag for task.
    Note that this is only for user level construction.
    Some values are exclusives (e.g. COOPERATIVE and CONCURRENT),
    and are only represented by one bit.
*/
#if !defined(KAAPI_TASK_FLAG_SHIFT)
#define KAAPI_TASK_FLAG_SHIFT 0
#endif

#define KAAPI_TASK_FLAG(f) ( (1 << (f)) << KAAPI_TASK_FLAG_SHIFT )
enum  {
  KAAPI_TASK_FLAG_DEFAULT       = 0, /* default value for initializing task flag */
  KAAPI_TASK_FLAG_UNSTEALABLE   = KAAPI_TASK_FLAG(0), /* task is not stealable */
  KAAPI_TASK_FLAG_SPLITTABLE    = KAAPI_TASK_FLAG(1), /* task may be split on demand */
  KAAPI_TASK_FLAG_INDEPENDENT   = KAAPI_TASK_FLAG(2), /* task is independent */
  KAAPI_TASK_FLAG_LD_BOUND      = KAAPI_TASK_FLAG(3), /* Bound to a locality domain */
  KAAPI_TASK_FLAG_PRIORITY      = KAAPI_TASK_FLAG(4), /* if set then high priority task */
  KAAPI_TASK_FLAG_AFF_ADDR      = KAAPI_TASK_FLAG(5), /* affinity with respect site = index of param */
  KAAPI_TASK_FLAG_AFF_NODE      = KAAPI_TASK_FLAG(6), /* affinity with respect to a numa node */
  KAAPI_TASK_FLAG_AFF_CORE      = KAAPI_TASK_FLAG(7), /* affinity with respect to a core */
  KAAPI_TASK_FLAG_NOLINK        = KAAPI_TASK_FLAG(8), /* do not link task in readylist */
  KAAPI_TASK_FLAG_OUTCOM        = KAAPI_TASK_FLAG(9), /* task that may generates outcom */
  KAAPI_TASK_FLAG_INCOM         = KAAPI_TASK_FLAG(10),/* task that may generates incom */
  KAAPI_TASK_FLAG_EXEC          = KAAPI_TASK_FLAG(11),/* mark task executed */
  KAAPI_TASK_PERFCNT            = KAAPI_TASK_FLAG(12),/* task will register perfcounter */
};

#define KAAPI_TASK_FLAG_AFF_MASK \
  (KAAPI_TASK_FLAG_AFF_ADDR|KAAPI_TASK_FLAG_AFF_NODE\
  |KAAPI_TASK_FLAG_AFF_CORE|KAAPI_TASK_FLAG_AFF_OCR)
#define KAAPI_TASK_MAX_PRIORITY     1
#define KAAPI_TASK_MIN_PRIORITY     0 /* must be 0 because the default initialisation == 0 */

#define KAAPI_TASK_LD_MASK_PARAM (1UL<<12) /* on 13th bit */
#define KAAPI_TASK_LD_MAX ((1UL<<12) -1)   /* max value on remaining 12 bits */
#define KAAPI_TASK_HIGH_PRIORITY    KAAPI_TASK_MAX_PRIORITY
#define KAAPI_TASK_DEFAULT_PRIORITY KAAPI_TASK_MIN_PRIORITY

static inline void kaapi_taskflag_set( kaapi_task_t* t, uint32_t flag )
{ t->flags |= flag; }
static inline void kaapi_taskflag_unset( kaapi_task_t* t, uint32_t flag )
{ t->flags &= ~flag; }
static inline uint32_t kaapi_taskflag_get( kaapi_task_t* t, uint32_t flag )
{ return t->flags & flag; }


/* ========================================================================= */
/* Context                                                                   */
/* ========================================================================= */
#define kaapi_thread2context(th) ((kaapi_context_t*)(th))
#define kaapi_context2thread(ctxt) ((kaapi_thread_t*)(ctxt))

/* ========================================================================= */
/* Access to data                                                            */
/* ========================================================================= */
/** Kaapi access mode mask
    \ingroup DFG
*/
/*@{*/
typedef enum kaapi_access_mode {
  KAAPI_ACCESS_MODE_VOID= 0x00,        /* 0000 0000 : */
  KAAPI_ACCESS_MODE_V   = 0x01,        /* 0000 0001 : */
  KAAPI_ACCESS_MODE_R   = 0x02,        /* 0000 0010 : */
  KAAPI_ACCESS_MODE_W   = 0x04,        /* 0000 0100 : */
  KAAPI_ACCESS_MODE_CW  = 0x08,        /* 0000 1000 : */
  KAAPI_ACCESS_MODE_S   = 0x10,        /* 0001 0000 : stack data */
  KAAPI_ACCESS_MODE_T   = 0x20,        /* 0010 0000 : temporary */
  KAAPI_ACCESS_MODE_P   = 0x40,        /* 0100 0000 : postponed access mode */
  KAAPI_ACCESS_MODE_IP  = 0x80,        /* 1000 0000 : in place, for CW only */

  KAAPI_ACCESS_MODE_RW      = KAAPI_ACCESS_MODE_R|KAAPI_ACCESS_MODE_W,
  KAAPI_ACCESS_MODE_STACK   = KAAPI_ACCESS_MODE_S|KAAPI_ACCESS_MODE_RW,
  KAAPI_ACCESS_MODE_SCRATCH = KAAPI_ACCESS_MODE_T|KAAPI_ACCESS_MODE_V,
  KAAPI_ACCESS_MODE_CWP     = KAAPI_ACCESS_MODE_P|KAAPI_ACCESS_MODE_CW,
  KAAPI_ACCESS_MODE_ICW     = KAAPI_ACCESS_MODE_IP|KAAPI_ACCESS_MODE_CW,

  KAAPI_ACCESS_SYNC     = 0xFF        /* 1111 1111 :  sync access */
} kaapi_access_mode_t;

#define KAAPI_ACCESS_ALL               0xFF   /* To write assertion on #bits of access_mode */

#define KAAPI_ACCESS_MASK_RIGHT_MODE   0x7F   /* 5 bits, ie bit 0, 1, 2, 3, 4, including P mode */
#define KAAPI_ACCESS_MASK_MODE         0x3F   /* without P, IP mode */
#define KAAPI_ACCESS_MASK_MODE_P       0x40   /* only P mode */
/*@}*/


/* Kaapi access : used to link together tasks through their memory access.
   There are 2 kinds of access:
    - normal access used to store relation between task and data accessed by the task
    - synchronisation access used to encode new version of the data
*/
typedef struct kaapi_access {
  kaapi_access_mode_t  mode:  8;  /* access mode */
  unsigned int         kind:  1;  /* set iff sync access */
  unsigned int         ready: 1;  /* set access is ready */
  unsigned int         reserv:22; /* */
  uint32_t             gen;       /* */
  void*                data;      /* the data */
  struct kaapi_access* next;      /* next conc. access if normal access or accesses to activate */
  struct kaapi_access* sync;      /* sync access. iff kind=1 -> link main syncpoints together */
  kaapi_task_t*        task;      /* the task making this access */
  union {
    void*              mdi;       /* optim: store pointer to metadata if multi-devices is on */
    kaapi_atomic_t     wc;        /* iff synchronisation access */
  };
#if KAAPI_DEBUG
  pthread_t            creator;   /* the thread that create the access */
#endif
} kaapi_access_t;

/*
*/
typedef struct kaapi_access_pair {
  kaapi_access_t*        first;
  kaapi_access_t*        last;
} kaapi_access_pair_t;

/* Handle = last access to a data
   Used to link together tasks without overhead of searching for the last handle.
*/
struct kaapi_handle;
typedef struct kaapi_handle kaapi_handle_t;

/** Kaapi macro on access mode
    \ingroup DFG
*/
/*@{*/
#define KAAPI_ACCESS_GET_MODE( m ) \
  ((m) & KAAPI_ACCESS_MASK_MODE )

#define KAAPI_ACCESS_IS_READ( m ) \
  ((m) & KAAPI_ACCESS_MODE_R)

#define KAAPI_ACCESS_IS_WRITE( m ) \
  ((m) & KAAPI_ACCESS_MODE_W)

#define KAAPI_ACCESS_IS_CUMULWRITE( m ) \
  ((m) & KAAPI_ACCESS_MODE_CW)

#define KAAPI_ACCESS_IS_STACK( m ) \
  ((m) & KAAPI_ACCESS_MODE_S)

#define KAAPI_ACCESS_IS_POSTPONED( m ) \
  ((m) & KAAPI_ACCESS_MASK_MODE_P)

/* W and CW */
#define KAAPI_ACCESS_IS_ONLYWRITE( m ) \
  (KAAPI_ACCESS_IS_WRITE(m) && !KAAPI_ACCESS_IS_READ(m))

#define KAAPI_ACCESS_IS_READWRITE( m ) \
  ( ((m) & KAAPI_ACCESS_MASK_MODE) == (KAAPI_ACCESS_MODE_W|KAAPI_ACCESS_MODE_R))

/** Return true if two modes are concurrents
    a == b and a or b is R or CW
    or a or b is postponed.
*/
#define KAAPI_ACCESS_IS_CONCURRENT(a,b) ((((a)==(b)) && (((b) == KAAPI_ACCESS_MODE_R)||((b)==KAAPI_ACCESS_MODE_CW))) || ((a|b) & KAAPI_ACCESS_MODE_P))
/*@}*/

/* =========================================================================  */
/* Managing data & sub data                                                   */
/* In the API - not yet used but multi-node and multi-GPU will be based on it */
/* =========================================================================  */
typedef uint64_t  kaapi_address_space_id_t;

extern kaapi_address_space_id_t kaapi_local_asid;

/** Type of pointer for all address spaces.
    The pointer encode both the pointer (field ptr) and the location of the address space
    in asid. Pointer arithmetic is allowed on this type on the ptr field.
    If pointer is on device with disjoint adress space, meta is a host data to help storing
    meta data.
*/
typedef struct kaapi_pointer_t {
  kaapi_address_space_id_t asid;
  uintptr_t                ptr;
  uintptr_t                meta;
} kaapi_pointer_t;

/** Type of allowed memory view for the memory interface:
    - 1D array (base, size)
      simple contiguous 1D array
    - 2D array (base, size[2], lda)
      assume a row major storage of the memory : the 2D array has
      size[0] rows of size[1] rowwidth. lda is used to pass from
      one row to the next one.
    The base (kaapi_pointer_t) is not part of the view description
*/
#define KAAPI_MEMORY_VIEW_1D 1
#define KAAPI_MEMORY_VIEW_2D 2  /* assume row major */
#define KAAPI_MEMORY_VIEW_3D 3
#define KAAPI_MEMORY_STORAGE_ROWMAJOR 1
#define KAAPI_MEMORY_STORAGE_COLMAJOR 2
typedef struct kaapi_memory_view_t {
  uintptr_t     offset;
  size_t        size[3];
  size_t        ld;
  size_t        wordsize;
  uint8_t       type;
  uint8_t       storage;
} kaapi_memory_view_t;


/*
*/
static inline int kaapi_pointer_isnull(kaapi_pointer_t p)
{ return p.ptr == 0; }


/*
*/
static inline void* kaapi_pointer2void(kaapi_pointer_t p)
{ return (void*)p.ptr; }

/*
*/
static inline kaapi_pointer_t kaapi_make_pointer( void* ptr, uintptr_t meta, kaapi_address_space_id_t asid)
{ kaapi_pointer_t p; p.asid = asid; p.ptr = (uintptr_t)ptr; p.meta = meta; return p; }

/*
*/
static inline kaapi_pointer_t kaapi_make_nullpointer(kaapi_address_space_id_t asid)
{ kaapi_pointer_t p; p.asid = asid; p.ptr = 0; p.meta = 0; return p; }

/** return the size of the view
*/
static inline size_t kaapi_memory_view_size( const kaapi_memory_view_t* const kmv )
{
  switch (kmv->type)
  {
    case KAAPI_MEMORY_VIEW_1D: return kmv->size[0]*kmv->wordsize;
    case KAAPI_MEMORY_VIEW_2D: return kmv->size[0]*kmv->size[1]*kmv->wordsize;
    default:
      kaapi_assert(0);
      break;
  }
  return 0;
}

/** assume that now the view points to a new allocate view
*/
static inline void kaapi_memory_view_reallocated( kaapi_memory_view_t* kmv )
{
  switch (kmv->type)
  {
    case KAAPI_MEMORY_VIEW_1D: return;
    case KAAPI_MEMORY_VIEW_2D:
      if (kmv->storage == KAAPI_MEMORY_STORAGE_ROWMAJOR)
        kmv->ld = kmv->size[1];
      else
        kmv->ld = kmv->size[0];
      return;
    default:
      kaapi_assert(0);
      break;
  }
}

/** Return non null value iff the view is contiguous
*/
static inline int kaapi_memory_view_iscontiguous( const kaapi_memory_view_t* const kmv )
{
  switch (kmv->type) {
    case KAAPI_MEMORY_VIEW_1D:
      return 1;
    case KAAPI_MEMORY_VIEW_2D:
      return  kmv->storage == KAAPI_MEMORY_STORAGE_ROWMAJOR ?
                  kmv->ld == kmv->size[1] : /* row major storage */
                  kmv->ld == kmv->size[0];
    default:
      break;
  }
  return 0;
}

/*
*/
static inline void* kaapi_memory_view2pointer(void* ptr, const kaapi_memory_view_t* view)
{
  return (void*)((uintptr_t)ptr + (uintptr_t)view->offset*view->wordsize);
}

/* Create a 1D memory view
*/
static inline void kaapi_memory_view_make1d(
    kaapi_memory_view_t* view,
    ptrdiff_t offset,
    size_t size,
    size_t wordsize
)
{
  view->offset   = offset;
  view->size[0]  = size;
  view->wordsize = wordsize;
  view->size[1]  = 0;
  view->ld      = 0;
  view->type     = KAAPI_MEMORY_VIEW_1D;
  view->storage  = KAAPI_MEMORY_STORAGE_ROWMAJOR;
}

/*
*/
static inline void kaapi_memory_view_make2d(
  kaapi_memory_view_t* view,
  ptrdiff_t offset,
  size_t n, size_t m,
  size_t lda, size_t wordsize,
  int storage
)
{
  view->offset   = offset;
  view->size[0]  = n;
  view->size[1]  = m;
  view->ld      = lda;
  view->wordsize = wordsize;
  view->type     = KAAPI_MEMORY_VIEW_2D;
  view->storage  = storage;
  kaapi_assert_debug( (storage == KAAPI_MEMORY_STORAGE_ROWMAJOR) ||
                      (storage == KAAPI_MEMORY_STORAGE_COLMAJOR) );
}

/*
*/
extern int kaapi_memory_copy(
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src
);

/* Make memory coherent with cached data on devices
*/
extern void kaapi_memory_synchronize(void);

/* Invalides all copies
*/
extern int kaapi_memory_invalidate_caches(void);

/* Initialize group of asynchronous operations
*/
extern int kaapi_memory_group_init( kaapi_memgroup_t* );

/* Synchronize copies a data, asynchronous operation
   On completion of the operation the data is valid of the host memory
*/
extern int kaapi_memory_sync_data(kaapi_memgroup_t*, void*);

/* Wait for the completion of all memory group operation
*/
extern int kaapi_memory_group_wait( kaapi_memgroup_t* );

/* Destroy th group
*/
extern int kaapi_memory_group_destroy( kaapi_memgroup_t* );

/* ========================================================================= */
/* Format of a task                                                          */
/* ========================================================================= */
/** predefined format 
*/
/*@{*/
extern kaapi_format_t* kaapi_schar_format;
extern kaapi_format_t* kaapi_char_format;
extern kaapi_format_t* kaapi_shrt_format;
extern kaapi_format_t* kaapi_int_format;
extern kaapi_format_t* kaapi_long_format;
extern kaapi_format_t* kaapi_llong_format;
extern kaapi_format_t* kaapi_int8_format;
extern kaapi_format_t* kaapi_int16_format;
extern kaapi_format_t* kaapi_int32_format;
extern kaapi_format_t* kaapi_int64_format;
extern kaapi_format_t* kaapi_uchar_format;
extern kaapi_format_t* kaapi_ushrt_format;
extern kaapi_format_t* kaapi_uint_format;
extern kaapi_format_t* kaapi_ulong_format;
extern kaapi_format_t* kaapi_ullong_format;
extern kaapi_format_t* kaapi_uint8_format;
extern kaapi_format_t* kaapi_uint16_format;
extern kaapi_format_t* kaapi_uint32_format;
extern kaapi_format_t* kaapi_uint64_format;
extern kaapi_format_t* kaapi_flt_format;
extern kaapi_format_t* kaapi_dbl_format;
extern kaapi_format_t* kaapi_ldbl_format;
extern kaapi_format_t* kaapi_voidp_format;
#if !defined(__STDC_NO_COMPLEX__)
extern kaapi_format_t* kaapi_scplx_format;
extern kaapi_format_t* kaapi_dcplx_format;
#endif
/*@}*/

/* ========================================================================= */
/* Format of data                                                            */
/* ========================================================================= */
/** Allocate a new format data
*/
extern kaapi_format_t* kaapi_format_allocate(void);

/* Type for splitter:
   - called in order to split a running or init task
   - return value is 0 in case of success call to the splitter.
   - return value is ECHILD if no work can be split again.
   This value mean also that all futures calls will failed to split
   work because the work set is empty (forever).
*/
typedef int (*kaapi_adaptivetask_splitter_t)(
  kaapi_task_t*                 /* task to be used to get arguments */,
  kaapi_listrequest_iterator_t* /* iterator over the list*/
);

/* typdefs
*/
typedef void (*kaapi_fmt_fnc_get_name)(
  const kaapi_format_t*, const void*, char* buffer, int size
);
typedef size_t (*kaapi_fmt_fnc_get_size)(
  const kaapi_format_t*, const void*
);
typedef void (*kaapi_fmt_fnc_task_copy)(
  const kaapi_format_t*, void*, const void*
);
typedef unsigned int (*kaapi_fmt_fnc_get_count_params)(
  const kaapi_format_t*, const void*
);
typedef kaapi_access_mode_t (*kaapi_fmt_fnc_get_mode_param)(
  const kaapi_format_t*, unsigned int, const void*
);
typedef void* (*kaapi_fmt_fnc_get_data_param)(
  const kaapi_format_t*, unsigned int, const void*
);
typedef kaapi_access_t* (*kaapi_fmt_fnc_get_access_param)(
  const kaapi_format_t*, unsigned int, const void*
);
typedef void (*kaapi_fmt_fnc_set_access_param)(
  const kaapi_format_t*, unsigned int, void*, const kaapi_access_t*
);
typedef const kaapi_format_t*(*kaapi_fmt_fnc_get_fmt_param)(
  const kaapi_format_t*, unsigned int, const void*
);
typedef int (*kaapi_fmt_fnc_get_affinity)(
  const kaapi_format_t*, const void*, kaapi_task_t*, uint16_t
);
typedef void (*kaapi_fmt_fnc_get_view_param)(
  const kaapi_format_t*, unsigned int, const void*, kaapi_memory_view_t*
);
typedef void (*kaapi_fmt_fnc_set_view_param)(
  const kaapi_format_t*, unsigned int, void*, const kaapi_memory_view_t*
);
typedef void (*kaapi_fmt_fnc_reducor)(
  const kaapi_format_t*, unsigned int, void*, const void*
);
typedef void (*kaapi_fmt_fnc_redinit)(
  const kaapi_format_t*, unsigned int, const void* sp, void*
);
typedef kaapi_adaptivetask_splitter_t  (*kaapi_fmt_fnc_get_splitter)(
  const kaapi_format_t*, const void*
);
typedef void  (*kaapi_fmt_fnc_get_cost)(
  const kaapi_format_t*, const void*, kaapi_task_t*,
  double*, double*
);


/** Register a task format with dynamic definition
    Static version of format is not part of this version of XKaapi
*/
extern kaapi_format_id_t kaapi_format_taskregister_func(
  kaapi_format_t*                fmt,
  void*                          key,
  kaapi_task_bodyfnc_t           body,
  kaapi_task_bodyfnc_gpu_t       body_gpu,
  const char*                    name,
  kaapi_fmt_fnc_get_name         get_name,
  kaapi_fmt_fnc_get_size         get_size,
  kaapi_fmt_fnc_task_copy        task_copy,
  kaapi_fmt_fnc_get_count_params get_count_params,
  kaapi_fmt_fnc_get_mode_param   get_mode_param,
  kaapi_fmt_fnc_get_data_param   get_data_param,
  kaapi_fmt_fnc_get_access_param get_access_param,
  kaapi_fmt_fnc_set_access_param set_access_param,
  kaapi_fmt_fnc_get_fmt_param    get_fmt_param,
  kaapi_fmt_fnc_get_view_param   get_view_param,
  kaapi_fmt_fnc_set_view_param   set_view_param,
  kaapi_fmt_fnc_reducor          reducor,
  kaapi_fmt_fnc_redinit          redinit,
  kaapi_fmt_fnc_get_splitter     get_splitter,
  kaapi_fmt_fnc_get_affinity     get_affinity,
  kaapi_fmt_fnc_get_cost         get_cost
);

/** Register a task body into its format (a variant for the given architecture).
    A task may have multiple implementation this function specifies in 'archi'
    the target archicture for the body.
*/
extern kaapi_task_body_t kaapi_format_taskregister_body(
        kaapi_format_t*      fmt,
        kaapi_task_bodyfnc_t body,
        unsigned int         archi
);

/** Register a data structure format
*/
extern kaapi_format_id_t kaapi_format_structregister(
        kaapi_format_t*      fmt,
        const char*          name,
        size_t               size,
        void               (*cstor)( void* ),
        void               (*dstor)( void* ),
        void               (*cstorcopy)( void*, const void*),
        void               (*copy)( void*, const void*),
        void               (*assign)( void*, const kaapi_memory_view_t*, const void*, const kaapi_memory_view_t*)
);

/** Resolve a format data structure from the body of a task
*/
extern kaapi_format_t* kaapi_format_resolve_byfmid( kaapi_format_id_t fmtid );

/** Resolve a format data structure from the key of its format
*/
extern kaapi_format_t* kaapi_format_resolve_bykey(void* key);

/** Resolve a format data structure from the body of a task
*/
extern kaapi_format_t* kaapi_format_resolve_bybody( kaapi_task_bodyfnc_t body );

/* Return name of a format in buffer
*/
extern void kaapi_format_get_name(
  const kaapi_format_t* fmt, const void* sp, char* buffer, int size
);

/* ========================================================================= */
/* Task and thread interface                                                  */
/* ========================================================================= */

/* Structure of the first task of each thread
*/
typedef struct {
  kaapi_task_t   task;
  void*          arg;
} kaapi_taskmain_t;

/** Allocate and initialize team object
*/
extern kaapi_team_t* kaapi_team_alloc(void);

/** Deallocate team object
*/
extern int kaapi_team_dealloc(kaapi_team_t*);

/** Allocate and initialize team object
*/
extern int kaapi_team_size(kaapi_team_t*);

/** Attach new thread with given id in the team
    Depending of the type of the thread as given in kaapi_thread_bind,
    the thread may also attached to a device (CPU, GPU, ...). In that
    case it is responsable to driver communication between the host and
    the device.
    Depending of the attached devices to its threads, the team has
    a certain 'compute' capability to drive task execution of CPU or GPU or...
*/
extern int kaapi_team_attach(kaapi_team_t*, kaapi_thread_t*, int );

/** Deattach new thread with given id in the team
*/
extern int kaapi_team_deattach(kaapi_team_t*, kaapi_thread_t*);

/** basic performance counter
*/
enum kaapi_counter_name {
  KAAPI_CNT_TASK_SPAWN= 0,
  KAAPI_CNT_TASK_ASYNC_EXEC,
  KAAPI_CNT_TASK_EXEC,
  KAAPI_FLOPS_TASK_EXEC,
  KAAPI_FLOPS_TASK_PENDING,
  KAAPI_CNT_TASK_WORK,
  KAAPI_CNT_TASK_WORK_CPU, /* CPU view sum t2-t1, kaapi_io_kernel*/
  KAAPI_CNT_TASK_WORK_OVERHEAD_CPU, /* CPU view overhead, sum t1-t0+t3-t2 */
  KAAPI_LOAD_NOCOLLISION_COUNT,
  KAAPI_LOAD_COLLISION_COUNT,
  KAAPI_LOAD_COLLISION_GPU,
  KAAPI_CNT_GEMM_ONTC,
  KAAPI_CNT_GEMM_NOTONTC,
  KAAPI_FLOPS_GEMM_ONTC,
  KAAPI_FLOPS_GEMM_NOTONTC,
  KAAPI_CNT_SUSPEND,
  KAAPI_CNT_STEAL_OK,
  KAAPI_CNT_STEAL_NOK,
  KAAPI_CNT_ALLOC,
  KAAPI_CNT_FREE,
  KAAPI_CNT_CPYH2D,
  KAAPI_CNT_CPYD2H,
  KAAPI_CNT_CPYD2D,
  KAAPI_CNT_CPYH2D_BYTES,
  KAAPI_CNT_CPYD2H_BYTES,
  KAAPI_CNT_CPYD2D_BYTES,
  KAAPI_CNT_CACHE_HIT,
  KAAPI_CNT_CACHE_MISS,
  KAAPI_CNT_CACHE_HIT_BYTES,
  KAAPI_CNT_CACHE_MISS_BYTES,
  KAAPI_TIME_PIN_ASYNC,
  KAAPI_TIME_UNPIN_ASYNC,
  KAAPI_TIME_WAITPIN,
  KAAPI_TIME_OS_PIN,
  KAAPI_TIME_OS_UNPIN,
  KAAPI_TIME_OVERHEAD_PIN,
  KAAPI_CNT_REORDER_HIT,
  KAAPI_CNT_REORDER_MISS,
  KAAPI_CNT_REORDER_MISS_LEN,
  KAAPI_CNT_MAX
};

/* return the counter value
*/
extern int kaapi_stat_get_counter( int num, uint64_t* counter );
extern int kaapi_stat_get_dcounter( int num, double* counter );


/* return the counter value counter = get_counter(num) - counter
*/
extern int kaapi_stat_getdiff_counter( int num, uint64_t* counter );
extern int kaapi_stat_reset_counters(void);

/*
*/
extern void kaapi_print_counter(void);
extern int kaapi_counter_set_condition( int num, int (*cond)(void) );


/** Bind a Kaapi thread to the current running thread
    Allocate and return semi-opaque pointer to internal data structure
*/
extern kaapi_thread_t* kaapi_thread_bind(int proctype, size_t user_size);

/** Unbind a Kaapi thread from the current running thread
    Free internal data structure
*/
extern int kaapi_thread_unbind(kaapi_thread_t*);

/** Return the number of locality domains of the team
    This number can be changed using taskset.
*/
extern unsigned int kaapi_localitydomain_count(
    kaapi_ld_type_t ldtype
);

/** Return the id of the i-th locality domain of givent type
    This number can be changed using taskset.
*/
extern kaapi_ldid_t kaapi_localitydomain_get_num(
    kaapi_ld_type_t ldtype,
    unsigned int i
);

/* Return a string with extra information about the localitydomain
*/
extern const char* kaapi_localitydomain_info(
    kaapi_ld_type_t ldtype,
    unsigned int i
);


#define _kaapi_start_blocaddr( addr, type ) \
  ((type)((uintptr_t)(char*)(addr) & ~(KAAPI_STACKBLOCSIZE-1)))


#define _kaapi_end_blocaddr( addr, type ) \
  ((type)((((uintptr_t)(char*)(addr)) & ~(KAAPI_STACKBLOCSIZE-1)) + KAAPI_STACKBLOCSIZE))

#define _kaapi_addr_inbloc( bloc, addr ) \
  ((_kaapi_start_blocaddr(addr,char*) >= (char*)bloc) && \
  (_kaapi_end_blocaddr(addr,char*) < ((char*)bloc+KAAPI_STACKBLOCSIZE)))

#define _kaapi_has_enough_dataspace( thread, size )  \
  (_kaapi_start_blocaddr( (thread)->sp, char*) == _kaapi_start_blocaddr( (char*)(thread)->sp+size, char*))

extern void* __kaapi_start_blocaddr(void* addr);
extern void* __kaapi_end_blocaddr(void* addr);
extern int __kaapi_addr_inbloc(void*bloc, void* addr);
extern int __kaapi_has_enough_dataspace( kaapi_thread_t* thread, size_t size);

/* The function kaapi_access_init() initialize an access from a user defined pointer
    \param access INOUT a pointer to the kaapi_access_t data structure to initialize
    \param ptr INOUT a pointer to the user data
*/
static inline void kaapi_access_init(kaapi_access_t* a, void* ptr )
{
  a->mode = KAAPI_ACCESS_MODE_VOID;
  a->gen  = 0;
  a->kind = 0;
  a->ready= 0;
  a->data = ptr;
  a->next = 0;
  a->sync = 0;
  a->task = 0;
  a->mdi  = 0;
  return;
}

/* The function kaapi_access_sync_init() initialize a synchronisation access 
   from a user defined pointer
    \param access INOUT a pointer to the kaapi_access_t data structure to initialize
    \param ptr INOUT a pointer to the user data
*/
static inline void kaapi_access_sync_init(kaapi_access_t* a, void* ptr )
{
  a->mode = KAAPI_ACCESS_SYNC;
  a->gen  = 0;
  a->kind = 1;
  a->ready= 0;
  a->data = ptr;
  a->next = 0;
  a->sync = 0;
  a->task = 0;
  KAAPI_ATOMIC_WRITE(&a->wc, 0); /* for wc */
  return;
}


/**/
struct kaapi_metadata_info;
typedef struct kaapi_metadata_info kaapi_metadata_info_t;
extern int kaapi_handle_init(kaapi_thread_t* thread, kaapi_handle_t* h, void* data, kaapi_metadata_info_t* mdi);

/* add dependencies: task access to a using access mode
 */
extern int kaapi_update_dependencies(
  kaapi_thread_t* thread,
  kaapi_access_t* a,
  kaapi_task_t* task,
  kaapi_access_mode_t mode,
  kaapi_handle_t* h
);

static inline int kaapi_handle_destroy(kaapi_handle_t* h)
{
  (void)h;
  return 0;
}


/** Return a pointer to parameter of the task (void*) pointer
*/
#define kaapi_task_getargs( task) ((void*)((task)+1))


/** Return a reference to parameter of the task (type*) pointer
*/
#define kaapi_task_getargst(task,type) ((type*)((task)+1))


/* Anormal push
*/
extern void* kaapi_thread_slow_push_data( kaapi_thread_t* thread, unsigned long size );

/** The function kaapi_data_push() will return the pointer to the next top data.
    The top data is not yet into the stack.
    If successful, the kaapi_thread_pushdata() function will return a pointer to the next data to push.
    Otherwise, an 0 is returned to indicate the error.
    \param thread INOUT a pointer to the kaapi_thread_t data structure where to push data
    \retval a pointer to the next task to push or 0.
*/
static inline __attribute__((__always_inline__))
void* kaapi_data_push( kaapi_thread_t* thread, size_t size )
{
  void* retval;
  if (_kaapi_has_enough_dataspace(thread, size))
  {
    retval = thread->sp;
    thread->sp = size + (char*)thread->sp;
  }
  else
    retval = kaapi_thread_slow_push_data(thread,size);
  return retval;
}

/** same as kaapi_thread_pushdata, but with alignment constraints.
    note the alignment must be a power of 2 and not 0
    \param align the alignment size, in BYTES
*/
static inline __attribute__((__always_inline__))
void* kaapi_data_push_align(kaapi_thread_t* thread, uint32_t count, uintptr_t align)
{  
  kaapi_assert_debug( (align !=0) && /*(__BIGGEST_ALIGNMENT__ >= align) && */ ((align & (align - 1)) == 0));
  const uintptr_t mask = align - (uintptr_t)1;
  thread->sp = (char*)((uintptr_t)((char*)thread->sp + mask) & ~mask);
  return kaapi_data_push(thread, count);
}


/**  Shortcut to data_push
*/
static inline __attribute__((__always_inline__))
void*  kaapi_alloca( kaapi_thread_t* thread, uint32_t count)
{ return kaapi_data_push(thread, count); }

static inline __attribute__((__always_inline__))
void* kaapi_stack_restore(
     kaapi_thread_t* thread,
     void* sp
)
{
  void* retval = thread->sp;
  thread->sp = sp;
  return retval;
}

/* The function kaapi_task_init() pushes the top task into the stack.
   If successful, the kaapi_task_init() function will return zero.
   Otherwise, an error number will be returned to indicate the error.
   \param thread INOUT a pointer to the kaapi_stack_t data structure.
   \retval EINVAL invalid argument: bad stack pointer.
*/
static inline __attribute__((__always_inline__))
kaapi_task_t* kaapi_task_init(
    kaapi_task_t* task,
    kaapi_task_body_t body
)
{
  task->body      = body;
  task->flags     = KAAPI_TASK_FLAG_DEFAULT;
  KAAPI_ATOMIC_WRITE(&task->wc, 1); 
  return task;
}


/*
 */
extern int32_t kaapi_thread_push(
    kaapi_thread_t* thread,
    kaapi_task_t* task
);


extern int32_t _kaapi_task_commit(kaapi_thread_t* thread, kaapi_task_t* task);

/* Commit the task to the thread.
   After the call to function kaapi_task_commit,
   the last pushed task may be potentially thieft by any concurrent worker.
*/
static inline int32_t kaapi_task_commit(kaapi_thread_t* thread, kaapi_task_t* task)
{
  /* KAAPI_TASK_FLAG_NOLINK: used to no push task in ready list */
  if (task->flags & KAAPI_TASK_FLAG_NOLINK)
    return -1;
  ++thread->cnt;
  return _kaapi_task_commit(thread,task);
}

/** Allocate and initialize task
*/
static inline __attribute__((__always_inline__))
kaapi_task_t* kaapi_task_alloc(
     kaapi_thread_t* thread,
     kaapi_task_body_t body,
     uint32_t size
)
{
  kaapi_task_t* task = (kaapi_task_t*)thread->sp;
  if (!_kaapi_has_enough_dataspace(thread, size))
    task = (kaapi_task_t*)kaapi_thread_slow_push_data(thread, size);
  kaapi_task_init(task, body);
  thread->sp = size + (char*)thread->sp;
  return task;
}

/*  The function kaapi_task_set_ld() set the task attribut to
    indicate to the runtime that the task has interest to be pushed to
    the localitydomain ld.
    If successful, the kaapi_task_set_ld() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the kaapi_stack_t data structure.
    \retval EINVAL invalid argument: bad pointer or incompatible other flag (priority)
*/
#define KAAPI_TASK_LD_BOUND  0
#define KAAPI_TASK_OCR_PARAM 1
static inline int kaapi_task_set_ld(
    kaapi_task_t* task,
    int subtype, /* 0: ld value, 1: index of parameter */
    int ldid
)
{
  if (ldid >= KAAPI_TASK_LD_MAX) return EINVAL;
  if ((subtype & ~0x1) || (ldid <0)) return EINVAL;
  task->ld    = (unsigned int)ldid;
  if (subtype == 1)
    task->ld |= KAAPI_TASK_LD_MASK_PARAM;
  kaapi_taskflag_set( task, KAAPI_TASK_FLAG_LD_BOUND );
  return 0;
}

/* Return the associated ldid or -1
*/
static inline kaapi_ldid_t kaapi_task_get_ld(
    kaapi_task_t* task
)
{
  if (kaapi_taskflag_get(task,KAAPI_TASK_FLAG_LD_BOUND))
    return (kaapi_ldid_t)task->ld;
  return (kaapi_ldid_t)-1;
}

/*  The function kaapi_task_set_priority() set the priority value to the task.
    If successful, the kaapi_task_set_priority() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the kaapi_stack_t data structure.
    \param prio IN integer >= KAAPI_TASK_MIN_PRIORITY and <= KAAPI_TASK_MAX_PRIORITY
    \retval EINVAL invalid argument: bad pointer or incompatible other flag (LDBOUND)
*/
static inline int kaapi_task_set_priority(
    kaapi_task_t* task,
    unsigned int prio
)
{
  if (prio == KAAPI_TASK_MIN_PRIORITY)
  {
    if (kaapi_taskflag_get(task, KAAPI_TASK_FLAG_PRIORITY))
      kaapi_taskflag_unset( task, KAAPI_TASK_FLAG_PRIORITY );
  }
  else
  {
    kaapi_taskflag_set( task, KAAPI_TASK_FLAG_PRIORITY );
    task->prio    = (uint16_t)(prio >=KAAPI_TASK_MAX_PRIORITY ? KAAPI_TASK_MAX_PRIORITY : prio);
  }
  return 0;
}

/*
*/
static inline unsigned int kaapi_task_get_priority(
    kaapi_task_t* task
)
{
  if (kaapi_taskflag_get(task, KAAPI_TASK_FLAG_PRIORITY))
    return task->prio;
  return KAAPI_TASK_MIN_PRIORITY;
}

/*  The function kaapi_thread_save() saves the registers of the current thread.
    If successful, the kaapi_thread_save() return 0.
    Otherwise, return the error code.
*/
extern int kaapi_thread_save(kaapi_thread_t* thread, kaapi_thread_register_t* regs);

/*  The function kaapi_thread_restore() restore the registers saved by a call to kaapi_thread_save.
    Both kaapi_thread_restore and kaapi_thread_save muste be called within the same task region.
    If successful, the kaapi_thread_restore() return 0
    Otherwise, return the error code.
*/
extern int kaapi_thread_restore(kaapi_thread_t* thread, const kaapi_thread_register_t* regs);

/*  The function kaapi_sched_sync() execute all childs tasks of the current running task.
    If successful, the kaapi_sched_sync() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the thread data structure 
    \retval EINTR the control flow has received a KAAPI interrupt.
*/
extern int kaapi_sched_sync( kaapi_thread_t* thread );

#if KAAPI_USE_OFFLOAD
extern int kaapi_sched_sync_offload( kaapi_thread_t* thread );
#endif

/*  The function kaapi_sched_idle() execute all tasks of the current
    team. The function is a barrier so all threads of the team should call it.
    If successful, the kaapi_sched_idle() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the thread data structure
    \retval EINTR the control flow has received a KAAPI interrupt.
*/
extern int kaapi_sched_idle(
  kaapi_thread_t* thread, int (*f_fini)(void*), void* arg
);

#if KAAPI_USE_OFFLOAD
extern int kaapi_sched_idle_offload(
  kaapi_thread_t* thread, int (*f_fini)(void*), void* arg
);
#endif

/* Better specification here. 
*/
enum kaapi_team_barrier_wait_flag_t {
  KAAPI_BARRIER_FLAG_DEFAULT    = 0,
  KAAPI_BARRIER_FLAG_RESET_WORK = 0x1,   /* reset state for work / foreach */
  KAAPI_BARRIER_FLAG_WAITEXIT   = 0x2,   /* wait until the master thread of the team sends signal */
  KAAPI_BARRIER_FLAG_NOSCHEDULE = 0x4    /* do no schedule task */
};

/* Barrier for each thread of a team
   flag allows to change internal behavior.
   If flag & KAAPI_BARRIER_FLAG_WAITEXIT then all the threads, except the master,
   are waiting to be wakeuped when the master calls kaapi_team_barrier_wait_signal.
*/
void kaapi_team_barrier_wait (
    kaapi_team_t *team,
    kaapi_thread_t* thread,
    int count,
    int flag
);

#define KAAPI_FRAME_FLAG_DFG_VOID     0
#define KAAPI_FRAME_FLAG_NO_UNLINK    0x1 /* do not implicit unlink in sched_sync() */
#define KAAPI_FRAME_FLAG_DFG_OK       0x2
#define KAAPI_FRAME_FLAG_DELAY_STEAL  0x4 /* sync */
#define KAAPI_FRAME_FLAG_STACKALLOC   0x8


/*  The function kaapi_begin_dfg() prepare the current frame to receive tasks with dfg links built at runtime. All tasks in the current frame must have been commited with
    calls to kaapi_task_commit_dfg().
    If successful, the kaapi_begin_dfg() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the thread data structure
    \param flag IN flag for the frame or 0 (default value)
    \retval !=0 in case of error
*/
extern int kaapi_begin_dfg( kaapi_thread_t* thread, int flag );

/*  The function kaapi_end_dfg() mark the end of the data flow graph construction in current frame. Next call to kaapi_sync_dfg() could be use to garantee that all tasks
    have been executed.
    If successful, the kaapi_end_dfg() function will return zero.
    Otherwise, an error number will be returned to indicate the error.
    \param thread INOUT a pointer to the thread data structure
    \retval !=0 in case of error
*/
extern int kaapi_end_dfg( kaapi_thread_t* thread );


/* ========================================================================= */
/* Misc                                                                      */
/* ========================================================================= */

/** kaapi_get_elapsedtime
    The function kaapi_get_elapsedtime() will return the elapsed time in second
    since an epoch.
    Default (generic) function is based on system clock (gettimeofday).
    Optimized function is based on clock_gettime.
*/
extern uint64_t kaapi_get_elapsedns(void);

/** kaapi_get_elapsedns
    The function kaapi_get_elapsedtime() will return the elapsed time since an epoch
    in nano second unit.
*/
extern double kaapi_get_elapsedtime(void);


#if defined(KAAPI_DEBUG)
extern void kaapi_dump_dot( kaapi_thread_t* thread, const char* filename );
extern void kaapi_dump_dot_list_handle( kaapi_thread_t* thread, kaapi_handle_t* first, const char* filename );
extern void kaapi_dump_raw_dot( kaapi_thread_t* thread, const char* filename );
extern void kaapi_dbg_register_name( const void* ptr, const char* name );
extern const char* kaapi_dbg_get_name( const void* ptr );
#endif

#ifdef __cplusplus
}
#endif


#endif /* _KAAPI_H */
