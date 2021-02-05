/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** vincent.danjean@imag.fr
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
#ifndef _KAAPI_IMPL_H
#define _KAAPI_IMPL_H 1

#define KAAPI_ADVANCED_VERSION 0   /* to compile with mix code where next features are already present but not yet fully validated */

#define KAAPI_HAVE_IO_THREADS 0    /* do not use IO threads [Experimental feature!!!!] */

#define KAAPI_SLEEP_DEVICETHREAD 0 /* activate a sleeping state for device thread [Yet experimental feature] */

#ifndef KAAPI_USE_DYNLOADER
#define KAAPI_USE_DYNLOADER   0    /* do not use dynamically loaded plugin */
#endif

#define KAAPI_USE_HOST_PLUGIN 1
#define KAAPI_USE_CUDA_PLUGIN 1

#define KAAPI_USE_PERFCOUNTER 1    /* compile with some performance counters */

/* 2^KAAPI_SIZE_DSM_MAP is the size of the hash map
*/
#define KAAPI_SIZE_DSM_MAP 20

/* Define to 1 if xkaapi/xkblas uses its own heap allocator (experimental) else 0
*/
#define KAAPI_USE_OWN_HEAP_ALLOCATOR 1
#if KAAPI_USE_OWN_HEAP_ALLOCATOR
#  define KAAPI_HEAP_FIRST_FIT 1    /* number from 1 to .. max strategy */
#  define KAAPI_HEAP_BEST_FIT  2
#  define KAAPI_HEAP_STRATEGY KAAPI_HEAP_BEST_FIT
#endif

/* To activate use of OCR data management. Should be the default when ok.
*/
#define KAAPI_USE_OCR 0

/* To activate or not the loadbalancing between GPUs
*/
#define KAAPI_WS_GPUTASK 1

/* use pipeline to order task insertions, communications and kernel launchs
   else the only the number of inserted task + pending task in the stream is limited
*/
#define KAAPI_PIPELINE_GPUTASK 1

#if KAAPI_PIPELINE_GPUTASK
/* reorder stream execution on GPU */
#define KAAPI_REORDER_TASK_EXEC 1
#endif

/* do not use prefetch for successor task */
#define KAAPI_USE_PREFETCH 0
#define KAAPI_MAX_PREFETCH_WINDOW 2

/* to allow transfert between GPUi to GPUj if data is under xfer data to GPUi*/
#define KAAPI_USE_FAVOR_D2D_1 1

/* to allow to route data through NVlink is 2 GPUs is not interconnected */
#define KAAPI_USE_D2D_ROUTE 0

/* to use specific stream for D2D operation. */
#define KAAPI_USE_STREAM_D2D 1

/* to use D2D topology and performance group */
#define KAAPI_USE_TOPO_D2D 1

/* Mark that we compile source of the library.
   Only used to avoid to include public definitition of some types.
*/
#define KAAPI_COMPILE_SOURCE 1

#if defined(__cplusplus)
extern "C" {
#endif


#if defined(KAAPI_DEBUG)
#define KAAPI_RETURN_ERROR(err,val) \
  {\
    if (err != val) { printf("%s %i: error\n", __FILE__, __LINE__); }\
    return err; \
  }
#else
#define KAAPI_RETURN_ERROR(err,val) \
  {\
    return err; \
  }
#endif


#include <errno.h>  // error code
#include <stdlib.h> // free, malloc

#include "kaapi.h"
#include "kaapi_atomic.h"
#include "kaapi_hashmap.h"
#include "kaapi_format.h"
#include "kaapi_offload_stream.h"
#include "kaapi_memory.h"


#if defined(KAAPI_DEBUG)
#include <signal.h>
#define KAAPI_ATOMIC_PRINT( inst ) \
{\
  inst;\
  fflush(stdout); \
}
#else
#define KAAPI_ATOMIC_PRINT( inst ) 
#endif

/** This is the new version on top of X-Kaapi
*/
extern const char* get_kaapi_version(void);
extern const char* get_kaapi_git_hash(void);
extern const char* get_kaapi_configure_log(void);

/* Fwd declaration 
*/
struct kaapi_frame;
struct kaapi_stack;
struct kaapi_stack_bloc;
struct kaapi_processor_t;
struct kaapi_ressource_t;
struct kaapi_queue;

/* Basic stats, per thread basis, see kaapi_counter_name in kaapi.h
*/
#define KAAPI_MAX_THREAD_COUNT 256
#define KAAPI_MAX_THREAD_CUDA_COUNT 16
typedef struct stat_internal  {
  union {
    uint64_t counter[KAAPI_CNT_MAX];
    double   dcounter[KAAPI_CNT_MAX];
  };
} __attribute__((aligned(KAAPI_CACHE_LINE))) kaapi_stat_internal_t;

#if KAAPI_USE_PERFCOUNTER
extern kaapi_stat_internal_t kaapi_perthread_stat[KAAPI_MAX_THREAD_COUNT];
extern kaapi_stat_internal_t kaapi_perthread_asyncpin[KAAPI_MAX_THREAD_CUDA_COUNT];
#endif

/* steal request header */
struct kaapi_header_request_t;

/* Definition of parameters for the runtime system
*/
typedef struct kaapi_rtparam_t {
  size_t                stackblocsize;      /* default stack bloc size */
  uint8_t               sys_ngpus;          /* number of GPU plugged to this node */
  uint8_t               ngpus;              /* number of GPU for this node */
  uint32_t              gpu_set;            /* GPU to use */
  double                cuda_cache_factor;  /* percent of total free memory used by cache */
  uint16_t              cuda_stream_capacity;  /* capacity of input stream */
  uint8_t               cuda_conc_stream_kernel;/* number of concurrent cuda kernel stream per device*/
  uint8_t               cuda_conc_kernel;  /* number of pending kernel per kernel stream */
  uint8_t               cuda_conc_h2d;     /* number of concurrent cuda h2d stream per device*/
  uint8_t               cuda_conc_d2h;     /* number of concurrent cuda d2h stream per device*/
  uint8_t               cuda_conc_d2d;     /* number of concurrent cuda d2d stream per device*/
  float                 cuda_cache_limit;  /* percent reserved for cache */
} kaapi_rtparam_t;

extern kaapi_rtparam_t kaapi_default_param;

/** Initialize specific format for some task
*/
extern int kaapi_taskformat_init(void);
extern int kaapi_taskformat_finalize(void);

/** Scheduling information for the task:
    - KAAPI_TASK_IS_UNSTEALABLE: defined iff the task is not stealable
    - KAAPI_TASK_IS_SPLITTABLE : defined iff the task is splittable
*/
#define KAAPI_TASK_UNSTEALABLE_MASK    0x0001
#define KAAPI_TASK_SPLITTABLE_MASK     0x0002    /* means that a splitter can be called */
#define KAAPI_TASK_STRICTAFFINITY_MASK 0x0010
#define KAAPI_TASK_FLAG_MASK_AFFINITY  0x0700


/*
*/
typedef struct __attribute__((aligned(KAAPI_CACHE_LINE))) kaapi_threadinfo {
  kaapi_atomic32_t refcount;
  int              state;    /* 0: idle, 1: active, 2: destroyed */
  int              flag;     /* stealable or not ? */
} kaapi_threadinfo_t;


/* Locality domain.
   - push/pop are multiple operations (may be called concurrently by n>1 threads)
   - push is called by external ressource to the domain
   - pop is called by ressource of the domain
   The queue is a mail box between external ressources and the ressource of the domain.
   It is managed as a bounded FIFO priority queue using kaapi_queue_fifo_push and kaapi_queue_fifo_pop.
*/
struct kaapi_localitydomain {
  kaapi_ld_type_t         type;
  kaapi_ldid_t            ldid;     /* global id on 64 bits */
  kaapi_device_t*         device;   /* attached device */
  unsigned int            idx;      /* in kaapi_all_lddomains[type]->ld */
  kaapi_fifo_queue_t*     queue;    /* same data structure as a queue, but managed to be FIFO */
  uint64_t                perfrank; /* inspired from perfrank in cuda: number of affinity group with ~ same characteristic in communication performance */
  uint64_t*               affinity; /* of size perfrank -1, if affinity[perf] has bit i set to 1, then localitydomain i has affinity with it */
  kaapi_localitydomain_t* parent;
  unsigned int            subldcount;
  kaapi_localitydomain_t**subld;
};


/* Meta inf about locality domain type
*/
typedef struct {
  kaapi_ld_type_t type;
  unsigned int count;
  kaapi_localitydomain_t** ld;
} kaapi_localitydomain_type_t;


/* Barrier
*/
#ifndef KAAPI_CACHE_LINE_SIZE
#define KAAPI_CACHE_LINE_SIZE 64
#endif
#define KAAPI_BAR_CYCLES 3
struct kaapi_barrier {
  kaapi_atomic_t cycle __attribute__ ((aligned (KAAPI_CACHE_LINE_SIZE)));
  kaapi_atomic_t wait_cycle __attribute__ ((aligned (KAAPI_CACHE_LINE_SIZE)));
  int term __attribute__ ((aligned (KAAPI_CACHE_LINE_SIZE)));
  char __attribute__ ((aligned (KAAPI_CACHE_LINE_SIZE))) count[KAAPI_BAR_CYCLES * KAAPI_CACHE_LINE_SIZE];
};
typedef struct kaapi_barrier kaapi_barrier_t;


/* Kaapi team= set of threads
*/
struct kaapi_team {
  int volatile               finish;
  kaapi_lock_t               lock;
  int                        capacity;
  kaapi_atomic_t             count;
  struct {
    int count;
    int capacity;
    kaapi_thread_t** threads;
  }                          pertype[KAAPI_PROC_TYPE_MAX];
  struct {
    int count;
    int capacity;
    kaapi_localitydomain_t** ld;
  }                          lds[KAAPI_LD_COUNTTYPE];
  kaapi_thread_t**           threads;
  kaapi_threadinfo_t*        thread_metadata;
  kaapi_barrier_t            barrier; // barrier need if use Kaapi standalone
};


/*
*/
typedef struct kaapi_stack_bloc {
  struct kaapi_stack_bloc* next;
  uintptr_t            size;
  uintptr_t            pos;     /* of the current bloc */
  void*                save_sp;
  char                 data[0] __attribute__((aligned(sizeof(kaapi_task_t))));
} kaapi_stack_bloc_t;

/* A stack
   A stack is manage by stack of bloc.
   bloc->next is the next bloc (from the oldest to the newest).
   bloc->save_ptr is the
   bloc0 is the first bloc of the stack. 'bloc' is the current bloc.
*/
typedef struct kaapi_stack {
  kaapi_stack_bloc_t* bloc;
  kaapi_stack_bloc_t* bloc0;
} kaapi_stack_t;

/* Stack bloc allocator
*/
typedef struct kaapi_stack_allocator {
  kaapi_stack_bloc_t* head;
} kaapi_stack_allocator_t;



/* Thread context = private part of kaapi_thread_t
   thread.sp points to bloc with address aligned to KAAPI_STACKBLOCSIZE boundary.
*/
typedef struct kaapi_context {
  kaapi_thread_t             thread;       /* public definition of kaapi_context_t */
  kaapi_stack_t              st_data;      /* stack of data (task' arguments + alloca) */
  kaapi_task_t*              pc;           /* the running task */
  int                        proctype;     /* */
  kaapi_localitydomain_t*    ld;
  kaapi_device_t*            device;
  int                      (*sync)( kaapi_thread_t* );
  int                      (*sched_idle)( kaapi_thread_t*, int (*)(void*), void* );
  unsigned                   seed;         /* seed */
  int                        tid;          /* thread id != kid */
  uintptr_t                  kid;          /* K-thread id in the team group */
  struct kaapi_frame*        unlink;       /* prev of the current frame */
  kaapi_stack_allocator_t    st_allocator; /* stack bloc allocator */
  struct kaapi_team*         team;
  kaapi_lock_t               lock;             /* lock for the queue operation */
  kaapi_queue_t*             queue;            /* the running queue */
  kaapi_queue_t*             free_wqueue;      /* free queues list */
  kaapi_queue_t*             suspended_queues; /* suspended queue */
  int                        last_ldid;        /* for round robin distribution */
} kaapi_context_t;


/* A frame: push on stack when new task starts execution
*/
struct kaapi_frame {
  struct kaapi_frame*       next;       /* next frame to steal (from oldest to newest) */
  uintptr_t                 flag;       /* flag on frame */
  kaapi_atomic32_t          spawn_count; /* number of tasks completed */
  kaapi_atomic32_t          exec_count; /* number of tasks completed */
  kaapi_task_t*             start_task;
  kaapi_context_t*          ctxt;
  kaapi_thread_t            save_thread;
  int32_t                   save_T[KAAPI_TASK_MAX_PRIORITY+1];
  kaapi_task_t*             save_pc;
  struct kaapi_stack_bloc*  save_bloc_sp;
  struct kaapi_frame*       save_frame;
};

/*
*/
static inline __attribute__((__always_inline__))
kaapi_task_bodyfnc_t kaapi_task_getbody(
    const kaapi_task_t* task
)
{ return kaapi_all_formats_fnc[task->body]; }

static inline __attribute__((__always_inline__))
const struct kaapi_format* kaapi_task_getformat(
    const kaapi_task_t* task
)
{ return kaapi_all_formats[task->body]; }

static inline struct kaapi_format* kaapi_task_getformat_ref(
    const kaapi_task_t* task
)
{ return kaapi_all_formats[task->body]; }

/*
*/
static inline size_t _kaapi_task_getsize(
    const kaapi_task_t* task, const kaapi_format_t* fmt
)
{
  size_t size = 0;
  unsigned int count = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));
  unsigned int i;
  for (i=0; i<count; ++i)
  {
    if (KAAPI_ACCESS_GET_MODE(kaapi_format_get_mode_param(fmt, i, kaapi_task_getargs(task))) == KAAPI_ACCESS_MODE_V)
      continue;
    kaapi_memory_view_t view;
    kaapi_format_get_view_param(fmt, i, kaapi_task_getargs(task), &view);
    size += kaapi_memory_view_size(&view);
  }
  return size;
}

/* Compute a score and return the total volume of data
 */
extern int kaapi_compute_affinity_score(kaapi_ldid_t ldid, kaapi_task_t* task, size_t* score, int level);


/* ========================================================================= */
/* Thread context & Frame & Stack definitions                                */
/* ========================================================================= */

/** Stack organized by blocs
*/
#define KAAPI_STACKBLOCSIZE 1048576ULL

#define kaapi_firstin_stack_bloc(bloc,type) \
  ((type*)&(bloc)->data)
#define kaapi_lastin_stack_bloc(bloc,type) \
  (((type*)&(bloc)->data)+((bloc)->size-sizeof(type))/sizeof(type))


///* List of ready tasks to execute all tasks of the ready list are executed
//   when cnt_exec % cnt_task == 0.
//*/
//struct kaapi_queue {
//  kaapi_task_t*                  head;
//  kaapi_task_t*                  tail;
//  uint64_t                       cnt_task;     /* number of tasks after DFG computation */
//  kaapi_atomic64_t               cnt_exec;     /* number of tasks exec */
//  kaapi_atomic64_t               cnt_steal;    /* number of tasks stolen */
//};

#define QUEUE_DEFAULT_SIZE 16384

/* Queue of ready tasks to execute.
   Queue store task with priority order.

   data is the array of size 'size' of pointers to task.
   push and pop use T. steal uses H.
   At the begining T==H. On push, T++ and we have always T >=H.
*/
struct kaapi_queue {
  int32_t volatile T[KAAPI_TASK_MAX_PRIORITY+1] __attribute__((aligned(KAAPI_CACHE_LINE))); /* cache line */
  int32_t volatile bitmap;  /* bit i == 1 iff task with priority i exists */
  int32_t          size[KAAPI_TASK_MAX_PRIORITY+1];         /* size of queue */
  kaapi_task_t**   data[KAAPI_TASK_MAX_PRIORITY+1];         /* queue of task */
  kaapi_task_t**   data0[KAAPI_TASK_MAX_PRIORITY+1];        /* initial task queue task */
  kaapi_queue_t*   next; /* link of suspended queue */
  int32_t volatile H[KAAPI_TASK_MAX_PRIORITY+1] __attribute__((aligned(KAAPI_CACHE_LINE)));
#if KAAPI_DEBUG_LOW
  pthread_t        owner;
  kaapi_atomic_t   cnt_push;
  kaapi_atomic_t   cnt_pop;
  kaapi_atomic_t   cnt_steal;
#endif
};

/* fifo queue: based on pthread implementation
 * T, H are increasing index. T % size and H % size are the entry on data (frame) where
 * to load or store task and frame values.
 */
struct kaapi_fifo_queue {
  pthread_mutex_t lock;
  int32_t         T;        /* pos where to push */
  int32_t         H;        /* pos where to pop */
  int32_t         size;     /* size of queue */
  uint64_t        push_count;
  uint64_t        pop_count;
  kaapi_task_t**  data;     /* queue of task */
  pthread_cond_t  cond_push;
  int             waiter_push;
  //pthread_cond_t* cond_pop;
  //int             waiter_pop;
  void          (*cbk_fnc)(void*);
  void*           cbk_arg;
};

/*
*/
extern kaapi_stack_bloc_t* kaapi_stackallocator_alloc(
    kaapi_stack_allocator_t* sta, size_t size
);

/*
*/
extern void kaapi_stackallocator_dealloc(
    kaapi_stack_allocator_t* sta,
    kaapi_stack_bloc_t* bloc
);


/* return 1 iff queue is empty
*/
static inline
int kaapi_queue_empty( kaapi_queue_t* rd)
{
  return rd->bitmap == 0;
}

/* return 1 iff it exist i such that rd->H[i] > T[i] */
static inline
int kaapi_queue_headgreather( kaapi_queue_t* rd, int32_t* T)
{
  for (int i=0; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
    if (rd->H[i] > T[i]) return 1;
  return 0;
}

/* return 1 iff rd->T == T */
static inline
int kaapi_queue_topequal( kaapi_queue_t* rd, int32_t* T)
{
  for (int i=0; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
    if (rd->T[i] != T[i]) return 0;
  return 1;
}

/* Approximate size (+-2) if concurrent accesses
*/
static inline __attribute__((__always_inline__))
int32_t kaapi_queue_size( kaapi_queue_t* rd)
{
  int32_t size = 0;
  for (int i=0; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
    size += rd->T[i]-rd->H[i];
  return size;
}

static inline __attribute__((__always_inline__))
int32_t kaapi_queue_size_prio( int p, kaapi_queue_t* rd)
{
  return rd->T[p]-rd->H[p];
}

/*
*/
extern int kaapi_queue_init( kaapi_queue_t* rd, kaapi_task_t** bloc0, int32_t size );

/*
*/
extern void kaapi_queue_realloc( kaapi_lock_t* owner, unsigned int p, kaapi_queue_t* rd );


/*
*/
extern kaapi_task_t* kaapi_queue_pop(
    kaapi_context_t* owner,
    kaapi_queue_t* rd,
    int32_t* T0
);

/*
*/
extern int32_t kaapi_queue_push(
    kaapi_context_t* owner,
    kaapi_task_t* task
);

/* size. estimation because do not lock field
*/
static inline uint64_t kaapi_fifo_queue_size(
    kaapi_fifo_queue_t* ld
)
{
  uint64_t puc = ld->push_count;
  uint64_t pos = ld->pop_count;
  if (puc>pos) return puc-pos;
  return 0;
}

/*  push/pop: fifo order
*/
extern int32_t kaapi_fifo_queue_push(
    kaapi_fifo_queue_t* ld,
    kaapi_task_t* task
);


/*  owner_push/pop: LIFO order
*/
extern int32_t kaapi_fifo_queue_owner_push(
    kaapi_fifo_queue_t* ld,
    kaapi_task_t* task
);

/*
*/
extern kaapi_task_t* kaapi_fifo_queue_pop(
    kaapi_fifo_queue_t* ld
);

/* steal/push: lifo, steal/owner_push: fifo
*/
extern kaapi_task_t* kaapi_fifo_queue_steal(
    kaapi_fifo_queue_t* ld
);

/* owner_push/pop: LIFO
*/
extern kaapi_task_t* kaapi_fifo_queue_pop_with_affinity(
    kaapi_fifo_queue_t* ld,
    kaapi_device_t* device,
    int level
);

/* steal/push: lifo, steal/owner_push: fifo
*/
extern kaapi_task_t* kaapi_fifo_queue_steal_with_affinity(
    kaapi_fifo_queue_t* ld,
    kaapi_device_t* device,
    int level
);

/*
*/
extern int kaapi_fifo_register_waiter(
    kaapi_fifo_queue_t* rd,
    void (*callback)(void*),
    void* arg
);


/*
*/
extern int kaapi_frame_push(
    kaapi_frame_t* frame,
    kaapi_context_t* ctxt,
    int flag,
    kaapi_stack_bloc_t* sp_bloc
);

/*
*/
extern int kaapi_frame_pop(
  kaapi_context_t* ctxt
);


/** The function kaapi_stack_topframe() will return the top frame (the oldest pushed frame).
    The function returns a pointer to the bottom frame or 0.
    \param ctxt IN a pointer to the dest context
    \retval a pointer to the top frame
*/
static inline kaapi_frame_t* kaapi_stack_topframe(const kaapi_context_t* ctxt)
{
  return kaapi_firstin_stack_bloc(ctxt->st_data.bloc0, kaapi_frame_t);
}

/*
*/
struct queue_frame_t {
  kaapi_queue_t* queue;
  int32_t H[KAAPI_TASK_MAX_PRIORITY+1];
  int32_t T[KAAPI_TASK_MAX_PRIORITY+1];
};
extern int _kaapi_queue_frame_ready( void* arg);


/*
*/
extern void kaapi_barrier_init (kaapi_barrier_t *barrier);

/*
*/
extern void kaapi_barrier_destroy (kaapi_barrier_t *barrier);


/* ===================== Locality Domain  ============================================= */
extern kaapi_localitydomain_type_t* kaapi_all_lddomains;

/*
*/
extern int kaapi_localitydomain_finalize(void);

/*
*/
extern int kaapi_localitydomain_init( kaapi_localitydomain_t* ld, kaapi_device_t* device );

/*
*/
extern int kaapi_localitydomain_destroy( kaapi_localitydomain_t* ld );

/* Attach a new locality domain.
   Set its type, identifier and its parent if not null.
   Register the locality domain to the corresponding tables.
   Return 0 iff no error
*/
extern int kaapi_localitydomain_attach(
    kaapi_ld_type_t type,
    kaapi_localitydomain_t* parent,
    kaapi_localitydomain_t* ld
);

/*
*/
extern int kaapi_localitydomain_deattach( kaapi_ld_type_t type, kaapi_localitydomain_t* ld );

/*
*/
extern kaapi_localitydomain_t* kaapi_localitydomain_get(
    kaapi_ldid_t ldid
);

/*
*/
extern kaapi_localitydomain_t* kaapi_localitydomain_get_bytype(
    kaapi_ld_type_t type,
    unsigned int ith
);



/* ===================== Sched  ============================================= */

/* Handle is used to compute dependencies.
*/
struct kaapi_handle {
  kaapi_access_t      sync0;  /* first synchronization */
  kaapi_access_t*     last;   /* last concurrent access */
  kaapi_access_t*     sync;   /* new synchronisation access */
  kaapi_metadata_info_t* mdi; /* cache metadata */
};

/* Returns the number of activated successors
*/
extern uint32_t kaapi_sched_activate_successors(
    kaapi_thread_t* thread,
    kaapi_task_t* task,
    void (*cbk)(kaapi_task_t*, unsigned int, kaapi_access_t*, uint64_t arg),
    uint64_t arg
);

/* Activated accesses that depends on the access synchronisation point sync
   Returns the number of activated successors
*/
extern uint32_t kaapi_sched_activate_syncpoint(
    kaapi_thread_t* thread,
    kaapi_access_t* sync
);

/* ===================== Stack iterator  ============================================= */
/* Iterator on a stack of task (used by thief)
   - it iterates form oldest to newest task in the creation order
*/
typedef struct kaapi_stack_iterator_t {
  kaapi_task_t*  first;
  kaapi_task_t*  last;
} kaapi_stack_iterator_t;


typedef enum {
  KAAPI_STACK_ITERATE_DEFAULT
} kaapi_stack_iterator_flag_t;

/*
*/
extern int kaapi_stack_iterator_init(
  kaapi_stack_iterator_t* iter,
  kaapi_stack_t* stack,
  kaapi_frame_t* frame
);

/*
*/
static inline int kaapi_stack_iterator_destroy( kaapi_stack_iterator_t* iter )
{
  return 0;
}

/* Pass to the next task.
*/
extern void kaapi_stack_iterator_next( kaapi_stack_iterator_t* iter );

/* Return true if the iteration space is empty
*/
static inline int kaapi_stack_iterator_empty( kaapi_stack_iterator_t* iter )
{
  return (iter->first == iter->last);
}

/* Return the current task
*/
static inline kaapi_task_t* kaapi_stack_iterator_get( kaapi_stack_iterator_t* iter )
{
  return iter->first;
}


/* ===================== Default implementations  ==================================== */
typedef struct {
  kaapi_access_t a;
} kaapi_tasksync_t;

/* Do sync task
*/
extern kaapi_format_id_t kaapi_sync_body;

/* Do nop task
*/
extern kaapi_format_id_t kaapi_nop_body;

/* Represent the main task of a thread
*/
extern kaapi_format_id_t kaapi_taskmain_body;


/* ===================== Work stealing functions  ==================================== */
/* Request status
*/
typedef enum kaapi_request_status_t {
  KAAPI_REQUEST_S_INIT     = 0,
  KAAPI_REQUEST_S_NOK      = 1,
  KAAPI_REQUEST_S_OK       = 2,
  KAAPI_REQUEST_S_STOP     = 3,  /* stop to steal, return to upper level */
  KAAPI_REQUEST_S_ERROR    = 4,
  KAAPI_REQUEST_S_POSTED   = 5   /* asynchronous request */
} kaapi_request_status_t;

/** \ingroup WS
    Opcode for different kind of request on a task'queue
*/
typedef enum {
  KAAPI_REQUEST_OP_VOID,
  KAAPI_REQUEST_OP_PUSH,
  KAAPI_REQUEST_OP_PUSH_REMOTE,
  KAAPI_REQUEST_OP_PUSHLIST,
  KAAPI_REQUEST_OP_POP,
  KAAPI_REQUEST_OP_STEAL
} kaapi_request_op_t;

#define KAAPI_INHERITED_FIELD_REQUEST    \
  kaapi_request_op_t            op;      \
  int                           thiefid; \
  int                           status;

/** \ingroup WS
    Common header to all requests.
    WARNING these fields must appears AT THE BEGINING of the sub
    request data structure (missing C++ class heritage !)
*/
typedef struct kaapi_header_request_t {
  KAAPI_INHERITED_FIELD_REQUEST
} kaapi_header_request_t;

/** \ingroup WS
    A steal request to the victim context->queue
    First fields are those of kaapi_header_request_t.
*/
typedef struct kaapi_steal_request_t {
  KAAPI_INHERITED_FIELD_REQUEST
  int                           arch;           /* compatibility arch requested on task if !=0 */
  kaapi_task_t**                task;           /* where to reply stolen task  */
  kaapi_queue_t*                queue;          /* queue where task has been thief */
  kaapi_frame_t*                frame;          /* frame where task has been thief */
  uint32_t                      idx;            /* index in queue of the stolen task */
  uint8_t                       prio;           /* prio in queue of the stolen task */
} kaapi_steal_request_t;


/** \ingroup WS
    A pop request
*/
typedef struct kaapi_pop_request_t {
  KAAPI_INHERITED_FIELD_REQUEST
  int32_t                       limit[KAAPI_TASK_MAX_PRIORITY+1]; /* where to reply pop task  */
  kaapi_task_t**                task;           /* where to reply pop task  */
} kaapi_pop_request_t;


/** \ingroup WS
    Arg for push request
*/
typedef struct kaapi_push_request_t {
  KAAPI_INHERITED_FIELD_REQUEST
  kaapi_task_t*                 task;           /* task to push  */
} kaapi_push_request_t;


/** \ingroup WS
    Request emitted to get work.
    This data structure is pass in parameter of the splitter function.
    On return, the receiver that emits the request will retreive task(s) in the frame.
*/
typedef union kaapi_request_t {
    kaapi_header_request_t   header;
    kaapi_steal_request_t    steal_a;
    kaapi_pop_request_t      pop_a;
    kaapi_push_request_t     push_a;
} kaapi_request_t;


/** Post request to the ctxt.
    Request can be steal, push, pop, ... request where ctxt if the victim context.
    Return the status of the request
*/
extern int kaapi_sched_process_request (
  kaapi_team_t*    team,
  kaapi_context_t* ctxt,
  kaapi_request_t* request
);

#if 0
/*
*/
static inline void kaapi_place_group_operation_init( struct kaapi_place_group_operation_t* kpgo )
{
  if (kaapi_default_param.pgo_init)
    kaapi_default_param.pgo_init( kpgo );
}

/*
*/
static inline void kaapi_place_group_operation_wait( struct kaapi_place_group_operation_t* kpgo )
{
  if (kaapi_default_param.pgo_wait)
    kaapi_default_param.pgo_wait( kpgo );
}

/*
*/
static inline void kaapi_place_group_operation_fini( struct kaapi_place_group_operation_t* kpgo )
{
  if (kaapi_default_param.pgo_fini)
    kaapi_default_param.pgo_fini( kpgo );
}
#endif

/*
*/
extern int kaapi_taskmodule_init(void);

/*
*/
extern int kaapi_taskmodule_finalize(void);


#if defined(KAAPI_DEBUG)
/* Signal handler to dump the state of the internal kprocessors
   This signal handler is attached to SIGALARM when KAAPI_DUMP_PERIOD env. var. is defined.
*/
extern void _kaapi_signal_dump_state(int);

/* Signal handler to print the backtrace
   This signal handler is attached to:
    - SIGABRT
    - SIGTERM
    - SIGSEGV
    - SIGFPE 
    - SIGILL
    If the library is configured with --with-perfcounter, then the function call _kaapi_signal_dump_counters.
*/
extern void _kaapi_signal_dump_backtrace(int, siginfo_t *si, void *unused);

/*
*/
extern int kaapi_dbg_init(void);

extern int kaapi_dbg_finalize(void);
#endif

#if defined(__cplusplus)
}
#endif

#endif
