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

kaapi_localitydomain_type_t* kaapi_all_lddomains = 0;
static kaapi_localitydomain_t** map_ldid2ld = 0;
static kaapi_ldid_t kaapi_ldid = 1;
static kaapi_lock_t kaapi_ldlock = KAAPI_LOCK_INITIALIZER;

/*
static kaapi_localitydomain_t** affinity_ldid2ld
 */


/*
*/
int kaapi_localitydomain_finalize(void)
{
  if (kaapi_all_lddomains !=0)
  {
    for (int i=0; i<KAAPI_LD_COUNTTYPE; ++i)
      free(kaapi_all_lddomains[i].ld);
    free(kaapi_all_lddomains);
    kaapi_all_lddomains = 0;
  }
  if (map_ldid2ld !=0)
  {
    free(map_ldid2ld);
    map_ldid2ld = 0;
  }

  kaapi_ldid = 1;
  return 0;
}

/*
*/
int kaapi_localitydomain_init( kaapi_localitydomain_t* ld, kaapi_device_t* device )
{
  int err;
  kaapi_fifo_queue_t* rd;
  ld->device = device;
  ld->queue = rd = malloc(sizeof(kaapi_fifo_queue_t));
  if (rd ==0) return ENOMEM;

  /* fifo_queue_init */
  rd->T = rd->H = 0;
  rd->data = 0;
  rd->frame = 0;
  rd->size = 0;
  rd->push_count = 0;
  rd->pop_count = 0;
  rd->waiter_push = 0;
  //rd->waiter_pop  = 0;
  err = pthread_mutex_init(&rd->lock, 0);
  if (err) return err;
  err = pthread_cond_init(&rd->cond_push, 0);
  if (err)
    goto label_err1;
  rd->cbk_fnc = 0;
  rd->cbk_arg = 0;
  //err = pthread_cond_init(&rd->cond_pop, 0);

  if (err)
  {
    pthread_cond_destroy(&rd->cond_push);
label_err1:
    pthread_mutex_destroy(&rd->lock);
    free(ld->queue);
    ld->queue = 0;
    return err;
  }
  return 0;
}

/*
*/
int kaapi_localitydomain_destroy( kaapi_localitydomain_t* ld )
{
  kaapi_fifo_queue_t* rd = ld->queue;
  pthread_mutex_destroy(&rd->lock);
  pthread_cond_destroy(&rd->cond_push);
  //pthread_cond_destroy(&rd->cond_pop);
  free(ld->queue->data);
  free(ld->queue->frame);
  free(ld->queue);
  return 0;
}

/* Attach a new locality domain.
   Set its identifier.
   Return 0 iff no error
*/
int kaapi_localitydomain_attach( kaapi_ld_type_t type, kaapi_localitydomain_t* ld )
{
  int err =0;
  if ((__builtin_popcountl(((unsigned int)type) & KAAPI_LD_ALLTYPE) != 1) &&
      ((type & ~KAAPI_LD_ALLTYPE) != 0)
    )
  {
    err = EINVAL;
    goto out;
  }
  kaapi_assert( KAAPI_LD_COUNTTYPE == __builtin_popcountl(KAAPI_LD_ALLTYPE));
  kaapi_atomic_lock(&kaapi_ldlock);
  int idx = __builtin_ffs((unsigned int)type);
  kaapi_assert( idx != 0);
  --idx;

  if (kaapi_all_lddomains ==0)
  {
    kaapi_all_lddomains = calloc(sizeof(kaapi_localitydomain_type_t), KAAPI_LD_COUNTTYPE);
    for (int i=0; i<KAAPI_LD_COUNTTYPE; ++i)
      kaapi_all_lddomains[i].type = (1<<i);
  }
  /* reallocate kaapi_all_lddomains[idx]->ld */
  ld->type = type;
  ld->idx = kaapi_all_lddomains[idx].count++;
  ld->ldid = kaapi_ldid++;
  map_ldid2ld = (kaapi_localitydomain_t**)realloc(
    map_ldid2ld, sizeof(kaapi_localitydomain_t*)*kaapi_ldid
  );
  if (map_ldid2ld ==0)
  {
    err = ENOMEM;
    goto out;
  }
  map_ldid2ld[ld->ldid] = ld;
  kaapi_all_lddomains[idx].ld =
    (kaapi_localitydomain_t**)realloc(kaapi_all_lddomains[idx].ld,
          kaapi_all_lddomains[idx].count*sizeof(kaapi_localitydomain_t*)
  );
  if (kaapi_all_lddomains[idx].ld ==0)
  {
    err = ENOMEM;
    goto out;
  }
  kaapi_all_lddomains[idx].ld[ld->idx] = ld;

out:
  kaapi_atomic_unlock(&kaapi_ldlock);
  return err;
}

/*
*/
int kaapi_localitydomain_deattach( kaapi_ld_type_t type, kaapi_localitydomain_t* ld )
{
  int err =0;
  if ((__builtin_popcountl(((unsigned int)type) & KAAPI_LD_ALLTYPE) != 1) &&
      ((type & ~KAAPI_LD_ALLTYPE) != 0)
    )
  {
    err = EINVAL;
    goto out;
  }
  kaapi_atomic_lock(&kaapi_ldlock);
  int idx = __builtin_ffs((unsigned int)type);
  kaapi_assert( idx != 0);
  --idx;
  kaapi_all_lddomains[idx].ld[ld->idx] = 0;
  ld->idx = (unsigned int)-1;
  map_ldid2ld[ld->ldid] = 0;

out:
  kaapi_atomic_unlock(&kaapi_ldlock);
  return err;
}


/*
*/
kaapi_localitydomain_t* kaapi_localitydomain_get(
    kaapi_ldid_t ldid
)
{
  if (ldid >= kaapi_ldid) return 0;
  return map_ldid2ld[ldid];
}


/*
*/
unsigned int kaapi_localitydomain_count(
    kaapi_ld_type_t type
)
{
  if (kaapi_all_lddomains ==0) return (unsigned int)-1;
  if ((__builtin_popcountl(((unsigned int)type) & KAAPI_LD_ALLTYPE) != 1) &&
      ((type & ~KAAPI_LD_ALLTYPE) != 0)
    )
  {
    return (unsigned int)-1;
  }
  int idx = __builtin_ffs((unsigned int)type);
  kaapi_assert( idx != 0);
  --idx;
  return kaapi_all_lddomains[idx].count;
}


/*
*/
kaapi_localitydomain_t* kaapi_localitydomain_get_bytype(
    kaapi_ld_type_t type,
    kaapi_ldid_t ldid
)
{
  if (kaapi_all_lddomains ==0) return 0;
  if ((__builtin_popcountl(((unsigned int)type) & KAAPI_LD_ALLTYPE) != 1) &&
      ((type & ~KAAPI_LD_ALLTYPE) != 0)
    )
    return 0;

  int idx = __builtin_ffs((unsigned int)type);
  kaapi_assert( idx != 0);
  --idx;
  if (ldid >= kaapi_all_lddomains[idx].count) return 0;
  return kaapi_all_lddomains[idx].ld[ldid];
}


/*
*/
kaapi_ldid_t kaapi_localitydomain_get_num(
    kaapi_ld_type_t type,
    unsigned int i
)
{
  kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(type,i);
  if (ld == 0) return (unsigned int)-1;
  return ld->ldid;
}


/*
*/
const char* kaapi_localitydomain_info(
    kaapi_ld_type_t type,
    unsigned int i
)
{
  static const char* ARCH2STR[] = { "host", "cuda" };
  static char buffer[256];
  kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(type,i);
  if (ld == 0) return 0;
  snprintf(buffer, 256, "asid:%s-gid-%i, @device:%p, info: %s",
    ARCH2STR[kaapi_memory_asid_get_arch(ld->device->memdev.asid)],
    kaapi_memory_asid_get_lid(ld->device->memdev.asid),
    (void*)ld->device,
    kaapi_offload_device_info(ld->device)
  );
  return buffer;
}




/* */
static void kaapi_fifo_queue_alloc(
  unsigned int p,
  kaapi_fifo_queue_t* rd
)
{
  if (rd->data ==0)
  {
    kaapi_task_t** data;
    kaapi_frame_t** frame;
    int32_t size = QUEUE_DEFAULT_SIZE;
    data = (kaapi_task_t**)malloc( size*sizeof(kaapi_task_t*));
    frame = (kaapi_frame_t**)malloc( size*sizeof(kaapi_frame_t*));
    memset(data, 0, size*sizeof(kaapi_task_t*));
    memset(frame, 0, size*sizeof(kaapi_frame_t*));
    rd->size = size;
    rd->data = data;
    rd->frame = frame;
  }
}


/* */
int32_t kaapi_fifo_queue_push(
    kaapi_fifo_queue_t* rd,
    kaapi_frame_t* frame,
    kaapi_task_t* task
)
{
  unsigned int p = kaapi_task_get_priority(task);
  kaapi_assert_debug(p<= KAAPI_TASK_MAX_PRIORITY);
  kaapi_assert_debug(frame != 0);

  pthread_mutex_lock(&rd->lock);

  if (rd->data == 0) /* only allocate null entry */
    kaapi_fifo_queue_alloc( p, rd );

  /* wait enough space == wait entry ==0 in the array */
  int32_t size = rd->size;
  while (rd->T - rd->H >=size)
  {
    rd->waiter_push = 1;
    pthread_cond_wait(&rd->cond_push, &rd->lock);
  }
  rd->waiter_push = 0;

  /* push task */
  int32_t T      = rd->T++;
  int32_t idx    = T%size;
  rd->data[idx]  = task;
  rd->frame[idx] = frame;
  ++rd->push_count;

  if (rd->cbk_fnc)
    rd->cbk_fnc(rd->cbk_arg);

  pthread_mutex_unlock(&rd->lock);

  return T; /* [debug] */;
}

/* non blocking version */
kaapi_task_t* kaapi_fifo_queue_pop(
    kaapi_fifo_queue_t* rd,
    kaapi_frame_t** frame
)
{
  kaapi_task_t* task = 0;

  pthread_mutex_lock(&rd->lock);
  if (rd->H < rd->T)
  {
    int32_t size = rd->size;
    kaapi_assert_debug(size >0);

    int32_t H = rd->H;
    int32_t idx = H%size;
    task = rd->data[idx];
    *frame = rd->frame[idx];
    rd->data[idx] = 0;
    rd->frame[idx] = 0;
    ++rd->pop_count;
    ++rd->H;
    if (rd->waiter_push)
      pthread_cond_signal(&rd->cond_push);
  }
  pthread_mutex_unlock(&rd->lock);
  kaapi_assert_debug((task ==0) || (*frame != 0));
  return task;
}


/* block caller while the queue is empty */
int kaapi_fifo_register_waiter(
    kaapi_fifo_queue_t* rd,
    void (*callback)(void*),
    void* arg
)
{
  //rd->waiter_pop = 1;
  rd->cbk_fnc = callback;
  rd->cbk_arg = arg;
  return 0;
}

