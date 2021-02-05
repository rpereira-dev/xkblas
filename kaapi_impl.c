/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** fabien.lementec@imag.fr
** vincent.danjean@imag.fr
** jvlima@inf.ufsm.br
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

#include <string.h>
#include <stdio.h>

#include "kaapi_impl.h"
#include "kaapi_offload.h"

#include <stdint.h>
#if defined(HAVE_CLOCK_GETTIME) || defined(KMP_OS_LINUX)
# include <time.h>
#else
# include <sys/time.h>
#endif
#if defined(HAVE_CLOCK_GETTIME) || defined(KMP_OS_LINUX)
typedef struct timespec struct_time;
#  define gettime(t) clock_gettime( CLOCK_REALTIME, t)
#  define get_sub_second(t) (1e-9*(double)t.tv_nsec)
#  define get_sub_second_ns(t) ((uint64_t)t.tv_nsec)
#else
typedef struct timeval struct_time;
#  define gettime(t) gettimeofday( t, 0)
#  define get_sub_second(t) (1e-6*(double)t.tv_usec)
#  define get_sub_second_ns(t) (1000*(uint64_t)t.tv_usec)
#endif

/* */
typedef struct kaapi_counter_info {
  uint8_t     mask;
  const char* name;
  const char* unit;
  uint8_t     type;     /* 0: field counter, 1: field dcounter */
  int        (*cond)(); /* 0, non condition, else evaluate it and display if != 0 */
} kaapi_counter_info_t;

/* */
static kaapi_counter_info_t kaapi_name_counter[] = {
  {0, "#task spawn", 0, 0, 0},
  {0, "#task launch", 0, 0, 0},
  {1, "#task exec", 0, 0, 0},
  {1, "flops exec", 0, 1, 0},
#if KAAPI_ADVANCED_VERSION
  {1, "flops pending", 0, 1, 0},
#else
  {0, "flops pending", 0, 1, 0},
#endif
  {1, "GPU Work", "s", 1, 0},
  {0, "CPU Work", "s", 1, 0},
  {0, "CPU Work overhead", "s", 1, 0},
#if KAAPI_ADVANCED_VERSION
  {1, "#GPU no conflict calls","",0, 0},
  {1, "#GPU conflict calls","",0, 0},
  {1, "#GPU avrg/conflict",0,1, 0},
#else
  {0, "experimental (0)",0,0, 0},
  {0, "experimental (1)",0,0, 0},
  {0, "experimental (2)",0,1, 0},
#endif
  {1, "#Gemm on TC", 0, 0, 0},
  {1, "#Gemm not on TC", 0, 0, 0},
  {1, "Gemm flops on TC", 0, 1, 0},
  {1, "Gemm flops not on TC", 0, 1, 0},
  {0, "#ctxt suspended", 0, 0, 0},
  {0, "#steal ok", 0, 0, 0},
  {0, "#steal nok", 0, 0, 0},
  {1, "GPU alloc", "Bytes", 0, 0},
  {1, "GPU free", "Bytes", 0, 0},
  {1, "#H2D", 0, 0, 0},
  {1, "#D2H", 0, 0, 0},
  {1, "#D2D", 0, 0, 0},
  {1, "H2D size", "Bytes", 0, 0},
  {1, "D2H size", "Bytes", 0, 0},
  {1, "D2D size", "Bytes", 0, 0},
  {0, "#HIT", 0, 0, 0},
  {0, "#MISS", 0, 0, 0},
  {0, "Hit bytes", "Bytes", 0, 0},
  {0, "Miss bytes", "Bytes", 0, 0},
  {0, "Async Pin", "s", 1, 0},
  {0, "Async Unpin", "s", 1, 0},
  {0, "Async Wait pin", "s", 1, 0},
  {0, "Cuda Pin", "s", 1, 0},
  {0, "Cuda Unpin", "s", 1, 0},
  {0, "Overhead pin", "s", 1, 0},
#if KAAPI_PIPELINE_GPUTASK
  {1, "Reorder hit", 0, 0, 0},
  {1, "Reorder miss", 0, 0, 0},
  {1, "Reorder miss len", 0, 0, 0},
#else
  {0, "Reorder hit", 0, 0, 0},
  {0, "Reorder miss", 0, 0, 0},
  {0, "Reorder miss len", 0, 0, 0},
#endif
  {0, "", "", 0, 0}
};


/*
*/
int kaapi_counter_set_condition( int idx, int (*cond)() )
{
  if ((idx<0)||(idx >= KAAPI_CNT_MAX)) return EINVAL;
  kaapi_name_counter[idx].cond = cond; 
  return 0;
}

void* __kaapi_start_blocaddr(void* addr)
{
  return _kaapi_start_blocaddr(addr, void*);
}
void* __kaapi_end_blocaddr(void* addr)
{
  return _kaapi_end_blocaddr(addr, void*);
}
int __kaapi_addr_inbloc(void* bloc, void* addr)
{
  return _kaapi_addr_inbloc(bloc, addr);
}

int __kaapi_has_enough_dataspace( kaapi_thread_t* thread, size_t size)
{
  return _kaapi_has_enough_dataspace(thread, size);
}


/* Default value reset in kaapi_init.
   Warning to keep them coherent.
*/
kaapi_rtparam_t kaapi_default_param = {
  .stackblocsize         = KAAPI_STACKBLOCSIZE,
  .sys_ngpus             = (uint8_t)-1,
  .ngpus                 = (uint8_t)-1,
  .gpu_set               = ~0,
  .cuda_stream_capacity  = 64,
  .cuda_conc_d2h         = 1,   /* output: D2H */
  .cuda_conc_stream_kernel = 4,
  .cuda_conc_kernel      = 8,
  .cuda_conc_h2d         = 1,   /* output: H2D */
  .cuda_conc_d2d         = 1,   /* output: D2D */
  .cuda_cache_limit      = 0.98
};

kaapi_address_space_id_t kaapi_local_asid = 0;


#if KAAPI_USE_PERFCOUNTER
/*
*/
kaapi_stat_internal_t kaapi_perthread_stat[KAAPI_MAX_THREAD_COUNT];
kaapi_stat_internal_t kaapi_perthread_asyncpin[KAAPI_MAX_THREAD_CUDA_COUNT];
#endif

/*
*/
static void kaapi_usage(void)
{
  printf("KAAPI_HELP:\n");
  printf("KAAPI_NUM_GPUS:\n");
  printf("KAAPI_CUDA_STREAM_CAPACITY:\n");
  printf("KAAPI_CUDA_KERNEL_STREAM_NUMS:\n");
}


/*
*/
void kaapi_print_counter(void)
{
  FILE* file = stdout;

  for (int i=0; i< (int)KAAPI_CNT_MAX; ++i)
  {
    if (kaapi_name_counter[i].cond && (kaapi_name_counter[i].cond() ==0)) continue;
    if (kaapi_name_counter[i].type ==0)
    {
      uint64_t sum = 0;
      kaapi_stat_get_counter(i, &sum);
      if (kaapi_name_counter[i].mask)
        if (kaapi_name_counter[i].unit)
          fprintf(file, "\t%24s: %16lu (%s)\n", kaapi_name_counter[i].name, (unsigned long)sum, kaapi_name_counter[i].unit);
        else 
          fprintf(file, "\t%24s: %16lu\n", kaapi_name_counter[i].name, (unsigned long)sum);
    }
    else // if (kaapi_name_counter[i].type ==1)
    {
      double sum = 0;
      kaapi_stat_get_dcounter(i, &sum);
      if (kaapi_name_counter[i].mask)
        if (kaapi_name_counter[i].unit)
          fprintf(file, "\t%24s: %16.4e (%s)\n", kaapi_name_counter[i].name, sum, kaapi_name_counter[i].unit);
        else
          fprintf(file, "\t%24s: %16.4e\n", kaapi_name_counter[i].name, sum);
    }
  }
}

/*
*/
int kaapi_setup_param(void)
{
  if (getenv("KAAPI_HELP")) kaapi_usage();

  if (getenv("KAAPI_NUM_GPUS"))
  {
    int ngpus = atoi(getenv("KAAPI_NUM_GPUS"));
    kaapi_assert(ngpus >=0);
    kaapi_default_param.ngpus = ngpus;
  }
  // KAAPI_GPUSET is the set of GPU to used for running a process
  if (getenv("KAAPI_GPUSET"))
  {
    unsigned int gpuset = atoi(getenv("KAAPI_GPUSET"));
    if (__builtin_popcount(gpuset) < kaapi_default_param.ngpus)
      kaapi_default_param.ngpus = __builtin_popcount(gpuset);
    else if (kaapi_default_param.ngpus ==0)
      gpuset = 0; 
    else
    {
      /* take only the first ngpus bits to 1 in gpuset */
      int tmp = gpuset;
      int idx = 0;
      for (int i=0; i<kaapi_default_param.ngpus; ++i)
      {
        idx = __builtin_ffs((unsigned int)tmp);
        kaapi_assert( idx != 0);
        --idx;
        tmp &= ~(1<<idx);
      }
      /* here idx == index of the ngpus bits to 1 in gpuset */
      gpuset &= ((1<<(1+idx))-1);
    }
    kaapi_default_param.gpu_set = gpuset;
    kaapi_assert( __builtin_popcount(kaapi_default_param.gpu_set)  == kaapi_default_param.ngpus );
  }
  else
    kaapi_default_param.gpu_set = (1<<kaapi_default_param.ngpus)-1;


  if (getenv("KAAPI_CUDA_KERNEL_STREAM_NUMS"))
  {
    uint8_t cuda_conc_stream_kernel = (uint8_t )atoi(getenv("KAAPI_CUDA_KERNEL_STREAM_NUMS"));
    if (cuda_conc_stream_kernel  < 1) cuda_conc_stream_kernel = 1;
    kaapi_default_param.cuda_conc_stream_kernel = cuda_conc_stream_kernel;
  }

  if (getenv("KAAPI_CUDA_KERNEL_PER_STREAM"))
  {
    uint8_t cuda_conc_kernel = (uint8_t )atoi(getenv("KAAPI_CUDA_KERNEL_PER_STREAM"));
    if (cuda_conc_kernel  < 1) 
    {
      cuda_conc_kernel = 1;
    }
    kaapi_default_param.cuda_conc_kernel = cuda_conc_kernel;
  }

  if (getenv("KAAPI_CUDA_STREAM_CAPACITY"))
  {
    uint16_t cuda_stream_capacity = (uint16_t)atoi(getenv("KAAPI_CUDA_STREAM_CAPACITY"));
    if (cuda_stream_capacity < 16) cuda_stream_capacity = 16;
    kaapi_default_param.cuda_stream_capacity = cuda_stream_capacity;
  }

  if (getenv("KAAPI_CUDA_CACHE_LIMIT")) /* percent */
  {
    float cuda_cache_limit = atof(getenv("KAAPI_CUDA_CACHE_LIMIT"))/100.0;
    if (cuda_cache_limit > 1) cuda_cache_limit = 1.00;
    if (cuda_cache_limit < 0.01) cuda_cache_limit = 0.01;
    kaapi_default_param.cuda_cache_limit = cuda_cache_limit;
  }

  return 0;
}



/* ------------------------------------------------------------------------------------------- */
/** Returns time in second
*/
double kaapi_get_elapsedtime(void)
{
  struct_time st;
  int err = gettime(&st);
  if (err !=0) return 0;
  return (double)st.tv_sec + get_sub_second(st);
}

/** Returns time in nanosecond
*/
uint64_t kaapi_get_elapsedns(void)
{
  uint64_t retval;
  struct_time st;
  int err = gettime(&st);
  if (err != 0) return (uint64_t)0UL;
  retval = (uint64_t)st.tv_sec * 1000000000ULL;
  retval += get_sub_second_ns(st);
  return retval;
}


/*
*/
int kaapi_init(void)
{
  /* default value. Please keep coherence with finalize and static init until
     better solution is implemented
  */ 
  kaapi_default_param.stackblocsize = KAAPI_STACKBLOCSIZE;
  kaapi_default_param.ngpus                 = (uint8_t)-1;
  kaapi_default_param.gpu_set               = ~0;
  kaapi_default_param.cuda_stream_capacity  = 64;
  kaapi_default_param.cuda_conc_d2h         = 1;
  kaapi_default_param.cuda_conc_stream_kernel= 4;
  kaapi_default_param.cuda_conc_kernel      = 8;
  kaapi_default_param.cuda_conc_h2d         = 1;
  kaapi_default_param.cuda_conc_d2d         = 1;
  kaapi_default_param.cuda_cache_limit      = 0.98;

  /* set up runtime parameters */
  kaapi_assert( 0 == kaapi_setup_param() );

#if defined(KAAPI_DEBUG)
  /* dbg part */
  kaapi_assert(0 == kaapi_dbg_init());
#endif

  /* set up runtime parameters */
  kaapi_assert( 0 == kaapi_format_init() );

  /* task module */
  kaapi_taskmodule_init();

#if KAAPI_USE_PERFCOUNTER
  memset(kaapi_perthread_stat, 0, sizeof(kaapi_perthread_stat));
  memset(kaapi_perthread_asyncpin, 0, sizeof(kaapi_perthread_asyncpin));
#endif
  kaapi_assert( 0 == kaapi_dsm_init() );

#if KAAPI_USE_OFFLOAD
  /* initialize offload */
  kaapi_assert(0 == kaapi_offload_init(0));
  kaapi_assert(0 == kaapi_offload_start());
#endif

  /* here all locality domain have been initialized */
  kaapi_assert( 0 == kaapi_dsm_commit() );

  return 0;
}


/*
*/
int kaapi_finalize(void)
{
  int err;

  kaapi_taskmodule_finalize();

  kaapi_format_finalize();


#if KAAPI_USE_OFFLOAD
  err = kaapi_offload_finalize();
  if (err !=0) printf("***[%s] error %i in kaapi_offload_finalize\n", __func__, err);
#endif

  err = kaapi_dsm_finalize();
  if (err !=0) printf("***[%s] error %i in kaapi_dsm_finalize\n", __func__, err);

  kaapi_localitydomain_finalize();

#if defined(KAAPI_DEBUG)
  kaapi_dbg_finalize();
#endif

#if KAAPI_DEBUG && KAAPI_ADVANCED_VERSION && 0
  /* before print_counter */   
  for (int i=0; i<KAAPI_MAX_THREAD_COUNT; ++i)
  {
    if (kaapi_perthread_stat[i].counter[KAAPI_LOAD_COLLISION_COUNT] >0)
      kaapi_perthread_stat[i].dcounter[KAAPI_LOAD_COLLISION_GPU] /= (double)kaapi_perthread_stat[i].counter[KAAPI_LOAD_COLLISION_COUNT];
//printf("%i:: COLLISION COUNT: %li, COLLISION GPUs: %f\n", i, kaapi_perthread_stat[i].counter[KAAPI_LOAD_COLLISION_COUNT], kaapi_perthread_stat[i].dcounter[KAAPI_LOAD_COLLISION_GPU] );
  }
#endif

  return 0;
}

