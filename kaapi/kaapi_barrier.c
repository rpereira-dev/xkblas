/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** francois.broquedis@imag.fr
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

#if defined(__MIC__) /*TEST: means active polling */
#undef HAVE_FUTEX
#endif

#if defined(__linux__)
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE         /* See feature_test_macros(7) ??? */
#endif
#include <unistd.h>
#  if HAVE_FUTEX
#    include <sys/syscall.h>   /* For SYS_xxx definitions */
#    include <linux/futex.h>
#  endif
#endif

#if HAVE_FUTEX
static inline int sys_futex(void *addr1, int op, int val1, struct timespec *timeout, void *addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}
#else /* active pool */
//#  warning "Use active pooling"
#endif

#include <string.h>

void
kaapi_barrier_init (kaapi_barrier_t *barrier)
{
  KAAPI_ATOMIC_WRITE (&barrier->cycle, 0);
  KAAPI_ATOMIC_WRITE (&barrier->wait_cycle, 0);
  barrier->term = 0;
  memset (barrier->count, 0, KAAPI_BAR_CYCLES * KAAPI_CACHE_LINE_SIZE);
}

void
kaapi_barrier_destroy (kaapi_barrier_t *barrier)
{
#if KAAPI_DEBUG
  KAAPI_ATOMIC_WRITE (&barrier->cycle, -1);
  memset (barrier->count, -1, KAAPI_BAR_CYCLES * KAAPI_CACHE_LINE_SIZE);
#endif
}

/* used to exit from scheduling loop */
struct _kaapi_barrier_cond_t {
  int                  next_cycle;
  kaapi_barrier_t     *barrier;
} _kaapi_barrier_cond_t;

static int _kaapi_condition_barrier_isready(void* arg)
{
  struct _kaapi_barrier_cond_t* cond = (struct _kaapi_barrier_cond_t*)arg;
  return (KAAPI_ATOMIC_READ (&cond->barrier->cycle) == cond->next_cycle);
}


/* call to wake up thread waiting on next cycle after
*/
void kaapi_team_barrier_wait_signal (
    kaapi_team_t* team,
    kaapi_thread_t* thread
)
{
#if KAAPI_DEBUG
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert_debug(ctxt->kid ==0);
#endif

  kaapi_barrier_t * const barrier = &team->barrier;
  kaapi_mem_barrier();
  KAAPI_ATOMIC_INCR(&barrier->wait_cycle);
#if HAVE_FUTEX
  sys_futex(&barrier->wait_cycle, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
#else
#endif
}


void kaapi_team_barrier_wait_signal_term(
    kaapi_team_t* team,
    kaapi_thread_t* thread
)
{
  kaapi_barrier_t * const barrier = &team->barrier;
#if KAAPI_DEBUG
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert_debug(ctxt->kid ==0);
#endif
#if HAVE_FUTEX
  int term =
#endif
      KAAPI_ATOMIC_INCR((kaapi_atomic_t*)&barrier->term);
#if HAVE_FUTEX
  if (term == KAAPI_ATOMIC_READ(&team->count))
  {
    int err = sys_futex(&barrier->term, FUTEX_WAKE, 1, NULL, NULL, 0);
    kaapi_assert(err != -1);
  }
#endif
}

/* call to wake up thread waiting on next cycle after
*/
void kaapi_team_barrier_wait_term (
    kaapi_team_t* team,
    kaapi_thread_t* thread
)
{
#if KAAPI_DEBUG
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert_debug(ctxt->kid ==0);
#endif

  kaapi_barrier_t * const barrier = &team->barrier;
  int term = KAAPI_ATOMIC_INCR((kaapi_atomic_t*)&barrier->term);
#if HAVE_FUTEX
  int i,err;
redo_test:
  for (i=0; (i<1500) && ((term = KAAPI_ATOMIC_READ((kaapi_atomic_t*)&barrier->term)) != KAAPI_ATOMIC_READ(&team->count)); ++i)
    kaapi_slowdown_cpu();
  if (i==1500)
  {
redo_wait:
    err = sys_futex(&barrier->term, FUTEX_WAIT, term, NULL, NULL, 0);
    kaapi_assert( (err != -1) || (errno == EWOULDBLOCK) );
    if (err ==-1) 
    {
      if (errno == EWOULDBLOCK)
         goto redo_test;
      if (errno == EINTR)
         goto redo_wait;
    }
  }
  else
  {
    //printf("Do no wait term !!! @:%p, term=%i\n", (void*)barrier, (int)barrier->term); fflush(stdout);
  }
#else
  while ((term = KAAPI_ATOMIC_READ((kaapi_atomic_t*)&barrier->term)) != KAAPI_ATOMIC_READ(&team->count))
    kaapi_slowdown_cpu();
#endif
}


/*
*/
void kaapi_team_barrier_wait (
    kaapi_team_t* team,
    kaapi_thread_t* thread,
    int count,
    int flag
)
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert (KAAPI_ATOMIC_READ(&team->count) !=0);

  kaapi_barrier_t * const barrier = &team->barrier;
  const int current_cycle = KAAPI_ATOMIC_READ (&barrier->cycle);
  const int next_cycle = (current_cycle + 1) % KAAPI_BAR_CYCLES;
  int wait_cycle = -1;
  kaapi_atomic_t *current_counter = (kaapi_atomic_t *)&barrier->count[current_cycle * KAAPI_CACHE_LINE_SIZE];

  if (flag & KAAPI_BARRIER_FLAG_WAITEXIT)
  {
    kaapi_assert_debug(ctxt->kid ==0);
    wait_cycle = KAAPI_ATOMIC_READ (&barrier->wait_cycle);
    kaapi_mem_barrier();
  }

  /* schedule local work, if any */
  if ((flag & KAAPI_BARRIER_FLAG_NOSCHEDULE) == 0)
  {
//    if (!kaapi_stack_isempty( &context->stack ) )
    kaapi_assert(ctxt->sync(thread) ==0);
  }

  int nb_arrived = KAAPI_ATOMIC_INCR (current_counter);

  if (nb_arrived == count)
  {
    const int cycle_to_clean = (next_cycle + 1) % KAAPI_BAR_CYCLES;
    kaapi_atomic_t* counter_to_clean = (kaapi_atomic_t *)&barrier->count[cycle_to_clean * KAAPI_CACHE_LINE_SIZE];
    KAAPI_ATOMIC_WRITE_BARRIER (counter_to_clean, 0);
    KAAPI_ATOMIC_WRITE( &barrier->cycle, next_cycle);
  }
  else
  {
    int err;
    struct _kaapi_barrier_cond_t cond = { next_cycle, barrier };

    while (KAAPI_ATOMIC_READ (&barrier->cycle) != next_cycle)
    {
#if defined(__MIC__) /* more sleep on MIC */
      for (int i=0; (i<1000) && (KAAPI_ATOMIC_READ (&barrier->cycle) != next_cycle); ++i)
        kaapi_slowdown_cpu();
#endif
      if ((flag & KAAPI_BARRIER_FLAG_NOSCHEDULE) == 0)
      {
        /* suspend until barrier is reached by anybody */
        err = ctxt->sched_idle( thread, _kaapi_condition_barrier_isready, &cond);
        kaapi_assert( (err == 0) || (err == EINTR) );
      }
      else
        kaapi_slowdown_cpu();
    }
  }

  /* stop thread on exit after the barrier: wait until master wakeup threads */
  if (flag & KAAPI_BARRIER_FLAG_WAITEXIT)
  {
     kaapi_assert_debug( ctxt->kid != 0);
#if HAVE_FUTEX
     int wc;
     int i;
     for (i=0; (i <30500) && (wait_cycle == (wc = KAAPI_ATOMIC_READ (&barrier->wait_cycle))); ++i)
     {
       kaapi_slowdown_cpu();
     }
     if (wait_cycle == wc)
     { 
       //printf("[%i] sleep...\n", kaapi_self_kid());
       sys_futex(&barrier->wait_cycle, FUTEX_WAIT_PRIVATE, wait_cycle, NULL, NULL, 0);
     }
#else
    while (wait_cycle == KAAPI_ATOMIC_READ (&barrier->wait_cycle))
      kaapi_slowdown_cpu();
#endif
  }
}
