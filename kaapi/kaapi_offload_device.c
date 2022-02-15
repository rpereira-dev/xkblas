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

#define _OFFLOAD_DEBUG  0
#define KAAPI_STREAM_CAPACITY 512

#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include "kaapi_impl.h"
#include "kaapi_offload.h"
#include "kaapi_memory.h"


#define LOGDEBUG(x)


/* Used to compute the global_max cpu delay time 
*/
#define KAAPI_CPUDELAY_MAX_UPDATETIME 1000000000UL /* in ns */
static pthread_mutex_t  truc_max = PTHREAD_MUTEX_INITIALIZER;
static float global_max_cpudelay = 0;
static uint64_t date_global_max  = 0;


static void _kaapi_offload_device_finalize(kaapi_device_t* const device);

#if KAAPI_SLEEP_DEVICETHREAD 
/* same as wakeup without lock/unlock */
static void kaapi_offload_device_wakeup_(kaapi_device_t* const device)
{
  if (device->issleeping == 1)
  {
    device->issleeping = 0;
    kaapi_assert(0 == pthread_cond_signal(&device->cond_sleep));
  }
}
#endif


/* call for each parameter
*/
static void callback_epilogue_perparam(
    kaapi_task_t* task,
    unsigned int i,
    kaapi_access_t* a,
    uint64_t arg
)
{
  kaapi_device_t* device = (kaapi_device_t*)arg;
  kaapi_dsm_release_data(a->mdi, device->memdev.asid, a );
}


/* call when the kernel is detected to be completed
*/
static void callback_epilogue(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s\n", __FUNCTION__);
  fflush(stdout);
#endif
  kaapi_device_t*  device       = (kaapi_device_t*)arg0;
#if KAAPI_USE_PERFCOUNTER
  kaapi_context_t* ctxt         = device->ctxt;
#endif
  kaapi_task_t*    task         = (kaapi_task_t*)arg1;
  uint64_t         index        = (uint64_t)(uintptr_t)arg2;

  kaapi_assert_debug( task->device ==device );

  KAAPI_ATOMIC_INCR(&device->cnt_exec);
  KAAPI_ATOMIC_DECR(&device->cnt_ready);
  KAAPI_EVENT_PUSH3( &kaapi_self_context()->kproc, KAAPI_EVT_TASK_EXEC,
       3 /* end */, task, kaapi_task_getformat_ref(task)->fmtid, kaapi_task_getargs(task) );
  
  /* update the finish index for the current tasks and all next task that may have finish earlier */
#if KAAPI_LOG_PIPE
  printf("Task[%i]=%p finish\n", index, device->pipeline[index % device->pipe_size]);
#endif
  
#if KAAPI_PIPELINE_GPUTASK
  /* must by try lock to avoid supspending the callback execution thread */
  pthread_mutex_lock(&device->pipe_lock);
  device->pipeline[index % device->pipe_size] = 0; /* free the slot in the pipeline */
#if KAAPI_LOG_PIPE
  printf("Task[%i]=%p mark finished\n", index, task );
#endif
  if (index == device->p_finish)
  {
    uint64_t p_ready = device->p_ready;
    ++device->p_finish;
    for ( index=device->p_finish; index<p_ready; ++index)
    {
      if (device->pipeline[index % device->pipe_size] !=0) break;
#if KAAPI_LOG_PIPE
      printf("Task[%i]=%p is finished, increment device finish index:%i\n", index, device->pipeline[index%device->pipe_size], index );
#endif
    }
    device->p_finish = index;
    kaapi_assert( index <= p_ready );
  }
  pthread_mutex_unlock(&device->pipe_lock);
#endif
  
  /* TODO: what is the order of pushed task versus the order of creation and insertion for dependencies ?
     Does the callback must activate successors ?
  */
  int cnt __attribute__((unused));
  cnt = kaapi_sched_activate_successors(
    kaapi_context2thread(device->ctxt), 
    task,
    callback_epilogue_perparam, (uint64_t)device
  );

  /* */
  double flops = 0, dflops= 0, data = 0;
  const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
  --device->pendingtasks;
  if (kaapi_taskflag_get(task,KAAPI_TASK_PERFCNT))
    kaapi_format_get_cost(fmt, kaapi_task_getargs(task), task, &flops, &dflops, &data );
  device->flops_tasks += flops+dflops;
  device->data_tasks += data;
  device->flops_pendingtasks -= flops+dflops;
  device->data_pendingtasks -= data;
  
#if KAAPI_USE_PERFCOUNTER
  device->sum_cpudelay += status.cpu_delay;
  ++device->cnt_task;
  if (status.cpu_delay > device->max_cpudelay)
  {
    device->max_cpudelay = status.cpu_delay;
    uint64_t t0 = kaapi_get_elapsedns();
    if ((device->max_cpudelay > global_max_cpudelay) || (t0-date_global_max>KAAPI_CPUDELAY_MAX_UPDATETIME))
    {
      pthread_mutex_lock(&truc_max);
      if ((device->max_cpudelay > global_max_cpudelay) || (t0-date_global_max>KAAPI_CPUDELAY_MAX_UPDATETIME))
      {
        date_global_max = t0;
        global_max_cpudelay = device->max_cpudelay;
      }
      pthread_mutex_unlock(&truc_max);
    }
  }
  if (status.cpu_delay < device->min_cpudelay)
    device->min_cpudelay = status.cpu_delay;
#if KAAPI_LOG_DELAY
  fprintf(device->flog_delay,"%i,%f,%f,%f\n",device->device_id,kaapi_get_elapsedtime(),status.cpu_delay,status.gpu_delay);
#endif

  KAAPI_CTXT_PERFREG_INCR(ctxt,KAAPI_PERF_ID_TASKEXEC);
  KAAPI_CTXT_PERFREG_ADD (ctxt,KAAPI_PERF_ID_WORK_CPU, status.cpu_delay);
  KAAPI_CTXT_PERFREG_ADD (ctxt,KAAPI_PERF_ID_WORK_GPU, status.gpu_delay);

  if (kaapi_taskflag_get(task,KAAPI_TASK_PERFCNT))
  {
    kaapi_offloadtask_perfcounter_t* perf = &device->perfcnt.task[fmt->fmtid];
    perf->time  += status.gpu_delay;
    perf->flops += flops;
    perf->ai += flops/data;
  }
#endif
  ++device->exec_count;
  KAAPI_ATOMIC_INCR(&task->frame->exec_count);
  task->flags |= KAAPI_TASK_FLAG_EXEC;
  KAAPI_OFFLOAD_TRACE_OUT
}


/* Call when data have been received on node
   When all data are valid for this task, insert the task into the stream
    arg0 : the device that acquire data for one of its task
    arg1 : the task
  May be called by any device threads...
*/
KAAPI_DEBUG_INST(kaapi_atomic64_t count_valid = {0};
                 kaapi_atomic64_t call_valid = {0};)
static void callback_set_valid(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_t*          device = (kaapi_device_t*)arg0; 
  kaapi_task_t*            task   = (kaapi_task_t*)arg1;
  uint64_t                 index  = (uint64_t)(uintptr_t)arg2;

  kaapi_assert(task !=0);
  KAAPI_DEBUG_INST(KAAPI_ATOMIC_INCR(&call_valid);)
  kaapi_assert_debug( task->device ==device );

  int wc = KAAPI_ATOMIC_DECR(&task->wc);
  if (wc == 0)
  {
    KAAPI_ATOMIC_INCR(&device->cnt_ready);
    KAAPI_ATOMIC_DECR(&device->cnt_pending);
#if KAAPI_PIPELINE_GPUTASK ==0
    kaapi_stream_insert_io_task_inst(
      &device->stream,
      KAAPI_IO_STREAM_KERN,
      task,
      callback_epilogue,
      (void*)device, (void*)task, (void*) (uintptr_t)index
    );
#endif
#if KAAPI_PIPELINE_GPUTASK
#  if KAAPI_USE_PERFCOUNTER
    if (index > device->p_ready)
      KAAPI_CTXT_PERFREG_INCR(device->ctxt,KAAPI_PERF_ID_REORDER_MISS);
    KAAPI_CTXT_PERFREG_ADD(device->ctxt,KAAPI_PERF_ID_REORDER_MISS_LEN, (index-device->p_ready));
#  endif
#endif
  }
  
#if KAAPI_PIPELINE_GPUTASK
# if KAAPI_REORDER_TASK_EXEC
  if (wc ==0)
  {
    kaapi_stream_insert_io_task_inst(
      &device->stream,
      KAAPI_IO_STREAM_KERN,
      task,
      callback_epilogue,
      (void*)device, (void*)task, (void*) (uintptr_t)index
    );
  }
#  if KAAPI_USE_PERFCOUNTER
  if (device->p_ready != index)
    KAAPI_CTXT_PERFREG_INCR(device->ctxt,KAAPI_PERF_ID_REORDER_HIT);
#endif

  pthread_mutex_lock(&device->pipe_lock);
  if (index == device->p_ready)
  { 
    for (; index < device->p_write; ++index)
    { 
      kaapi_task_t* task = device->pipeline[index % device->pipe_size];
      /* if task==0, means finish to be executed due to re-ordering execution */
      if ((task !=0) && (KAAPI_ATOMIC_READ(&task->wc) !=0))
        break;
    }
    device->p_ready = index;
  }
  pthread_mutex_unlock(&device->pipe_lock);

# else

  /* task is the nex ready, does it is the next ready task to insert ? */
  pthread_mutex_lock(&device->pipe_lock);
  if (index == device->p_ready)
  {
    for (; index < device->p_write; ++index)
    {
      task = device->pipeline[index % device->pipe_size];
      if (KAAPI_ATOMIC_READ(&task->wc) !=0)
        break;
#  if KAAPI_LOG_PIPE
      printf("Task[%i]=%p insert into stream\n",index, task);
#  endif
      kaapi_stream_insert_io_task_inst(
        &device->stream,
        KAAPI_IO_STREAM_KERN,
        task,
        callback_epilogue,
        (void*)device, (void*)task, (void*) (uintptr_t)index
      );
    }
    device->p_ready = index;
  }
  pthread_mutex_unlock(&device->pipe_lock);
# endif // reorder
#endif // #if KAAPI_PIPELINE_GPUTASK

  KAAPI_OFFLOAD_TRACE_OUT
}


/* Call the device' task entry point
   - task cannot create new tasks on the device
   - the context must has been attached to the device before calling the
   function
*/
int kaapi_offload_device_execute_task(
     kaapi_device_t* const device,
     kaapi_task_t* task,
     void* handle
 )
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_context_t* ctxt = device->ctxt;
  kaapi_format_t* fmt = kaapi_task_getformat_ref(task);

#if KAAPI_DEBUG
  kaapi_assert( task->device ==device );
#endif
  KAAPI_EVENT_PUSH3( &kaapi_self_context()->kproc, KAAPI_EVT_TASK_EXEC,
     2 /* begin */, task, kaapi_task_getformat_ref(task)->fmtid, kaapi_task_getargs(task) );
  
  /* handle comes form portability layer: for cuda its the cublas hande */
  ctxt->pc = task;
  ((kaapi_task_bodyfnc_gpu_t)fmt->entrypoint[device->driver->f_get_type()])(
      task, kaapi_context2thread(ctxt), handle
  );

  KAAPI_OFFLOAD_TRACE_OUT

  return 0;
}


#if KAAPI_USE_PREFETCH
#if KAAPI_DEBUG
static void callback_epilogue_prefetch_data(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
}
#endif


/* Send prefetch for next tasks.
   The tasks in the list are not ready and will not become ready during the call.
   There is not indenpedent task in the list.
*/
static void kaapi_do_prefetch_data(
     kaapi_device_t* const device,
     int count,
     kaapi_task_t** tasklist
)
{
  uint16_t lid = kaapi_memory_asid_get_lid( device->memdev.asid );

  /* */
  for (int i=0; i<count; ++i)
  {
    kaapi_task_t* task = tasklist[i];
    const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
    kaapi_assert_debug((task->flags & KAAPI_TASK_FLAG_INDEPENDENT) ==0);

    unsigned int ith;
    kaapi_memory_view_t view;
    kaapi_access_t* access;
    kaapi_metadata_info_t* mdi;
    unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));

    for(ith= 0; ith < count_params; ++ith)
    {
      kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, ith, kaapi_task_getargs(task));
      kaapi_access_mode_t mp = KAAPI_ACCESS_GET_MODE(m);
      if (mp & KAAPI_ACCESS_MODE_V)
        continue;
      access = kaapi_format_get_access_param(fmt, (unsigned int)ith, kaapi_task_getargs(task));
      if (!KAAPI_ACCESS_IS_READ(mp) || !access->ready)
        continue;

      /* do bind ptr to the device->asid */
      kaapi_format_get_view_param(fmt, (unsigned int)ith, kaapi_task_getargs(task), &view);

      /* Return the meta data information.
      */
      mdi = access->mdi;
      if (mdi ==0)
        mdi = kaapi_dsm_findaccess_on_node(
            &kaapi_the_dsm,
            device->memdev.asid,
            KAAPI_DSM_CREATE_DATA|KAAPI_DSM_CREATE_MDI,
            access,
            &view
        );

      /* if already valid or under transfer do nothing */
      if (kaapi_memory_replica_is_valid(mdi, lid)
       || kaapi_memory_replica_is_xfer(mdi, lid))
        continue;

      /* else : send prefetch request */
      int err = kaapi_dsm_prefetch_on( &kaapi_the_dsm, device->memdev.asid, mdi, 
#if KAAPI_DEBUG
              callback_epilogue_prefetch_data, access->data, 0, 0 
#else
              0, 0, 0, 0 
#endif
      );
      kaapi_assert((err ==0) || (err ==EINPROGRESS));
    }
  }
}
#endif


/* Send necessary input data to the device where task will be excuted
  Here: task is ready and will be prepared to be executed on GPU.
   - arguments will be send if not yet
   - once sending are completed, then callback to signal ends will
   enforce kernel execution.
*/
static int kaapi_offload_device_prepare_execute_task(
     kaapi_device_t* const device,
     kaapi_task_t* task
 )
{
  KAAPI_OFFLOAD_TRACE_IN
  int err;
  uint16_t lid = kaapi_memory_asid_get_lid( device->memdev.asid );
  const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
  kaapi_assert(fmt !=0);

#if KAAPI_DEBUG
  /* the device that starts a task is also the device that complete the task */
  kaapi_assert( task->device ==0 );
  task->device = device;
#endif

  /* Register counters for performance analysis
  */
  double flops = 0, dflops =0, data = 0;
  ++device->pendingtasks;
  if (kaapi_taskflag_get(task,KAAPI_TASK_PERFCNT))
  {
    kaapi_format_get_cost(fmt, kaapi_task_getargs(task), task, &flops, &dflops, &data );
    device->flops_pendingtasks += (flops+dflops);
    device->data_pendingtasks += data;
  }

  KAAPI_CTXT_PERFREG_INCR(kaapi_self_context(),KAAPI_PERF_ID_TASKSTARTEXEC);
  KAAPI_CTXT_PERFREG_ADD(kaapi_self_context(),KAAPI_PERF_ID_FLOPS_GPU, flops);
  KAAPI_CTXT_PERFREG_ADD(kaapi_self_context(),KAAPI_PERF_ID_DFLOPS_GPU, dflops);
  KAAPI_EVENT_PUSH3( &kaapi_self_context()->kproc, KAAPI_EVT_TASK_EXEC,
       1 /* Async Start exec */, task, fmt->fmtid, kaapi_task_getargs(task));

  kaapi_assert_debug(device == kaapi_offload_self_device());
  kaapi_assert_debug(KAAPI_ATOMIC_READ(&task->wc) ==0);

#if KAAPI_USE_PREFETCH
  int prefetch_taskcnt = 0;
  kaapi_task_t* prefetch_tasklist[KAAPI_MAX_PREFETCH_WINDOW];
#endif

  /* take 'pseudo' lock to avoid activation of the task when data is received.
     Wait until all  parameters have been processed.
  */
  KAAPI_ATOMIC_INCR(&task->wc);
  KAAPI_ATOMIC_INCR(&device->cnt_pending);

#if KAAPI_PIPELINE_GPUTASK
  /* Insert the task into the pipeline: no lock, only device thread may insert
     increment p_write after wc value is computed.
   */
  uint64_t index = device->p_write;
  device->pipeline[index % device->pipe_size] = task;
#else
  uint64_t index = 0;
#endif

  unsigned int ith;
  kaapi_memory_view_t view;
  kaapi_access_t* access = 0;
  kaapi_metadata_info_t* mdi = 0;
  unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));

  /* use task->wc as counter for asynchronous callback to detect
      completion of them
      - each callback decr counter
      - each access without need for callback decr it
      - after all parameters have been visited, then decr the counter.
      The last decr that set counter to 0 push the task on the kernel stream
  */
  for(ith= 0; ith < count_params; ++ith)
  {
    kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, ith, kaapi_task_getargs(task));
    kaapi_access_mode_t mp = KAAPI_ACCESS_GET_MODE(m);

    if (mp & KAAPI_ACCESS_MODE_V)
      continue;

    void* new_data;

    /* do bind ptr to the device->asid */
    access = kaapi_format_get_access_param(fmt, (unsigned int)ith, kaapi_task_getargs(task));
    kaapi_format_get_view_param(fmt, (unsigned int)ith, kaapi_task_getargs(task), &view);

    /* Return the meta data information about the data (ptr, view).
        If data is not registered to the DSM, then the call adds it as new data with
        valid copy only on the host.
        The memory block is allocated and replica information is
        set (with address on the device as well as its memory view).
        Replica information is stored in mdi.
        If replica for device lid is not allocated then on return, it is allocated.
    */
    mdi = kaapi_dsm_findaccess_on_node(
        &kaapi_the_dsm, 
        device->memdev.asid,
        KAAPI_DSM_CREATE_DATA|KAAPI_DSM_CREATE_MDI,
        access,
        &view
    );

    do {
      /* findaccess has already allocated the replica for asid with the right view.
          error code ENOMEM should be processed inside the dsm_acuire data that
          should only return iff memory has been allocated and transfer done.
      */
      kaapi_assert_debug(device == kaapi_offload_self_device());
      err = kaapi_dsm_acquire_data( &kaapi_the_dsm, device->memdev.asid,
          task,
          mp,
          mdi,
          callback_set_valid,
          (void*)device, (void*)task, (void*)(uintptr_t)index
      );
      KAAPI_DEBUG_INST(if (err ==EINPROGRESS) KAAPI_ATOMIC_INCR(&count_valid));

      kaapi_assert((err ==0)||(err == ENOMEM)||(err ==EINPROGRESS));
      if (err == ENOMEM)
        kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2H);
    } while (err == ENOMEM);
    kaapi_assert((err ==0) || (err ==EINPROGRESS));

#if KAAPI_USE_PREFETCH
    if (KAAPI_ACCESS_IS_WRITE(mp))
    {
      /* look for next dependent in order to prefetch data */
      kaapi_access_t* an = access->sync->next;
      if ((an !=0))// && KAAPI_ACCESS_IS_READ(an->mode))
      {
        kaapi_ldid_t ldid = kaapi_task_get_ld(an->task);
        if ((ldid == device->ld->ldid) && (prefetch_taskcnt < KAAPI_MAX_PREFETCH_WINDOW)) 
        {
          prefetch_tasklist[prefetch_taskcnt++] = an->task;
        }
        if (an->sync) an = (kaapi_access_t*)an->sync->next;
        else an = an->next;
      }
    }
#endif

    /* store in the data access the pointer translated by the original offset */
    new_data = kaapi_memory_view2pointer(
      kaapi_pointer2void(mdi->replicas[lid]->ptr),
      &mdi->replicas[lid]->view);

    /* update pointer in the task arguments */
    access->data    = new_data;
    kaapi_format_set_access_param(fmt, ith, kaapi_task_getargs(task), access );
    kaapi_format_set_view_param(fmt, ith,
      kaapi_task_getargs(task),
      &mdi->replicas[lid]->view
    );
  }

#if KAAPI_USE_PREFETCH
  /* prefetch data before launching kernel */
  if (prefetch_taskcnt>0)
  {
    kaapi_do_prefetch_data( device, prefetch_taskcnt, prefetch_tasklist );
  }
#endif
  
#if KAAPI_PIPELINE_GPUTASK
  /* if the current task is the leading ready task then insert it
     lock reading p_ready to avoid situation where callback also try
     to read/incr and insert the task in the stream
  */
  ++device->p_write;
  pthread_mutex_lock(&device->pipe_lock);
#endif //KAAPI_PIPELINE_GPUTASK
  
# if KAAPI_REORDER_TASK_EXEC || (KAAPI_PIPELINE_GPUTASK==0)
  if (KAAPI_ATOMIC_DECR(&task->wc) ==0)
# else
  if ((KAAPI_ATOMIC_DECR(&task->wc) ==0) && (device->p_ready == index))
# endif
  {
# if KAAPI_LOG_PIPE
    printf("Task[%i]=%p insert into stream\n",index, task);
# endif
    kaapi_stream_insert_io_task_inst(
      &device->stream,
      KAAPI_IO_STREAM_KERN,
      task,
      callback_epilogue,
      (void*)device, (void*)task, (void*) (uintptr_t)index
    );
    KAAPI_ATOMIC_INCR(&device->cnt_ready);
    KAAPI_ATOMIC_DECR(&device->cnt_pending);
#if KAAPI_REORDER_TASK_EXEC && KAAPI_USE_PERFCOUNTER
    if (device->p_ready != index)
      KAAPI_CTXT_PERFREG_INCR(device->ctxt,KAAPI_PERF_ID_REORDER_HIT);
#endif
#if KAAPI_PIPELINE_GPUTASK==0
    kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_KERN );
#endif

#if KAAPI_PIPELINE_GPUTASK
# if KAAPI_REORDER_TASK_EXEC
  if (device->p_ready == index)
# endif
    ++device->p_ready;
#endif
  }
  
#if KAAPI_PIPELINE_GPUTASK
# if KAAPI_LOG_PIPE
  else
    printf("Task[%i]=%p not ready into stream\n",index, task);
# endif
  pthread_mutex_unlock(&device->pipe_lock);
#endif //KAAPI_PIPELINE_GPUTASK
  return 0;
}


/* Sync call by non pure CPU thread
   Close to default kaapi_sched_sync, but task execution and activation to successors is deported
   to the callback_callback_epilogue which is called when the cuda kernel ends.
*/
int kaapi_sched_sync_offload( kaapi_thread_t* thread )
{
  return 0;
}


/* post until request processed by cuda thread */
static int kaapi_offload_request2device( kaapi_device_t* device, kaapi_device_op_t op)
{
  int res = 0;
  pthread_mutex_lock(&device->lock);
  while (device->request.op != KAAPI_DEVICEOP_NOP)
  {
#if KAAPI_SLEEP_DEVICETHREAD
    kaapi_offload_device_wakeup_(device);
#endif
    pthread_cond_wait(&device->cond, &device->lock);
  }  

  /* send op */
  device->request.arg = 0;
  device->request.op = op;
#if KAAPI_SLEEP_DEVICETHREAD
  kaapi_offload_device_wakeup_(device);
#endif
  pthread_mutex_unlock(&device->lock);
  return res;
}

/* wait reply
*/
static int kaapi_offload_requestwait( kaapi_device_t* device)
{
  int res;
  /* wait reply */
  pthread_mutex_lock(&device->lock);
  while (device->request.op != KAAPI_DEVICEOP_REPLY)
  {
#if KAAPI_SLEEP_DEVICETHREAD
    kaapi_offload_device_wakeup_(device);
#endif
    pthread_cond_wait(&device->cond, &device->lock);
  }
  res = device->request.err;
  device->request.op      = KAAPI_DEVICEOP_NOP;
  device->request.arg     = 0;
  device->request.counter = 0;
  pthread_mutex_unlock(&device->lock);
  return res;
}

/*
 */
static int kaapi_offload_requestreply( kaapi_device_t* device, int res )
{
  pthread_mutex_lock(&device->lock);
  device->request.op = KAAPI_DEVICEOP_REPLY;
  device->request.err = res;
  pthread_cond_signal(&device->cond_sleep);
  pthread_cond_signal(&device->cond);
  pthread_mutex_unlock(&device->lock);

  return 0;
}


/* Callback to reply to the client
*/
static void callback_replyrequest_memsync(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  kaapi_device_t* device = (kaapi_device_t*)arg0;
  if (device->request.counter !=0)
  {
    if (KAAPI_ATOMIC_DECR(device->request.counter) ==0)
      kaapi_offload_requestreply( device, 0 );
  }
  else /* always reply if no counter */
    kaapi_offload_requestreply( device, 0 );
}


/* Compute load of device. Return the number of GPUs having the maximal load
*/
#define KAAPI_IMAX 4
int _kaapi_compute_load_device(
    int* pmin,
    int* pmax, 
    float* pavrg, 
    float* pdelta, 
    int* imax,  /* of size at least KAAPI_IMAX */
    float* pload
)
{
  int ngpu= kaapi_localitydomain_count(KAAPI_LD_GPU);
  float load[ngpu];
  int max = 0;
  int min = INT_MAX;
  float sum = 0.0;
  int iimax = 0;
  for (int i=0; i<ngpu; ++i)
  {
    kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU,i);
    load[i] = ld->device->pendingtasks;
    //load[i] = ld->device->flops_tasks;
    sum += (float)load[i];
    int l = load[i];
    if (l> max) {
      max = l;
    }
    if (l < min) 
      min = l;
  }
  float minmax = max-min;
  float avrg = sum/ngpu;
  float delta = 0.0;
  for (int i=0; i<ngpu; ++i)
  {
    sum += load[i];
    float d = load[i] - avrg;
    delta += fabs(d);
    if (pload) pload[i] = load[i];

    if (load[i] == max)
    {
      imax[iimax%KAAPI_IMAX]=i;
      ++iimax;
    }
  }
  
  *pmin = min;
  *pmax = max;
  *pavrg = avrg;
  *pdelta = delta;
  return iimax;
}


/* Barrier and execution of task.
*/
int kaapi_sched_idle_offload(
    kaapi_thread_t* thread,
    int (*f_fini)(void*), void* arg
)
{
  int err;
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_device_t* device = ctxt->device;
  kaapi_task_t* task;

  uint64_t send_msg = 0;
  uint64_t tidle_start = 0;

  KAAPI_EVENT_PUSH0( &kaapi_self_context()->kproc, KAAPI_EVT_SCHED, 0 /* start */ );

  /* */
  kaapi_offload_set_current_device(device);

  do
  {
#if KAAPI_SLEEP_DEVICETHREAD
    while ((device->request.op == KAAPI_DEVICEOP_NOP)
        && kaapi_queue_empty(ctxt->queue)
        && (device->exec_count == device->spawn_count + device->ld->queue->push_count)
        && (device->ld->queue->push_count == device->ld->queue->pop_count)
        && kaapi_offload_stream_isempty(&device->stream, KAAPI_IO_STREAM_ALL)
    )
    {
      if (f_fini && f_fini(arg)) goto r_exit;
      kaapi_offload_device_sleep(device);
    }
#else
      if (f_fini && f_fini(arg)) goto r_exit;
#endif

    /* highly active loop to test if task has been enqueued and if asynchronous event has been completed
       - at each loop iteration the thread test:
         - new task to wait =iff= (device->exec_count < device->spawn_count + device->ld->queue->push_count)
    */
    task = 0;
    if ( kaapi_offload_device_accept_new_task(device) )
    {
      /* pop on local queue */
      if (task ==0)
        task = kaapi_fifo_queue_pop(device->ld->queue);

#if KAAPI_WS_GPUTASK
      /* no task ? try to steal */
      if (task ==0)
      {
        if (tidle_start ==0) tidle_start = kaapi_get_elapsedns();
        else if (1e-9*(kaapi_get_elapsedns() - tidle_start) > 0.01)
#if 0//KAAPI_USE_PERFCOUNTER
        else if ((device->cnt_task!=0) 
              && (1e-9*(kaapi_get_elapsedns() - tidle_start) > global_max_cpudelay/kaapi_default_param.cuda_conc_kernel) ///kaapi_default_param.cuda_conc_stream_kernel)) 
        ) 
#endif
        {
          /* Affinity: compute the best (=ldid with at least a write of the task, see IPDPS2013.
             It remains to transfer the affinity during the steal operations where device
             may has the capacity to select the best task or at least:
  	       0- a task with most of its input on the device.
  	       1- a task with inputs on the device close to the target device.
  	       2- a task with inputs on the device. 
  	       3- a task with inputs on the machine. 
          */
          kaapi_localitydomain_t* ld;
#if 1
          int ngpu= kaapi_localitydomain_count(KAAPI_LD_GPU);
          float load[ngpu]; 
          int imax[KAAPI_IMAX]; 
          int max;
          int min; 
          float avrg;
          float delta;
          int iimax = _kaapi_compute_load_device(&min, &max, &avrg, &delta, imax, load);
          float minmax = max-min;

          if ((avrg > 2.0/ngpu) && (delta > 0)) 
          //if ((avrg >= 1.0) && (delta >= 1.0*kaapi_default_param.cuda_conc_kernel))
          //if ((avrg >= kaapi_default_param.cuda_conc_kernel/2.0) && (delta > 1.0*kaapi_default_param.cuda_conc_kernel))
          {
            int d = rand_r(&ctxt->seed) % 100;
            for (int i=d; i<d+iimax; ++i)
            {
              kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, i % iimax );
              task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, 1);
              if (task!=0) break;
              task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, 2);
              if (task!=0) break;
            }
            if (task ==0)
              for (int i=d; i<d+iimax; ++i)
              {
                kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, i % iimax );
                task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, 3);
                if (task!=0) break;
              }
#if 0
            task = kaapi_fifo_queue_steal(ld->queue);
            if (task==0) task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, 2);
#endif
#if 0 // OUTPUT
            if (task != 0) 
            {
              char buffer[128];
              char* b = buffer;
              ssize_t sz = 0;
              sz = sprintf(b, "%02i:: Load: Avrg=%10f, Delta=%10f, MinMax=%10f iimax:%i  ::", device->ld->ldid, avrg, delta, minmax, iimax );
              b += sz;
              for (int i=0; i<ngpu; ++i)
              {
                sz = sprintf(b, " %02i", load[i] );
                b += sz;
              } 
              printf("%s\n",buffer);
            }
#endif
          }

#elif 0
        {
          int rank;
          for (rank=1; rank<device->ld->perfrank; ++rank)
          {
            uint64_t affinity = device->ld->affinity[rank];
            int nb = __builtin_popcount(affinity);
            if (nb >0)
            {
              int victim = rand_r(&device->ctxt->seed) % nb;
              int ldid = 0;
              ldid = __builtin_ffsll(affinity);
              for (int i=0; i<victim; ++i)
              {
                ldid = __builtin_ffsll(affinity);
                kaapi_assert( ldid != 0);
                ldid;
                affinity &= ~(1UL << ldid);
              }
              kaapi_localitydomain_t* ld = kaapi_localitydomain_get(ldid);
              task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, rank);
            }
            if (task !=0) break;
          }
          if (task ==0)
          {
            ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, rand_r(&device->ctxt->seed) % kaapi_localitydomain_count(KAAPI_LD_GPU) );
            task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device, 3);
          }
        }
#endif
        }
      }
#endif // KAAPI_WS_GPUTASK
    }
    if (task ==0)
    {
      /* may be request */
      switch (device->request.op)
      {
        case KAAPI_DEVICEOP_NOP:
        {
          //printf("DEVICEOP_NOP\n");
          kaapi_slowdown_cpu();
        } break;

        case KAAPI_DEVICEOP_WRITEBACK:
        {
do_writeback:
          /* writeback policy: if counter ==0, asynchronous call without any mean to view completion */
          if (device->request.counter ==0)
          {
            kaapi_memory_writeback_all( &kaapi_the_dsm, device->memdev.asid, 0, 0, 0, 0 );
            send_msg = 0;
            kaapi_offload_requestreply( device, 0 );
          }
          else
          { /* asynchronous call, but reply to client when local completion */
            send_msg = kaapi_memory_writeback_all( &kaapi_the_dsm, device->memdev.asid,
                callback_replyrequest_memsync, device, 0, 0 );
            if (send_msg >0)
              KAAPI_ATOMIC_ADD64(device->request.counter, send_msg);
            /* make progress of requests */
#if KAAPI_USE_STREAM_D2D
            /* test completion of input back data */
            err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
            if ((err != 0) && (err != EINPROGRESS)) goto out_device_writeback;
            err = kaapi_offload_test_stream( &device->stream, KAAPI_IO_STREAM_D2D);
            if (err) goto out_device_writeback;
#endif
            /* test completion of input back data */
            err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
            if ((err != 0) && (err != EINPROGRESS)) goto out_device_writeback;
            err = kaapi_offload_test_stream( &device->stream, KAAPI_IO_STREAM_D2H);
            if (err) goto out_device_writeback;
            
            /* reply if decr contribution to this device */
            if (KAAPI_ATOMIC_SUB64(device->request.counter, (1ULL<<32ULL)) ==0)
              kaapi_offload_requestreply( device, 0 );
            
            device->request.op = KAAPI_DEVICEOP_WRITEBACK_WAIT;
          }
          break;

out_device_writeback:
          kaapi_offload_requestreply( device, err );
          kaapi_assert(err== 0);
        } break;

        case KAAPI_DEVICEOP_WRITEBACK_WAIT:
        {
          LOGDEBUG(printf("DEVICEOP_WRITEBACK_WAIT\n"));

#if KAAPI_USE_STREAM_D2D
          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2D);
          if (err) goto out_device_writeback;
#endif
          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2H);
          if (err) goto out_device_writeback;

          //if ((send_msg >0) &&
          if (device->request.counter && (KAAPI_ATOMIC_READ(device->request.counter) ==0))
          {
            send_msg = 0;
            kaapi_offload_requestreply( device, 0 );
          }
        } break;

        case KAAPI_DEVICEOP_MEMSYNC:
        {
          //printf("DEVICEOP_MEMSYNC, device: %p\n",device);

          /* synchronize all streams : order is important*/
          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_H2D );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_H2D);
          if (err) goto out_device_memsync;

          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_KERN );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_KERN);
          if (err) goto out_device_memsync;

#if KAAPI_USE_STREAM_D2D
          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2D);
          if (err) goto out_device_memsync;
#endif

          err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
          if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
          err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2H);
          if (err) goto out_device_memsync;

          /* initiate write back only if streams are empty and there are no more tasks to execute */
          if (task ==0)
            task = kaapi_fifo_queue_pop(device->ld->queue);
          if (task !=0) goto prepare_execute;

#if 0
          /* wait more task */
          if (device->exec_count < device->spawn_count + device->ld->queue->push_count)
            break;
#endif

          /* non empty stream */
          if (kaapi_offload_stream_size( &device->stream, KAAPI_IO_STREAM_ALL ) != 0)
            break;

          /* do writeback: callback call will signal client if communications are finished */
          device->request.op = KAAPI_DEVICEOP_WRITEBACK;
          goto do_writeback;

out_device_memsync:
          kaapi_offload_requestreply( device, err );
          kaapi_assert(err== 0);
        } break;

        case KAAPI_DEVICEOP_INVALIDATE_CACHES:
        {
          LOGDEBUG(printf("DEVICEOP_INVALIDATE_CACHES\n"));
          err = kaapi_offload_poll_device( device );
          if (err != 0) goto out_ic;
          kaapi_memory_invalidate_cache( device->memdev.asid );
          device->exec_count = device->spawn_count = device->ld->queue->push_count = device->ld->queue->pop_count = 0;
out_ic:
          kaapi_offload_requestreply( device, err );
          kaapi_assert(err== 0);
        } break;

        case KAAPI_DEVICEOP_REPLY:
          break;
        default:
          abort();
          break;
      }
    }
    else
    {
prepare_execute:
//printf("Tidle time: %f\n", 1e-9*(double)(kaapi_get_elapsedns() - tidle_start));
      tidle_start = 0;
      LOGDEBUG(
        printf("%i:: device pop task:%p %s prio:%i runing queue:%p\n",
          (int)ctxt->kid, (void*)task,
          kaapi_task_get_priority(task),
          ctxt->queue);
      )
      
#if 0
      err = kaapi_offload_stream_process_instruction(&device->stream, KAAPI_IO_STREAM_D2D);
      kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
      err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
      kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
      err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_H2D );
      kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
#endif

      while (!kaapi_offload_device_accept_new_task(device))
      {
#if KAAPI_USE_STREAM_D2D
         err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
         if ((err != 0) && (err != EINPROGRESS)) goto out_device_memsync;
         err = kaapi_offload_wait_stream( &device->stream, KAAPI_IO_STREAM_D2D);
         if (err) goto out_device_memsync;
#endif
        kaapi_offload_poll_device( device );
      }

      kaapi_offload_device_prepare_execute_task(device, task );
    }
    kaapi_offload_poll_device( device );
  } while (f_fini && !f_fini(arg));

r_exit:

  return EINTR;
}



/*
*/
int _kaapi_device_finalize(  void* arg )
{
  kaapi_device_t* device = (kaapi_device_t*)arg;

  if (device->state == KAAPI_DEVICE_STATE_STOP)
    return true;
  return device->finalize != false; 
}

/* Main entry thread created per device
*/
void* kaapi_offload_device_thread( void* arg )
{
  kaapi_device_t* device = (kaapi_device_t*)arg;
  kaapi_assert(device->driver->f_device_attach(device) ==0);

  /* basic initialisation */
  kaapi_offload_device_init(device);
  kaapi_offload_device_commit(device);
  kaapi_assert( (device->state == KAAPI_DEVICE_STATE_COMMIT)||(device->state == KAAPI_DEVICE_STATE_DOSTART));

  kaapi_thread_t* thread = kaapi_thread_bind(device->driver->f_get_type(),0);
  if (thread ==0) return 0;
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  device->ctxt = ctxt;
  ctxt->device = device;
  ctxt->ld = device->ld;
  _kaapi_self_context = ctxt;

  /* infinite loop with the device context */
  kaapi_offload_device_push( device );

  /* */
#if KAAPI_SLEEP_DEVICETHREAD
  kaapi_fifo_register_waiter( device->ld->queue, (void (*)(void *))kaapi_offload_device_wakeup, device );
#else
  kaapi_fifo_register_waiter( device->ld->queue, 0, 0);
#endif

  /* wait start */
  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  while (device->state != KAAPI_DEVICE_STATE_DOSTART)
     kaapi_assert(0 == pthread_cond_wait(&device->cond_sleep, &device->lock));
  device->state = KAAPI_DEVICE_STATE_START;
  kaapi_assert(0 == pthread_cond_signal(&device->cond_sleep));
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));
  

  /* infinite loop */
  int err = kaapi_sched_idle_offload(thread, _kaapi_device_finalize, device);
  kaapi_assert((err==0)||(err==EINTR));

  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  do {
    if (device->state == KAAPI_DEVICE_STATE_STOP)
    {
      device->state = KAAPI_DEVICE_STATE_STOPPED;
      kaapi_assert(0 == pthread_cond_signal(&device->cond_sleep));
      break;
    }
    kaapi_assert(0 == pthread_cond_wait(&device->cond_sleep, &device->lock));
  } while (device->state != KAAPI_DEVICE_STATE_DESTROY);  

  kaapi_localitydomain_destroy(device->ld);

  kaapi_offload_device_pop( device );
  _kaapi_offload_device_finalize(device);
  device->state = KAAPI_DEVICE_STATE_FINALIZED;

#if KAAPI_DEBUG
  if (err != EINTR)
  {
    printf("%s: device %d/%p abort with natural interrup\n", __FUNCTION__, device->device_id, (void*)device);
    abort();
  }
#endif
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));
  kaapi_thread_unbind(thread);
  _kaapi_self_context = 0;
  device->state = KAAPI_DEVICE_STATE_DESTROYED;
  return 0;
}


/*
*/
int kaapi_offload_device_init(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  KAAPI_DEBUG_INST( KAAPI_ATOMIC_WRITE(&count_valid,0);
                    KAAPI_ATOMIC_WRITE(&call_valid,0);)
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: device %d/%p under initialization\n", __FUNCTION__, device->device_id, (void*) device );
  fflush(stdout);
#endif
  int err = 0;

  kaapi_driver_t* driver = device->driver;
  err = driver->f_device_init(device);
  if (err != 0)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: device '%s' failed during initialization\n", __FUNCTION__, device->name ==0 ? "<no name>": device->name );
    fflush(stdout);
#endif
    goto return_value;
  }

#if KAAPI_PIPELINE_GPUTASK
  /* */
  kaapi_assert(0== pthread_mutex_init(&device->pipe_lock, 0));

  device->p_write   = 0; /* next position where to write new task */
  device->p_ready   = 0; /* position of the next task to insert into the kernel submission stream */
  device->p_finish  = 0; /* position of the next task to erase from the pipeline */
  device->pipe_size = kaapi_default_param.cuda_conc_kernel; 
  device->pipeline  = (kaapi_task_t**)malloc(sizeof(kaapi_task_t*)*device->pipe_size);
  for (int i=0; i<device->pipe_size; ++i) 
    device->pipeline[i] = 0;
#endif

  /* */
  device->time_tasks = 0.0;
  device->flops_tasks = 0.0;
  device->data_tasks = 0.0;
  device->pendingtasks = 0;
  device->flops_pendingtasks= 0.0;
  device->data_pendingtasks = 0.0;

#if KAAPI_USE_PERFCOUNTER
  device->cnt_task     = 0.0;
  device->sum_cpudelay = 0.0;
  device->max_cpudelay = 0.0;
  device->min_cpudelay = FLT_MAX;
#if KAAPI_LOG_DELAY
  char filename[128];
  sprintf(filename,"log_delay.%i",device->device_id);
  device->flog_delay = fopen(filename,"w");
#endif
#endif
  
  /* */
  KAAPI_ATOMIC_WRITE(&device->cnt_pending, 0);
  KAAPI_ATOMIC_WRITE(&device->cnt_ready, 0);
  KAAPI_ATOMIC_WRITE(&device->cnt_exec, 0);

  /* */
  kaapi_offload_stream_init(device, &device->stream, KAAPI_STREAM_CAPACITY);

#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: device '%s' successfully initialized\n", __FUNCTION__, device->name ==0 ? "<no name>": device->name );
  fflush(stdout);
#endif
  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  if (device->state == KAAPI_DEVICE_STATE_CREATE)
    device->state = KAAPI_DEVICE_STATE_INIT;
  kaapi_assert(0 == pthread_cond_signal(&device->cond_sleep));
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));

return_value:
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
*/
int kaapi_offload_device_commit(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: device %d/%p under commit\n", __FUNCTION__, device->device_id, (void*) device );
  fflush(stdout);
#endif
  int err = 0;

  kaapi_driver_t* driver = device->driver;
  err = driver->f_device_commit(device);
  if (err != 0)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: device '%s' failed during commit\n", __FUNCTION__, device->name ==0 ? "<no name>": device->name );
    fflush(stdout);
#endif
  }
  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  if (device->state == KAAPI_DEVICE_STATE_INIT)
    device->state = KAAPI_DEVICE_STATE_COMMIT;
  kaapi_assert(0 == pthread_cond_signal(&device->cond_sleep));
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));

return_value:
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
*/
const char* kaapi_offload_device_info(kaapi_device_t* const device)
{
  if (device->state ==KAAPI_DEVICE_STATE_CREATE)
    return "<device not initialized>";
  return device->driver->f_device_info( device );
}

/*
*/
int kaapi_offload_device_start(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: device %d/%p under initialization\n", __FUNCTION__, device->device_id, (void*) device );
  fflush(stdout);
#endif
  int err = 0;

  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  while (device->state != KAAPI_DEVICE_STATE_COMMIT)
    kaapi_assert(0 == pthread_cond_wait(&device->cond_sleep, &device->lock));
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));

  kaapi_driver_t* driver = device->driver;
  err = driver->f_device_start(device);
  if (err != 0)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: device '%s' failed during start\n", __FUNCTION__, device->name ==0 ? "<no name>": device->name );
    fflush(stdout);
#endif
    goto return_value;
  }

#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: device '%s' successfully initialized\n", __FUNCTION__, device->name ==0 ? "<no name>": device->name );
  fflush(stdout);
#endif

return_value:
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
 */
void kaapi_offload_device_stop(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  KAAPI_OFFLOAD_TRACE_MSG("IN %s: current_device:%p,%i to finalize\n", __FUNCTION__,
                (void*)device, (device==0 ? -1 : device->device_id)
  );
  if (device->state == KAAPI_DEVICE_STATE_START)
  {
    device->driver->f_device_stop(device);
    kaapi_assert(device->state >= KAAPI_DEVICE_STATE_STOPPED);
  }
#if KAAPI_LOG_DELAY
  fclose(device->flog_delay);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
}


/*
*/
void kaapi_offload_device_free_memory(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_memory_freelist_destroy( &device->memdev );
  KAAPI_OFFLOAD_TRACE_OUT
}


/* Called by the owner thread that manages the device
*/
static void _kaapi_offload_device_finalize(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  KAAPI_OFFLOAD_TRACE_MSG("IN %s: current_device:%p,%i to finalize\n", __FUNCTION__,
                (void*)device, (device==0 ? -1 : device->device_id)
  );

  kaapi_assert(device->state == KAAPI_DEVICE_STATE_STOPPED);

  kaapi_dsm_unregister_device(&kaapi_the_dsm, &device->memdev);
  kaapi_localitydomain_deattach( KAAPI_LD_GPU, device->ld );
  device->driver->f_device_finalize(device);
  kaapi_assert(device->state == KAAPI_DEVICE_STATE_FINALIZED);


  KAAPI_OFFLOAD_TRACE_OUT
}

/* Finalize is called when the thread is stopped (see kaapi_offload_device_thread).
   Here call destroy that wait the thread and destroy the object
*/
void kaapi_offload_device_finalize(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  device->driver->f_device_destroy(device);
  KAAPI_OFFLOAD_TRACE_OUT
}



void kaapi_offload_device_sleep(kaapi_device_t* const device)
{
  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  device->issleeping = 1;
  while (device->issleeping == 1)
    kaapi_assert(0 == pthread_cond_wait(&device->cond_sleep, &device->lock));
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));
}


void kaapi_offload_device_wakeup(kaapi_device_t* const device)
{
#if KAAPI_SLEEP_DEVICETHREAD
  kaapi_assert(0 == pthread_mutex_lock(&device->lock));
  kaapi_offload_device_wakeup_(device);
  kaapi_assert(0 == pthread_mutex_unlock(&device->lock));
#endif
}


/* Push== push the device context as the default context
*/
kaapi_device_t* kaapi_offload_device_push(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_t* save_device;
  save_device = kaapi_offload_self_device();

  KAAPI_OFFLOAD_TRACE_MSG("IN %s: current_device:%p,%i -> device to attach:%p, %i\n", __FUNCTION__,
                (void*)save_device, (save_device==0 ? -1 : save_device->device_id),
                (void*)device, (device==0 ? -1 : device->device_id)
  );

  if (KAAPI_ATOMIC_INCR(&device->cnt_push) >1)
  {
    kaapi_assert_debug( device == save_device );
    goto out;
  }

  kaapi_offload_set_current_device(device);

  /* plugin specific */
  kaapi_assert(device->driver->f_device_attach(device) ==0);

out:
  KAAPI_OFFLOAD_TRACE_OUT
  return save_device;
}


/* device is the device returned by push
*/
void kaapi_offload_device_pop(kaapi_device_t* const device)
{
  kaapi_device_t* curr_device = kaapi_offload_self_device();

  KAAPI_OFFLOAD_TRACE_MSG("IN %s: current_device:%p,%i -> device to attach:%p, %i\n", __FUNCTION__,
                (void*)curr_device, (curr_device==0 ? -1 : curr_device->device_id),
                (void*)device, (device==0 ? -1 : device->device_id)
  );

  if (KAAPI_ATOMIC_DECR(&curr_device->cnt_push) !=0)
  {
    kaapi_assert_debug( device == curr_device );
    goto out;
  }

  curr_device->driver->f_device_detach(curr_device);
  kaapi_offload_set_current_device( device );

out:
  KAAPI_OFFLOAD_TRACE_OUT;
}

/*
*/
size_t kaapi_offload_get_mem_info(
  kaapi_device_t* device,
  size_t* mem_total, size_t* mem_limit
)
{
  KAAPI_OFFLOAD_TRACE_IN
  size_t retval = (size_t)-1UL;
  if (mem_total) *mem_total = (size_t)-1UL;
  if (mem_limit) *mem_limit = (size_t)-1UL;
  // kaapi_assert(device->state >= KAAPI_DEVICE_STATE_COMMIT);
  {
    if (mem_total) *mem_total = device->mem_total;
    if (mem_limit) *mem_limit = device->mem_limit;
  }
  KAAPI_OFFLOAD_TRACE_OUT
  return retval;
}

/* Memory synchronisation is a two steps process:
   a- insert message to get back non-coherent data from GPU to the host
   b- wait all in-transit messages
   To synchronize all devices, then first to all steps a/ on each device then
   step b/ for all devices.
*/


/*
*/
int kaapi_offload_synchronize_device(kaapi_device_t* device)
{
  int err = 0;
  if (device->memdev.asid != kaapi_local_asid)
  {
    err = kaapi_offload_request2device( device, KAAPI_DEVICEOP_MEMSYNC );
    if (err !=0) return err;
    err = kaapi_offload_requestwait( device );
    if (err !=0) return err;
  }
  return err;
}

/*
*/
int kaapi_offload_synchronize(void)
{
//printf("BEGIN Memory synchronize\n");
  kaapi_atomic64_t sync_counter = {0};

  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
    {
      /* preincrement per device the counter in order to ensure that callback will not prematurely 
         signal the client 
      */
//printf("[XKAAPI: add & KAAPI_DEVICEOP_MEMSYNC request, device:%p\n", device);
      KAAPI_ATOMIC_ADD64(&sync_counter, (1ULL<<32ULL));
      device->request.counter = &sync_counter;
#if 0
printf("Send memsync device:%i counter: %lu\n", kaapi_memory_asid_get_lid(device->memdev.asid), KAAPI_ATOMIC_READ(&sync_counter));
#endif
      err = kaapi_offload_request2device( device, KAAPI_DEVICEOP_MEMSYNC );
      if (err !=0) return err;
    }
  }
  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
    {
//printf("[XKAAPI: wait for MEMSYNC request, device: %p\n", device);
      err = kaapi_offload_requestwait( device );
    }
    if (err !=0) return err;
  }
//printf("END Memory synchronize\n");

  return 0;
}


/*
 */
int kaapi_offload_invalidate_caches(void)
{
  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);

    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
      err = kaapi_offload_request2device( device, KAAPI_DEVICEOP_INVALIDATE_CACHES );
    if (err !=0) return err;
  }

  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);

    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
      err = kaapi_offload_requestwait( device );
    if (err !=0) return err;
  }

  return 0;
}


/*
*/
extern void* kaapi_get_cublas_handle(void);
void* kaapi_get_cublas_handle(void)
{
  kaapi_device_t* device = kaapi_offload_self_device();
  return device->driver->f_get_cublas_handle( device );
}
