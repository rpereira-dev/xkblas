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
//#define LOGDEBUG(x) x

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


/*
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


/*
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
  kaapi_device_t* device     = (kaapi_device_t*)arg0;
  kaapi_context_t* ctxt      = device->ctxt;
  kaapi_task_t* task         = (kaapi_task_t*)arg1;
  kaapi_frame_t* frame       = (kaapi_frame_t*)arg2;

  --device->cnt_ready;
  ++device->cnt_exec;
//printf("callback_epilogue:: task: %p, device: %p\n", task, device);

  /* activate successors : reverse the natural order of ready successor
     - it could be best to keep this order by 1/ pushing into local list
     2/ then appening the lists togther
  */
  int cnt __attribute__((unused));
  cnt = kaapi_sched_activate_successors(
    kaapi_context2thread(device->ctxt), 
    frame, 
    task,
    callback_epilogue_perparam, (uint64_t)device
  );

  /* menage à faire */
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
  if (frame)
    KAAPI_ATOMIC_INCR(&frame->exec_count);
#if KAAPI_DEBUG
  task->flags |= KAAPI_TASK_FLAG_EXEC;
#endif
  KAAPI_OFFLOAD_TRACE_OUT
}


/* Call when data have been received
   When all data are valid for this task, insert the task into the stream
*/
static void callback_set_valid(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_t* device     = (kaapi_device_t*)arg0;
  kaapi_task_t* task         = (kaapi_task_t*)arg1;
  kaapi_frame_t* frame       = (kaapi_frame_t*)arg2;

  if (task !=0)
  {
    int wc = KAAPI_ATOMIC_DECR(&task->wc);
    if (wc ==0)
    {
      --device->cnt_pending;
      ++device->cnt_ready;
      /* launch task : be carrefull ios->stream may differs from &device->stream
         due to forward of callback 
      */
      kaapi_assert_debug( device->ld->ldid == kaapi_task_get_ld(task) );
      kaapi_stream_insert_io_task_inst(
        &device->stream,
        KAAPI_IO_STREAM_KERN,
        task, frame,
        callback_epilogue,
        device, task, frame
      );
#if 0// else possible deadlock on locking replicas
      kaapi_offload_stream_process_instruction( ios->stream, KAAPI_IO_STREAM_KERN );
#endif
    }
  }
  KAAPI_OFFLOAD_TRACE_OUT
}


/* Call the device' task entry point
   - task cannot create new tasks on the device
   - the context must has been attached to the device before calling the
   function
*/
int kaapi_offload_device_execute_task(
     kaapi_device_t* const device,
     kaapi_frame_t* frame, /* to signal */
     kaapi_task_t* task,    /* task was pushed in the context of 'frame' */
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
  ctxt->pc = task;
  ((kaapi_task_bodyfnc_gpu_t)fmt->entrypoint[device->driver->f_get_type()])(
      task, kaapi_context2thread(ctxt), handle
  );

#if KAAPI_HAVE_IO_THREADS==0
#if KAAPI_USE_STREAM_D2D
  kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
#endif
  if (kaapi_taskflag_get(task, KAAPI_TASK_FLAG_INCOM))
    kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
  if (kaapi_taskflag_get(task, KAAPI_TASK_FLAG_OUTCOM))
    kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_H2D );
#endif
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
  //printf("%f: prefetch callback/%p\n", kaapi_get_elapsedtime(), arg0 ); fflush(stdout);
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
      mdi = kaapi_dsm_findaccess_on_node(
          &kaapi_the_dsm,
          device->memdev.asid,
          1,
          access,
          &view
      );
      /* seems to be considered by dsm_prefetch also, isn't it ? */
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
     kaapi_frame_t* frame,            /* to signal */
     kaapi_task_t* task               /* task was pushed in the context of 'frame' */
 )
{
  KAAPI_OFFLOAD_TRACE_IN
  int err;
  uint16_t lid = kaapi_memory_asid_get_lid( device->memdev.asid );
  const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
  ++device->perfcnt.task[fmt->fmtid].spawn;

  kaapi_assert_debug(frame !=0);

  kaapi_assert_debug(device == kaapi_offload_get_current_device());
  kaapi_assert_debug(KAAPI_ATOMIC_READ(&task->wc) ==0);

  //printf("%li: %s prepare task: %p  %s\n", kaapi_get_elapsedns(), __func__, task, fmt->name );
#if KAAPI_USE_PREFETCH
  int prefetch_taskcnt = 0;
  kaapi_task_t* prefetch_tasklist[KAAPI_MAX_PREFETCH_WINDOW];
#endif

  /* take 'pseudo' lock to avoid activation of the task if a data
     becomes available on the device quickly. See above.
  */
  KAAPI_ATOMIC_INCR(&task->wc);
  ++device->cnt_pending;
  if ((fmt !=0) && ((task->flags & KAAPI_TASK_FLAG_INDEPENDENT) ==0))
  {
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
#if 0
    extern char kaapi_getmodename( kaapi_access_mode_t m ) ;
    all_modes[2*ith] = kaapi_getmodename( mp );
    if (ith == count_params-1) all_modes[2*ith+1] = 0;
    else all_modes[2*ith+1] = ',';
#endif

      if (mp & KAAPI_ACCESS_MODE_V)
        continue;

      void* new_data;

      /* do bind ptr to the device->asid */
      access = kaapi_format_get_access_param(fmt, (unsigned int)ith, kaapi_task_getargs(task));
      kaapi_format_get_view_param(fmt, (unsigned int)ith, kaapi_task_getargs(task), &view);

#if 0
printf("[%p]:: Task: %p %s param[%i] @:%p view[%lu, %lu, ld:%lu]\n",
    (void*)pthread_self(),
    task, fmt->name, ith, access->data, 
    view.size[0], view.size[1], view.ld );
#endif
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
        /* findaccess has already allocated the replica for asid with the right view
        */
        err = kaapi_dsm_acquire_data( &kaapi_the_dsm, device->memdev.asid,
            task,
            mp,
            mdi,
            callback_set_valid,
            (void*)device, (void*)task, (void*)frame
        );
//printf("acquire_data:: task: %p, device: %p\n", task, device);
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
//printf("Next in on same device\n");
            prefetch_tasklist[prefetch_taskcnt++] = an->task;
}
          if (an->sync) an = (kaapi_access_t*)an->sync->next;
          else an = an->next;
        }
#if 0 //NEXTNEXT
        if ((an !=0) && KAAPI_ACCESS_IS_READ(an->mode))
        {
          kaapi_ldid_t ldid = kaapi_task_get_ld(an->task);
          if ((ldid == device->ld->ldid) && (prefetch_taskcnt < KAAPI_MAX_PREFETCH_WINDOW)) 
{
//printf("Next of next in on same device\n");
            prefetch_tasklist[prefetch_taskcnt++] = an->task;
}
        }
#endif
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
#if 0
printf("[%p]:: Task: %p %s, #params=%i, modes=%s, #wc=%i\n",
    (void*)pthread_self(),
    task, fmt->name, count_params, all_modes, KAAPI_ATOMIC_READ(&task->wc) );
#endif
  }

#if KAAPI_USE_PREFETCH
  /* prefetch data before launching kernel */
  if (prefetch_taskcnt>0)
  {
    kaapi_do_prefetch_data( device, prefetch_taskcnt, prefetch_tasklist );
  }
#endif

#if KAAPI_USE_STREAM_D2D
  kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2D );
#endif
  kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_H2D );


  /* if no remote data required to be transfer or all transfers are finished, 
     then insert task for execution in the device stream.
  */
  if (KAAPI_ATOMIC_DECR(&task->wc)==0)
  {
    /* could be blocking call if windows is fill */
    --device->cnt_pending;
    ++device->cnt_ready;
    kaapi_assert_debug( device->ld->ldid == kaapi_task_get_ld(task) );
    kaapi_stream_insert_io_task_inst(
      &device->stream,
      KAAPI_IO_STREAM_KERN,
      task, frame,
      callback_epilogue,
      device, task, frame
    );
    kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_KERN );
  }

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

/* post until request processed */
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
#if 0
printf("Callback memsync device:%i counter: %lu\n", kaapi_memory_asid_get_lid(device->memdev.asid), KAAPI_ATOMIC_READ(device->request.counter));
#endif
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
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_device_t* device = ctxt->device;
  kaapi_frame_t* frame = 0;
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
    frame = 0;
    /* pop on local queue */
    task = kaapi_queue_pop(device->ctxt, device->ctxt->queue, 0);
    /* else pop on device specific queue (~ mailbox) */
    if (task ==0)
    {
      task = kaapi_fifo_queue_pop(device->ld->queue, &frame);
      kaapi_assert_debug((task ==0)||(frame != 0));
    }
    else
    {
      frame = task->frame;
      /* spawn_count counts the number of task locally created, not from the mailbox */
      ++device->spawn_count;
    }

    if (task ==0)
    {
      int err;
      /* wait no more local task */
      while ((device->request.op == KAAPI_DEVICEOP_NOP) 
          && (device->exec_count < device->spawn_count + device->ld->queue->push_count))
      {
        frame = device->ctxt->unlink;
        task = kaapi_queue_pop(device->ctxt, device->ctxt->queue, 0);
        if (task ==0)
        {
          task = kaapi_fifo_queue_pop(device->ld->queue, &frame);
          kaapi_assert_debug((task ==0)||(frame != 0));
        }
        else
        {
          frame = task->frame;
          ++device->spawn_count;
        }
        if (task !=0) goto prepare_execute;
        kaapi_offload_poll_device( device );
      }

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
          LOGDEBUG(printf("DEVICEOP_MEMSYNC, device: %p\n",device));
#if 0
printf("Recv memsync device:%i counter: %lu\n", kaapi_memory_asid_get_lid(device->memdev.asid), KAAPI_ATOMIC_READ(device->request.counter));
#endif
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
          frame = device->ctxt->unlink;
          task = kaapi_queue_pop(device->ctxt, device->ctxt->queue, 0);
          if (task ==0)
          {
            task = kaapi_fifo_queue_pop(device->ld->queue, &frame);
            kaapi_assert_debug((task ==0)||(frame != 0));
          }
          else
            ++device->spawn_count;
          if (task !=0) goto prepare_execute;

          /* wait more task */
          if (device->exec_count < device->spawn_count + device->ld->queue->push_count)
            break;

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
        printf("%i:: device pop task:%p %s prio:%i frame:%p runing queue:%p\n",
          (int)ctxt->kid, (void*)task,
          kaapi_task_get_priority(task),
          frame, ctxt->queue);
      )

      //kaapi_assert_debug( (frame ==0) || (frame == task->frame) );
      // Currently, each task commited to stack store its allocation frame used to signal it.
      // This extra field may be deleted if when the task is stolen the frame pointer is pass to the thief.
      //frame = task->frame;
      kaapi_offload_device_prepare_execute_task(device, frame, task );

      do {
        kaapi_offload_poll_device( device );

      } while (
#if 0
              /*kaapi_offload_stream_size(&device->stream, KAAPI_IO_STREAM_KERN)*/
                kaapi_offload_stream_sizepending(&device->stream, KAAPI_IO_STREAM_KERN) >= kaapi_default_param.cuda_conc_stream_kernel*kaapi_default_param.cuda_conc_kernel
#else
               //device->cnt_pending >= kaapi_default_param.cuda_conc_stream_kernel*kaapi_default_param.cuda_conc_kernel
               device->cnt_ready >= kaapi_default_param.cuda_conc_stream_kernel*kaapi_default_param.cuda_conc_kernel
#endif
      );
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

  KAAPI_DEBUG_INST(printf("Device thread for device:%i started\n",device->device_id ));
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
  KAAPI_DEBUG_INST(printf("Device thread for device:%i exit\n",device->device_id ));
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

  /* */
  device->cnt_pending = 0;
  device->cnt_ready = 0;
  device->cnt_exec = 0;

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
  save_device = kaapi_offload_get_current_device();

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
  kaapi_device_t* curr_device = kaapi_offload_get_current_device();

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
#if 0
  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
      err = kaapi_offload_request2device( device, KAAPI_DEVICEOP_MEMSYNC );
    if (err !=0) return err;
  }

  for (unsigned int i=0; i<kaapi_offload_get_num_devices(); ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    int err = 0;
    if (device->memdev.asid != kaapi_local_asid)
      err = kaapi_offload_request2device( device, KAAPI_DEVICEOP_WRITEBACK );
    if (err !=0) return err;
  }
#endif
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
//printf("[XKAAPI: add & KAAPI_DEVICEOP_MEMSYNC request\n");
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
      err = kaapi_offload_requestwait( device );
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
  kaapi_device_t* device = kaapi_offload_get_current_device();
  return device->handle;
}
