/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012,2021 INRIA.
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** fabien.lementec@gmail.com / fabien.lementec@imag.fr
** 
** This software is a computer program whose purpose is to execute
** multithreaded computation with data flow synchronization between
** threads.
** 
** This software is governed by the CeCILL-C license under French law
** and abiding by the rules of distribution of free software.  You can
** use, modify and/ or redistribute the software under the terms of
** the CeCILL-C license as circulated by CEA, CNRS and INRIA at the
** following URL "http://www.cecill.info".
** 
** As a counterpart to the access to the source code and rights to
** copy, modify and redistribute granted by the license, users are
** provided only with a limited warranty and the software's author,
** the holder of the economic rights, and the successive licensors
** have only limited liability.
** 
** In this respect, the user's attention is drawn to the risks
** associated with loading, using, modifying and/or developing or
** reproducing the software by the user in light of its specific
** status of free software, that may mean that it is complicated to
** manipulate, and that also therefore means that it is reserved for
** developers and experienced professionals having in-depth computer
** knowledge. Users are therefore encouraged to load and test the
** software's suitability as regards their requirements in conditions
** enabling the security of their systems and/or data to be ensured
** and, more generally, to use and operate it in the same conditions
** as regards security.
** 
** The fact that you are presently reading this means that you have
** had knowledge of the CeCILL-C license and that you accept its
** terms.
** 
*/
#ifndef _KAAPI_PERFLIB_H
#define _KAAPI_PERFLIB_H 1
/* ========================================================================= */
/* Kaapi trace lib.
   The library is a reduced version of the kaapi tracelib library that focus
   only on efficient tracing functionality with pre-defined events for tracing
   execution of program on multi-cpu/multi-GPU architecture.
   A part of the definition deals with the capacity to trace performance counters
   value.
*/
/* ========================================================================= */


#include <stdint.h>
#include <stddef.h>

#include "kaapi.h"
#include "kaapi_format.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if KAAPI_USE_TRACELIB==1 /* all the file ... */

#if !defined(KAAPI_CACHE_LINE)
#  define KAAPI_CACHE_LINE 64
#endif


/* ========================================================================= */
/* Kaapi format definition                                                   */
/* ========================================================================= */
typedef struct kaapi_fmttrace_def {
  uint64_t          fmtid;       /* id */
  char              name[64];    /* name */
  char              color[32];   /* color to represent the task */
} kaapi_fmttrace_def;


/* Information about task in the trace file
*/
typedef struct kaapi_descrformat_t {
  uint64_t                    fmtid;
  const char*                 name;
  const char*                 color;
} kaapi_descrformat_t;


/* Type of task id. Should be unique. */
typedef void* kaapi_task_id_t;

/* Fwd decl */
struct kaapi_context;
typedef struct kaapi_context kaapi_context_t;

/* ========================================================================= */
/* Kaapi event data type                                                     */
/* ========================================================================= */
/* Event data, easy the decoding of multiple information with respect
   to scalar data type
*/
typedef union {
    void*     p;
    uintptr_t i;
    double    d;
    uint64_t  u;
    uint64_t  i64[1];
    uint32_t  i32[2];
    uint16_t  i16[4];
    uint8_t   i8[8];
    char      c8[8];
} kaapi_event_data_t;


/* Kaapi Event data type
  - fixed sized data structure. To be extended if required.
  - at most 4 data fields (64 bits width) + common event information
  An event number if associated with a kind field to identify:
    - begin/start (00)
    - end/stop   (01)
    - XY
    - YW
*/
typedef struct kaapi_event_t {
  uint8_t     evtno: 6;     /* event number */
  uint8_t     kind:  2;     /* sub kind of event */
  uint16_t    kid;          /* kaapi processor identifier or device identifier or other identifier */
  uint64_t    date;         /* nano second since an epoch */
  union {
    struct {
      kaapi_event_data_t d0;
      kaapi_event_data_t d1;
      kaapi_event_data_t d2;
      kaapi_event_data_t d3;
    } s;
    kaapi_event_data_t data[4];
  } u;
} kaapi_event_t;

#define KAAPI_EVENT_DATA(evt,i,f) (evt)->u.s.d##i.f

/* ========================================================================= */
/* Kaapi events                                                              */
/* ========================================================================= */
/* Version 3 is not upper compatible with version 2.
   Only reader with trace version upper than 3 may have chance to read trace file
   Version 5 is not uppercompatible with older version.
   Version 6 is not uppercompatible with older versions.
   Version 7 is not uppercompatible with older versions and correspond to version integrated 
   in libomp.
   Version 9 add loop information, memory event, perf counter.
   Version 10 break backward compatibility. Reduction of code size.
*/
#define __KAAPI_TRACE_VERSION__     10

/* Definition of internal KAAPI events.
   Note that any extension or modification of the events must be reflected in
   the reader tool.

   TODO: reduce the size of event by encoding information in extra field: e.g. begin / end
   of state, or group events by features and then used extra fields for number of subevent
*/
#define KAAPI_EVT_KPROC              0     /* kproc: kind: start(0), stop(1), i0: processor type (0:CPU, 1:GPU), i1: core if pined or gpu */
#define KAAPI_EVT_KPROC_INFO         1     /* kind: 0 (bind to cpu), d0: cpu */
#define KAAPI_EVT_TASK_EXEC          2     /* execution of tasks:
                                                 kind: 0(push), d0: task pointer, d1: fmtid, d2: task arg
                                                 kind: 1(async exec), d0: task pointer, d1: fmtid, d2: task arg
                                                 kind: 2(exec), d0: task pointer, d1: fmtid, d2: task arg
                                                 kind: 3(end),  d0: task pointer, d1: fmtid, d2: task arg  */
#define KAAPI_EVT_TASK_INFO          3     /* kind: 0 (successor), d0: task, d1: succ;
                                              kind: 1 (data access), d0: task, d1: pointer, d2: size, d3.i32[0]: mode, d3.i32[1]: numaid */
#define KAAPI_EVT_TASK_USERATTR      4     /* kind: 0->3 1+#data, d0: task, d1[,d2[,d3,[d4]]]: user info*/
#define KAAPI_EVT_SCHED              5     /* kind: 0(idle begin), kind:1 (idle end) */
#define KAAPI_EVT_STEAL_REQUEST      6     /* when k-processor begin to process requests, data=victim.id */

#define KAAPI_EVT_OFFLOAD_CPY        7     /* data transfer: kind: 0 (push into stream), 1(begin), 2(end),
                                              d0: id, d1.i32[0]: src kid, d1.i32[1]: dest kid, d2: size, d3.i8[0]: 0(H2H), 1(H2D), 2(D2H), 3(D2D), d3.i8[1]: stream id
                                              Event for begin and end only requires 1 data (the id)
                                           */
#define KAAPI_EVT_OFFLOAD_KERN       8     /* kernel exec: kind: 0 (push into stream), 1(begin), 2(end),
                                              d0: id: d1: task, d2: fmtid, d3.i8[0]: stream id
                                              Event for begin and end only requires 1 data (the task)
                                              */
#define KAAPI_EVT_TASKSYNC           9     /* sched_sync: kind: 0(begin), 1(end) */
#define KAAPI_EVT_PERFCOUNTER        10    /* format <perf id (0, 1, 2..)>, <value> */
#define KAAPI_EVT_TASK_PERFCOUNTER   11    /* d0=task; d1.i8[0..2]: perf counter id; d2, d3: values */
                                           /* several KAAPI_EVT_TASK_PERFCOUNTER may follow KAAPI_EVT_TASK_END */
#define KAAPI_EVT_PERF_UNCORE        12    /* d0.i8[0]: number of uncore events in the record, d0.i8[1]..i8[3]: ids of uncore events in perfset d1,d2,d3: event counters  */
#define KAAPI_EVT_CALL               13    /* kind: 0(begin), 1(end), 2(info), 3(info). If begin: c8[0..7]: name, d1,d2,d3: param */
#define KAAPI_EVT_LOOP               14    /* kind: 0(begin), 1(end), 2(next); d0.i8[0]: sched type, d0.i32[1]: workshareid; if kind=0 or 2, d1: ub, d2: lb, d3 stride */
#define KAAPI_EVT_ENERGY             15    /* */

#define KAAPI_EVT_LAST               16

/* Mask of events = kaapi_tracelib_param.eventmask
   The mask is set at runtime to select events that will be registered to file.
   The bit i-th in the mask is 1 iff the event number i is registered.
*/
typedef uint64_t kaapi_event_mask_type_t;

/** Heler for creating mask from an event
*/
#define KAAPI_EVT_MASK(eventno) \
  (((kaapi_event_mask_type_t)1) << (kaapi_event_mask_type_t)eventno)

/* The following set is always in the mask
*/
#define KAAPI_EVT_MASK_STARTUP \
    (  KAAPI_EVT_MASK(KAAPI_EVT_KPROC) \
    )

#define KAAPI_EVT_MASK_COMPUTE \
    (  KAAPI_EVT_MASK(KAAPI_EVT_TASK_EXEC) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASK_INFO) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASK_USERATTR) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASKSYNC) \
     | KAAPI_EVT_MASK(KAAPI_EVT_CALL) \
    )
    
#define KAAPI_EVT_MASK_CALL \
    (  KAAPI_EVT_MASK(KAAPI_EVT_CALL) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASKSYNC) \
    )

#define KAAPI_EVT_MASK_SCHED \
    (  KAAPI_EVT_MASK(KAAPI_EVT_SCHED) \
     | KAAPI_EVT_MASK(KAAPI_EVT_STEAL_REQUEST) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASKSYNC) \
    )

#define KAAPI_EVT_MASK_ENERGY \
    (  KAAPI_EVT_MASK(KAAPI_EVT_ENERGY) \
    )

#define KAAPI_EVT_MASK_OFFLOAD \
    (  KAAPI_EVT_MASK(KAAPI_EVT_OFFLOAD_CPY) \
     | KAAPI_EVT_MASK(KAAPI_EVT_OFFLOAD_KERN) \
    )

#define KAAPI_EVT_MASK_PERFCOUNTER \
    (  KAAPI_EVT_MASK(KAAPI_EVT_PERFCOUNTER) \
     | KAAPI_EVT_MASK(KAAPI_EVT_TASK_PERFCOUNTER)\
     | KAAPI_EVT_MASK(KAAPI_EVT_PERF_UNCORE)\
    )


/* the datation used for event */
static inline uint64_t kaapi_event_date(void)
{
#if defined(KAAPI_USE_GETTICK) && (defined(__i386__) || defined(__pentium__) || defined(__pentiumpro__) || defined(__i586__) || defined(__i686__) || defined(__k6__) || defined(__k7__) || defined(__x86_64__))
#  define KAAPI_GET_TICK(t) __asm__ volatile("rdtsc" : "=a" ((t).sub.low), "=d" ((t).sub.high))
  union tick_t
  {
    uint64_t tick;
    struct {
      uint32_t low;
      uint32_t high;
    }
    sub;
  };
  union tick_t t; 
  KAAPI_GET_TICK(t);
  return t.tick;
#else
  return kaapi_get_elapsedns();
#endif
}

/* info about timer, must be coherent with kaapi_event_date() */
static inline const char* kaapi_event_date_unit(void)
{
#if defined(KAAPI_USE_GETTICK) && (defined(__i386__) || defined(__pentium__) || defined(__pentiumpro__) || defined(__i586__) || defined(__i686__) || defined(__k6__) || defined(__k7__) || defined(__x86_64__))
  return "cycle";
#else
  return "ns";
#endif
}



/* ========================================================================= */
/* Kaapi events buffer                                                       */
/* ========================================================================= */
#define KAAPI_EVENT_BUFFER_SIZE 65520
typedef struct kaapi_event_buffer_t {
  int                      kid;      /* set when buffer is pushed to the flushimator */
  int                      ptype;    /* set when buffer is pushed to the flushimator */
  uint32_t                 pos;
  kaapi_event_t            buffer[KAAPI_EVENT_BUFFER_SIZE];
  struct kaapi_event_buffer_t* next;
} kaapi_event_buffer_t;



/* ========================================================================= */
/* Context per thread for tracing and measuring some performance counters    */
/* ========================================================================= */
typedef struct kaapi_tracelib_thread_t {
#if KAAPI_USE_PERFCOUNTER==1
  kaapi_perf_idset_t       perfset;
  kaapi_perf_idset_t       task_perfset; /* subset of perfset */
#endif
  struct kaapi_context*    ctxt;
  uint64_t                 kid;             /* */
  int                      ptype;           /* processor type */
  int                      numaid;          /* thread binding's NUMA node id */
  int                      cpu;             /* thread binding's NUMA node id */
  int                      trace_isstarted; /* 0/1: to dynamically disable/enable tracing */
  uint64_t                 tstart;
  kaapi_task_id_t          task;            /* current running task or 0 */
  kaapi_event_buffer_t*    eventbuffer;
  uint64_t                 event_mask;
#if KAAPI_USE_PERFCOUNTER==1
  int                      papi_event_set;
  unsigned int	           papi_event_count;
  kaapi_perf_idset_t	     papi_event_mask;
#endif
} __attribute__((aligned (KAAPI_CACHE_LINE))) kaapi_tracelib_thread_t;


/* ========================================================================= */
/* Global variable for the sublibrary tracelib                               */
/* ========================================================================= */
typedef struct {
  int                       gid;
  int                       cpucount;
  int                       gpucount;
  int                       numaplacecount;
  uint64_t                  eventmask;
  const char*               recordfilename;      /* prefix for record filenames */
#if KAAPI_USE_PERFCOUNTER==1
  kaapi_perf_idset_t        perfctr_idset;       /* per thread events */
  kaapi_perf_idset_t        taskperfctr_idset;   /* per task events */
  kaapi_perf_idset_t        uncoreperfctr_idset; /* uncore events */
  uint64_t                  uncore_period;       /* period (ns) to collect uncore event counters */
#endif
  int                       proc_event_count;    /* !=0 number of events in perfctr_idset */
  kaapi_descrformat_t**     fmt_list;            /* array of fdescr */
  int                       fmt_listsize;
  kaapi_atomic_t            nthreads;
  kaapi_tracelib_thread_t** threads;
} kaapi_tracelib_param_t;
extern kaapi_tracelib_param_t kaapi_tracelib_param;


/* Initialize Kaapi sublibrary
   Initialize different masks from KAAPI_RECORD_MASK & KAAPI_RECORD_TRACE
   Initialize different masks from KAAPI_TASKPERF_EVENTS & KAAPI_PERF_EVENTS
*/
extern int kaapi_tracelib_init(
  int gid
);


/* ========================================================================= */
/* Public API of exported function for the Kaapi tracing library             */
/* ========================================================================= */
/* Finalization 
*/
extern void kaapi_tracelib_fini(void);


/* Initialize trace & performance counters for the current thread
   A call to kaapi_tracelib_thread_init declares a performance counters set and
   an event stream. If the tracing sublibrary is initialized such that the performance
   counter set or the event set to capture are empty, then the corresponding
   performance counter set or the event stream is set to 0.
   Return 0 in case of error.
*/
extern int kaapi_tracelib_thread_init (
    kaapi_tracelib_thread_t* kproc,
    kaapi_context_t*         ctxt,
    uint64_t                 kid,
    int                      cpu,
    int                      numaid,
    int                      proctype /* 0: cpu, 1: gpu, 2: uncore collector */
);


#if KAAPI_USE_PERFCOUNTER==1
/* Return the size of events defined to be captured.
*/
static inline unsigned int kaapi_tracelib_thread_idsetsize(
    const kaapi_tracelib_thread_t* kproc
)
{ return __builtin_popcountl( kproc->perfset ); }

/* Return the size of events defined to be captured.
*/
static inline unsigned int kaapi_tracelib_thread_taskidsetsize(
    const kaapi_tracelib_thread_t* kproc
)
{ return __builtin_popcountl( kproc->task_perfset ); }
#endif


/* Start tracing for the thread kproc
*/
extern void kaapi_tracelib_thread_start (
    kaapi_tracelib_thread_t*     kproc
);

/* Stop tracing for the thread kproc
*/
extern void kaapi_tracelib_thread_stop (
    kaapi_tracelib_thread_t*     kproc
);


#if KAAPI_USE_PERFCOUNTER==1
/* Read the current value of counters and report them in regs
   regs should have enough size to store all events defined in the kproc set 
*/
extern void kaapi_tracelib_thread_read(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_perf_idset_t           idset,
    kaapi_perf_counter_t*        regs
);
#endif


/* Switch to count time from tidle to twork
   Return the time take into accounting
*/
extern int kaapi_tracelib_thread_switchstate(
    kaapi_tracelib_thread_t*     kproc
);

/* Finalization for the thread performance counter set or event stream.
   After the call to kaapi_tracelib_thread_fini data pointer by perf or
   eventstream are not usable.
*/
extern void kaapi_tracelib_thread_fini (
    kaapi_tracelib_thread_t*     kproc
);


/* Generates event 'BEGIN|END_STATE' information for the current thread.
   Interpretation is free.
*/
extern void kaapi_tracelib_thread_state (
    kaapi_tracelib_thread_t*     kproc,
    uint8_t                      begend, /* 0==begin, 1==end */
    uint32_t                     cpu,
    uint32_t                     node,
    uint64_t                     state
);


#if 0
/* Generates event 'STEAL' information for the current thread.
   Interpretation is free.
*/
extern void kaapi_tracelib_thread_stealop (
    kaapi_tracelib_thread_t*     kproc,
    uint8_t                      op,
    uint32_t                     cpu,
    uint32_t                     node,
    uint32_t                     victim_level,
    uint32_t                     victim_level_id
);
#endif


/*
*/
extern kaapi_task_id_t kaapi_tracelib_newtask_id(void);

/* Start executing the task 'task' with specified format id.
   perfctr0 is the starting set of counters maintained by the running thread.
*/
extern void kaapi_tracelib_task_begin(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    uint64_t                     fmtid
);

extern void kaapi_tracelib_task_end(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    kaapi_task_id_t              parent
);

#if KAAPI_USE_PERFCOUNTER==1
/* Returns the name of the performance counter id */
extern const char* kaapi_tracelib_perfid_to_name(kaapi_perf_id_t id);

/* create new user counter with function used to collect counter value */
extern kaapi_perf_id_t kaapi_tracelib_create_user_perfid(
  const char* name,
  kaapi_perf_counter_t (*read)(void*), void* ctxt,
  int opaccum,
  const char* type, /* uint64_t, double */
  char unit
);
#endif

/* Register a new format descriptor if not already done and returns it.
   New created descriptor entry are serialized.
*/
kaapi_descrformat_t* kaapi_tracelib_register_fmtdescr(
      int implicit,
      void* key,
      const char* loc,
      const char* task_name,
      char* (*filter_func)(char*, int, const char*, const char*)
);

/*
*/
extern void kaapi_tracelib_task_depend(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              source,
    kaapi_task_id_t              sink
);

#if 0
/*
*/
extern void kaapi_tracelib_task_attr(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t task,
    int kind,
    int64_t value
);
#endif

/* Decoder should returns the address and the access mode of the i-th parameters stored in deps.
   The access mode corresponds to the Kaapi access mode KAAPI_ACCESS_XX defined above. 
*/
extern void kaapi_tracelib_task_access(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    int                          count,
    void*                        deps,
    int                          count_noalias,
    void*                        deps_noalias,
    void                       (*decoder)(void*, int, void**, size_t*, int*)
);


/* begin task wait/sync
*/
extern void kaapi_tracelib_taskwait_begin(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task
);

/* End task wait/sync
*/
extern void kaapi_tracelib_taskwait_end(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task
);


/* utility */
extern size_t kaapi_tracelib_count_perfctr(void);

#if KAAPI_USE_PERFCOUNTER==1
/* Human readable name for event mask */
extern size_t kaapi_perfctr_get_name_mask( kaapi_perf_idset_t  perfctr_idset, size_t ssize, char* buffer);
#endif

/* Human readable name for event mask */
extern size_t kaapi_event_get_name_mask( uint64_t eventmask, size_t ssize, char* buffer);

/* Human readable name of events */
extern int kaapi_event_get_name( int8_t evtno, int8_t kind, char* buffer, int ssize);

/* Array of name for event, without considering kind as in kaapi_event_get_name */
extern const char* kaapi_event_name[];


/* ========================================================================= */
/* Header of the trace file. First block of each trace file.
    Very static size defining what KAAPI generates.
*/
/* ========================================================================= */
#define KAAPI_SIZE_PERFCTR_NAME 128
typedef struct kaapi_eventfile_header {
  uint32_t       version;
  uint32_t       minor_version;
  uint32_t       trace_version;
  char           hostname[32];
  uint32_t       kid;
  uint32_t       numaid;
  uint32_t       ptype;
  uint32_t       cpucount;
  uint32_t       gpucount;
  uint32_t       gpuset;
  uint8_t        s_kern;
  uint8_t        s_d2h;
  uint8_t        s_h2d;
  uint8_t        s_d2d;
  uint32_t       numacount;           /* numa count */
  char           event_date_unit[8];  /* unit for the clock used to take date of event */
  uint64_t       event_mask;
  char           package[128];
  uint64_t       perf_mask;
  uint64_t       task_perf_mask;
  uint64_t       uncore_perf_mask;
  uint64_t       uncore_perf_period;
  uint32_t       perfcounter_count;   /* (idmax & 0xFF) | (base  & 0xFF for papi << 8) | (base uncore & 0xFF << 16) */
  uint32_t       taskfmt_count;       /* number of task's formats */
#if KAAPI_USE_PERFCOUNTER==1
  char           perfcounter_name[KAAPI_PERF_ID_MAX][128]; /* name for each perf counter */
#endif
  kaapi_fmttrace_def fmtdefs[KAAPI_FORMAT_MAX]; /* of size taskfmt_count */
} kaapi_eventfile_header_t;

/** Flush the event buffer evb and return and new buffer.
    \param evb the event buffer to flush
    \retval the new event buffer to use for futur records.
*/
extern kaapi_event_buffer_t* kaapi_event_flushbuffer( kaapi_event_buffer_t* evb );


/** Return a new event into the eventbuffer of the kprocessor.
*/
static inline kaapi_event_t*  kaapi_event_get(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;
  return evt;
}

static inline kaapi_event_buffer_t* kaapi_event_push(
    kaapi_tracelib_thread_t*  kproc
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  evb->pos++;
  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}

/** Push a new event into the eventbuffer of the kprocessor.
    Assume that the event buffer was allocated into the kprocessor.
    Current implementation only work if library is compiled 
    with KAAPI_USE_PERFCOUNTER flag.
*/
static inline kaapi_event_buffer_t* kaapi_event_push0(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos++];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;

  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}

/** Push a new event into the eventbuffer of the kprocessor.
    Assume that the event buffer was allocated into the kprocessor.
    Current implementation only work if library is compiled 
    with KAAPI_USE_PERFCOUNTER flag.
*/
static inline kaapi_event_buffer_t*  kaapi_event_push1(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind,
    uint64_t                  p0
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos++];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;
  KAAPI_EVENT_DATA(evt,0,u) = p0;

  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}

/** Push a new event into the eventbuffer of the kprocessor.
    Assume that the event buffer was allocated into the kprocessor.
    Current implementation only work if library is compiled 
    with KAAPI_USE_PERFCOUNTER flag.
*/
static inline kaapi_event_buffer_t*  kaapi_event_push2(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind,
    uint64_t                  p0,
    uint64_t                  p1
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos++];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;
  KAAPI_EVENT_DATA(evt,0,u) = p0;
  KAAPI_EVENT_DATA(evt,1,u) = p1;

  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}


/** Push a new event into the eventbuffer of the kprocessor.
    Assume that the event buffer was allocated into the kprocessor.
    Current implementation only work if library is compiled 
    with KAAPI_USE_PERFCOUNTER flag.
*/
static inline kaapi_event_buffer_t*  kaapi_event_push3(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind,
    uint64_t                  p0,
    uint64_t                  p1,
    uint64_t                  p2
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos++];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;
  KAAPI_EVENT_DATA(evt,0,u) = p0;
  KAAPI_EVENT_DATA(evt,1,u) = p1;
  KAAPI_EVENT_DATA(evt,2,u) = p2;

  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}


/** Push a new event into the eventbuffer of the kprocessor.
    Assume that the event buffer was allocated into the kprocessor.
    Current implementation only work if library is compiled
    with KAAPI_USE_PERFCOUNTER flag.
*/
static inline kaapi_event_buffer_t*  kaapi_event_push4(
    kaapi_tracelib_thread_t*  kproc,
    uint64_t                  tclock,
    uint8_t                   eventno,
    uint8_t                   kind,
    uint64_t                  p0,
    uint64_t                  p1,
    uint64_t                  p2,
    uint64_t                  p3
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = &evb->buffer[evb->pos++];
  evt->evtno   = eventno;
  evt->kind    = kind;
  evt->kid     = kproc->kid;
  evt->date    = tclock;
  KAAPI_EVENT_DATA(evt,0,u) = p0;
  KAAPI_EVENT_DATA(evt,1,u) = p1;
  KAAPI_EVENT_DATA(evt,2,u) = p2;
  KAAPI_EVENT_DATA(evt,3,u) = p3;

  if (evb->pos == KAAPI_EVENT_BUFFER_SIZE)
    evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
  return evb;
}



#if KAAPI_USE_PERFCOUNTER==1
/* Write event counter values in idset to the trace file
*/
extern kaapi_event_buffer_t* kaapi_event_push_perfctr(
    kaapi_tracelib_thread_t*    kproc,
    uint64_t                    date,
    uint8_t                     eventno,
    uint8_t                     kind,
    uint64_t                    d0,
    const kaapi_perf_idset_t*   idset,
    const kaapi_perf_counter_t* perfctr
);
#endif


/*
*/
#define KAAPI_IFUSE_TRACE(kproc,inst) \
    if ((kproc)->eventbuffer) { inst; }
#define KAAPI_EVENT_GET(kproc, eventno, kind ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_get((kproc), kaapi_event_date(), eventno, kind ) : 0 )
#define KAAPI_EVENT_PUSH(kproc, eventno ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push((kproc)) : 0 )

#define KAAPI_EVENT_PUSH0(kproc, eventno, kind ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push0((kproc), kaapi_event_date(), eventno, kind ) : 0 )
#define KAAPI_EVENT_PUSH1(kproc, eventno, kind, p1 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push1((kproc), kaapi_event_date(), eventno, kind, (uint64_t)(p1) ) : 0 )
#define KAAPI_EVENT_PUSH2(kproc, eventno, kind, p1, p2 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push2((kproc), kaapi_event_date(), eventno, kind, (uint64_t)(p1), (uint64_t)(p2) ) : 0)
#define KAAPI_EVENT_PUSH3(kproc, eventno, kind, p1, p2, p3 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push3((kproc), kaapi_event_date(), eventno, kind, (uint64_t)(p1), (uint64_t)(p2), (uint64_t)(p3) ): 0)
#define KAAPI_EVENT_PUSH4(kproc, eventno, kind, p1, p2, p3, p4 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push4((kproc), kaapi_event_date(), eventno, kind, (uint64_t)(p1), (uint64_t)(p2), (uint64_t)(p3), (uint64_t)(p4) ): 0)
#if KAAPI_USE_PERFCOUNTER==1
#define KAAPI_EVENT_PUSH_PERFCTR(kproc, eventno, kind, pc, idset, perfctr ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push_perfctr((kproc), kaapi_event_date(), eventno, kind, (uintptr_t)pc, idset, perfctr ) : 0)
#define KAAPI_EVENT_PUSH_UNCORE_PERFCTR(kproc, numaid, idset, perfctr ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(KAAPI_EVT_PERF_UNCORE))) ? \
      kaapi_event_push_perfctr((kproc), kaapi_event_date(), KAAPI_EVT_PERF_UNCORE, 0, (uintptr_t)numaid, idset, perfctr ) : 0)
#else
#define KAAPI_EVENT_PUSH_PERFCTR(kproc, eventno, kind, pc, idset, perfctr ) 
#define KAAPI_EVENT_PUSH_UNCORE_PERFCTR(kproc, numaid, idset, perfctr ) 
#endif


/* push new event with given date (value returned by kaapi_event_date())
   the macros returns the date of the event else 0
*/
#define KAAPI_EVENT_PUSH0_AT(kproc, tclock, eventno, kind ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push0((kproc), tclock, eventno, kind ) : 0UL )
#define KAAPI_EVENT_PUSH1_AT(kproc, tclock, eventno, kind, p1 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push1((kproc), tclock, eventno, kind, (uint64_t)(p1)) : 0UL )
#define KAAPI_EVENT_PUSH2_AT(kproc, tclock, eventno, kind, p1, p2 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push2((kproc), tclock, eventno, kind, (uint64_t)(p1), (uint64_t)(p2)) : 0UL)
#define KAAPI_EVENT_PUSH3_AT(kproc, tclock, eventno, kind, p1, p2, p3 ) \
    ( ((kproc) && ((kproc)->eventbuffer) && ((kproc)->event_mask & KAAPI_EVT_MASK(eventno))) ? \
      kaapi_event_push3((kproc), tclock, eventno, kind, (uint64_t)(p1), (uint64_t)(p2), (uint64_t)(p3)) : 0)

#if KAAPI_USE_PERFCOUNTER==1
/* idset */
static inline
void kaapi_perf_idset_zero(kaapi_perf_idset_t* set)
{
  *set = 0;
}
static inline
void kaapi_perf_idset_add(kaapi_perf_idset_t* set, kaapi_perf_id_t id)
{
  *set |= KAAPI_PERF_ID_MASK(id);
}
static inline
void kaapi_perf_idset_addset(kaapi_perf_idset_t* set, const kaapi_perf_idset_t mask)
{
  *set |= mask;
}
static inline
int kaapi_perf_idset_clear(kaapi_perf_idset_t* set, kaapi_perf_id_t pcid)
{
  *set &= (kaapi_perf_idset_t)(~KAAPI_PERF_ID_MASK(pcid));
  return 0;
}
static inline
int kaapi_perf_idset_test(const kaapi_perf_idset_t* set, kaapi_perf_id_t pcid)
{
  return (*set & (kaapi_perf_idset_t)KAAPI_PERF_ID_MASK(pcid)) !=0;
}
static inline
int kaapi_perf_idset_test_mask(const kaapi_perf_idset_t* set, kaapi_perf_idset_t mask)
{
  return (*set & mask) !=0;
}
static inline
unsigned int kaapi_perf_idset_size(const kaapi_perf_idset_t set)
{
  return __builtin_popcountl( set );
}
static inline
unsigned int kaapi_perf_idset_empty(const kaapi_perf_idset_t* set)
{
  return *set == 0;
}
#endif


#else // #if KAAPI_USE_TRACELIB==1
#define KAAPI_IFUSE_TRACE(kproc,inst)
#define KAAPI_EVENT_GET(kproc, eventno, kind ) 0
#define KAAPI_EVENT_PUSH(kproc, eventno )

#define KAAPI_EVENT_PUSH0(kproc, eventno, kind )
#define KAAPI_EVENT_PUSH1(kproc, eventno, kind, p1 )
#define KAAPI_EVENT_PUSH2(kproc, eventno, kind, p1, p2 )
#define KAAPI_EVENT_PUSH3(kproc, eventno, kind, p1, p2, p3 )
#define KAAPI_EVENT_PUSH4(kproc, eventno, kind, p1, p2, p3, p4 )
#define KAAPI_EVENT_PUSH_PERFCTR(kproc, eventno, kind, pc, idset, perfctr )
#define KAAPI_EVENT_PUSH_UNCORE_PERFCTR(kproc, numaid, idset, perfctr )

/* push new event with given date (value returned by kaapi_event_date())
   the macros returns the date of the event else 0
*/
#define KAAPI_EVENT_PUSH0_AT(kproc, tclock, eventno, kind )
#define KAAPI_EVENT_PUSH1_AT(kproc, tclock, eventno, kind, p1 )
#define KAAPI_EVENT_PUSH2_AT(kproc, tclock, eventno, kind, p1, p2 )
#define KAAPI_EVENT_PUSH3_AT(kproc, tclock, eventno, kind, p1, p2, p3 )

#endif // #if KAAPI_USE_TRACELIB==1

#if defined(__cplusplus)
}
#endif

#endif
