/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012 INRIA.
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
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/resource.h>
#include <pthread.h>


#include <stdint.h>
//TODO: restore this in cmake
#if 0
#  if defined(__linux__)
#    define __USE_MISC 1
#    include <sched.h>
#    if LIBOMP_USE_NUMA
#      include <numa.h>
#      include <numaif.h>
#    endif
#  endif
#endif


#if KAAPI_USE_PAPI
#include <papi.h>
#include "hw_count.h"
#endif

#include "kaapi.h"
#include "kaapi_impl.h"
#include "kaapi_trace.h"
#include "kaapi_trace_util.h"
#include "kaapi_trace_recorder.h"
#include "kaapi_hashmap.h"

#if ((KAAPI_PERF_ID_ENDSOFTWARE+KAAPI_MAX_HWCOUNTERS) > KAAPI_PERF_ID_MAX)
#error "The maximal size of the peformance counters handled by Kaapi should be extended. Please contact the authors."
#endif

#if defined(__cplusplus)
extern "C" {
#endif
#define RESTRICT

/* ------------------------------------------------------------------------------------------- */
/*
  Global Variable
*/
/* ------------------------------------------------------------------------------------------- */
/* +1 each time kaapi_trace_init is called. The first call initialize the library
*/
static int once_init = 0;
kaapi_tracelib_param_t kaapi_tracelib_param;
static int fmt_listcapacity = 0;


/* ------------------------------------------------------------------------------------------- */
/*
  Human readable name of events
*/
/* ------------------------------------------------------------------------------------------- */
const char* kaapi_event_name[]
= {
/* 0 */  "K-ProcExec",
/* 1 */  "K-ProcInfo",
/* 2 */  "TaskExec",
/* 3 */  "TaskInfo",
/* 4 */  "TaskUserAttr",
/* 5 */  "Sched",
/* 6 */  "StealRequest",
/* 7 */  "OffloadCpy",
/* 8 */  "OffloadKern",
/* 9 */  "TaskSync",
/*10 */  "PerfCounter",
/*11 */  "TaskPerfCntr",
/*12 */  "PerUncore",
/*13 */  "Call",
/*14 */  "Loop",
/*15 */  "Energy"
};

/*
 */
int kaapi_event_get_name( int8_t evtno, int8_t kind, char* buffer, int ssize)
{
  if ((evtno<0) || (evtno >=KAAPI_EVT_LAST)) return EINVAL;
  const char* kindstr = "<?>";;
  switch(evtno) {
  case KAAPI_EVT_KPROC:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    break;
  case KAAPI_EVT_TASK_EXEC:
    if (kind==0) kindstr = "Push";
    else if (kind==1) kindstr = "Async";
    else if (kind==2) kindstr = "Begin";
    else if (kind==3) kindstr = "End";
    break;
  case KAAPI_EVT_TASK_INFO:
    if (kind==0) kindstr = "Succ";
    else if (kind==1) kindstr = "Access";
    break;
  case KAAPI_EVT_TASK_USERATTR:
    if (kind==0) kindstr = "1";
    else if (kind==1) kindstr = "2";
    else if (kind==2) kindstr = "3";
    else if (kind==3) kindstr = "4";
    break;
  case KAAPI_EVT_SCHED:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    break;
  case KAAPI_EVT_STEAL_REQUEST:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    break;
  case KAAPI_EVT_OFFLOAD_CPY:
    if (kind==0) kindstr = "Push";
    else if (kind==1) kindstr = "Begin";
    else if (kind==2) kindstr = "End";
    break;
  case KAAPI_EVT_OFFLOAD_KERN:
    if (kind==0) kindstr = "Push";
    else if (kind==1) kindstr = "Begin";
    else if (kind==2) kindstr = "End";
    break;
  case KAAPI_EVT_TASKSYNC:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    break;
  case KAAPI_EVT_PERFCOUNTER:
  case KAAPI_EVT_TASK_PERFCOUNTER:
  case KAAPI_EVT_PERF_UNCORE:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    else if (kind==2) kindstr = "Switch"; /* for evt_perfcounter*/
    break;
  case KAAPI_EVT_CALL:
    if (kind==0) kindstr = "Begin";
    else if (kind==1) kindstr = "End";
    else if (kind==2) kindstr = "info";
    else if (kind==3) kindstr = "info";
    break;
  case KAAPI_EVT_LOOP:
    break;
  case KAAPI_EVT_ENERGY:
    break;
  };
  snprintf(buffer, ssize, "%s-%s", kaapi_event_name[evtno], kindstr );
}


/* ------------------------------------------------------------------------------------------- */
/*
   Meta data about performance counter
*/
/* ------------------------------------------------------------------------------------------- */
#if KAAPI_USE_PERFCOUNTER==1
typedef struct {
  const char*            name;         /* human readable */
  const char*            cmdlinename;  /* command line name */
  uint8_t                eventcode;
  uint8_t                ns2s;         /* 1 iff need conversion ns -> s when displayed */
  uint8_t                opaccum;      /* 0 = add, 1=max */
  uint8_t                kind;
  uint8_t                ctype;
  char                   unit;
  const char*            helpstring;
  kaapi_perf_counter_t  (*reader)(void*arg);
  void*                   arg_reader;
} kaapi_perfctr_info_t;


/* not that eventcode is set during initialization
   - type:
      0: uint64_t
      1: int64_t
      2: double
  - unit:
      s: -> second
      #: count (integer)
      B: -> Bytes
*/
kaapi_perfctr_info_t kaapi_perfctr_info[KAAPI_PERF_ID_MAX] = {
                                        /* evt ns2s accum kind ctype unit   help */
/* 0 */  { "WorkCPU",       "WORK_CPU",      0,   1,   0,   0,   0,   's',  "<not yet implemented>" },
/* 1 */  { "WorkGPU",       "WORK_GPU",      0,   1,   0,   0,   0,   's',  "<not yet implemented>" },
/* 2 */  { "Tidle",         "TIDLE",         0,   1,   0,   0,   0,   's',  "" },
/* 3 */  { "TInf",          "TINF",          0,   1,   1,   0,   0,   's',  "<not implemented>" },
/* 4 */  { "TaskSpawn",     "TASKSPAWN",     0,   1,   0,   0,   0,   '#',  "" },
/* 5 */  { "TaskStartExec", "TASKSTARTEXEC", 0,   1,   0,   0,   0,   '#',  "" },
/* 6 */  { "TaskExec",      "TASKEXEC",      0,   1,   0,   0,   0,   '#',  "" },
/* 7 */  { "TaskSteal",     "TASKSTEAL",     0,   1,   0,   0,   0,   '#',  "" },

/* 8 */  { "StealReq",      "STEALREQ",      0,   1,   0,   0,   0,   '#',  "" },
/* 9 */  { "StealReqOk",    "STEALREQOK",    0,   1,   0,   0,   0,   '#',  "" },
/* 10 */ { "StealOp",       "STEALOP",       0,   1,   0,   0,   0,   '#',  "" },
/* 11 */ { "Sync",          "SYNC",          0,   1,   0,   0,   0,   '#',  "" },
/* 12 */ { "GPUAlloc",      "GPUALLOC",      0,   1,   0,   0,   0,   'B',  "" },
/* 13 */ { "GPUFree",       "GPUFREE",       0,   1,   0,   0,   0,   'B',  "" },
/* 14 */ { "CPY_H2H",       "CPYH2H",        0,   1,   0,   0,   0,   'B',  "" },
/* 15 */ { "CPY_H2D",       "CPYH2D",        0,   1,   0,   0,   0,   'B',  "" },
/* 16 */ { "CPY_D2H",       "CPYD2H",        0,   1,   0,   0,   0,   'B',  "" },
/* 17 */ { "CPY_D2D",       "CPYD2D",        0,   1,   0,   0,   0,   'B',  "" },
/* 18 */ { "CacheHit",      "CACHE_HIT",     0,   1,   0,   0,   0,   '#',  "" },
/* 19 */ { "CacheMiss",     "CACHE_MISS",    0,   1,   0,   0,   0,   '#',  "" },
/* 20 */ { "ByteCacheHit",  "CACHE_HIT_B",   0,   1,   0,   0,   0,   'B',  "" },
/* 21 */ { "ByteCacheMiss", "CACHE_MISS_B",  0,   1,   0,   0,   0,   'B',  "" },

/* 22 */ { "CPUFlops",      "CPU_FLOPS",     0,   1,   0,   0,   0,   '#',  "" },
/* 23 */ { "CPUDFlops",     "CPU_DFLOPS",    0,   1,   0,   0,   0,   '#',  "" },
/* 24 */ { "GPUFlops",      "GPU_FLOPS",     0,   1,   0,   0,   0,   '#',  "" },
/* 25 */ { "GPUDFlops",     "GPU_DFLOPS",    0,   1,   0,   0,   0,   '#',  "" },
/* 26 */ { "",              0,               0,   0,   0,   0,   0,   ' ',  "" },
/* 27 */ { "",              0,               0,   0,   0,   0,   0,   ' ',  "" },

/* 28 */ { "ReorderHit",    "REORDER_HIT",   0,   1,   0,   0,   0,   '#',  "" },
/* 29 */ { "ReorderMiss",   "REORDER_MISS",  0,   1,   0,   0,   0,   '#',  "" },
/* 30 */ { "ReorderMissLen","REORDER_MISS_LEN",0, 1,   0,   0,   0,   '#',  "" },

/* 31 */ { "LocalRead",     "LOCAL_READ",    0,   1,   0,   0,   0,   'B',  "" },
/* 32 */ { "LocalWrite",    "LOCAL_WRITE",   0,   1,   0,   0,   0,   'B',  "" },
/* 33 */ { "RemoteRead",    "REMOTE_READ",   0,   1,   0,   0,   0,   'B',  "" },
/* 34 */ { "RemoteWrite",   "REMOTE_WRITE",  0,   1,   0,   0,   0,   'B',  "" },
/* 35 */ { "DFGBuild",      "DFGBUILD",      0,   1,   0,   0,   0,   's',  "" },
/* 36 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 37 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 38 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 39 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 40 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 41 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 42 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 43 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 44 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 45 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 46 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 47 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 48 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 49 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 50 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 51 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 52 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 53 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 54 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 55 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 56 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 57 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 58 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 59 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 60 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 61 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 62 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" },
/* 63 */ { "",              "",              0,   0,   0,   0,   0,   ' ',  "" }
};

/*
*/
const char* kaapi_tracelib_perfid_to_name(kaapi_perf_id_t id)
{
  kaapi_assert_debug( (0 <= id) && (id <KAAPI_PERF_ID_MAX) );
  return kaapi_perfctr_info[id].name;
}

/* return a reference to the idp-th performance counter of the k-processor
   in the current set of counters
*/
#define KAAPI_PERF_REG(perfproc, idp) ((perfproc)->ctxt->perf_regs[(idp)])
#define KAAPI_PERFCTR_INCR(perfproc,id, value) KAAPI_PERF_REG(perfproc,id) += value
#define KAAPI_PERFCTR_MAX(perfproc,id, value) if (KAAPI_PERF_REG(perfproc,id) < value) \
                                              KAAPI_PERF_REG(perfproc,id)=value


extern kaapi_perfctr_info_t kaapi_perfctr_info[];

/* type of counter */
#define KAAPI_PCTR_LIBRARY      0x1
#define KAAPI_PCTR_PAPI         0x2
#define KAAPI_PCTR_PAPI_UNCORE  0x3
#define KAAPI_PCTR_USER         0x4

static kaapi_perf_id_t kaapi_tracelib_create_perfid(
      const char* name,
      int kind
);


/* ------------------------------------------------------------------------------------------- */
/*
   Predefined group of perf counter
*/
/* ------------------------------------------------------------------------------------------- */
typedef struct  {
  const char*        name;
  int                code;
  kaapi_perf_idset_t mask;
  kaapi_perf_idset_t mask_pertask;
} kaapi_perfctr_group_t;

static kaapi_perfctr_group_t kaapi_perfctr_group[] = {
  { /* KAAPI_PERF_GROUP_TASK */
    .name = "TASK",
    .code = KAAPI_PERF_GROUP_TASK,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_WORK_CPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_WORK_GPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TINF)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TASKSPAWN)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TASKEXEC)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TASKSTARTEXEC)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TASKSTEAL),
    .mask_pertask = 0
  },
  { /* KAAPI_PERF_GROUP_SCHED */
    .name = "SCHED",
    .code = KAAPI_PERF_GROUP_SCHED,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TIDLE)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_STEALREQ)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_STEALREQOK)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_STEALOP)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_TASKSTEAL),
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_SYNCINST),
    .mask_pertask = 0
  },
#if KAAPI_USE_NUMA
  { /* KAAPI_PERF_GROUP_NUMA for both thread and task */
    .name = "NUMA",
    .code = KAAPI_PERF_GROUP_NUMA,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_READ) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_WRITE) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_READ) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_WRITE),
    .mask_pertask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_READ) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_WRITE) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_READ) |
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_WRITE),

  },
#else
  {
    .name = 0,
    .code = -1,
    .mask = 0,
    .mask_pertask = 0
  },
#endif
  { /* KAAPI_PERF_GROUP_DFGBUILD */
    .name = "DFG",
    .code = KAAPI_PERF_GROUP_DFGBUILD,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_DFGBUILD),
    .mask_pertask = 0
  }
  ,
  { /* KAAPI_PERF_GROUP_OFFLOAD */
    .name = "OFFLOAD",
    .code = KAAPI_PERF_GROUP_OFFLOAD,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_ALLOC_GPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_FREE_GPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CPYH2H_BYTES)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CPYH2D_BYTES)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CPYD2H_BYTES)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CPYD2D_BYTES)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CACHE_HIT)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CACHE_MISS)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CACHE_HIT_BYTES)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_CACHE_MISS_BYTES),
    .mask_pertask = 0
  },
  { /* KAAPI_PERF_GROUP_OFFLOAD */
    .name = "FLOPS",
    .code = KAAPI_PERF_GROUP_FLOPS,
    .mask = KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_FLOPS_CPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_DFLOPS_CPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_FLOPS_GPU)|
            KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_DFLOPS_GPU),
    .mask_pertask = 0
  },
};
#endif //#if KAAPI_USE_PERFCOUNTER==1

/* log2 of the size of hashmaps
*/
#define KAAPI_SIZE_DFGCTXT 9

/* Hash map for task format descriptor
*/
static kaapi_hashmap_t                fdescr_map_routine;
static kaapi_hashentries_t*           fdescr_mapentries[1<<KAAPI_SIZE_DFGCTXT];
static kaapi_hashentries_bloc_t       fdescr_mapbloc;
static kaapi_lock_t                   fdescr_map_lock = KAAPI_LOCK_INITIALIZER;

#if KAAPI_USE_PERFCOUNTER==1
/* internal */
static unsigned int user_event_count = 0;
static unsigned int papi_event_count = 0;
static unsigned int papi_uncore_event_count = 0;
static kaapi_perf_idset_t papi_event_mask = 0;
static kaapi_perf_idset_t papi_uncore_event_mask = 0;
#endif

/* ------------------------------------------------------------------------------------------- */
/* Fwd declarations
*/
/*
*/
static int kaapi_get_events(
  const char* env,
  int task_set
);


/** kaapi_get_elapsedns_since_start
    The function kaapi_get_elapsedns_since_start() returns the elapsed time in second
    since an epoch after the kaapi initialization.
*/
static uint64_t kaapi_startup_time = 0;
uint64_t kaapi_get_elapsedns_since_start(void)
{ 
  return (kaapi_get_elapsedns() - kaapi_startup_time);
}

/*
*/
static void kaapi_timelib_init(void)
{
  static int once= 0;
  if (once) return;
  once = 1;
  kaapi_startup_time = kaapi_get_elapsedns();
}

/*
*/
static void kaapi_timelib_fini(void)
{
}

static const char* _get_group( int event)
{
  if (KAAPI_EVT_MASK(event) & KAAPI_EVT_MASK_COMPUTE)
    return "COMPUTE";
  if (KAAPI_EVT_MASK(event) & KAAPI_EVT_MASK_CALL)
    return "CALL";
  if (KAAPI_EVT_MASK(event) & KAAPI_EVT_MASK_SCHED)
    return "SCHED";
#if KAAPI_USE_PERFCOUNTER==1
  if (KAAPI_EVT_MASK(event) & KAAPI_EVT_MASK_PERFCOUNTER)
    return "PERFCTR";
#endif
  if (KAAPI_EVT_MASK(event) & KAAPI_EVT_MASK_ENERGY)
    return "ENERGY";
  return 0;
}

#if LIBOMP_USE_NUMA
/* Fwd Decl
*/
static void* _kaapi_uncore_collector(void*);
static kaapi_atomic_t kaapi_uncore_thread_finish;
static kaapi_atomic_t kaapi_uncore_thread_counter;
static pthread_t* thread_uncore = 0;
static int thread_uncore_count = 0;
#endif

/* ------------------------------------------------------------------------------------------- */
/*
*/
static void _kaapi_print_help(void)
{
  fprintf(stdout,
   "*** Welcome to the quick start of Kaapi options!\n"
   "  For more information, please visit http://kaapi.gforge.inria.fr\n"
   "  All options are controlled by environement variables. The list is the following:\n"
   "  * KAAPI_HELP | KAAPI_HELPME       : show this help\n"
#if KAAPI_USE_PERFCOUNTER==1
   "  * KAAPI_PERF_EVENTS <perflist>    : defines performance counters to be captured by threads\n"
   "  * KAAPI_TASKPERF_EVENTS <perflist>: defines performance counters to be captured by tasks\n"
   "  * KAAPI_UNCOREPERF_EVENTS <perflist>: defines uncore performance counters to be capture by socket\n"
   "  * KAAPI_UNCOREPERF_PERIOD <int>   : defines period to capture KAAPI_UNCOREPERF_EVENTS\n"
#endif
   "  * KAAPI_RECORD_TRACE [0|1]        : to record events at runtime for postmortem analysis\n"
   "  * KAAPI_RECORD_MASK <eventlist>   : selection of events to record\n"
   "Where:\n"
   "<perflist>: is a list (separator ',') of perf counter' names or names of perf counters' groups.\n"
   "    Kaapi following names are available:\n "
  );
#if KAAPI_USE_PERFCOUNTER==1
  for (int i=0; i<KAAPI_PERF_ID_ENDSOFTWARE; ++i)
  {
    if (kaapi_perfctr_info[i].cmdlinename ==0) continue;
    fprintf(stdout, "\t%16.16s: code=%i, %s  %s\n",
        kaapi_perfctr_info[i].cmdlinename,
        kaapi_perfctr_info[i].eventcode,
        kaapi_perfctr_info[i].name,
        kaapi_perfctr_info[i].helpstring
    );
  }
  fprintf(stdout,
   "    Performance counter groups are:\n"
  );
  for (int i=0; i<KAAPI_PERF_GROUP_OFFLOAD; ++i)
  {
    if (kaapi_perfctr_group[i].name == 0) continue;
    fprintf(stdout, "\t%16.16s: ", kaapi_perfctr_group[i].name);
    kaapi_perf_idset_t set = kaapi_perfctr_group[i].mask;
    while (set !=0)
    {
      unsigned int idx;
      idx = __builtin_ffsl( set )-1;
      set &= ~(1UL << idx);
      if (kaapi_perfctr_info[idx].cmdlinename ==0) continue;
      fprintf(stdout, "%s%c ", kaapi_perfctr_info[idx].cmdlinename, (set ==0 ? ' ': ','));
    }
    fprintf(stdout,"\n");
  }
#if KAAPI_USE_PAPI
  fprintf(stdout,
   "    Performance counter name could be PAPI name. Please see papi_avail and related commands.\n"
  );
#else
  fprintf(stdout,
   "    The current library implementation is not configured to use PAPI, thus access to hardware\n"
   "    performance counters is not available. If you want to access those counters, please install\n"
   "    the library once configured with PAPI.\n"
  );
#endif
#endif

  fprintf(stdout,
   "<eventlist>: is a list (separator ',') of event name of groups of events.\n"
   "    Predefined groups are COMPUTE|CALL|PERFCTR|OFFLOAD|SCHED\n"
  );
  fprintf(stdout,"\t%16.16s: %s", "COMPUTE",
     "related to all events concerning task executions.\n"
     "\t                  This group is enough to build data flow graph from the trace.\n"
  );
  fprintf(stdout,"\t%16.16s: %s", "CALL",
     "related to all events concerning call to xkblas routines.\n"
     "\t                  This group is mandatory to evaluate performance of blas routines.\n"
  );
#if KAAPI_USE_PERFCOUNTER==1
  fprintf(stdout,"\t%16.16s: %s", "PERFCTR",
      "to be used to include performance counters in the trace.\n"
  );
#endif
  fprintf(stdout,"\t%16.16s: %s", "OFFLOAD",
   "related to all events concerning offloading support.\n"
  );
  fprintf(stdout,"\t%16.16s: %s", "SCHED",
   "to include events concerning the dynamic scheduling.\n"
  );
  fprintf(stdout,
   "    Individual events can be added to the event list using their code.\n"
   "    The list of events is:\n"
  );
  for (int i=0; i<KAAPI_EVT_LAST; ++i)
  {
    if (kaapi_event_name[i] == 0) continue;
    const char* grp = _get_group( i );
    if (grp)
      fprintf(stdout, "\t%16i: name '%s' in %s\n", i, kaapi_event_name[i], grp );
    else
      fprintf(stdout, "\t%16i: name '%s'\n", i, kaapi_event_name[i] );
  }
  fprintf(stdout,
   "<kind>: is either\n"
   "        * no|0  : to not display information\n"
   "        * final : to display information at the end of the program execution\n"
   "\n"
  );
}


/* ------------------------------------------------------------------------------------------- */
/**
*/
int kaapi_tracelib_init(
  int gid
)
{
  if (++once_init >1) return 0;
  int i, error;

#if KAAPI_USE_PERFCOUNTER==1
  /* Update counters: undefined code */
  for (i=0; i<KAAPI_PERF_ID_ENDSOFTWARE; ++i)
    kaapi_perfctr_info[i].eventcode = i;
#endif

  if(getenv("KAAPI_HELP") || getenv("KAAPI_HELPME"))
    _kaapi_print_help();

  kaapi_tracelib_param.cpucount = 0;
  kaapi_tracelib_param.gpucount = 0;
  kaapi_tracelib_param.numaplacecount = 0;
  kaapi_tracelib_param.fmt_list = 0;
  kaapi_tracelib_param.fmt_listsize = 0;

  kaapi_tracelib_param.eventmask               = 0;
  kaapi_tracelib_param.recordfilename         = 0;
  KAAPI_ATOMIC_WRITE(&kaapi_tracelib_param.nthreads, 0);
  kaapi_tracelib_param.threads = (kaapi_tracelib_thread_t**)calloc( 512, sizeof(kaapi_tracelib_thread_t*) );

  /* allows to have the delay between init/fini */
  kaapi_timelib_init();

  /* map for format descriptor */
  kaapi_hashmap_init( &fdescr_map_routine,
                      fdescr_mapentries,
                      KAAPI_SIZE_DFGCTXT,
                      &fdescr_mapbloc );

  kaapi_tracelib_param.gid = gid;
  kaapi_tracelib_param.cpucount = -1;
  kaapi_tracelib_param.gpucount = -1;
#if LIBOMP_USE_NUMA
  kaapi_tracelib_param.numaplacecount = numa_num_configured_nodes();
#else
  kaapi_tracelib_param.numaplacecount = 1;
#endif

  /* generating prefix for events' files 
  */
  kaapi_tracelib_param.recordfilename = getenv("KAAPI_RECORD_PREFIX");
  if (kaapi_tracelib_param.recordfilename ==0)
  {
    char filename[128];
    const char* uname = getenv("USER");
    if (uname !=0)
      sprintf(filename,"/tmp/events.%s.%i", uname, kaapi_tracelib_param.gid );
    else
      sprintf(filename,"/tmp/events.%i", kaapi_tracelib_param.gid );
    kaapi_tracelib_param.recordfilename = strdup(filename);
  }

#if KAAPI_USE_PERFCOUNTER==1
  /* perf counters initialization */
  kaapi_perf_idset_zero( &kaapi_tracelib_param.perfctr_idset ) ;
  kaapi_perf_idset_zero( &kaapi_tracelib_param.taskperfctr_idset ) ;
  kaapi_perf_idset_zero( &kaapi_tracelib_param.uncoreperfctr_idset ) ;

#if KAAPI_USE_PAPI
  error = PAPI_library_init(PAPI_VER_CURRENT);
  kaapi_assert(error == PAPI_VER_CURRENT);
  
  error = PAPI_thread_init(pthread_self);
  kaapi_assert(error == PAPI_OK);

  kaapi_assert(sizeof(long_long) == sizeof(kaapi_perf_counter_t) );
#endif

  error = kaapi_get_events("KAAPI_PERF_EVENTS", 0);
  kaapi_assert(0 == error);

  /* always add task counter into the global set of counter */
  /* may be user add counter perf task (e.g. etf WSPUSH strategy) */
  error = kaapi_get_events("KAAPI_TASKPERF_EVENTS", 1);
  kaapi_assert(0 == error);

  error = kaapi_get_events("KAAPI_UNCOREPERF_EVENTS", 2);
  kaapi_assert(0 == error);

  char* str = getenv("KAAPI_UNCOREPERF_PERIOD");
  if (str)
  {
    error = kaapi_parse_delay(&str, &kaapi_tracelib_param.uncore_period);
    kaapi_assert(0 != error);
  }
#else // #if KAAPI_USE_PERFCOUNTER==1
  if ( (0!=getenv("KAAPI_UNCOREPERF_PERIOD"))
    || (0!=getenv("KAAPI_UNCOREPERF_EVENTS"))
    || (0!=getenv("KAAPI_PERF_EVENTS"))
    || (0!=getenv("KAAPI_TASKPERF_EVENTS"))
     )
  {
    fprintf(stderr,"*** Warning. At least one of the environment variables KAAPI_PERF_EVENTS| KAAPI_TASKPERF_EVENTS| KAAPI_UNCOREPERF_EVENTS| KAAPI_UNCOREPERF_PERIOD is defined but Kaapi library is compiled without support for performance counter\n");
  }
#endif

  /* event mask */
  kaapi_tracelib_param.eventmask = 0;
  if ((getenv("KAAPI_RECORD_TRACE") !=0) && !strcasecmp(getenv("KAAPI_RECORD_TRACE"),"1"))
  {
    if (getenv("KAAPI_RECORD_MASK") !=0)
    {
      /* actual grammar:
         descr[,descr]*
         descr -> groupname | eventno
         eventno is an integer less than 2^sizeof(kaapi_event_mask_type_t)
         grammar must be more complex using predefined set
      */
      uint64_t mask = 0;
      char* name = getenv("KAAPI_RECORD_MASK");
      bool err = kaapi_parse_listkeywords( &mask, &name, ',',
         5,
           "COMPUTE", (uint64_t)KAAPI_EVT_MASK_COMPUTE,
           "CALL",    (uint64_t)KAAPI_EVT_MASK_CALL,
           "SCHED",   (uint64_t)KAAPI_EVT_MASK_SCHED,
/* parse perfctr event masks even if support is not defined */
           "PERFCTR", (uint64_t)KAAPI_EVT_MASK_PERFCOUNTER,
           "OFFLOAD", (uint64_t)KAAPI_EVT_MASK_OFFLOAD
      );
      if (err ==false)
      {
        fprintf(stderr, "*** Kaapi: mal formed mask list 'KAAPI_RECORD_MASK': '%s'\n",
          getenv("KAAPI_RECORD_MASK")
        );
        return EINVAL;
      }
      else 
        fprintf(stdout, "*** Kaapi: record ok with record mask: '%s'\n", name);

      /* always add startup set */
      kaapi_tracelib_param.eventmask = mask|KAAPI_EVT_MASK_STARTUP;
    }
  }

  kaapi_tracelib_param.fmt_list = 0;
  kaapi_tracelib_param.fmt_listsize = 0;
  fmt_listcapacity = 0;

  /* init recorder module */
  kaapi_eventrecorder_init();

#if (KAAPI_USE_PERFCOUNTER==1) && (LIBOMP_USE_NUMA==1)
  /* Start Monitoring thread uncore event that should be collected once... */
  if (kaapi_tracelib_param.uncoreperfctr_idset || papi_uncore_event_mask)
  {
    int nnodes = numa_num_configured_nodes();
    thread_uncore = (pthread_t*)malloc(sizeof(pthread_t)*nnodes);
    KAAPI_ATOMIC_WRITE(&kaapi_uncore_thread_finish, 0);
    KAAPI_ATOMIC_WRITE(&kaapi_uncore_thread_counter, 0);
    thread_uncore_count = 0;
    for (int i=0; i<nnodes; ++i)
    {
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      KAAPI_ATOMIC_INCR(&kaapi_uncore_thread_counter);
      kaapi_assert(pthread_create(&thread_uncore[i], &attr, _kaapi_uncore_collector, (void*)(intptr_t)i) ==0);
      ++thread_uncore_count;
    }
    while (KAAPI_ATOMIC_READ(&kaapi_uncore_thread_counter) !=0)
      sched_yield();
  }
#endif

  return 0;
}



/* ------------------------------------------------------------------------------------------- */
/** Finish trace. Assume that threads have reach the barrier and flush
    their event buffers.
*/
static int once_fini = 0;
void kaapi_tracelib_fini(void)
{
  if (++once_fini < once_init) return;

  FILE *file = 0;
  char buffer[8192];
  char filename[128];

#if LIBOMP_USE_NUMA
  KAAPI_ATOMIC_WRITE(&kaapi_uncore_thread_finish, 1);
  for (int i=0; i<thread_uncore_count; ++i)
  {
    void* res;
    kaapi_assert(pthread_join(thread_uncore[i], &res) ==0);
  }
#endif

#if KAAPI_USE_PAPI
  PAPI_shutdown();
#endif

  if (kaapi_tracelib_param.eventmask)
    kaapi_eventrecorder_fini();

  kaapi_timelib_fini();
}

/* ------------------------------------------------------------------------------------------- */
/**
*/
int kaapi_tracelib_thread_init(
    kaapi_tracelib_thread_t* kproc,
    kaapi_context_t*         ctxt,
    uint64_t                 kid,
    int                      cpu,       /* if protype ==2, used to attach event to cpu */
    int                      numaid,    /*  */
    int                      proctype   /* 0: cpu, 1: gpu, 2: uncore collector */
)
{
  /* reuse team and per thread data. Init or re-init papi in the both case.
  */
  size_t size = sizeof(kaapi_tracelib_thread_t);
  memset( kproc, 0, size );
  kproc->ctxt         = ctxt;
  kproc->kid          = kid;
  kproc->numaid       = numaid;
  kproc->cpu          = cpu;
  kproc->ptype        = proctype;
  kproc->task         = 0;
#if KAAPI_USE_PERFCOUNTER==1
  kproc->papi_event_count  = 0;
#endif

  switch (proctype) {
    case 2: /* uncore collector */
      kproc->event_mask      = kaapi_tracelib_param.eventmask;
#if KAAPI_USE_PERFCOUNTER==1
      kproc->perfset         = kaapi_tracelib_param.uncoreperfctr_idset;
      kproc->task_perfset    = 0;
      kproc->papi_event_mask = papi_uncore_event_mask;
      kproc->papi_event_count= papi_uncore_event_count;
#endif
    break;

    case 1: /* GPU */
      kproc->event_mask      = kaapi_tracelib_param.eventmask;
#if KAAPI_USE_PERFCOUNTER==1
      kproc->perfset         = kaapi_tracelib_param.perfctr_idset;
      kproc->task_perfset    = kaapi_tracelib_param.taskperfctr_idset;
      kproc->papi_event_mask = 0;
      kproc->papi_event_count= 0;
#endif
    break;

    case 0:  /* CPU */
    default:
      kproc->event_mask      = kaapi_tracelib_param.eventmask;
#if KAAPI_USE_PERFCOUNTER==1
      kproc->perfset         = kaapi_tracelib_param.perfctr_idset;
      kproc->task_perfset    = kaapi_tracelib_param.taskperfctr_idset;
      kproc->papi_event_mask = papi_event_mask;
      kproc->papi_event_count= papi_event_count;
#endif
    break;
  }

  if (kaapi_tracelib_param.eventmask)
    kproc->eventbuffer = kaapi_event_openbuffer((int)kid, proctype );

#if (KAAPI_USE_PERFCOUNTER==1) && (KAAPI_USE_PAPI==1)
  int papi_event_codes[KAAPI_MAX_HWCOUNTERS];
  if (kproc->papi_event_mask)  /* papi_event_count */
  {
    int err;
    PAPI_option_t opt;

    /* register the thread */
    err = PAPI_register_thread();
    kaapi_assert(PAPI_OK == err);

    /* create event set */
    kproc->papi_event_set = PAPI_NULL;
    err = PAPI_create_eventset(&kproc->papi_event_set);
    kaapi_assert(PAPI_OK == err);

    if (proctype ==0)
    {
      /* set cpu as the default component. mandatory in newer interfaces. */
      err = PAPI_assign_eventset_component(kproc->papi_event_set, 0);
      kaapi_assert(PAPI_OK == err);

      /* thread granularity */
      memset(&opt, 0, sizeof(opt));
      opt.granularity.def_cidx = kproc->papi_event_set;
      opt.granularity.eventset = kproc->papi_event_set;
      opt.granularity.granularity = PAPI_GRN_THR;
      err = PAPI_set_opt(PAPI_GRANUL, &opt);
      kaapi_assert(PAPI_OK == err);

      /* user domain */
      memset(&opt, 0, sizeof(opt));
      opt.domain.eventset = kproc->papi_event_set;
      opt.domain.domain = PAPI_DOM_USER;
      err = PAPI_set_opt(PAPI_DOMAIN, &opt);
      kaapi_assert(PAPI_OK == err);
    }
    else if (proctype ==2)
    {
      /* Find the uncore PMU */
      int uncore_cidx=PAPI_get_component_index("perf_event_uncore");
      if (uncore_cidx<0) {
        fprintf(stderr, "*** PAPI: perf_event_uncore component not found\n");
        return 0;
      }
      /* Check if component disabled */
      const PAPI_component_info_t *info =PAPI_get_component_info(uncore_cidx);
      if (info->disabled) {
        fprintf(stderr,"*** PAPI: perf_event_uncore component is disabled\n");
      }

      /* set cpu as the default component. mandatory in newer interfaces. */
      err = PAPI_assign_eventset_component(kproc->papi_event_set, uncore_cidx);
      kaapi_assert(PAPI_OK == err);

      PAPI_cpu_option_t cpu_opt;
      memset(&cpu_opt, 0, sizeof(cpu_opt));
      cpu_opt.eventset=kproc->papi_event_set;
      cpu_opt.cpu_num=cpu;
      err = PAPI_set_opt(PAPI_CPU_ATTACH,(PAPI_option_t*)&cpu_opt);
      kaapi_assert(PAPI_OK == err);

#if 0
      memset(&opt, 0, sizeof(opt));
      opt.granularity.def_cidx = 0;
      opt.granularity.eventset = kproc->papi_event_set;
      opt.granularity.granularity = PAPI_GRN_SYS;
      err = PAPI_set_opt(PAPI_GRANUL, &opt);

      kaapi_assert(PAPI_OK == err);
      memset(&opt, 0, sizeof(opt));
      opt.domain.def_cidx = 0;
      opt.domain.eventset = kproc->papi_event_set;
      opt.domain.domain = PAPI_DOM_ALL;
      err = PAPI_set_opt(PAPI_DOMAIN, &opt);
      kaapi_assert(PAPI_OK == err);
#endif
    }

    /* configure the papi event set */
    unsigned int i;
    int count =0;
    for ( i=KAAPI_PERF_ID_PAPI_BASE; i<KAAPI_PERF_ID_MAX; ++i)
    {
      if (kaapi_perf_idset_test( &kproc->perfset, i))
        papi_event_codes[count++] = kaapi_perfctr_info[i].eventcode;
    }
    kaapi_assert_debug( count == papi_event_count );

    err = PAPI_add_events(kproc->papi_event_set, papi_event_codes, count);
    if (err != PAPI_OK) 
      fprintf(stderr,"PAPI error code:%i, could not add events in set. Msg: %s\n",err, PAPI_strerror(err));
    kaapi_assert(PAPI_OK == err);
  }
#endif

  int idx = KAAPI_ATOMIC_INCR_ORIG(&kaapi_tracelib_param.nthreads);
  kaapi_assert( idx < 512 ); /* see tracelib_init */
  kaapi_tracelib_param.threads[idx] = kproc;
  return 0;
}


/*
*/
void kaapi_tracelib_thread_start( kaapi_tracelib_thread_t* kproc )
{
  /* reset all counters in both sys/usr states */
  kproc->tstart = kaapi_get_elapsedns();

#if KAAPI_USE_PAPI
  if (kproc->papi_event_count)
  {
    int err = PAPI_start(kproc->papi_event_set);
    if (err != PAPI_OK) 
      fprintf(stderr,"PAPI error code:%i, could not start counting. Msg: %s\n",err, PAPI_strerror(err));
    
    kaapi_assert(PAPI_OK == err);
  }
#endif
#if LIBOMP_USE_NUMA
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_KPROC, 0 /*start*/, kproc->ptype, __kmp_cpu2node(sched_getcpu()) );
#else
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_KPROC, 0 /*start*/, kproc->ptype, 0 );
#endif
#if KAAPI_USE_PERFCOUNTER==1
  /* capture perfcounter */
  if (kproc->perfset & KAAPI_EVT_MASK(KAAPI_EVT_PERFCOUNTER))
  {
/* "Here to patch the fact that kproc->perf_regs has migrated to kaapi_context -> use backward pointer but at the price of merge of interface....*/
    kaapi_tracelib_thread_read( kproc,
                                kproc->perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_PERFCOUNTER, 0 /*start*/, 0/*notask*/, &kproc->perfset, kproc->ctxt->perf_regs );
  }
#endif
}


/*
*/
void kaapi_tracelib_thread_stop(
    kaapi_tracelib_thread_t*     kproc
)
{
#if KAAPI_USE_PERFCOUNTER==1
  if (kproc->perfset & KAAPI_EVT_MASK(KAAPI_EVT_PERFCOUNTER))
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_PERFCOUNTER, 1 /*stop*/, 0/*no task*/, &kproc->perfset, kproc->ctxt->perf_regs );
  }
#endif
#if LIBOMP_USE_NUMA
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_KPROC, 1 /*stop*/, kproc->ptype, __kmp_cpu2node(sched_getcpu()) );
#else
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_KPROC, 1 /*stop*/, kproc->ptype, 0 );
#endif
}


#if KAAPI_USE_PERFCOUNTER==1
/*
*/
void kaapi_tracelib_thread_read(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_perf_idset_t           idset,
    kaapi_perf_counter_t*        regs
)
{
  kaapi_assert_debug( sizeof(idset) == 64/8 ); /* __builtin functions on 64 bits */

  /* read first software counters */
  unsigned int i;
  kaapi_perf_idset_t set = idset & ((1UL << KAAPI_PERF_ID_ENDSOFTWARE) -1);

  /* recopy software registers */
  while (set !=0)
  {
    i = __builtin_ffsl( set )-1;
    set &= ~(1UL << i);
    *regs = kproc->ctxt->perf_regs[i];
    ++regs;
  }

#if KAAPI_USE_PAPI
  if (idset & kproc->papi_event_mask)
  {
    /* not that event counts between kaapi_tracelib_thread_init and here represent the
       cost to this thread wait all intialization of other threads, set setconcurrency.
       After this call we assume that we are counting.
    */
    kaapi_assert(PAPI_OK == PAPI_read(kproc->papi_event_set, (long_long*)regs));
    regs += kproc->papi_event_count;
  }
#endif

  /* here is the user defined counter ! */
  while (idset !=0)
  {
    i = __builtin_ffsl( idset )-1;
    idset &= ~(1UL << i);
    kaapi_assert( kaapi_perfctr_info[i].kind== KAAPI_PCTR_USER);
    //if (kaapi_perfctr_info[i].kind== KAAPI_PCTR_USER)
    {
      kaapi_assert( kaapi_perfctr_info[i].reader != 0);
      *regs = kaapi_perfctr_info[i].reader(kaapi_perfctr_info[i].arg_reader);
      ++regs;
    }
  }
}
#endif



/* Switch to count time from tidle to twork
   Return the time take into accounting
*/
/* Switch to mode state
*/
int kaapi_tracelib_thread_switchstate(
    kaapi_tracelib_thread_t*     kproc
)
{
#if KAAPI_USE_PERFCOUNTER==1
  if (kproc->perfset & KAAPI_EVT_MASK(KAAPI_EVT_PERFCOUNTER))
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_PERFCOUNTER, 2 /*switch*/, kproc->task, &kproc->perfset, kproc->ctxt->perf_regs );
  }
#endif
  return 0;
}


/*
*/
void kaapi_tracelib_thread_fini(
    kaapi_tracelib_thread_t*     kproc
)
{
  if (kproc->event_mask)
    kaapi_event_closebuffer(kproc->eventbuffer);

#if KAAPI_USE_PAPI
  if (kproc->papi_event_count)
  {
    PAPI_cleanup_eventset(kproc->papi_event_set);
    PAPI_destroy_eventset(&kproc->papi_event_set);
    kproc->papi_event_set = PAPI_NULL;
    kproc->papi_event_count = 0;
    PAPI_unregister_thread();
  }
#endif
}


#if 0 // TODO
/*
*/
void kaapi_tracelib_thread_stealop (
    kaapi_tracelib_thread_t*     kproc,
    uint8_t                      op,
    uint32_t                     cpu,
    uint32_t                     node,
    uint32_t                     victim_level,
    uint32_t                     victim_level_id
)
{
  kaapi_event_t* evt = KAAPI_EVENT_GET(kproc, 0, KAAPI_EVT_TASK_STEALOP );
  if (evt)
  {
    evt->u.s.d0.i8[0]  = op;
    evt->u.s.d1.i32[0] = cpu;
    evt->u.s.d1.i32[1] = node;
    evt->u.s.d2.i32[0] = victim_level;
    evt->u.s.d2.i32[1] = victim_level_id;
    KAAPI_EVENT_PUSH(kproc,0, KAAPI_EVT_TASK_STEALOP);
  }
}
#endif



/* ------------------------------------------------------------------------------------------- */
/* 
*/

/*
*/
__thread uint64_t task_id = 0;
kaapi_task_id_t kaapi_tracelib_newtask_id(void)
{
  kaapi_task_id_t value = (kaapi_task_id_t)(++task_id | (((uint64_t)pthread_self()) << 32UL));
  return value;
}

/* 
*/
void kaapi_tracelib_task_begin(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    uint64_t                     fmtid
)
{
  KAAPI_EVENT_PUSH3(kproc, KAAPI_EVT_TASK_EXEC, 2/*begin */, task, fmtid, kproc->numaid );
#if KAAPI_USE_PERFCOUNTER==1
  if (kproc->task_perfset)
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->task_perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_TASK_PERFCOUNTER, 0 /*begin*/, task, &kproc->task_perfset, kproc->ctxt->perf_regs );
  }
#endif
  kproc->task = task;
}


/* */
void kaapi_tracelib_task_end(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    kaapi_task_id_t              parent
)
{
#if KAAPI_USE_PERFCOUNTER==1
  /* incr before counter is dumped into the event */
  KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_TASKEXEC, 1);
  if (kproc->task_perfset)
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->task_perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_TASK_PERFCOUNTER, 1 /*end*/, task, &kproc->task_perfset, kproc->ctxt->perf_regs );
  }
#endif
  KAAPI_EVENT_PUSH3(kproc, KAAPI_EVT_TASK_EXEC, 3/*end */, task, 0, kproc->numaid );
  kproc->task = parent;
}


/*
*/
void kaapi_tracelib_task_depend(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              source,
    kaapi_task_id_t              sink
)
{
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_TASK_INFO, 0 /* succ*/, source, sink );
}


/* To adapt
*/
#if 0
void kaapi_tracelib_task_attr(
    kaapi_tracelib_thread_t* kproc,
    kaapi_task_id_t task,
    int kind,
    int64_t value
)
{
}
#endif


/*
*/
#if defined(__linux__) && KAAPI_USE_NUMA
static unsigned int kaapi_numa_getpage_id(const void* addr)
{
  int mode = -1;
  const int err = get_mempolicy(&mode, (unsigned long*)0, 0, (void* )addr, MPOL_F_NODE | MPOL_F_ADDR);

  if (err)
    return (unsigned int)-1;

  /* convert to internal kaapi identifier */
  return mode;
}
#endif


#if KAAPI_USE_PERFCOUNTER==1
static const uint64_t MASK_PERF_READWRITE_ACCESS = 
   KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_READ)  | KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_LOCAL_WRITE) |
   KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_READ) | KAAPI_PERF_ID_MASK(KAAPI_PERF_ID_REMOTE_WRITE);
#endif


static void __kaapi_dump_access(
    kaapi_tracelib_thread_t*     kproc,
    int                          local_numaid,
    kaapi_task_id_t              task,
    int                          count,
    void*                        deps,
    void                       (*decoder)(void*, int, void**, size_t*, int*)
)
{
  for (int i=0; i<count; ++i)
  {
    int mode = KAAPI_ACCESS_MODE_VOID;
    void* addr = 0;
    size_t len;
    decoder( deps, i, &addr, &len, &mode);
    if (mode & KAAPI_ACCESS_MODE_V)
      continue;

#if defined(__linux__) && KAAPI_USE_NUMA
    unsigned int numaid = kaapi_numa_getpage_id( addr );
#else
    unsigned int numaid = 0;
#endif
    kaapi_event_t* evt = KAAPI_EVENT_GET(kproc, KAAPI_EVT_TASK_INFO, 1 );
    if (evt)
    {
      evt->u.s.d0.u = (uint64_t)task;
      evt->u.s.d1.p = addr;
      evt->u.s.d2.u = len;
      evt->u.s.d3.i32[0] = mode;
      evt->u.s.d3.i32[1] = numaid;
      KAAPI_EVENT_PUSH(kproc, KAAPI_EVT_TASK_INFO);
    }

    /* how to count remote access if numa information not available ? */
    if (numaid == (unsigned int)-1) return;

#if KAAPI_USE_PERFCOUNTER==1
    if (kaapi_perf_idset_test_mask(&kproc->perfset, MASK_PERF_READWRITE_ACCESS))
    {
      if ((numaid == local_numaid) || (numaid == (unsigned int)-1))
      {
        if (KAAPI_ACCESS_IS_READ(mode))
          KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_LOCAL_READ, 1);
        if (KAAPI_ACCESS_IS_WRITE(mode))
          KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_LOCAL_WRITE, 1);
      }
      else
      {
        if (KAAPI_ACCESS_IS_READ(mode))
          KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_REMOTE_READ, 1);
        if (KAAPI_ACCESS_IS_WRITE(mode))
          KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_REMOTE_WRITE, 1);
      }
    }
#endif
  }
}


/*
*/
void kaapi_tracelib_task_access(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task,
    int                          count,
    void*                        deps,
    int                          count_noalias,
    void*                        deps_noalias,
    void                       (*decoder)(void*, int, void**, size_t*, int*)
)
{
  if ( 
#if KAAPI_USE_PERFCOUNTER==1
       !kaapi_perf_idset_test_mask(&kproc->perfset, MASK_PERF_READWRITE_ACCESS) && 
#endif
       !(kproc->event_mask & KAAPI_EVT_MASK(KAAPI_EVT_TASK_INFO))
  )
    return;

#if defined(__linux__)
  int localcpu = sched_getcpu();
#if KAAPI_USE_NUMA
  int local_numaid = numa_node_of_cpu(localcpu);
#else
  int local_numaid = 0;
#endif
#else
  int local_numaid = 0;
#endif
  __kaapi_dump_access(kproc, local_numaid, task, count, deps, decoder);
  __kaapi_dump_access(kproc, local_numaid, task, count_noalias, deps_noalias, decoder);
}


/*
*/
void kaapi_tracelib_taskwait_begin(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task
)
{
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_TASKSYNC, 0/* begin*/, task, kproc->numaid );
#if KAAPI_USE_PERFCOUNTER==1
  if (kproc->task_perfset)
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->task_perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_TASK_PERFCOUNTER, 2 /*switch*/, task, &kproc->task_perfset, kproc->ctxt->perf_regs );
  }
#endif
}

/*
*/
void kaapi_tracelib_taskwait_end(
    kaapi_tracelib_thread_t*     kproc,
    kaapi_task_id_t              task
)
{
#if KAAPI_USE_PERFCOUNTER==1
  KAAPI_PERFCTR_INCR(kproc, KAAPI_PERF_ID_SYNCINST, 1);
  if (kproc->task_perfset)
  {
    kaapi_tracelib_thread_read( kproc,
                                kproc->task_perfset,
                                kproc->ctxt->perf_regs);
    KAAPI_EVENT_PUSH_PERFCTR(kproc, KAAPI_EVT_TASK_PERFCOUNTER, 2 /*switch*/, task, &kproc->task_perfset, kproc->ctxt->perf_regs );
  }
#endif
  KAAPI_EVENT_PUSH2(kproc, KAAPI_EVT_TASKSYNC, 1 /* end*/, task, kproc->numaid );
}


/*
*/
static kaapi_descrformat_t* kaapi_tracelib_reserve_perfcounter(void)
{
  kaapi_descrformat_t* retval = 0;
  if (kaapi_tracelib_param.fmt_listsize+1 >= fmt_listcapacity)
  {
    if (fmt_listcapacity ==0) fmt_listcapacity = 2;
    kaapi_tracelib_param.fmt_list = (kaapi_descrformat_t**)realloc( kaapi_tracelib_param.fmt_list, 2*fmt_listcapacity*sizeof(kaapi_descrformat_t) );
    fmt_listcapacity *= 2;
  }

  retval = (kaapi_descrformat_t*)malloc(sizeof(kaapi_descrformat_t));
  retval->fmtid    = 0;
  retval->name     = 0;
  retval->color    = 0;
  kaapi_tracelib_param.fmt_list[kaapi_tracelib_param.fmt_listsize] = retval;
  ++kaapi_tracelib_param.fmt_listsize;

  kaapi_assert(retval != 0);
  return retval;
}


/*
*/
kaapi_descrformat_t* kaapi_tracelib_register_fmtdescr(
    int implicit,
    void* key,
    const char* psource,
    const char* name,
    char* (*filter_func)(char*, int, const char*, const char*)
)
{
  kaapi_hashentries_t* entry = kaapi_hashmap_find( &fdescr_map_routine, key );
  if (entry != 0)
    return KAAPI_HASHENTRIES_GET(entry, kaapi_descrformat_t*);

  kaapi_atomic_lock(&fdescr_map_lock);
  entry = kaapi_hashmap_find( &fdescr_map_routine, key );
  if (entry != 0)
  {
    kaapi_descrformat_t* pdescr =KAAPI_HASHENTRIES_GET(entry, kaapi_descrformat_t*);
    kaapi_atomic_unlock(&fdescr_map_lock);
    return pdescr;
  }

  /* kaapi_tracelib_reserve_perfcounter recopy descr in kaapi_tracelib_param.fmt_list array*/
  kaapi_descrformat_t* fdescr = kaapi_tracelib_reserve_perfcounter();
  fdescr->fmtid = (uint64_t)(uintptr_t)key;
  fdescr->color = "0 0.5 1.0";
  char buffer [128];
  if (name && filter_func)
  {
    filter_func(buffer, 128, psource, name);
    fdescr->name = strdup(buffer);
  }
  else if (name)
    fdescr->name = strdup(name);
  else if (psource)
    fdescr->name = strdup(psource);
  else
  {
    snprintf(buffer,128,"task_%" PRIu64 "/%p", fdescr->fmtid, key );
    fdescr->name = strdup(buffer);
  }
  entry = kaapi_hashmap_insert( &fdescr_map_routine, key );
  KAAPI_HASHENTRIES_SET(entry, fdescr, kaapi_descrformat_t*);
  kaapi_atomic_unlock(&fdescr_map_lock);
  return fdescr;
}



/**
*/
void _kaapi_signal_dump_counters(int xxdummy)
{
  kaapi_event_fencebuffers();
  _exit(-1);
}


#if KAAPI_USE_PERFCOUNTER==1
static kaapi_perf_id_t kaapi_tracelib_create_perfid(
  const char* name,
  int kind
)
{
  kaapi_perf_id_t id = KAAPI_PERF_ID_PAPI_BASE+papi_event_count+papi_uncore_event_count+user_event_count;
  if (id >= KAAPI_PERF_ID_MAX) return -1;
  kaapi_assert( kind != KAAPI_PCTR_LIBRARY );
  kaapi_perfctr_info[id].name = strdup(name);
  kaapi_perfctr_info[id].kind = kind;
  kaapi_perfctr_info[id].ns2s = 0;
  kaapi_perfctr_info[id].opaccum = 0;
  kaapi_perfctr_info[id].ctype = 0;
  kaapi_perfctr_info[id].unit = '#';
  kaapi_perfctr_info[id].helpstring = 0;
  kaapi_perfctr_info[id].reader = 0;
  kaapi_perfctr_info[id].arg_reader = 0;
  switch (kind) {
  case KAAPI_PCTR_PAPI:
    ++papi_event_count; break;
  case KAAPI_PCTR_PAPI_UNCORE:
    ++papi_uncore_event_count; break;
  case KAAPI_PCTR_LIBRARY:
    /* already preallocated ! */
    break; 
  case KAAPI_PCTR_USER:
    ++user_event_count; break;
  }
  return id;
}

/* This function should be called first to declare papi event counted before user defined counters
*/
kaapi_perf_id_t kaapi_tracelib_create_user_perfid(
  const char* name,
  kaapi_perf_counter_t (*read)(void*), void* ctxt,
  int opaccum,
  const char* type, /* uint64_t, double */
  char unit
)
{
  uint8_t code_ctype = (uint8_t)-1;
  if ((type==0)||(strcmp(type,"uint64_t")==0)) code_ctype = 0;
  else if (strcmp(type,"double")==0) code_ctype = 1;
  else return -1;

  kaapi_perf_id_t id = kaapi_tracelib_create_perfid( name, KAAPI_PCTR_USER );
  if (id >= KAAPI_PERF_ID_MAX) return -1;
  kaapi_perfctr_info[id].ns2s = 0;
  kaapi_perfctr_info[id].opaccum = opaccum;
  kaapi_perfctr_info[id].ctype = code_ctype;
  kaapi_perfctr_info[id].unit = unit;
  kaapi_perfctr_info[id].helpstring = 0;
  kaapi_perfctr_info[id].reader = read;
  kaapi_perfctr_info[id].arg_reader = ctxt;
  return id;
}



/* Used to loop over all perfcounters in set
*/
size_t kaapi_tracelib_count_perfctr(void)
{
  return KAAPI_PERF_ID_PAPI_BASE + papi_event_count + papi_uncore_event_count + user_event_count;
}
#endif

/* fwd decl */
static int get_event_code(char* name, int* code, uint8_t* type);

/* initialize global field kaapi_event, kaapi_name, kaapi_event_count
*/
static int kaapi_get_events(
  const char* env,
  int task_set /* 0== thread, 1== task, 2==uncore for collector */
)
{
  if ((task_set <0) || (task_set >2))
    return EINVAL;

  /* todo: [u|k]:EVENT_NAME */
  unsigned int i = 0;
  unsigned int j;
  const char* s = 0;
  char name[64];
  int event_code;

  s = getenv(env);
  if (s == NULL)
    return 0;

#if KAAPI_USE_PERFCOUNTER==1
  if (task_set == 1)
    kaapi_perf_idset_zero( &kaapi_tracelib_param.taskperfctr_idset );
#endif

  while (*s)
  {
    uint8_t type = 0;

    for (j = 0; j < (sizeof(name) - 1) && *s && (*s != ','); ++s, ++j)
      name[j] = *s;
    name[j] = 0;

    if (get_event_code(name, &event_code, &type ) != 0)
      return -1;


#if (KAAPI_USE_PERFCOUNTER==1) && (KAAPI_USE_PAPI==1)
    /* Register PAPI counter to be at KAAPI_PERF_ID_PAPI_BASE+cnt in kaapi_perfctr_info
    */
    if (type == KAAPI_PCTR_PAPI)
    {
      kaapi_assert(task_set != 1);
      kaapi_perf_id_t newid = kaapi_tracelib_create_perfid( strdup(name), (task_set ==0? KAAPI_PCTR_PAPI : KAAPI_PCTR_PAPI_UNCORE) );
      if (newid == -1)
      {
        fprintf(stderr,"*** Kaapi: too many hardware counters defined while adding event from '%s'\n", env);
        return -1;
      }
      kaapi_perfctr_info[newid].type = KAAPI_PCTR_PAPI;
      kaapi_perfctr_info[newid].eventcode = event_code;
      switch (task_set) {
        case 1:
          kaapi_perf_idset_add( &kaapi_tracelib_param.taskperfctr_idset, newid );
        case 0:
          kaapi_perf_idset_add( &kaapi_tracelib_param.perfctr_idset, newid );
          papi_event_mask |= KAAPI_PERF_ID_MASK(newid);
          if (papi_event_count >= KAAPI_MAX_HWCOUNTERS)
          {
            fprintf(stderr,"*** Kaapi: too many hardware counters defined while adding event from '%s'\n", env);
            kaapi_abort(__LINE__, __FILE__, "*** aborting");
          }
          break;
        case 2:
          kaapi_perf_idset_add( &kaapi_tracelib_param.uncoreperfctr_idset, newid );
          papi_uncore_event_mask |= KAAPI_PERF_ID_MASK(newid);
          if (papi_uncore_event_count >= KAAPI_MAX_HWCOUNTERS)
          {
            fprintf(stderr,"*** Kaapi: too many hardware counters defined while adding event from '%s'\n", env);
            kaapi_abort(__LINE__, __FILE__, "*** aborting");
          }
          break;
      }
    } else
#endif
#if KAAPI_USE_PERFCOUNTER==1
    if (type == KAAPI_PCTR_LIBRARY)
    {
      if (event_code <KAAPI_PERF_ID_MAX)
      {
        kaapi_perfctr_info[event_code].kind = KAAPI_PCTR_LIBRARY;
        kaapi_perfctr_info[event_code].eventcode = event_code;
        switch (task_set) {
          case 1:
            kaapi_perf_idset_add( &kaapi_tracelib_param.taskperfctr_idset, event_code );
          case 0:
            kaapi_perf_idset_add( &kaapi_tracelib_param.perfctr_idset, event_code );
            break;
          case 2:
            kaapi_perf_idset_add( &kaapi_tracelib_param.uncoreperfctr_idset, event_code );
        }
      }
      else { /* group event */
        event_code -= KAAPI_PERF_ID_MAX;
        if (task_set)
        switch (task_set) {
          case 1:
            kaapi_perf_idset_addset( &kaapi_tracelib_param.taskperfctr_idset, kaapi_perfctr_group[event_code].mask_pertask);
          case 0:
            kaapi_perf_idset_addset( &kaapi_tracelib_param.perfctr_idset, kaapi_perfctr_group[event_code].mask);
            break;
          case 2:
            kaapi_perf_idset_addset( &kaapi_tracelib_param.uncoreperfctr_idset, kaapi_perfctr_group[event_code].mask);
        }
      }
    }
#endif
    ++i;

    if (*s == 0)
      break;

    ++s;
  }
  return 0;
}

/* Find event name '<name>' or 'KAAPI_<name>' in list of predefined events
   Return -1 if not found
*/
int get_kaapi_code( char* name )
{
  int i;
#if KAAPI_USE_PERFCOUNTER==1
  /* group of events */
  for (i=0; i<sizeof(kaapi_perfctr_group)/sizeof(kaapi_perfctr_group_t); ++i)
  {
    if (kaapi_perfctr_group[i].name ==0)
      continue;
    if (strcasecmp(name, kaapi_perfctr_group[i].name) ==0) /* group */
      return KAAPI_PERF_ID_MAX+kaapi_perfctr_group[i].code;
    /* test KAAPI_<name> definition */
    if ((strncasecmp(name, "KAAPI_", 6) ==0)
     && (strcasecmp(name+6, kaapi_perfctr_group[i].name) ==0)) /* group */
        return KAAPI_PERF_ID_MAX+kaapi_perfctr_group[i].code;
  }

  /* specific event name ? */
  for (i=0; i<KAAPI_PERF_ID_MAX; ++i)
  {
    if (kaapi_perfctr_info[i].cmdlinename ==0) continue;
    if (strcasecmp(kaapi_perfctr_info[i].cmdlinename,name) ==0)
      return kaapi_perfctr_info[i].eventcode;
    if ((strncasecmp(name, "KAAPI_", 6) ==0)
     && (strcasecmp(kaapi_perfctr_info[i].cmdlinename,name+6) ==0))
        return kaapi_perfctr_info[i].eventcode;
  }
#endif
  return -1;
}

/* any update in this list of event => update README.envvars */
static int get_event_code(char* name, int* code, uint8_t* type)
{
  *code = -1;
  if (strncasecmp(name, "PAPI", 4) !=0)
  {
#if (KAAPI_USE_PERFCOUNTER==1) && (KAAPI_USE_PAPI==1)
    int c = get_kaapi_code(name);
    if (c != -1)
    {
      *code = c;
      *type = KAAPI_PCTR_LIBRARY;
      return 0;
    }
#endif
    /* if name start with KAAPI_ but not known -> warning */
    if (strncasecmp(name, "KAAPI_", 5) ==0)
    {
      fprintf(stderr, "*** KAAPI bad event name:%s\n", name);
      return -1;
    }
  }
  /* else assume that its PAPI counter (included native counter) */
#if (KAAPI_USE_PERFCOUNTER==1) && (KAAPI_USE_PAPI==1)
  int err = PAPI_event_name_to_code(name, code);
  if (err != PAPI_OK)
  {
    fprintf(stderr, "*** PAPI error code:%i\n", err);
    return -1;
  }
  *type = KAAPI_PCTR_PAPI;
  return 0;
#elif KAAPI_USE_PERFCOUNTER==1
  fprintf(stderr, "*** KAAPI bad event name:%s. May be because PerCounter is not configured\n", name);
  return -1;
#else
  fprintf(stderr, "*** KAAPI bad event name:%s. May be because PAPI is not configured\n", name);
  return -1;
#endif
}


#if (KAAPI_USE_PERFCOUNTER==1) && (LIBOMP_USE_NUMA==1)
/* Each uncore thread is attached to a socket (i.e. to a core to a socket).
*/
void* _kaapi_uncore_collector(void* arg )
{
  if (kaapi_tracelib_param.uncoreperfctr_idset ==0 )
    return 0;
  /* force to run on given socket */
  int numaid = (int)(intptr_t)arg;
  int cpu;
  int err = numa_run_on_node(numaid);
  if (err) fprintf(stderr,"numa_run_on_node(%i): error '%s'\n", numaid, strerror(errno));
  while (1)
  {
    cpu = sched_getcpu();
    int node = numa_node_of_cpu(cpu);
    if (node == numaid)
      break;
    sched_yield();
  }
  /* signal for master thread */
  KAAPI_ATOMIC_DECR(&kaapi_uncore_thread_counter);

  kaapi_tracelib_thread_t* kproc = kaapi_tracelib_thread_init(-1, cpu, -1, 2 /* to be uncore collector */ );
  kaapi_perf_counter_t regs[KAAPI_PERF_ID_MAX];

  /* infinite loop */
  struct timespec delay = { kaapi_tracelib_param.uncore_period/1000000000ULL, kaapi_tracelib_param.uncore_period%1000000000 };
  kaapi_tracelib_thread_start(kproc);
  while (KAAPI_ATOMIC_READ(&kaapi_uncore_thread_finish) == 0)
  {
    kaapi_tracelib_thread_read(kproc, kproc->perfset, regs);
    KAAPI_EVENT_PUSH_UNCORE_PERFCTR( kproc, 0, numaid, &kproc->perfset, regs );
#if __linux__
    clock_nanosleep(CLOCK_REALTIME, 0, &delay, 0 );
#else
    struct timespec ts = delay;
    struct timespec rem;
    clock_gettime( CLOCK_REALTIME, &ts );
    ts.tv_nsec += delay.tv_nsec;
    ts.tv_sec += delay.tv_sec;
    if (ts.tv_nsec >= 1000000000ULL)
    {
      ts.tv_sec = ts.tv_nsec / 1000000000ULL;
      ts.tv_nsec = ts.tv_nsec % 1000000000ULL;
    }
    while (nanosleep( &ts, &rem) ==EINTR)
    {
      ts = rem;
    }
#endif
  }
  kaapi_tracelib_thread_stop(kproc);
  kaapi_tracelib_thread_fini(kproc);

  return 0;
}
#endif //NUMA && PERFCOUNTER

#if defined(__cplusplus)
}
#endif
