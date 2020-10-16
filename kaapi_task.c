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

#include "kaapi_impl.h"
#include "kaapi_offload.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LOGDEBUG(x)
//#define LOGDEBUG(x) x


#define KAAPI_THE_ORIGINAL 1
//#define KAAPI_THE_RELAXED_TSO 1
#define KAAPI_THE_RELAXED_TSO_BOUND 2
//#define KAAPI_LOCK_QUEUE 1

/*
*/
int kaapi_stat_get_counter( int num, uint64_t* counter )
{
  uint64_t retval = 0;
  if ((num <0) || (num >= KAAPI_CNT_MAX))
    return EINVAL;
#if KAAPI_USE_PERFCOUNTER
  for (int i=0; i<sizeof(kaapi_perthread_stat)/sizeof(kaapi_stat_internal_t); ++i)
    retval += kaapi_perthread_stat[i].counter[num];
  *counter = retval;
#else
  *counter = 0;
#endif
  return 0;
}

int kaapi_stat_get_dcounter( int num, double* counter )
{
  double retval = 0;
  if ((num <0) || (num >= KAAPI_CNT_MAX))
    return EINVAL;
#if KAAPI_USE_PERFCOUNTER
  for (int i=0; i<KAAPI_MAX_THREAD_COUNT; ++i)
    retval += kaapi_perthread_stat[i].dcounter[num];
  if (num == KAAPI_LOAD_COLLISION_GPU)
    retval /= kaapi_default_param.ngpus;
  *counter = retval;
#else
  *counter = 0;
#endif
  return 0;
}

int kaapi_stat_getdiff_counter( int num, uint64_t* counter )
{
  uint64_t value;
  kaapi_stat_get_counter(num, &value);
  *counter = value - *counter;
  return 0;
}

/*
*/
extern int kaapi_stat_reset_counters(void)
{
#if KAAPI_USE_PERFCOUNTER
  memset( kaapi_perthread_stat, 0, sizeof(kaapi_perthread_stat));
#endif
  return 0;
}


/*
*/
unsigned int kaapi_thread_kid(kaapi_thread_t* thread)
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  return (unsigned int)ctxt->kid;
}


/* ================================= Ready List implementation ============================ */
/* List implementation is mixed form of array and double linked list. It is a list of bloc.
   Each bloc contains pushed task.
*/

/*
*/
char kaapi_getmodename( kaapi_access_mode_t m )
{
  switch (m) {
    case KAAPI_ACCESS_MODE_V:  return 'v';
    case KAAPI_ACCESS_MODE_R:  return 'r';
    case KAAPI_ACCESS_MODE_W:  return 'w';
    case KAAPI_ACCESS_MODE_CW: return 'c';
    case KAAPI_ACCESS_MODE_RW: return 'x';
    case KAAPI_ACCESS_MODE_SCRATCH: return 't';
    default: return '!';
  }
}


/*
*/
int kaapi_queue_init( kaapi_queue_t* rd, kaapi_task_t** bloc0, int32_t size )
{
  for (int i=0; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
    rd->T[i] = rd->H[i] = 0;
  rd->bitmap = 0;
  rd->next = 0;
  /* if bloc0 is null then push should allocate on demand */ 
  rd->data[0] = bloc0;
  rd->data0[0] = bloc0;
  rd->size[0] = size;
  for (int i=1; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
  {
    rd->data[i] = rd->data0[i] = 0;
    rd->size[i] = 0;
  }
  if (rd->data[0] ==0) return ENOMEM;
  return 0;
}


/*
*/
static void kaapi_queue_destroy( kaapi_queue_t* rd )
{
  for (int i=0; i<=KAAPI_TASK_MAX_PRIORITY; ++i)
  {
    rd->T[i] = rd->H[i] = 0;
    if (rd->data[i] != rd->data0[i]) free(rd->data[i]);
    rd->size[i] = 0;
    rd->data[i] = rd->data0[i] = 0;
  }
  rd->bitmap = 0;
}



/* */
void kaapi_queue_realloc(
  kaapi_lock_t* owner,
  unsigned int p,
  kaapi_queue_t* rd
)
{
  int32_t newsize = 2*rd->size[p];
  if (newsize ==0) newsize =  QUEUE_DEFAULT_SIZE;
printf("Realloc queue: size=%i\n", newsize);
  kaapi_task_t** todel = 0;
  kaapi_task_t** data;

  data = (kaapi_task_t**)malloc( newsize*sizeof(kaapi_task_t*) );
  if (rd->data[p] != rd->data0[p])
    todel = rd->data[p];
  /* recopy old array */
  memcpy( data, rd->data0[p], sizeof(kaapi_task_t*)*rd->size[p]);
#if KAAPI_DEBUG
  /* clear new part of the array [debug mode only because should never occurs ]*/
  memset(&data[rd->size[p]], 0, rd->size[p]*sizeof(kaapi_task_t*));
#endif

  kaapi_atomic_lock(owner);
  rd->size[p] = newsize;
  rd->data[p] = data;
  kaapi_atomic_unlock(owner);
  if (todel) free(todel);
}




/*
*/
int32_t _kaapi_task_commit(kaapi_thread_t* thread, kaapi_task_t* task)
{
  int wc = KAAPI_ATOMIC_DECR(&task->wc); //
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  task->frame = ctxt->unlink;
  if (kaapi_taskflag_get(task, KAAPI_TASK_FLAG_INDEPENDENT) || (wc==0))
    return kaapi_thread_push(thread, task);
  return -1;
}


#define LOG_AFF 0

#define KAAPI_USE_AFFINITY     1
#define KAAPI_USE_OCR_AFFINITY 1
/* Affinity: compute the score of executing the task on the ressource ldid
  The algorithm returns a score for 4 criteria:
  0: size of data store in the ressource ldid
  1: size of data stored in a ressource close to ldid with higher performance
  2: size of data stored in the ressources of the same type than ldid with high performance network (nvlink)
  3: size of data outside the ressources of the same type than ldid
  score must be of size 4 size_t.
*/
int kaapi_compute_affinity_score(kaapi_ldid_t ldid, kaapi_task_t* task, size_t* score, int level)
{
#if KAAPI_USE_AFFINITY
  int s = 0;
  /* look if task as affinity with one of the ressource */
  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_localitydomain_t* ld_target = kaapi_localitydomain_get(ldid);
  unsigned int ith;
  kaapi_memory_view_t view;
  kaapi_access_t* access;
  kaapi_metadata_info_t* mdi;
  const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
  unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));
  
  kaapi_assert((level>=1) && (level <=4));
  for (int i=0; i<4; ++i)
    score[i] = 0;

  for(ith= 0; ith < count_params; ++ith)
  {
    kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, ith, kaapi_task_getargs(task));
    kaapi_access_mode_t mp = KAAPI_ACCESS_GET_MODE(m);
    if (mp & KAAPI_ACCESS_MODE_V)
      continue;

#if KAAPI_USE_OCR_AFFINITY
    if (!KAAPI_ACCESS_IS_WRITE(mp))
      continue;
#else
    /* else affinity == best size of any task parameter */
#endif
    /* do bind ptr to the device->asid */
    access = kaapi_format_get_access_param(fmt, (unsigned int)ith, kaapi_task_getargs(task));
    kaapi_format_get_view_param(fmt, (unsigned int)ith, kaapi_task_getargs(task), &view);
    mdi = (kaapi_metadata_info_t*)access->mdi;

    /* Return the meta data information about the data (ptr, view) */
    if (mdi ==0)
      mdi = kaapi_dsm_findaccess_on_node(
          &kaapi_the_dsm,
          kaapi_local_asid,
          KAAPI_DSM_NOCREATE, /* do not create*/
          access,
          &view
      );
    //else 
    //  printf("Found access to metadata\n");
    
    if (mdi !=0)
    {
      /* set affinity for each valid replica */
      for (int lid=0; lid< KAAPI_MEMORY_MAX_NODES; ++lid)
      {
        /* here: a way to implement info of matrix mapping is to allocate meta data replica without valid bit...
           thus we are first interesting to map task where 1/ it exists valid data and 2/ it exist replica, even if not valid
           This would also suppress the management in blas of mapping information within the matrix descriptor.
         */
        if (kaapi_memory_replica_is_valid(mdi, lid)) //||kaapi_memory_replica_is_xfer(mdi,lid))
        {
          kaapi_localitydomain_t* ld = kaapi_localitydomain_get(lid+1);
          if (ld !=0)
          {
            s =1;
            size_t sz = kaapi_memory_view_size(&mdi->replicas[lid]->view);
            if (ld ==ld_target)
              score[0] += sz;
            else if (ld->type == ld_target->type)
              score[1] += sz;
            else
              score[3] += sz;
          }
        }
      }
    }
    else 
    {
      size_t sz = kaapi_memory_view_size(&view);
      score[3] += sz;
    }
  }

#if LOG_AFF
  printf("%p: task %p for ld %i  metadata score=[%i, %i, %i, %i]\n", pthread_self(), task, ldid, score[0], score[1], score[2], score[3]);
#endif
  for (int i=0; i<level; ++i)
    if (score[i] >0) return 1;
#endif // if AFFINITY
  return 0;
}

/* Affinity: compute the best (=ldid with at least a write of the task, see IPDPS2013.
   It remains to transfer the affinity during the steal operations where device
   may has the capacity to select the best task or at least:
	- a task with most of its input on the device
	- a task with inputs on the device close to the target device.
*/
static kaapi_ldid_t kaapi_compute_best_ld( kaapi_task_t* task)
{
  kaapi_ldid_t ldid = (kaapi_ldid_t)-1;

#if KAAPI_USE_AFFINITY
  /* look if task as affinity with one of the ressource */
  uint16_t lid0 = kaapi_memory_asid_get_lid(kaapi_local_asid);
  unsigned int ith;
  kaapi_memory_view_t view;
  kaapi_access_t* access;
  kaapi_metadata_info_t* mdi;
  const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
  unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));
  
  size_t affinity_r[KAAPI_MEMORY_MAX_NODES];
  size_t affinity_w[KAAPI_MEMORY_MAX_NODES];
  size_t* affinity = 0;
  for (int i=0; i<KAAPI_MEMORY_MAX_NODES; ++i)
    affinity_r[i] = affinity_w[i] = 0;

  for(ith= 0; ith < count_params; ++ith)
  {
    kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, ith, kaapi_task_getargs(task));
    kaapi_access_mode_t mp = KAAPI_ACCESS_GET_MODE(m);
    if (mp & KAAPI_ACCESS_MODE_V)
      continue;

    if (KAAPI_ACCESS_IS_WRITE(mp))
      affinity = affinity_w;
    else
      affinity = affinity_r;

    /* do bind ptr to the device->asid */
    access = kaapi_format_get_access_param(fmt, (unsigned int)ith, kaapi_task_getargs(task));
    kaapi_format_get_view_param(fmt, (unsigned int)ith, kaapi_task_getargs(task), &view);
    mdi = (kaapi_metadata_info_t*)access->mdi;

    /* Return the meta data information about the data (ptr, view) */
    if (mdi ==0)
      mdi = kaapi_dsm_findaccess_on_node(
          &kaapi_the_dsm,
          kaapi_local_asid,
          KAAPI_DSM_NOCREATE, /* do not create*/
          access,
          &view
      );
    
    if (mdi !=0)
    {
      /* set affinity for each valid replica */
      KAAPI_MEMORY_VALUE_TYPE valid_bit= KAAPI_ATOMIC_READ(&mdi->valid);
      valid_bit &= ~(1<<lid0);
      while (valid_bit !=0)
      {
        KAAPI_MEMORY_VALUE_TYPE lid = KAAPI_MEMORY_FFS(valid_bit);
        --lid;
        /* here: a way to implement info of matrix mapping is to allocate meta data replica without valid bit...
           thus we are first interesting to map task where 1/ it exists valid data and 2/ it exist replica, even if not valid
           This would also suppress the management in blas of mapping information within the matrix descriptor.
         */
        if (kaapi_memory_replica_is_valid(mdi, lid))
          affinity[lid] += kaapi_memory_view_size(&mdi->replicas[lid]->view);
        valid_bit &= ~(1<<lid);
      }
    }
  }

  /* find max in affinity */
  int lidmax_w = 0;
  int lidmax_r = 0;

#if LOG_AFF
  char buff[256];
  char* b=buff;
  ssize_t s = sprintf(b,"%p: task %p affinity [", pthread_self(), task );
  b+=s;
#endif
  for (int i=0; i<KAAPI_MEMORY_MAX_NODES; ++i)
  {
#if LOG_AFF
    s = sprintf(b,"%i=%i/%i,", i+1,affinity_w[i],affinity_r[i]); b+= s;
#endif
    if (affinity_w[i] > affinity_w[lidmax_w])
      lidmax_w = i;
    if (affinity_r[i] > affinity_r[lidmax_r])
      lidmax_r = i;
  }
#if LOG_AFF
  s = sprintf(b," ]\n"); b+= s;
#endif

  ldid = (kaapi_ldid_t)-1;
  if (affinity_w[lidmax_w] !=0)
    ldid = lidmax_w+1;
  else if (affinity_r[lidmax_r] !=0)
    ldid = lidmax_r+1;
  if (ldid != (kaapi_ldid_t)-1)
    kaapi_task_set_ld(task, 0, ldid );
#if LOG_AFF
  printf(buff);
#endif
#endif // if AFFINITY
  return ldid;
}


/* 
*/
int32_t kaapi_thread_push( kaapi_thread_t* thread, kaapi_task_t* task)
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert(ctxt->ld !=0);

  /* this restricted version for xkblas where only GPU tasks are defined */
  kaapi_localitydomain_t* ld = 0;
  kaapi_ldid_t ldid = kaapi_task_get_ld(task);
  if (ldid != (kaapi_ldid_t)-1) 
  {
    if ((KAAPI_TASK_LD_MASK_PARAM & ldid) !=0)
    {
      /* bound to a parameter */
      ldid &= ~KAAPI_TASK_LD_MASK_PARAM;
      const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
      unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));
      if (ldid < count_params) 
      {
        kaapi_access_t* access = kaapi_format_get_access_param(fmt, (unsigned int)ldid, kaapi_task_getargs(task));
        kaapi_metadata_info_t* mdi = access->mdi;
        if (mdi ==0)
        {
          mdi = kaapi_dsm_findaccess_on_node(
            &kaapi_the_dsm,
            kaapi_local_asid,
            KAAPI_DSM_NOCREATE,
            access,
            0
          );
        }
        kaapi_assert(mdi != 0);
        KAAPI_MEMORY_VALUE_TYPE valid_bit = KAAPI_ATOMIC_READ(&mdi->valid);
        valid_bit &= ~(1<< kaapi_memory_asid_get_lid(kaapi_local_asid));
        ldid = KAAPI_MEMORY_FFS( valid_bit );
        --ldid;
        ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, ldid-1);
      } 
    }
    else 
      ld = kaapi_localitydomain_get(ldid);
  }

  if (ld ==0) 
  {
    ld = kaapi_localitydomain_get_bytype(KAAPI_LD_GPU, ctxt->last_ldid++);
    int count = kaapi_localitydomain_count(KAAPI_LD_GPU);
    if (ctxt->last_ldid >= count) ctxt->last_ldid = 0;
  }

  if (ld == ctxt->ld)
    return
      kaapi_fifo_queue_owner_push(ld->queue, task );
  return
    kaapi_fifo_queue_push(
        ld->queue,
        task
    );
}


/*
*/
int32_t kaapi_queue_push(
    kaapi_context_t* ctxt,
    kaapi_task_t* task
)
{
  kaapi_queue_t* rd = ctxt->queue;

  unsigned int p = kaapi_task_get_priority(task);
  kaapi_assert_debug(p<= KAAPI_TASK_MAX_PRIORITY);
  int32_t T = rd->T[p];
  if (T == rd->size[p])
    kaapi_queue_realloc( &ctxt->lock, p, rd );

  kaapi_assert_debug(T< rd->size[p]);
  rd->data[p][ T++ ] = task;
  /* atomic version: __sync_or_and_fetch( &rd->bitmap, 1<<p); */
  rd->bitmap |= 1U<<p;

#if ARCH_TSO
  /* this is a compiler barrier */
  kaapi_writemem_barrier();
#else
  kaapi_mem_barrier();
#endif

  rd->T[p] = T;
  return T-1 /* [debug] */;
}


/* pop from T: THE
   Return the task popped if popped has index bigger than T0
   Else return 0;
   Never pop
*/
extern kaapi_task_t* kaapi_queue_pop(
    kaapi_context_t* owner,
    kaapi_queue_t* rd,
    int32_t* T0
)
{
  uint32_t bitmap;
  int32_t p;
  int32_t T;
  kaapi_task_t* task;
redo:
  bitmap = rd->bitmap;
  if (bitmap ==0)
  {
    return 0;
  }
  p = sizeof(int)*8 - __builtin_clz( bitmap )-1;
  kaapi_assert_debug(p<= KAAPI_TASK_MAX_PRIORITY);
  T = rd->T[p];
  if (T0 && (T == T0[p])) return 0;
  rd->T[p] = --T;

  /* this is original THE protocol */
#if KAAPI_THE_ORIGINAL
  kaapi_mem_barrier();
#elif KAAPI_THE_RELAXED_TSO
  kaapi_writemem_barrier();
#else // KAAPI_LOCK_QUEUE do not need barrier 
#endif

  if (T >= rd->H[p])
  {
    /* no conflict */
    task = rd->data[p][T];
    goto out;
  }

//printf("conflict\n");
  /* conflict */
  rd->T[p] = T+1; /* roll back */
#if KAAPI_THE_ORIGINAL || KAAPI_THE_RELAXED_TSO
  kaapi_atomic_lock(&owner->lock);
#endif
  T = rd->T[p]-1;
  rd->T[p] = T;
  if (T >= rd->H[p])
  {
    task = rd->data[p][T];
  }
  else
  { /* empty queue */
    ++T;
    rd->T[p] = T; /* roll back */
    task = 0;
  }
#if KAAPI_THE_ORIGINAL || KAAPI_THE_RELAXED_TSO
  kaapi_atomic_unlock(&owner->lock);
#endif

out:
  /* if no task, clear the bitmap */
  if (task ==0)
  {
    rd->bitmap &= ~(1U<<p);
    goto redo;
  }
  return task;
}


/* steal from H: THE
   Assume that mutex on queue is locked.
   Return 0 in case of failure, else return the task stolen
*/
static inline __attribute__((__always_inline__))
kaapi_task_t* kaapi_queue_steal(
  kaapi_context_t* victim,
  kaapi_queue_t* rd,
  uint32_t* idx, 
  uint8_t* prio
)
{
  int p;
  int32_t H0;
  int32_t H;
  int32_t T;
  kaapi_task_t* task;
  uint32_t bitmap = rd->bitmap;

redo:
  if (bitmap ==0) return 0;
  p = sizeof(int)*8 - __builtin_clz( bitmap )-1;
  H0 = rd->H[p];
  H = H0+1;
  rd->H[p] = H;

  /* this is original THE protocol */
#if KAAPI_THE_ORIGINAL || KAAPI_THE_RELAXED_TSO
  kaapi_mem_barrier();
#else // KAAPI_LOCK_QUEUE do not need barrier 
#endif

  T = rd->T[p];
  if (T <= H)
  { /* conflict or empty */
    rd->H[p] = H0;
    task = 0;
    goto out;
  }
#if KAAPI_THE_RELAXED_TSO
  if (T-H <= KAAPI_THE_RELAXED_TSO_BOUND)
  { /* TSO buffer */
    rd->H[p] = H0;
    task = 0;
    goto out;
  }
#endif
  task = rd->data[p][H0];

out:
  /* if no task, clear the bitmap */
  if (task ==0)
  {
    bitmap &= ~(1U<<p);
    goto redo;
  }
  *idx = H0;
  *prio = p;
  return task;
}



/* ============================== Stack API implementation ======================== */

/*
*/
static inline __attribute__((__always_inline__))
void
_kaapi_frame_init( kaapi_frame_t* frame )
{
  frame->next               = 0;
  frame->flag               = KAAPI_FRAME_FLAG_DFG_VOID;
  KAAPI_ATOMIC_WRITE(&frame->spawn_count, 0);
  KAAPI_ATOMIC_WRITE(&frame->exec_count, 0);
  frame->start_task         = 0;
#if !defined(KAAPI_NDEBUG)
  frame->save_thread.sp     = 0;
  frame->save_pc            = 0;
#endif
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
    frame->save_T[p]              = 0;
  frame->save_bloc_sp       = 0;
  frame->save_frame         = 0;
}


/*
*/
kaapi_stack_bloc_t* kaapi_stackallocator_alloc(kaapi_stack_allocator_t* sta, size_t size)
{
  kaapi_assert_debug( size % sizeof(kaapi_task_t) == 0);
  size_t sizealloc = kaapi_default_param.stackblocsize;
  if (size > sizealloc) return 0;
  kaapi_stack_bloc_t* bloc;

redo_pop:
  bloc = sta->head;
  if (bloc !=0)
  {
    if (!KAAPI_ATOMIC_CASPTR(&sta->head, bloc, bloc->next))
      goto redo_pop;

    bloc->next = 0;
    bloc->save_sp = 0;
    return bloc;
  }
  int err = posix_memalign((void**)&bloc, KAAPI_STACKBLOCSIZE, sizealloc );
  kaapi_assert_debug_m(err ==0, "stack alloc");
  if (err !=0) return 0;
  bloc->size = sizealloc;
  bloc->pos = 0;
  bloc->save_sp = 0;
  bloc->next = 0;
  return bloc;
}


/*
*/
void kaapi_stackallocator_dealloc(
    kaapi_stack_allocator_t* sta,
    kaapi_stack_bloc_t* bloc
)
{
  if (bloc == 0) return;
  kaapi_stack_bloc_t* head;

redo_push:
  head = sta->head;
  bloc->next = head;
  if (!KAAPI_ATOMIC_CASPTR(&sta->head, head, bloc))
    goto redo_push;
}

/* Resize stack and return the new SP pointer
*/
static void* _kaapi_resize_stack( kaapi_stack_t* stack, void* sp, kaapi_stack_allocator_t* sta, size_t size )
{
  if (size >= kaapi_default_param.stackblocsize)
  {
#if 1//defined(KAAPI_DEBUG)
    fprintf(stderr,"*** stack bloc overflow: data too big for one allocation,"
         " please extend your KAAPI_STACKBLOCSIZE and recompile the library."
         " KAAPI_STACKBLOCSIZE is actually set to %li bytes / %.2f MBytes. Data required is %li (bytes)",
      kaapi_default_param.stackblocsize, kaapi_default_param.stackblocsize/1024.0/1024.0, size
    );
#endif
    kaapi_abort(__LINE__,__FILE__, "Invalid value");
  }

  kaapi_stack_bloc_t* newbloc = kaapi_stackallocator_alloc(sta, kaapi_default_param.stackblocsize);

  if (newbloc ==0)
    kaapi_abort(__LINE__,__FILE__, "[_kaapi_resize_stack] cannot allocate memory bloc.");
  kaapi_assert_debug(newbloc->next == 0);

  /* link new bloc */
  newbloc->save_sp = sp;
  stack->bloc->next = newbloc;
  stack->bloc = newbloc;

  return kaapi_firstin_stack_bloc( newbloc, kaapi_task_t);
}

/*
*/
void* kaapi_thread_slow_push_data( kaapi_thread_t* thread, unsigned long size )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  char* retval = (char*) _kaapi_resize_stack(
        &ctxt->st_data,
        thread->sp,
        &ctxt->st_allocator,
        size );
  thread->sp = retval + size;
  return retval;
}

/*
*/
static void* kaapi_stack_init( kaapi_stack_allocator_t* sta, kaapi_stack_t* stack )
{
  kaapi_task_t* first_data;

  stack->bloc = kaapi_stackallocator_alloc(sta, kaapi_default_param.stackblocsize);
  if (stack->bloc ==0) return 0;
  stack->bloc0 = stack->bloc;
  first_data = kaapi_firstin_stack_bloc(stack->bloc, kaapi_task_t);
  return (void*)first_data;
}


/*
*/
static void kaapi_stack_free( kaapi_stack_allocator_t* sta, kaapi_stack_t* stack )
{
  while (stack->bloc0 !=0)
  {
    kaapi_stack_bloc_t* bloc = stack->bloc0;
    stack->bloc0 = bloc->next;
    bloc->next = 0;
    kaapi_stackallocator_dealloc( sta, bloc);
  }
}

/*
*/
static void kaapi_stack_destroy( kaapi_stack_allocator_t* sta, kaapi_stack_t* stack )
{
  kaapi_stack_free(sta, stack);
  while (sta->head !=0)
  {
    kaapi_stack_bloc_t* bloc = sta->head;
    sta->head = bloc->next;
    free(bloc);
  }
}


/* suppress all blocs from next bloc that contains addr
*/
static void
_kaapi_stack_free_until(kaapi_stack_allocator_t* sta, kaapi_stack_t* stack, void* addr )
{
  kaapi_stack_bloc_t* bloc = _kaapi_start_blocaddr(addr, kaapi_stack_bloc_t*);
  if (bloc != stack->bloc)
  {
    kaapi_stack_bloc_t* blocn = bloc->next;
    bloc->next = 0;
    stack->bloc = bloc;
    while (blocn !=0)
    {
      kaapi_stack_bloc_t* next = blocn->next;
      kaapi_stackallocator_dealloc( sta, blocn );
      blocn = next;
    }
  }
}

/* =============================== Frame utilities functions ======================== */

/*
*/
int kaapi_frame_pop( kaapi_context_t* ctxt )
{
  kaapi_frame_t* frame_to_restore = ctxt->unlink->save_frame;

  /* NO_UNLINK is to make special frame, such as pushed in begin_dfg that should not be release
     because we may want to keep frame into the stack for multiple consecutive invocation of
     kaapi_sched_sync.
  */
  if (frame_to_restore->flag & KAAPI_FRAME_FLAG_NO_UNLINK)
    return 0;

  /* pop the frame and return */
  if (frame_to_restore->save_bloc_sp != ctxt->st_data.bloc)
    _kaapi_stack_free_until( &ctxt->st_allocator, &ctxt->st_data, frame_to_restore->save_thread.sp );

  frame_to_restore->next = 0;
  ctxt->pc = frame_to_restore->save_pc;
  ctxt->thread.sp = frame_to_restore->save_thread.sp;
  ctxt->unlink = frame_to_restore;
  return 0;
}

/*
*/
int kaapi_frame_push(
    kaapi_frame_t* frame,
    kaapi_context_t* ctxt,
    int flag,
    kaapi_stack_bloc_t* sp_bloc
)
{
  frame->next               = 0;
  frame->flag               = flag;
  KAAPI_ATOMIC_WRITE(&frame->spawn_count, 0);
  KAAPI_ATOMIC_WRITE(&frame->exec_count, 0);
  frame->start_task         = 0;
  frame->save_thread.sp     = ctxt->thread.sp;
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
    frame->save_T[p]        = ctxt->queue->T[p];
  frame->save_pc            = ctxt->pc;
  frame->save_bloc_sp       = sp_bloc;
  frame->save_frame         = ctxt->unlink;
  frame->ctxt               = ctxt;

  ctxt->unlink->next        = frame;
  ctxt->unlink              = frame;

  return 0;
}


/* ================================ Thread API implementation ======================== */
/* Init with max to force respect of calling task_init
*/
static kaapi_atomic_t _kaapi_thread_tid = {KAAPI_MAX_THREAD_COUNT};

/*
*/
kaapi_thread_t* kaapi_thread_bind(int proctype, size_t user_size)
{
  kaapi_context_t* ctxt = (kaapi_context_t*)malloc(sizeof(kaapi_context_t)+user_size);
  /* */
  ctxt->proctype = proctype;
  if (proctype == KAAPI_PROC_TYPE_HOST)
  {
    ctxt->sync = kaapi_sched_sync;
    ctxt->sched_idle = kaapi_sched_idle;
  }
#if KAAPI_USE_OFFLOAD
  else if (proctype == KAAPI_PROC_TYPE_GPU)
  { /* should be also true for other type */
    ctxt->sync = kaapi_sched_sync_offload;
    ctxt->sched_idle = kaapi_sched_idle_offload;
  }
#endif
  else {
    errno = EINVAL;
    kaapi_assert_debug( errno == 0);
    return 0;
  }

  /* */
  ctxt->st_allocator.head = 0; // = &global_stack_allocator;
  ctxt->thread.sp  = (kaapi_task_t*)kaapi_stack_init( &ctxt->st_allocator, &ctxt->st_data);
  ctxt->thread.cnt = 0;
  ctxt->pc = (kaapi_task_t*)ctxt->thread.sp;
  ctxt->device = 0;
  ctxt->ld     = 0;
  ctxt->seed   = rand();
  ctxt->tid    = KAAPI_ATOMIC_INCR(&_kaapi_thread_tid);
  ctxt->kid    = 0;
  ctxt->team   = 0;
  ctxt->last_ldid = 0;
  kaapi_atomic_initlock(&ctxt->lock);
  ctxt->queue  = kaapi_data_push(&ctxt->thread, sizeof(kaapi_queue_t));
  kaapi_task_t** bloc0= malloc(sizeof(kaapi_task_t*)*QUEUE_DEFAULT_SIZE);
  //kaapi_task_t** bloc0= kaapi_data_push(&ctxt->thread, sizeof(kaapi_task_t*)*QUEUE_DEFAULT_SIZE);

  if (ctxt->tid >= KAAPI_MAX_THREAD_COUNT)
  {
    free(bloc0);
    free(ctxt);
    return 0;
  }
#if KAAPI_USE_PERFCOUNTER
  memset(&kaapi_perthread_stat[ctxt->tid], 0, sizeof(kaapi_perthread_stat[ctxt->tid]));
#endif

  if (0 != kaapi_queue_init(ctxt->queue, bloc0, QUEUE_DEFAULT_SIZE))
  {
    kaapi_stack_free( &ctxt->st_allocator, &ctxt->st_data);
    free(ctxt);
    return 0;
  }
  ctxt->free_wqueue = 0;
  ctxt->suspended_queues = 0;

  /* this frame should never be deleted */
  kaapi_frame_t* topframe = (kaapi_frame_t*)kaapi_data_push(&ctxt->thread, sizeof(kaapi_frame_t));
  _kaapi_frame_init( topframe );
  topframe->flag               = KAAPI_FRAME_FLAG_DFG_OK|KAAPI_FRAME_FLAG_DELAY_STEAL;
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
    topframe->save_T[p]             = ctxt->queue->T[p];
  topframe->save_pc            = ctxt->pc;            /* always restore the dummy running task */
  topframe->save_thread.sp     = ctxt->thread.sp;
  topframe->save_bloc_sp       = ctxt->st_data.bloc;
  ctxt->unlink                 = topframe;

  /* dummy running internal kaapi task */
  kaapi_taskmain_t* tmain = (kaapi_taskmain_t*)kaapi_task_alloc(
     &ctxt->thread,
     kaapi_taskmain_body,
     sizeof(kaapi_taskmain_t));
  tmain->arg = 0;

  /* push but non in readylist */
  kaapi_taskflag_set((kaapi_task_t*)tmain, KAAPI_TASK_FLAG_NOLINK);
  kaapi_task_commit(&ctxt->thread, (kaapi_task_t*)tmain);

  /* this frame should never be deleted */
  kaapi_frame_t* frame = (kaapi_frame_t*)kaapi_data_push(&ctxt->thread, sizeof(kaapi_frame_t));
  _kaapi_frame_init( frame );
  frame->flag               = KAAPI_FRAME_FLAG_DFG_OK|KAAPI_FRAME_FLAG_DELAY_STEAL;
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
    frame->save_T[p]             = ctxt->queue->T[p];
  frame->save_pc            = ctxt->pc;            /* always restore the dummy running task */
  frame->save_thread.sp     = ctxt->thread.sp;
  frame->save_bloc_sp       = ctxt->st_data.bloc;

  frame->save_frame         = topframe;
  topframe->next            = frame;
  ctxt->unlink              = frame;

  return &ctxt->thread;
}

/*
*/
int kaapi_thread_unbind(kaapi_thread_t* thread)
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  ctxt->unlink = 0;
  free(ctxt->queue->data0[0]);
  kaapi_queue_destroy(ctxt->queue);
  kaapi_stack_destroy( &ctxt->st_allocator, &ctxt->st_data );
  free(ctxt);
  return 0;
}

/**
*/
struct kaapi_team* kaapi_team_alloc(void)
{
  kaapi_team_t* team = (kaapi_team_t*)malloc(sizeof(kaapi_team_t));
  team->finish = 0;
  kaapi_atomic_initlock(&team->lock);
  KAAPI_ATOMIC_WRITE(&team->count,0);
  for (int i=0; i<KAAPI_PROC_TYPE_MAX; ++i)
  {
    team->pertype[i].count = 0;
    team->pertype[i].capacity = 0;
    team->pertype[i].threads = 0;
  }
  for (int i=0; i<KAAPI_LD_COUNTTYPE; ++i)
  {
    team->lds[i].count = 0;
    team->lds[i].capacity = 0;
    team->lds[i].ld = 0;
  }
  team->capacity = 0;
  team->threads = 0;
  team->thread_metadata = 0;
  kaapi_barrier_init (&team->barrier);
  return team;
}

/**
*/
int kaapi_team_dealloc(kaapi_team_t* team)
{
  kaapi_atomic_destroylock(&team->lock);
  kaapi_barrier_destroy (&team->barrier);

  for (int i=0; i<KAAPI_LD_COUNTTYPE; ++i)
    free(team->lds[i].ld);
  for (int i=0; i<KAAPI_PROC_TYPE_MAX; ++i)
    free(team->pertype[i].threads);
  free(team->threads);
  free(team->thread_metadata);
  free(team);
  return 0;
}

/*
*/
int kaapi_team_size(kaapi_team_t* team)
{
  return KAAPI_ATOMIC_READ(&team->count);
}

/**
*/
int kaapi_team_attach(kaapi_team_t* team, kaapi_thread_t* thread, int idx)
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);

  kaapi_atomic_lock( &team->lock );
  if (team->capacity <= idx)
  {
    int size = idx+1;
    size *= 2;
    kaapi_assert(size >0);

    kaapi_thread_t** threads = (kaapi_thread_t**)realloc(
        team->threads, size*sizeof(kaapi_thread_t*)
    );
    for (int i=team->capacity; i<size; ++i)
      threads[i] = 0;

    kaapi_threadinfo_t* thread_md = (kaapi_threadinfo_t*)realloc(
        team->thread_metadata, size*sizeof(kaapi_threadinfo_t)
    );
    for (int i=team->capacity; i<size; ++i)
      memset(&thread_md[i], 0, sizeof(thread_md[i]));

    team->threads          = threads;
    team->thread_metadata  = thread_md;
    team->capacity         = size;
  }

  team->threads[idx]    = thread;
  KAAPI_ATOMIC_WRITE(&team->thread_metadata[idx].refcount, 1);
  team->thread_metadata[idx].state = 1;
  team->thread_metadata[idx].flag  = 0;
  int pertype_idx = team->pertype[ctxt->proctype].count++;
  if (team->pertype[ctxt->proctype].capacity <=pertype_idx)
  {
    int capacity = team->pertype[ctxt->proctype].capacity*2;
    if (capacity ==0) capacity = 8;
    team->pertype[ctxt->proctype].threads = realloc(
      team->pertype[ctxt->proctype].threads, capacity*sizeof(kaapi_thread_t*)
    );
    for (int i=team->pertype[ctxt->proctype].capacity; i<capacity; ++i)
      team->pertype[ctxt->proctype].threads[i] = 0;
    team->pertype[ctxt->proctype].capacity = capacity;
  }
  /* convert proc type to kaapi_ld_type_t
     - only CPU et GPU are supported
  */
  kaapi_ld_type_t ldtype = 0;
  if (ctxt->proctype == KAAPI_PROC_TYPE_CPU)
    ldtype = KAAPI_LD_NUMA;
  else if (ctxt->proctype == KAAPI_PROC_TYPE_GPU)
    ldtype = KAAPI_LD_GPU;
  else kaapi_assert(0);

  if (ldtype != 0)
  {
    if (pertype_idx < kaapi_localitydomain_count(ldtype))
    {
      ctxt->ld = kaapi_localitydomain_get_bytype( ldtype, pertype_idx);
      int ldidx = __builtin_ffs((unsigned int)ldtype);
      kaapi_assert_debug( ldidx != 0);
      --ldidx;
      if (team->lds[ldidx].capacity <= team->lds[ldidx].count)
      {
        int capacity = team->lds[ldidx].capacity*2;
        if (capacity ==0) capacity = 8;
        team->lds[ldidx].ld = realloc(
          team->lds[ldidx].ld, capacity*sizeof(kaapi_localitydomain_t*)
        );
        team->lds[ldidx].capacity = capacity;
      }
      team->lds[ldidx].ld[team->lds[ldidx].count++] = ctxt->ld;
    }
  }
  //static const char* proctype2name[] = {"host","cuda"};
  //printf("Attach Thread %i to be manager of localitydomain: %i (%s)\n", idx, pertype_idx, proctype2name[ctxt->proctype]);
  team->pertype[ctxt->proctype].threads[idx] = thread;
  team->threads[idx]    = thread;
  kaapi_atomic_unlock( &team->lock );

  ctxt->kid  = idx;
  ctxt->team = team;
  KAAPI_ATOMIC_INCR(&team->count);

  return 0;
}

/*
*/
int kaapi_team_deattach(kaapi_team_t* team, kaapi_thread_t* thread )
{
  kaapi_atomic_lock( &team->lock );
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  if (team->thread_metadata[ctxt->kid].state != 1) abort();
  team->threads[ctxt->kid] = 0;
  team->thread_metadata[ctxt->kid].state = 2;
  KAAPI_ATOMIC_DECR(&team->count);
  ctxt->team = 0;
  kaapi_atomic_unlock( &team->lock );
  return 0;
}


/* ===================================== Executive part ======================== */

/*
*/
int kaapi_thread_save(kaapi_thread_t* thread, kaapi_thread_register_t* regs)
{
  return ENOSYS;
}

/*
*/
int kaapi_thread_restore(kaapi_thread_t* thread, const kaapi_thread_register_t* regs)
{
  return ENOSYS;
}

/*
*/
kaapi_task_t* kaapi_thread_base_task(kaapi_thread_t* thread )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_frame_t* frame = kaapi_stack_topframe( ctxt );
  return frame->save_pc;
}

/*
*/
kaapi_task_t* kaapi_thread_parent_task(kaapi_thread_t* thread )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  return ctxt->unlink->save_pc;
}

/*
*/
kaapi_task_t* kaapi_thread_current_task(kaapi_thread_t* thread )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  return ctxt->pc;
}

/*
*/
int kaapi_thread_set_current_task(kaapi_thread_t* thread, kaapi_task_t* task )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  ctxt->pc = task;
  return 0;
}


/*
*/
int kaapi_handle_init(kaapi_thread_t* thread, kaapi_handle_t* h, void* data, kaapi_metadata_info_t* mdi)
{
  kaapi_assert_debug( KAAPI_ACCESS_ALL < (1<<8) );
  kaapi_access_sync_init(&h->sync0, data);
#if defined(KAAPI_DEBUG)
  h->sync0.reserv = 1; // DEBUG
#endif
  /* wc not ready initially */
  KAAPI_ATOMIC_WRITE(&h->sync0.wc, 1);
  h->last    = &h->sync0;
  h->sync    = 0;
  h->mdi     = mdi;
  return 0;
}

/* Warning.
   This version only consider task graph construction followed by execution.
   Concurrent of task graph building and execution does not produce correct execution.
*/
int kaapi_update_dependencies(
  kaapi_thread_t* thread,
  kaapi_access_t* a,
  kaapi_task_t*   task,
  kaapi_access_mode_t mode,
  kaapi_handle_t* h
)
{
  a->data    = h->sync0.data;
  a->next    = 0;
  a->task    = task;
  a->mode    = mode;
  a->mdi     = 0;
#if KAAPI_DEBUG
  a->creator = pthread_self();
#endif

  if ( (h->last->mode != KAAPI_ACCESS_MODE_VOID)
    && !KAAPI_ACCESS_IS_CONCURRENT(mode, h->last->mode))
  {
    /* non concurrent access: swap sync to a new sync task.
       Previous spawned task will signal completion on h->sync_access.
       Link next to newly added access.
    */
    h->last = h->sync;
    h->sync = 0;
  }

  /* allocate new synchronisation point */
  if (h->sync ==0)
  {
    h->sync = kaapi_data_push( thread, sizeof(kaapi_access_t));
    kaapi_access_sync_init(h->sync, h->sync0.data);
    /* h->sync->wc: set to 1 in access_sync_init */
  }

  /* link */
  if (h->last !=0)
    h->last->next = a;
  h->last = a;
  a->sync = h->sync;
  a->ready= 0;

  /* assume in this version, task is not ready until activation of sync is done */
  kaapi_writemem_barrier();
  KAAPI_ATOMIC_INCR(&h->sync->wc);
  KAAPI_ATOMIC_INCR(&task->wc);
  return 0;
}


/*
*/
int kaapi_begin_dfg( kaapi_thread_t* thread, int flag )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);

  /* push empty frame to automatically restore the stack after the sequence
     of calls to begin_dfg() / end_dfg() / sync_dfg().
WARNING: NO
     when pop in sync_dfg, the stack will be restored to the value of sp_data
     before the call to begin_dfg.
     frame that are push on data stack is deleted and could ne be accessible any more.
WARNING: END NO
     Now: call to kaapi_sched_sync never pops the frame pushed by begin_dfg.
  */
  //void* sp = ctxt->thread.sp;
  kaapi_stack_bloc_t* sp_bloc = ctxt->st_data.bloc;

  kaapi_frame_t* frame      = (kaapi_frame_t*)kaapi_data_push(thread, sizeof(kaapi_frame_t));
  kaapi_frame_push(frame, ctxt, flag, sp_bloc);
  return 0;
}


/* Nothing, place here heuristic to reorder task ?
*/
int kaapi_end_dfg( kaapi_thread_t* thread )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_sched_sync(thread);
  kaapi_frame_pop(ctxt);
  return 0;
}


/* test if all tasks between ]T..H] (H>T >= 0) are executed
*/
int _kaapi_frame_completed( void* arg)
{
  kaapi_frame_t* frame = (kaapi_frame_t*)arg;
  return KAAPI_ATOMIC_READ(&frame->spawn_count) == KAAPI_ATOMIC_READ(&frame->exec_count);
}


/* test if all tasks between ]T..H] (H>T >= 0) are executed
*/
int _kaapi_queue_frame_ready( void* arg)
{
  int count = 0;
  struct queue_frame_t* qf = (struct queue_frame_t*)arg;
  int32_t H;
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
  {
redo:
    for (H = qf->H[p]-1; H >= qf->T[p]; --H)
    {
      if (qf->queue->data[p][H] !=0)
      {
        ++count;
        if (count == 1) return 0;
        goto redo;
      }
      /* means that task at possition H was complete by the thief - increment qf->H for the next
         call to _kaapi_queue_frame_ready
       */
      qf->H[p] = H;
    }
  }

  return 1;
}


/* Activate all tasks waiting the version from sync.
   TOD: separate the version marker contains in kaapi_access_t.
*/
uint32_t kaapi_sched_activate_syncpoint(
    kaapi_thread_t* thread,
    kaapi_access_t* sync
)
{
  kaapi_access_t* a;
  uint32_t activated = 0;
  if (KAAPI_ATOMIC_DECR(&sync->wc) ==0)
  {
    a = sync->next;
    while (a != 0)
    {
      a->ready = 1;
      if (KAAPI_ATOMIC_DECR(&a->task->wc)==0)
      {
        kaapi_thread_push(thread, a->task);
        ++activated;
      }
      a = a->next;
    }
  }
  return activated;
}


/*
*/
uint32_t kaapi_sched_activate_successors (
    kaapi_thread_t* thread,
    kaapi_task_t* task,
    void (*cbk)(kaapi_task_t*, unsigned int, kaapi_access_t*, uint64_t),
    uint64_t arg
)
{
  uint32_t activated = 0;
  if (! (task->flags & KAAPI_TASK_FLAG_INDEPENDENT))
  {
    const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
    unsigned int count_params = kaapi_format_get_count_params(fmt, kaapi_task_getargs(task));
#if 0
printf(">>>>>>>>end task %p: %s\n", task, fmt->name );
#endif
    for (unsigned int i=0; i<count_params; ++i)
    {
      kaapi_access_mode_t mode = kaapi_format_get_mode_param(fmt, i, kaapi_task_getargs(task));
      if (mode & KAAPI_ACCESS_MODE_V)
        continue;

      kaapi_access_t* a  = kaapi_format_get_access_param( fmt, i, kaapi_task_getargs(task));
      if (cbk) cbk(task, i, a, arg);

      activated += kaapi_sched_activate_syncpoint( thread, a->sync );
    }
#if 0
printf("<<<<<<<<end task %p: %s\n", task, fmt->name );
#endif
  }
  
  return activated;
}


/*
*/
int kaapi_sched_sync( kaapi_thread_t* thread )
{
  /* return if empty task to perform */
  kaapi_frame_t frame;
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_frame_t* unlink = ctxt->unlink;
  if ((unlink->start_task ==0) 
   && (kaapi_queue_topequal(ctxt->queue, unlink->save_T))
   && (thread->cnt ==0)
  )
    return 0;

  uint32_t exec_cnt = 0;
  /* cannot do WRITE here: some spawned task may have been processed by GPU and
     (e.g. writeback task) have incremented spawn_count or exec_count before sync...
  */
  KAAPI_ATOMIC_ADD(&unlink->spawn_count, (uint32_t)thread->cnt);

#if KAAPI_USE_PERFCOUNTER
  /* stat */
  kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_TASK_SPAWN] += thread->cnt;
#endif

  thread->cnt = 0;

  /* save thread state and push frame (automatic variable) */
  kaapi_frame_push( &frame, ctxt, KAAPI_FRAME_FLAG_DFG_OK, ctxt->st_data.bloc);

  kaapi_task_t* task = unlink->start_task;
  if (task)
  {
    unlink->start_task = 0;
    goto exec_start;
  }

  do
  {
    {
      kaapi_pop_request_t request;
      /* pop in my list */
      request.op      = KAAPI_REQUEST_OP_POP;
      request.thiefid = (int)ctxt->kid;
      for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
        request.limit[p]   = unlink->save_T[p];
      request.task    = &task;
      if (KAAPI_REQUEST_S_OK !=
          kaapi_sched_process_request(ctxt->team, ctxt,
                                      (kaapi_request_t*) &request ))
        break;
    }
exec_start:
    {
      const kaapi_format_t* fmt = kaapi_task_getformat_ref(task);
      ctxt->pc = task;
      for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
        frame.save_T[p] = ctxt->queue->T[p];
      fmt->entrypoint[KAAPI_PROC_TYPE_CPU](
          task, kaapi_context2thread(ctxt)
      );
#if KAAPI_DEBUG
      task->flags |= KAAPI_TASK_FLAG_EXEC;
#endif
#if KAAPI_USE_PERFCOUNTER
      ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_TASK_EXEC];
#endif
      ++exec_cnt;

      /* new task(s) ? */
      if (!kaapi_queue_topequal(ctxt->queue, frame.save_T))
        kaapi_sched_sync(thread);

      /* activate successors : reverse the natural order of ready successor
         - it could be best to keep this order by 1/ pushing into local list
         2/ then appening the lists togethers
      */
      kaapi_sched_activate_successors(thread, task, 0, 0);
    }
  } while (1);

  /* report exec count to frame data structure */
  if (exec_cnt !=0)
    KAAPI_ATOMIC_ADD(& unlink->exec_count, exec_cnt );

  /* ici reset T de la queue à save_T */
  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
    ctxt->queue->T[p] = unlink->save_T[p];

  /* si vol */
  if (kaapi_queue_headgreather(ctxt->queue, unlink->save_T))
  {
    /* there is stolen tasks. Suspend execution until all tasks have been executed
       These is linear complexity test for N stolen requests - using join counter
       on the frame may be more performant (but O(n) increment in parallel).
    */
    struct queue_frame_t qf;
    qf.queue = ctxt->queue;

    /* read atomically H because steal operation may increment H before rollback is steal fail */
    kaapi_atomic_lock(&ctxt->lock);
    for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
      qf.H[p] = ctxt->queue->H[p];
    kaapi_atomic_unlock(&ctxt->lock);
    for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
      qf.T[p] = unlink->save_T[p];
    if (1) //qf.H > qf.T)
    {
#if KAAPI_USE_PERFCOUNTER
      ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_SUSPEND];
#endif
      if (!_kaapi_queue_frame_ready(&qf))
      {
        LOGDEBUG(
          printf("%i:: Suspend work on queue:%p [T:%i, H:%i] T0:%i, waiting for task in [%i, %i]\n",
            (int)ctxt->kid, (void*)ctxt->queue, ctxt->queue->T, qf.H, unlink->save_T, qf.T, qf.H-1);
        )
        kaapi_assert_debug( kaapi_sched_idle == ctxt->sched_idle );
        kaapi_sched_idle( thread, _kaapi_queue_frame_ready, &qf );
        LOGDEBUG(
          printf("%i:: Return from suspend work on queue:%p [T:%i, H:%i]\n",
          (int)ctxt->kid, (void*)ctxt->queue, ctxt->queue->T, ctxt->queue->H);
        )
      }
      kaapi_atomic_lock(&ctxt->lock);
      for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
        ctxt->queue->H[p] = unlink->save_T[p];
      kaapi_atomic_unlock(&ctxt->lock);
    }
  }

  /* si push extern */
  if (!_kaapi_frame_completed(unlink))
  {
    kaapi_assert_debug( kaapi_sched_idle == ctxt->sched_idle );
    kaapi_sched_idle( thread, _kaapi_frame_completed, unlink );
  }
  //printf("[sched_sync] spawn: %i / exec: %i\n", 
  //      KAAPI_ATOMIC_READ(&unlink->spawn_count), KAAPI_ATOMIC_READ(&unlink->exec_count));
  kaapi_frame_pop( ctxt );

  return 0;
}


/* Idle thread. What to do ?
   Only search task into different queues of the stack frame.
*/
int kaapi_sched_idle( kaapi_thread_t* thread, int (*f_fini)(void*), void* arg )
{
  /* work stealing between attached thread of the team
     - steal task in stack of frames
     - steal task in ready list of frames
  */
  kaapi_context_t* victim;
  kaapi_task_t* task;
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_queue_t* save_queue = ctxt->queue;
  kaapi_team_t* team = ctxt->team;
  kaapi_steal_request_t request;
  request.op      = KAAPI_REQUEST_OP_VOID;
  int victim_id=0;


  /* push queue into front of suspend_queues list */
  ctxt->queue = 0;
  save_queue->next = ctxt->suspended_queues;
  ctxt->suspended_queues = save_queue;

  int count = 0;

  do
  {
    ++count;

    if (f_fini && f_fini(arg)) goto r_exit;

    /* select victim: make virtualisation of the function here */
    victim_id = rand_r(&ctxt->seed);
    if (victim_id >= team->pertype[KAAPI_PROC_TYPE_CPU].count)
      victim_id %= team->pertype[KAAPI_PROC_TYPE_CPU].count;
    victim = kaapi_thread2context(
      team->pertype[KAAPI_PROC_TYPE_CPU].threads[victim_id]
    );
    if (victim ==0) {
      //pthread_yield_np();
      sched_yield();
      continue;
    }

    { /* steal victim */
      kaapi_queue_t* queue = victim->queue;
      if ((queue !=0) && !kaapi_queue_empty(queue))
      {
        LOGDEBUG(printf("%i:: Try to steal task on %s queue:%p (T:%i, H:%i)\n", (int)ctxt->kid,
          (queue == victim->queue ? "default" : "suspended"),
          (void*)queue, queue->T, queue->H);)
        /* steal in list */
        request.op      = KAAPI_REQUEST_OP_STEAL;
        request.arch    = (1 << KAAPI_PROC_TYPE_HOST);
        request.thiefid = (int)ctxt->kid;
        request.task    = &task;
        kaapi_sched_process_request(team, victim, (kaapi_request_t*) &request );
        //iTG debug: block stealrequest.status=  KAAPI_REQUEST_S_NOK;
        switch (request.status)
        {
          case KAAPI_REQUEST_S_OK:
          {
#if KAAPI_USE_PERFCOUNTER
            ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_STEAL_OK];
#endif
            if (ctxt->queue ==0) {
              kaapi_queue_t* queue = kaapi_data_push(thread, sizeof(kaapi_queue_t));
              kaapi_task_t** bloc0= kaapi_data_push(&ctxt->thread, sizeof(kaapi_task_t*)*QUEUE_DEFAULT_SIZE);
              kaapi_assert(0 == kaapi_queue_init(queue, bloc0, QUEUE_DEFAULT_SIZE));
              ctxt->queue = queue;
              kaapi_begin_dfg(thread, KAAPI_FRAME_FLAG_DFG_OK);
            }
            LOGDEBUG(printf("%i:: Steal task:%p on %s prio:%i queue:%p (T:%i, H:%i), reqid:%i, runing queue:%p\n",
              (int)ctxt->kid, (void*)task,
              (request.queue == victim->queue ? "default" : "suspended"), (int)request.prio,
              (void*)request.queue, request.queue->T[request.prio],request.queue->H[request.prio],request.idx,ctxt->queue);)

            /* start execution with starttasks already pushed */
            ctxt->unlink->start_task = task;
            ctxt->sync( thread );

            /* signal request.frame */
            LOGDEBUG(
              printf("%i:: Signal stolen task:%p on queue:%p, index:%i\n",
                 (int)ctxt->kid, (void*)task, (void*)request.queue, request.idx);
            )
#if defined(KAAPI_NO_TSO_ARCH)
            kaapi_mem_barrier();
#else
            kaapi_writemem_barrier();
#endif
            /* mark stolen task as executed */
            request.queue->data[request.prio][request.idx] = 0;
            request.status = KAAPI_REQUEST_S_INIT;
            break;
          }

          case KAAPI_REQUEST_S_NOK:
          {
#if KAAPI_USE_PERFCOUNTER
            ++kaapi_perthread_stat[ctxt->tid].counter[KAAPI_CNT_STEAL_NOK];
#endif
            LOGDEBUG(printf("%i:: Steal fail on %s queue:%p (T:%i, H:%i)\n", (int)ctxt->kid,
              (queue == victim->queue ? "default" : "suspended"),
              (void*)queue, queue->T, queue->H);)
            sched_yield();
            break;
          }

          case  KAAPI_REQUEST_S_POSTED:
          {
            abort(); /* not in this fast implementation */
          }

          default:
            abort(); /* error */
        }
      }
    }
  } while ( f_fini && !f_fini(arg));

r_exit:
  /* restore original queue */
  if (ctxt->queue)
  {
    kaapi_queue_t* tmp = ctxt->queue;
    kaapi_atomic_lock(&ctxt->lock);
    kaapi_frame_pop( ctxt );
    ctxt->queue = 0;
    kaapi_atomic_unlock(&ctxt->lock);
    tmp->next = ctxt->free_wqueue;
    ctxt->free_wqueue = tmp;
    kaapi_queue_destroy(tmp);
  }
  ctxt->suspended_queues = save_queue->next;
  save_queue->next = 0;
  ctxt->queue = save_queue;

  ctxt->unlink->flag &= ~KAAPI_FRAME_FLAG_NO_UNLINK;
  return EINTR;
}




/* Current implementation lock the context.
   CCsync algorithm has to been back-ported from XKaapi
*/
int kaapi_sched_process_request (
  kaapi_team_t*    team,    // the team
  kaapi_context_t* ctxt,    // the target context of the request
  kaapi_request_t* request  // the request
)
{
  switch (request->header.op)
  {
    case KAAPI_REQUEST_OP_STEAL:
    { /* tail not implemented in this version */
      kaapi_atomic_lock(&ctxt->lock);
      kaapi_queue_t* queue = ctxt->queue;
      if (queue !=0)
      {
        kaapi_task_t* task = kaapi_queue_steal(ctxt, queue, &request->steal_a.idx, &request->steal_a.prio);
        *request->steal_a.task = task;
        request->steal_a.queue = queue;
        request->header.status = (task == 0 ? KAAPI_REQUEST_S_NOK : KAAPI_REQUEST_S_OK);
      }
      else
      {
        request->header.status = KAAPI_REQUEST_S_NOK;
        *request->steal_a.task = 0;
        request->steal_a.queue = 0;
        request->steal_a.idx   = 0;
      }
      kaapi_atomic_unlock(&ctxt->lock);
      break;
    }

    case KAAPI_REQUEST_OP_PUSH:
    {
#if 0
      int err = kaapi_queue_push(ctxt, request->push_a.task);
      request->header.status = (err ==0 ? KAAPI_REQUEST_S_OK : KAAPI_REQUEST_S_NOK);
#else
      abort();
#endif
    } break;

    case KAAPI_REQUEST_OP_POP:
    {
#if KAAPI_LOCK_QUEUE
      kaapi_atomic_lock(&ctxt->lock);
#endif
      kaapi_task_t* task = kaapi_queue_pop(ctxt, ctxt->queue, request->pop_a.limit);
      *request->pop_a.task = task;
      request->header.status = (task ==0 ? KAAPI_REQUEST_S_NOK : KAAPI_REQUEST_S_OK);
#if KAAPI_LOCK_QUEUE
      kaapi_atomic_unlock(&ctxt->lock);
#endif
    } break;

    case KAAPI_REQUEST_OP_PUSH_REMOTE:
    {
      request->header.status = KAAPI_REQUEST_S_NOK;
    } break;

    default:
      request->header.status = KAAPI_REQUEST_S_ERROR;
    break;
  }

  return request->header.status;
}




/* ================================== Internal Task implementation ======================== */
/*
*/
kaapi_format_id_t kaapi_sync_body;
static void kaapi_sync_body_fnc(
  kaapi_task_t*   task,
  kaapi_thread_t* thread
)
{
}

static
unsigned int task_sync_format_get_count_params(const kaapi_format_t* fmt, const void* sp)
{
  return 1;
}
static
kaapi_access_mode_t task_sync_format_get_mode_param(const kaapi_format_t* fmt, unsigned int i, const void* sp)
{
  return KAAPI_ACCESS_MODE_RW;
}
static
kaapi_access_t* task_sync_format_get_access_param(const kaapi_format_t* fmt, unsigned int i, const void* sp)
{
  kaapi_tasksync_t *arg = (kaapi_tasksync_t*)sp;
  return &arg->a;
}
static
void task_sync_format_get_view_param(
  const kaapi_format_t* fmt, unsigned int i, const void* sp,
  kaapi_memory_view_t* view
)
{
  //kaapi_tasksync_t *arg = (kaapi_tasksync_t*)sp;
  kaapi_memory_view_make1d(view, 0, 1, sizeof(char));
}


/*
*/
kaapi_format_id_t kaapi_nop_body;
static void kaapi_nop_body_fnc(
  kaapi_task_t*   task,
  kaapi_thread_t* thread
)
{
}

/*
*/
kaapi_format_id_t kaapi_taskmain_body;
static void kaapi_taskmain_body_fnc(
  kaapi_task_t* task,
  kaapi_thread_t* thread __attribute__((unused))
)
{
}

static
unsigned int _internal_task_format_get_count_params(const kaapi_format_t* fmt, const void* sp)
{
  return 0;
}

/*
*/
int kaapi_taskformat_init(void)
{
  kaapi_sync_body = kaapi_format_taskregister_func( kaapi_format_allocate(),
           (void*)kaapi_sync_body_fnc, /* key */
           kaapi_sync_body_fnc, /* body CPU */
           0, /* body GPU */
           "sync",
           0,
           0, //task_format_get_size,
           0, //task_format_task_copy,
           task_sync_format_get_count_params,
           task_sync_format_get_mode_param,
           0, //task_format_get_data_param,
           task_sync_format_get_access_param,
           0, //task_format_set_access_param,
           0, //task_format_get_fmt_param,
           task_sync_format_get_view_param,
           0, //task_format_set_view_param,
           0, //task_format_reducor,
           0, //task_format_redinit,
           0, //task_fnc_get_splitter,
           0, //task_fnc_get_affinity
           0  //task_fnc_get_cost
  );
  kaapi_nop_body = kaapi_format_taskregister_func( kaapi_format_allocate(),
           (void*)kaapi_nop_body_fnc, /* key */
           kaapi_nop_body_fnc, /* body CPU */
           0, /* body GPU */
           "no op",
           0,
           0, //task_format_get_size,
           0, //task_format_task_copy,
           _internal_task_format_get_count_params,
           0, //task_format_get_mode_param,
           0, //task_format_get_data_param,
           0, //task_format_get_access_param,
           0, //task_format_set_access_param,
           0, //task_format_get_fmt_param,
           0, //task_format_get_view_param,
           0, //task_format_set_view_param,
           0, //task_format_reducor,
           0, //task_format_redinit,
           0, //task_fnc_get_splitter,
           0, //task_fnc_get_affinity
           0  //task_fnc_get_cost
  );
  kaapi_taskmain_body = kaapi_format_taskregister_func( kaapi_format_allocate(),
           (void*)kaapi_taskmain_body_fnc, /* key */
           kaapi_taskmain_body_fnc, /* body CPU */
           0, /* body GPU */
           "task main",
           0,
           0, //task_format_get_size,
           0, //task_format_task_copy,
           _internal_task_format_get_count_params,
           0, //task_format_get_mode_param,
           0, //task_format_get_data_param,
           0, //task_format_get_access_param,
           0, //task_format_set_access_param,
           0, //task_format_get_fmt_param,
           0, //task_format_get_view_param,
           0, //task_format_set_view_param,
           0, //task_format_reducor,
           0, //task_format_redinit,
           0, //task_fnc_get_splitter,
           0, //task_fnc_get_affinity
           0  //task_fnc_get_cost
  );
#if 0
#if KAAPI_THE_ORIGINAL 
  printf("***Protocol: Original THE algorithm\n");
#elif KAAPI_THE_RELAXED_TSO 
  printf("***Protocol: Adapted THE algorithm for TSO[%i] model\n",KAAPI_THE_RELAXED_TSO_BOUND);
#elif KAAPI_LOCK_QUEUE 
  printf("***Protocol: Locked queue\n");
#endif
#endif
  return 0;
}

/*
*/
int kaapi_taskformat_finalize(void)
{
  return 0;
}

/*
*/
int kaapi_taskmodule_init(void)
{
  KAAPI_ATOMIC_WRITE(&_kaapi_thread_tid, 0);
}

/*
*/
int kaapi_taskmodule_finalize(void)
{
}

