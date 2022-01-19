/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
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
#ifndef _KAAPI_PERFCTR_H
#define _KAAPI_PERFCTR_H

#if defined(__cplusplus)
extern "C" {
#endif

#if KAAPI_USE_PERFCOUNTER==1
/* ========================================================================= */
/* Kaapi performance counter                                                 */
/* ========================================================================= */
/** \ingroup PERF
    Performace counters.
    TKAAPI_PERF_ID_TASKSTARTEXECKAAPI_PERF_ID_ALLOC_GPUhe set of counters is decomposed in two classes:
    - software counters
    - hardware counters using PAPI support to read and accumulate them.
    Both classes are unified in the Kaapi runtime : selection of events to count are specified using :
      - KAAPI_PERF_ (per process)
      - KAAPI_TASKPERF_ (per thread) env. vars.
    
    On specific points in the execution, the runtime read selected event counters. Counters are available on
    a thread basis. <todo: unicore counters>

    Comments about the performance counters:
    * KAAPI_PERF_ID_WORK_CPU: the CPU work performed by a thread. The work is the sum of time for executing user code of CPU task.
    * KAAPI_PERF_ID_WORK_GPU: the GPU work performed by a thread. The work is the sum of time for executing user code of GPU task.
              Note that WORK_CPU and WORK_GPU are separate because some thread, as thread managing GPU, could perform CPU
              or GPU work.
    * KAAPI_PERF_ID_TIDLE: the work performed by the thread during scheduling phase
    * KAAPI_PERF_ID_TINF: the critical path. Not yet computed online. Named is reserved for future extension.
    * KAAPI_PERF_ID_TASKSPAWN: the number of tasks spawned (created).
    * KAAPI_PERF_ID_TASKSTARTEXEC: number of tasks started for execution.
    * KAAPI_PERF_ID_TASKEXEC: number of tasks that complete execution. N=KAAPI_PERF_ID_TASKSTARTEXEC-KAAPI_PERF_ID_TASKEXEC=0 or 1
               if a thread execute one task at time. For GPU, N may be up to the degree of concurrency in kernel execution as defined by cuda_conc_kernel
               in kaapi_default_param.
    * KAAPI_PERF_ID_TASKSTEAL: number of tasks stolen to the running thread
    * KAAPI_PERF_ID_FLOPS_CPU, KAAPI_PERF_ID_DFLOPS_CPU: number of floating point operators performed on CPU.
               The accounting relies on user definitions (this is not hardware counter!) and accumulation is always done before task execution.
    * KAAPI_PERF_ID_FLOPS_GPU, KAAPI_PERF_ID_DFLOPS_GPU: number of floating point operators performed on GPU.
               The accounting relies on user definitions (this is not hardware counter!) and accumulation is always done before task execution and
               on GPU it is done when the task is prepared to be launch and waits for the data transfers of its inputs.
*/
#define KAAPI_PERF_ID_MAX          64  /* maximal number of performance counters simultaneously captured */

#define KAAPI_PERF_ID_WORK_CPU         0  /* computation time (user) or schedule time (sys) = tidle */
#define KAAPI_PERF_ID_WORK_GPU         1  /* computation time (user) or schedule time (sys) = tidle */
#define KAAPI_PERF_ID_TIDLE            2  /* idle time, where no computation time is done. May include scheduling time */
#define KAAPI_PERF_ID_TINF             3  /* critical path estimation by following the dependency */

#define KAAPI_PERF_ID_TASKSPAWN        4  /* count number of spawned tasks */
#define KAAPI_PERF_ID_TASKSTARTEXEC    5  /* count number of tasks started for execution */
#define KAAPI_PERF_ID_TASKEXEC         6  /* count number of tasks that complete execution */
#define KAAPI_PERF_ID_TASKSTEAL        7  /* count number of stolen tasks */

#define KAAPI_PERF_ID_STEALREQ         8  /* count number of steal requests emitted */
#define KAAPI_PERF_ID_STEALREQOK       9  /* count number of successful steal requests */
#define KAAPI_PERF_ID_STEALOP          10 /* count number of steal operation (after agregation) */
#define KAAPI_PERF_ID_SYNCINST         11 /* number of sync instruction */

#define KAAPI_PERF_ID_ALLOC_GPU        12 /* number of bytes allocated on the GPU */
#define KAAPI_PERF_ID_FREE_GPU         13 /* number of bytes freed on the GPU */
#define KAAPI_PERF_ID_CPYH2H_BYTES     14 /* number of bytes copied H2H */
#define KAAPI_PERF_ID_CPYH2D_BYTES     15 /* number of bytes copied H2D */
#define KAAPI_PERF_ID_CPYD2H_BYTES     16 /* number of bytes copied D2H */
#define KAAPI_PERF_ID_CPYD2D_BYTES     17 /* number of bytes copied D2D */
#define KAAPI_PERF_ID_CACHE_HIT        18 /* DSM cache HIT */
#define KAAPI_PERF_ID_CACHE_MISS       19 /* DSM cache MISS */
#define KAAPI_PERF_ID_CACHE_HIT_BYTES  20 /* DSM cache HIT (Bytes) */
#define KAAPI_PERF_ID_CACHE_MISS_BYTES 21 /* DSM cache MISS (Bytes) */

/* experimental counters */
#define KAAPI_PERF_ID_FLOPS_CPU        22 /* floating point operations (FP32) performed by CPU */
#define KAAPI_PERF_ID_DFLOPS_CPU       23 /* double floating point operations (FP64) performed by CPU */
#define KAAPI_PERF_ID_FLOPS_GPU        24 /* floating point operations (FP32) performed by GPU */
#define KAAPI_PERF_ID_DFLOPS_GPU       25 /* double floating point operations (FP64) performed by GPU */
/*#define UNUSED                       26 */
/*#define UNUSED                       27 */

#define KAAPI_PERF_ID_REORDER_HIT      28 /* */
#define KAAPI_PERF_ID_REORDER_MISS     29 /* */
#define KAAPI_PERF_ID_REORDER_MISS_LEN 30 /* */

#define KAAPI_PERF_ID_LOCAL_READ       31 /* software counter of number of read parameter local (NUMA) to a task */
#define KAAPI_PERF_ID_LOCAL_WRITE      32 /* software counter of number of written parameter local to a task  */
#define KAAPI_PERF_ID_REMOTE_READ      33 /* idem for remote data */
#define KAAPI_PERF_ID_REMOTE_WRITE     34 /* idem for remote data */

#define KAAPI_PERF_ID_DFGBUILD         35

#define KAAPI_PERF_ID_ENDSOFTWARE      36 /* mark end of software counters */

#define KAAPI_MASK_PERF_ID_SOFTWARE ((1UL << KAAPI_PERF_ID_ENDSOFTWARE) -1)
#define KAAPI_PERF_ID_MASK(ID) (1UL << (ID))

/* Each bit set to 1 in an eventset after position KAAPI_PERF_ID_PAPI_BASE corresponds to a papi events
   The name of the counter at bit i, is papi_names[i-KAAPI_PERF_ID_PAPI_BASE].
   Predefined PAPI event counter: those events exist only if they are defined
   in KAAPI_PERF_EVENTS or KAAPI_TASKPERF_EVENTS
*/
#define KAAPI_PERF_ID_PAPI_BASE    (KAAPI_PERF_ID_ENDSOFTWARE)


/* Group of events
   Predefined groups of counters to make easy the definition of the event set to capture.
   Exact definitions are available using environment variable KAAPI_HELPME set to non 0 value.
*/
#define KAAPI_PERF_GROUP_TASK      0
#define KAAPI_PERF_GROUP_SCHED     1
#define KAAPI_PERF_GROUP_OMP       2
#define KAAPI_PERF_GROUP_NUMA      3
#define KAAPI_PERF_GROUP_DFGBUILD  4
#define KAAPI_PERF_GROUP_OFFLOAD   5
#define KAAPI_PERF_GROUP_FLOPS     6



/* All counters per thread
*/
typedef kaapi_perf_counter_t kaapi_thread_perfctr_t[KAAPI_PERF_ID_MAX];
#endif

#if defined(__cplusplus)
}
#endif

#endif
