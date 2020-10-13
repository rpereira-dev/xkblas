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

#include <stdio.h>
#include "kaapi_impl.h"
#include "kaapi_offload.h"
#include "kaapi_memory.h"


#define LOGDEBUG(x)


#if KAAPI_SLEEP_DEVICETHREAD //quick prototype but buggy
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

  KAAPI_ATOMIC_INCR(&device->cnt_exec);
  KAAPI_ATOMIC_DECR(&device->cnt_ready);
  
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

#if KAAPI_USE_PERFCOUNTER
  ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_TASK_EXEC];
  kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_CNT_TASK_WORK]     += status.gpu_delay;
  kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_CNT_TASK_WORK_CPU] += status.cpu_delay;
  if (kaapi_taskflag_get(task,KAAPI_TASK_PERFCNT))
  {
    kaapi_task_withperfcnt_t* stask = (kaapi_task_withperfcnt_t*)task;
    const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
    kaapi_offloadtask_perfcounter_t* perf = &device->perfcnt.task[fmt->fmtid];
    double flops = 0, data = 0;
    kaapi_format_get_cost(fmt, kaapi_task_getargs(task), task, &flops, &data );
    perf->time  += status.gpu_delay;
    perf->flops += flops;
    perf->ai += flops/data;
    kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_FLOPS_TASK_EXEC] += flops;
    kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_FLOPS_TASK_PENDING] -= flops;
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
    kaapi_perthread_stat[device->ctxt->tid].counter[KAAPI_CNT_REORDER_MISS_LEN] += (index-device->p_ready);
    if (index > device->p_ready)
       ++kaapi_perthread_stat[device->ctxt->tid].counter[KAAPI_CNT_REORDER_MISS];
#  endif
#endif
  }
  
#if KAAPI_PIPELINE_GPUTASK
# if KAAPI_REORDER_TASK_EXEC
  if (wc ==0)
    kaapi_stream_insert_io_task_inst(
      &device->stream,
      KAAPI_IO_STREAM_KERN,
      task,
      callback_epilogue,
      (void*)device, (void*)task, (void*) (uintptr_t)index
    );
#  if KAAPI_USE_PERFCOUNTER
  if (device->p_ready != index)
     ++kaapi_perthread_stat[device->ctxt->tid].counter[KAAPI_CNT_REORDER_HIT];
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
  
#if KAAPI_USE_PERFCOUNTER
  ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_TASK_ASYNC_EXEC];
  if (kaapi_taskflag_get(task,KAAPI_TASK_PERFCNT))
  {
    const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
    kaapi_task_withperfcnt_t* stask = (kaapi_task_withperfcnt_t*)task;
    double flops = 0, data = 0;
    kaapi_format_get_cost(fmt, kaapi_task_getargs(task), task, &flops, &data );
    kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_FLOPS_TASK_PENDING] += flops;
  }
#endif
  
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
            1,
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
  ++device->perfcnt.task[fmt->fmtid].spawn;

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
  kaapi_access_t* access;
  kaapi_metadata_info_t* mdi;
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
        1, /* force creation */
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
       ++kaapi_perthread_stat[device->ctxt->tid].counter[KAAPI_CNT_REORDER_HIT];
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

  /* */
  kaapi_offload_set_current_device(device);

  do
  {
#if KAAPI_SLEEP_DEVICETHREAD
    while ((device->request.op == KAAPI_DEVICEOP_NOP)
        && kaapi_queue_empty(device->ctxt->queue)
        && (device->exec_count == device->spawn_count + device->ld->queue->push_count)
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
    if ((task ==0) && kaapi_offload_device_accept_new_task(device))
    {
      /* pop on local queue */
      task = kaapi_queue_pop(device->ctxt, device->ctxt->queue, 0);
      /* else pop on device specific queue (~ mailbox) */
      if (task ==0)
      {
#if 0  // origin
        task = kaapi_fifo_queue_pop(device->ld->queue);
#else
        task = kaapi_fifo_queue_steal_with_affinity(device->ld->queue, device);
        if (task ==0) task = kaapi_fifo_queue_pop(device->ld->queue);
        //if (task) printf("(1)pop from:%p, device->ld:%p\n", device->ld->queue, device->ld);
#endif
#if 1//STEAL
{
//13/10: this is the best for syrk. But deadlock at the end: thread is waiting for some thing...
        if (task ==0)
        {
         /* Affinity: compute the best (=ldid with at least a write of the task, see IPDPS2013.
            It remains to transfer the affinity during the steal operations where device
            may has the capacity to select the best task or at least:
  	       0- a task with most of its input on the device.
  	       1- a task with inputs on the device close to the target device.
  	       2- a task with inputs on the device. 
  	       3- a task with inputs on the machine. 
          */
          kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(KAAPI_LD_NUMA, 0);
          //task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device);
#define LOG_AFF 0
#if 0//LOG_AFF
          if (task) printf("%p: (3) ld:%i steal task %p from ld: %i\n", pthread_self(), device->ld->ldid, task, ld->ldid);
#endif
#if 1
          if (task ==0) 
          {
            kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, rand_r(&device->ctxt->seed) % kaapi_localitydomain_count(KAAPI_LD_GPU) );
            task = kaapi_fifo_queue_steal_with_affinity(ld->queue, device);
            //task = kaapi_fifo_queue_pop(ld->queue);
            //task = kaapi_fifo_queue_pop(ld->queue);
            //if (task != 0) printf("Steal task !\n");
          }
#endif
        }
}
#endif // STEAL
      }
      else
      {
        /* spawn_count counts the number of task locally created, not from the mailbox */
        ++device->spawn_count;
      }
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
          task = kaapi_queue_pop(device->ctxt, device->ctxt->queue, 0);
          if (task ==0)
            task = kaapi_fifo_queue_pop(device->ld->queue);
          else
            ++device->spawn_count;
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
  return device->finalize != false; 
}

/* Main entry thread created per device
*/
void* kaapi_offload_device_thread( void* arg )
{
  kaapi_device_t* device = (kaapi_device_t*)arg;

  kaapi_thread_t* thread = kaapi_thread_bind(device->driver->f_get_type(),0);
  if (thread ==0) return 0;
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  device->ctxt = ctxt;
  ctxt->device = device;
  ctxt->ld = device->ld;

  /* infinite loop with the device context */
  kaapi_offload_device_push( device );

  /* */
#if KAAPI_SLEEP_DEVICETHREAD
  kaapi_fifo_register_waiter( device->ld->queue, kaapi_offload_device_wakeup, device );
#else
  kaapi_fifo_register_waiter( device->ld->queue, 0, 0);
#endif
  int err = kaapi_sched_idle_offload(thread, _kaapi_device_finalize, device);
  kaapi_assert((err==0)||(err==EINTR));
  kaapi_offload_device_pop( device );
#if KAAPI_DEBUG
  if (err != EINTR)
  {
    printf("%s: device %d/%p abort with natural interrup\n", __FUNCTION__, device->device_id, (void*)device);
    abort();
  }
#endif
  kaapi_thread_unbind(thread);
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
  if (device->is_initialized) 
    goto return_value;

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
  device->is_initialized = true;

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
  KAAPI_ATOMIC_WRITE(&device->cnt_pending, 0);
  KAAPI_ATOMIC_WRITE(&device->cnt_ready, 0);
  KAAPI_ATOMIC_WRITE(&device->cnt_exec, 0);

  /* */
  kaapi_offload_device_push( device );
  kaapi_offload_stream_init(device, &device->stream, KAAPI_STREAM_CAPACITY);
  kaapi_offload_device_pop( device );

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

return_value:
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
*/
const char* kaapi_offload_device_info(kaapi_device_t* const device)
{
  if (device->is_initialized ==0)
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
  if (device->is_initialized ==0)
  {
    err = EINVAL;
    goto return_value;
  }

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
  if (device->is_initialized)
  {
    device->driver->f_device_stop(device);
  }
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


/*
*/
void kaapi_offload_device_finalize(kaapi_device_t* const device)
{
  KAAPI_OFFLOAD_TRACE_IN
  KAAPI_OFFLOAD_TRACE_MSG("IN %s: current_device:%p,%i to finalize\n", __FUNCTION__,
                (void*)device, (device==0 ? -1 : device->device_id)
  );
  if (device->is_initialized)
  {
    kaapi_device_t* save_device __attribute__((unused)) = kaapi_offload_device_push(device);
    kaapi_offload_stream_destroy(&device->stream);
#if KAAPI_PIPELINE_GPUTASK
    kaapi_assert(0== pthread_mutex_destroy(&device->pipe_lock));
#endif
    device->driver->f_device_finalize(device);
    device->is_initialized = false;
    device->driver->f_device_destroy(device);
    kaapi_offload_set_current_device( save_device );
  }
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
  if (device->is_initialized) 
  {
    kaapi_device_t* save_device __attribute__((unused))= kaapi_offload_device_push( device );
    retval = device->memdev.f_get_mem_info( &device->memdev, mem_total, mem_limit );
    kaapi_offload_device_pop( device );
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
  return device->handle;
}
