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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>

#define CUDA_API_PER_THREAD_DEFAULT_STREAM  1
//#include <driver_types.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cublas_v2.h>

/* Set to 1 for using tensor core */
#define KAAPI_USE_TC 0

/* Unactivate GPU allocator: set to 0. 1 to enable it */
#define KAAPI_CUDA_CACHE 0

/* use peer communication */
#define CONFIG_USE_P2P  1

#define KAAPI_CUDA_USE_NVLINK_TOPO 1

/* for debuging: 0 device thread, 1 IO helper thread */
static __thread int thread_type = 0;


/* HWLOC (tested with 2.x) is used to get the cpuset where to bind
   device thread with high affinity (the cpuset of the package hwloc object)
   that contains the card.
   It does not seems to introduce or not gain on grunch machine (two fat memory hierarchy ?)
*/
#if KAAPI_USE_HWLOC
#include "hwloc.h"
#include "hwloc/cuda.h"
#include "hwloc/cudart.h"
#include "hwloc/glibc-sched.h"
#endif

#include "kaapi_impl.h"
#include "kaapi_offload.h"

/*
*/
#define _PLUGIN_NAME   "cuda"
#define _PLUGIN_DEBUG   0

#if KAAPI_USE_DYNLOADER
#  define KAAPI_CLASS_ENTRYPOINT
#else
#  define KAAPI_CLASS_ENTRYPOINT static
#endif

#include "kaapi_plugin.h"


/* to use event for synchronisation */
#define CONFIG_USE_EVENT 1

/* make all call synchronous -debug- */
#define CONFIG_SYNCHRONOUS 0

#if CONFIG_SYNCHRONOUS
#  define CONFIG_SYNCHRONOUS_COPY 1
#  define CONFIG_SYNCHRONOUS_KERNEL 1
#else 
#  define CONFIG_SYNCHRONOUS_COPY 0
#  define CONFIG_SYNCHRONOUS_KERNEL 0
#endif

// Debug: force redefinition
//#  undef CONFIG_SYNCHRONOUS_COPY
//#  define CONFIG_SYNCHRONOUS_COPY 1

//#  undef CONFIG_SYNCHRONOUS_KERNEL 
//#  define CONFIG_SYNCHRONOUS_KERNEL 0

/* counters */
enum {
  CUDA_CNT_H2D =0,
  CUDA_SIZE_H2D,
  CUDA_CNT_D2H,
  CUDA_SIZE_D2H,
  CUDA_CNT_D2D,
  CUDA_SIZE_D2D,
  CUDA_MAX_COUNTERS
};
#define COUNTER_CNT_H2D   device->counter[CUDA_CNT_H2D]
#define COUNTER_SIZE_H2D  device->counter[CUDA_SIZE_H2D]
#define COUNTER_CNT_D2H   device->counter[CUDA_CNT_D2H]
#define COUNTER_SIZE_D2H  device->counter[CUDA_SIZE_D2H]
#define COUNTER_CNT_D2D   device->counter[CUDA_CNT_D2D]
#define COUNTER_SIZE_D2D  device->counter[CUDA_SIZE_D2D]


#if KAAPI_CUDA_CACHE
typedef struct cuda_cache_blk cuda_cache_blk_t;
struct cuda_cache_blk {
  uintptr_t ptr;
  size_t size;
  cuda_cache_blk_t* next;
};

typedef struct cuda_cache cuda_cache_t;
struct cuda_cache {
  uintptr_t base;
  size_t size;
  cuda_cache_blk_t* freelist;
};
#endif

typedef struct {
  kaapi_device_t inherited;
  CUdevice       cu_device;
  CUcontext      ctx;
  uint64_t*      affinity; /* of size cuda_count_perfrank -1 */
  size_t         free_mem;
  size_t         size_alloc;
  size_t         size_free;
  size_t         mem_limit;

  /* device properties (from NVIDIA website) */
  struct {
    bool overlap;      /* if the device can concurrently copy memory between host and device while executing a kernel */
    bool integrated;   /* if the device is integrated with the memory subsystem */
    bool map;          /* if the device can map host memory into the CUDA address space */
    bool concurrent;   /* if the device supports executing multiple kernels within the same context simultaneously */
    int async_engines; /* Number of asynchronous engines */
    size_t mem_total;  /* total GPU memory size in bytes */
    char name[64];     /* GPU name */
  } prop;
#if KAAPI_HAVE_IO_THREADS
  pthread_t tidio[2];
#endif
#if KAAPI_CUDA_CACHE
  cuda_cache_t* cache;
#endif
  size_t counter[CUDA_MAX_COUNTERS];
} kaapi_device_cuda_t;

/* IO stream with specific field for CUDA
   If event is configured on, then one event is insert just after each asynchronous
   operations (memcpy, kernel launch). Then the runtime can wait or test specific event.
   Each Kaapi IOstream represents count CUDA stream. Currently only kernel stream may
   represents multiple CUDA streams. And because IOstream does not preserve order, the
   kernels are dispatch to a different stream at each time.

   Synchronizations between Kaapi IOstream uses Event.
 */
typedef struct kaapi_cuda_io_stream_t {
  kaapi_io_stream_t inherited;
  CUstream          stream;
  CUstream          stream_low;
  cublasHandle_t    handle;
#if CONFIG_USE_EVENT
  CUevent* end_events;               /* size: capacity */
#if KAAPI_USE_PERFCOUNTER
  CUevent* start_events;             /* size: capacity */
#endif
#endif
} kaapi_cuda_io_stream_t;

/* number of used device for this run */
static int kaapi_device_count = 0;

/* list of devices */
static kaapi_device_cuda_t** kaapi_device_list = 0;

/* array of the device id to used */
static int* kaapi_device_ids = 0;

static bool plugin_initialized = false;
static pthread_mutex_t kaapi_cuda_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t cuda_get_free_mem(kaapi_memory_device_t* dev);

/* Thread for registering array
*/
static void* kaapi_cuda_register_thread(void*);

#if KAAPI_USE_HWLOC
static hwloc_topology_t topology;
#endif


/*
*/
static void __cudaCheckError( CUresult err,  char *file, const int line )
{
    extern void kaapi_memory_cache_print_all(void);
    static char msg[256];
    const char* tmp ="";
    if ( CUDA_SUCCESS != err )
    {
      kaapi_assert_debug( CUDA_SUCCESS == cuGetErrorName( err, &tmp ));
      snprintf( msg, 256, "cuCheckError() error:%i, failed at %s:%i : %s\n",
                 err, file, line, tmp );
      kaapi_memory_cache_print_all();
      kaapi_abort( line, file, msg );
    }
    return;
}
#define CudaCheckError(cerr)    __cudaCheckError( cerr, __FILE__, __LINE__ )
#define CudaCheckErrorWithDump(cerr,instr)  if ((cerr) != CUDA_SUCCESS) { instr; __cudaCheckError( cerr, __FILE__, __LINE__ ); }



#if KAAPI_CUDA_USE_NVLINK_TOPO
/* cuda_perf_topo[device1,device2] returns the perfRank of the communication link between
   device.
   cuda_perf_device[d][i] for i=0,..,cuda_count_perfrank-1 is the mask of device
   for which the device d has link with performance i.
*/
static int cuda_device_count = 0;    
static int* cuda_perf_topo = 0;    
static int cuda_count_perfrank = 0;
static uint64_t* cuda_perf_device = 0;
static int* cuda_routing_table = 0;

/* buffer must be at least device_count */
static void _print_mask( char* buffer, ssize_t sz, uint64_t v )
{
  int device_count;
  for (int i=0; i<sz; ++i)
  {
     if ( v & (1ULL<<i)) buffer[sz-1-i]='1';
     else buffer[sz-1-i]='0';
  }
}


/* */
static void _kaapi_get_gpu_topo(void)
{
  CUdevice cu_device1, cu_device2;
  CUresult res;
  int min_perf= 0; /* min_perf >= max_perf */
  int max_perf= 0;
  int device_count;
  CudaCheckError(cuDeviceGetCount(&device_count));

  if (device_count ==0) return;
  cuda_device_count = device_count;
  cuda_perf_topo = (int*)malloc(sizeof(int)*device_count*device_count);

  // Enumerates Device <-> Device links and store perfRank
  for (int device1 = 0; device1 < device_count; device1++)
  {
    res = cuDeviceGet(&cu_device1, device1); 
    for (int device2 = 0; device2 < device_count; device2++)
    {
      if (device1 == device2) 
        cuda_perf_topo[device1*device_count+device2] = 0;
      else {
        res = cuDeviceGet(&cu_device2, device2);

        int perfRank = 0;
        int accessSupported = 0;

        CudaCheckError(
          cuDeviceGetP2PAttribute(&accessSupported, CU_DEVICE_P2P_ATTRIBUTE_ACCESS_SUPPORTED,
            cu_device1, cu_device2));
        if (accessSupported)
        {
          CudaCheckError(
            cuDeviceGetP2PAttribute(&perfRank, CU_DEVICE_P2P_ATTRIBUTE_PERFORMANCE_RANK,
              cu_device1, cu_device2));
          if (perfRank < max_perf)
            max_perf= perfRank;
          if (perfRank > min_perf)
            min_perf= perfRank;
          cuda_perf_topo[device1*device_count+device2] = 1+perfRank;
        }
        else
          cuda_perf_topo[device1*device_count+device2] = -1; /* should be higher than previous value: computed after */
      }
    }
  }

  /* number of performance links: max_perf-min_perf+3  
     - max_perf-min_perf+1 if GPUs peer access is enable
     - +1 if GPU peer access is not enable
     - +1 for local inter access
  */
  min_perf++;
  cuda_count_perfrank= min_perf-max_perf+2;
  int perfrank_nolink= min_perf-max_perf+1;
  int rank;
  for (int device = 0; device < device_count*device_count; device++)
    if (cuda_perf_topo[device] == -1) cuda_perf_topo[device] = min_perf+1;
  cuda_perf_device = malloc( sizeof(uint64_t)*device_count*cuda_count_perfrank);
  memset(cuda_perf_device, 0, sizeof(uint64_t)*cuda_count_perfrank*device_count );
  for (int device1 = 0; device1 < device_count; device1++)
  {
    for (int device2 = 0; device2 < device_count; device2++)
    {
      rank = cuda_perf_topo[device1*device_count+device2]; 
      kaapi_assert( 0<= device1*device_count+ rank );
      kaapi_assert( device1*cuda_count_perfrank+ rank <= device_count*cuda_count_perfrank);
      cuda_perf_device[device1*cuda_count_perfrank+ rank] |= (1UL<<device2);
//char buffer[device_count+1]; buffer[device_count] = 0;
//_print_mask( buffer, device_count, cuda_perf_device[device1*cuda_count_perfrank+ rank] );
//printf("Mask(%i, rank=%i) |= (1UL << %i) == %s\n", device1, rank, device2,buffer);
    }
  }

#if KAAPI_DEBUG
  if (getenv("KAAPI_VERBOSE"))
  {
    char buffer[device_count+1];
    buffer[device_count] = 0;
    printf("Connection between GPU, #perf rank: %i.\nLowest values means best performance interconnection.\n", cuda_count_perfrank );
    printf("Local performance rank:0, Link perf. perf rank from %i (max perf) to %i (perf)\n", max_perf+1, min_perf+1);
    printf("%10s:", "src\\dest");
    for (int device1 = 0; device1 < device_count; device1++)
      printf("      GPU%i", device1);
    printf("\n");

    for (int device1 = 0; device1 < device_count; device1++)
    {
      printf("      GPU%i:", device1 );
      for (int device2 = 0; device2 < device_count; device2++)
         printf("%10i", cuda_perf_topo[device1*device_count+device2]);
      printf("\n");
    }

    /* mask */
    printf("\nDevice performance rank mask\n");
    if (1)
    for (int device1 = 0; device1 < device_count; device1++)
    { 
      printf("GPU%i: ", device1);
      for (int rank = 0; rank < cuda_count_perfrank; ++rank)
      {
        _print_mask( buffer, device_count, cuda_perf_device[device1*cuda_count_perfrank+ rank] );
        printf(buffer);
        if (rank != cuda_count_perfrank-1) printf(", ");
      }
      printf("\n");
    } 
  } 
#endif
}
#endif



/*
*/
static inline void kaapi_cuda_plugin_lock(void)
{
  pthread_mutex_lock(&kaapi_cuda_lock);
}

/*
*/
static inline void kaapi_cuda_plugin_unlock(void)
{
  pthread_mutex_unlock(&kaapi_cuda_lock);
}

/*
*/
static inline bool kaapi_cuda_device_is_initialized(kaapi_device_cuda_t* dev)
{
  return dev->inherited.is_initialized;
}

/*
*/
static inline bool kaapi_cuda_check_address(uintptr_t ptr)
{
  CUresult res;
  CUdeviceptr pbase;
  size_t size;
  res = cuMemGetAddressRange( &pbase, &size, (CUdeviceptr)ptr );
  CudaCheckError(res);
  
  if(pbase != (CUdeviceptr)ptr)
      return false;
  
  return true;
}


#if KAAPI_CUDA_CACHE

#define PERCENTAGE    0.8
static void cuda_mem_cache_init(kaapi_device_cuda_t* dev)
{
  CUdeviceptr ptr;
  CUresult res;
  size_t size;
  
  size = (size_t)(dev->free_mem * PERCENTAGE);
  res = cuMemAlloc( &ptr, size );
  kaapi_assert(res != CUDA_ERROR_OUT_OF_MEMORY );
  CudaCheckError(res);

  kaapi_assert(ptr != 0);
  dev->cache = calloc(1, sizeof(cuda_cache_t));
  dev->cache->base = (uintptr_t)ptr;
  dev->cache->size = size;
  dev->cache->freelist = 0;
}

static void cuda_mem_cache_destroy(kaapi_device_cuda_t* dev)
{
  cuMemFree((CUdeviceptr)dev->cache->base);
  free(dev->cache);
}

static uintptr_t cuda_mem_alloc_cache(kaapi_memory_device_t* dev, size_t size)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  uintptr_t ptr;

  if (size > device->cache->size)
  {
    // try to recover previous memory block 
    if (device->cache->freelist==0)
      return 0; // no free block

    cuda_cache_blk_t* blk = device->cache->freelist;
    cuda_cache_blk_t* prev = 0;
    size_t size_infree=0;
    while (blk->size < size)
    {
      prev = blk;
      size_infree+= blk->size;
      blk = blk->next;
    }
    //kaapi_assert(blk != 0);
    if (blk == 0) return 0;

    // first
    if (prev == 0)
      device->cache->freelist = blk->next;
    else
      prev->next = blk->next;

    ptr = blk->ptr;
    free(blk);

 //   fprintf(stdout, "ERROR NO MEMORY %ld\n", size);
 //   fprintf(stdout, "CUDA dev %d MEM reuse %p %ld\n", device->inherited.device_id, (void*)ptr, size);
//    fflush(stdout);
    return ptr;
  }

  device->cache->size -= size;
  ptr = device->cache->base;
  device->cache->base += size;
  //printf("Alloc: %li\n",size);

  return ptr;
}

static void cuda_mem_free_cache(kaapi_memory_device_t* dev, uintptr_t ptr, size_t size)
{
//fprintf(stdout, "FREE %p %ld\n", (void*)ptr, size);
//fflush(stdout);
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  cuda_cache_blk_t* blk = malloc(sizeof(cuda_cache_blk_t));
  blk->ptr = ptr;
  blk->size = size;
  blk->next = 0;
  if (device->cache->freelist==0) {
    device->cache->freelist = blk;
  } else {
    blk->next = device->cache->freelist;
    device->cache->freelist = blk;
  }
  //printf("Fee: %li\n",size);
}
#endif // KAAPI_CUDA_CACHE


/*
*/
static uintptr_t cuda_alloc(kaapi_memory_device_t* dev, size_t size, int* flag)
{
  CUdeviceptr ptr;
  CUresult res;
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;

  /* here we limit the size of allocated memory for the cache system */
  if (((device->size_alloc - device->size_free) + size) > device->mem_limit)
  {
    if (flag) *flag = KAAPI_MEMORY_DEVICE_FLAG_FULL;
    return 0;
  }

  kaapi_assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

#if _PLUGIN_DEBUG
  CUcontext ctx;
  res = cuCtxGetCurrent( &ctx );
  assert( ctx == device->ctx );
#endif

  res = cuMemAlloc( &ptr, size );
  if (res == CUDA_ERROR_OUT_OF_MEMORY )
  {
    printf(" CUDA_ERROR_OUT_OF_MEMORY:: free %li, request: %li\n", cuda_get_free_mem(dev), size);
    if (flag) *flag = KAAPI_MEMORY_DEVICE_FLAG_FULL;
    kaapi_assert(0);
    return 0;
  }
  CudaCheckError(res);

  kaapi_assert(ptr != 0);
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: self:%p, tid:%i, alloc ptr=%p size=%ld\n", __FUNCTION__, pthread_self(), device->inherited.ctxt->tid, (void*)ptr, size);
#endif
  device->size_alloc += size;

  if (flag)
  {
    if ( 1.0*(device->size_alloc - device->size_free) / device->mem_limit >= 0.9)
      *flag = KAAPI_MEMORY_DEVICE_FLAG_MOSTLY_FULL;
  }
  return (uintptr_t)ptr;
}


/*
*/
static void cuda_free(kaapi_memory_device_t* dev, uintptr_t ptr, size_t size)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  CUresult res;

  kaapi_assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

#if _PLUGIN_DEBUG
  CUcontext ctx;
  res = cuCtxGetCurrent( &ctx );
  assert( ctx == device->ctx );
  kaapi_assert(kaapi_cuda_check_address(ptr) );
#endif

#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: free ptr=%p\n", __FUNCTION__, (void*)ptr);
  fflush(stdout);
#endif

  res = cuMemFree((CUdeviceptr)ptr);
  CudaCheckError(res);
  device->size_free += size;

#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: self:%p, tid:%i, free ptr=%p size=%ld\n", __FUNCTION__, pthread_self(), device->inherited.ctxt->tid, (void*)ptr, size);
#endif
}


/* Implementation of kaapi_memory_copy_async through f_copy interface of memory device.
    
*/
static int cuda_copy(
    kaapi_memory_device_t* dev,
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src,
    int flags,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  //kaapi_assert_debug( &device->inherited == kaapi_offload_self_device() );

#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif

#if _PLUGIN_DEBUG
  CUcontext ctx;
  CUresult res = cuCtxGetCurrent( &ctx );
  assert( res == CUDA_SUCCESS );
  assert( ctx == device->ctx );
#endif

  /* enforce method to only process H2D or D2D or D2H copy */
  kaapi_assert( (kaapi_memory_asid_get_arch(dest.asid) != KAAPI_PROC_TYPE_HOST)
            ||  (kaapi_memory_asid_get_arch(src.asid) != KAAPI_PROC_TYPE_HOST) );
  kaapi_assert( (dest.ptr != 0)
            &&  (src.ptr != 0) );

  kaapi_io_type_t io_type = 0;
  kaapi_io_stream_type_t tstream = 0;
  kaapi_io_copy_priority_t priority = KAAPI_IO_COPY_PRIORITY_NORMAL;

  if (flags == 0) /* low */
    priority = KAAPI_IO_COPY_PRIORITY_LOW;
  else if (flags == 2)
    priority = KAAPI_IO_COPY_PRIORITY_HIGH;
  /* else == normal */

  if ( (kaapi_memory_asid_get_arch(src.asid) == KAAPI_PROC_TYPE_HOST)
    && (kaapi_memory_asid_get_arch(dest.asid) != KAAPI_PROC_TYPE_HOST) )
  {
#if _PLUGIN_DEBUG
    kaapi_assert(kaapi_cuda_check_address(dest.ptr));
#endif
    io_type = KAAPI_IO_COPY_H2D;
    tstream = KAAPI_IO_STREAM_H2D;
#if KAAPI_USE_PERFCOUNTER
    ++kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYH2D];
    kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYH2D_BYTES] +=
      kaapi_memory_view_size( view_dest );
#endif
  }
  else if ( (kaapi_memory_asid_get_arch(src.asid) != KAAPI_PROC_TYPE_HOST)
      && (kaapi_memory_asid_get_arch(dest.asid) == KAAPI_PROC_TYPE_HOST) )
  {
#if _PLUGIN_DEBUG
    kaapi_assert(kaapi_cuda_check_address(src.ptr));
#endif
    io_type = KAAPI_IO_COPY_D2H;
    tstream = KAAPI_IO_STREAM_D2H;
#if KAAPI_USE_PERFCOUNTER
    ++kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYD2H];
    kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYD2H_BYTES] +=
      kaapi_memory_view_size( view_dest );
#endif
  }
  else if ( (kaapi_memory_asid_get_arch(src.asid) != KAAPI_PROC_TYPE_HOST)
      && (kaapi_memory_asid_get_arch(dest.asid) != KAAPI_PROC_TYPE_HOST) )
  {
#if _PLUGIN_DEBUG
    kaapi_assert(kaapi_cuda_check_address(src.ptr));
    kaapi_assert(kaapi_cuda_check_address(dest.ptr));
#endif 
    io_type = KAAPI_IO_COPY_D2D;
#if KAAPI_USE_STREAM_D2D
    tstream = KAAPI_IO_STREAM_D2D;
#else
    tstream = KAAPI_IO_STREAM_H2D;
#endif
# if KAAPI_USE_PERFCOUNTER
    ++kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYD2D];
    kaapi_perthread_stat[device->inherited.ctxt->tid].counter[KAAPI_CNT_CPYD2D_BYTES] +=
      kaapi_memory_view_size( view_dest );
# endif
  }

  /* verify iff all inputs are in local node */
  kaapi_assert_debug( device->inherited.stream.device == &device->inherited );

  kaapi_stream_insert_io_copy_inst(
      &device->inherited.stream,
      tstream,
      io_type,
      priority,
      kaapi_pointer2void(src), view_src, kaapi_memory_device_get(src.asid),
      kaapi_pointer2void(dest), view_dest, kaapi_memory_device_get(dest.asid),
      cbk, arg0, arg1, arg2
  );

  return EINPROGRESS;
}



/*
*/
static int cuda_memsync(kaapi_memory_device_t* dev, int begend)
{
#if _PLUGIN_DEBUG
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
#endif
  CUresult res;

#if _PLUGIN_DEBUG
process  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif

#if _PLUGIN_DEBUG
  CUcontext ctx;
  res = cuCtxGetCurrent( &ctx );
  assert( ctx == device->ctx );
#endif

  res = cuCtxSynchronize();
  CudaCheckError(res);
  return 0;
}



/*
*/
static size_t cuda_get_mem_info(kaapi_memory_device_t* dev, size_t* mem_total, size_t* mem_limit)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  if (mem_total) *mem_total = device->prop.mem_total;
  if (mem_limit) *mem_limit = device->mem_limit;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, device->inherited.device_id, (mem_total ==0 ? -1 : *mem_total), (mem_limit ==0 ? -1 : *mem_limit));
#endif
  return device->prop.mem_total;
}


/*
*/
static size_t cuda_get_free_mem(kaapi_memory_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  CUcontext ctx;
  CUresult res;
  res = cuCtxGetCurrent(&ctx);
  CudaCheckError(res);
  res = cuCtxSetCurrent(device->ctx);
  CudaCheckError(res);
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  size_t free;
  size_t total;
  res = cuMemGetInfo(&free, &total);
  CudaCheckError(res);

  device->free_mem = (size_t)free;
  res = cuCtxSetCurrent(ctx);
  CudaCheckError(res);
  return device->free_mem;
}


/*
*/
static void kaapi_cuda_init_cuda_stream(
    kaapi_cuda_io_stream_t* cios,
    int type,
    unsigned int capacity
)
{
  CUresult res;

#if CONFIG_USE_EVENT
  /* */
  for (int k=0; k<capacity; ++k)
  {
    /* TG NOTE: here if perfcounter is enable, then 2 events per communication may be inserted:
       the first that inserted just before the communication, the second just after.
       Using such insertions, it is able to compute (online) the bandwidth of communication.
       + enable timing
     */
#if KAAPI_USE_PERFCOUNTER
    res = cuEventCreate(&cios->end_events[k], CU_EVENT_DEFAULT);
    CudaCheckError(res);
    res = cuEventCreate(&cios->start_events[k], CU_EVENT_DEFAULT);
    CudaCheckError(res);
#else
    res = cuEventCreate(&cios->end_events[k], CU_EVENT_DISABLE_TIMING);
    CudaCheckError(res);
#endif
  }
#endif
  

  int leastPriority, greatestPriority;
  res = cuCtxGetStreamPriorityRange ( &leastPriority, &greatestPriority );
  CudaCheckError(res);

  res = cuStreamCreateWithPriority (&cios->stream, CU_STREAM_NON_BLOCKING, greatestPriority);
  CudaCheckError(res);
  /* used by prefetching operation */
  res = cuStreamCreateWithPriority (&cios->stream_low, CU_STREAM_NON_BLOCKING, leastPriority);
  CudaCheckError(res);

  if (type == KAAPI_IO_STREAM_KERN)
  {
    kaapi_assert_debug( thread_type == 0 );
    /*
     */
    cublasStatus_t cres = cublasCreate(&cios->handle);
    kaapi_assert(cres == CUBLAS_STATUS_SUCCESS);
    cres = cublasSetStream( cios->handle, cios->stream);
    kaapi_assert(cres == CUBLAS_STATUS_SUCCESS);

#if KAAPI_USE_TC
    /*
    */
// Not the default
//#warning "Compile with support for tensor core"
//    res = cublasSetMathMode(cios->handle, CUBLAS_TENSOR_OP_MATH);
//    kaapi_assert(cres == CUBLAS_STATUS_SUCCESS);
#endif
  }
  else
  {
#if KAAPI_HAVE_IO_THREADS
    kaapi_assert_debug( ((type == KAAPI_IO_STREAM_H2D) && (thread_type == 1)) 
                     || ((type == KAAPI_IO_STREAM_D2H) && (thread_type == 2)) );
#endif
    cios->handle = 0;
  }
}


/*
 */
static kaapi_io_stream_t* cuda_stream_alloc(
    kaapi_device_t* dev,
    int type,
    unsigned int capacity
)
{
  CUresult res;
  kaapi_assert_debug(plugin_initialized == true);
  kaapi_assert_debug((dev->device_id >= 0) && (dev->device_id < kaapi_device_count) );


  kaapi_cuda_io_stream_t* cios = (kaapi_cuda_io_stream_t*)malloc(sizeof(kaapi_cuda_io_stream_t));
  if (cios ==0)
    return 0;

#if CONFIG_USE_EVENT
  cios->end_events = (CUevent*)malloc( capacity * sizeof(CUevent) );
  if (cios->end_events ==0)
  {
    free(cios);
    return 0;
  }
#if KAAPI_USE_PERFCOUNTER
  cios->start_events = (CUevent*)malloc( capacity * sizeof(CUevent) );
  if (cios->start_events ==0)
  {
    free(cios->end_events);
    free(cios);
    return 0;
  }
#endif
#endif

  cios->stream = 0;
  cios->stream_low = 0;
#if KAAPI_HAVE_IO_THREADS
  /* do not initialize kernel streams if io threads are spawned */
  if (type != KAAPI_IO_STREAM_KERN) return &cios->inherited;
#endif
  kaapi_cuda_init_cuda_stream( cios, type, capacity );
  return &cios->inherited;
}

/*
 */
static void cuda_stream_free(
    kaapi_device_t* dev,
    kaapi_io_stream_t* ios
)
{
  kaapi_cuda_io_stream_t* cios = (kaapi_cuda_io_stream_t*)ios;
  if (cios->handle)
    cublasDestroy(cios->handle);

#if CONFIG_USE_EVENT
  free(cios->end_events);
#if KAAPI_USE_PERFCOUNTER
  free(cios->start_events);
#endif
#endif
  cuStreamDestroy(cios->stream);
  cuStreamDestroy(cios->stream_low);
  free(cios);
}


/*
*/
typedef enum host_register_request_op {
  DEVICE_REGISTER_REQUEST,
  DEVICE_UNREGISTER_REQUEST
} host_register_request_op_t;

/*
*/
typedef enum register_state {
  REQUEST_INIT,
  REQUEST_POST,
  REQUEST_WAIT,
  REQUEST_DONE
} request_state_t;

/* Request for asynchronous memory pinning operation.
   Store request for pinning memory region (ptr,size).
   Once the operation complets, call the callback
   cbk(status, arg0, arg1, arg2), if cbk is not null.
   The caller thread waiting on the request lock the request' mutex
   and wait on the request condition.
*/
typedef struct host_register_request {
  host_register_request_op_t op;
  request_state_t            state;
  int                        err;    /* error code if any */
  pthread_mutex_t            lock;
  pthread_cond_t             cond;
  void*                      ptr;
  size_t                     size;
#if KAAPI_USE_PERFCOUNTER
  double                     t0;    /* time to the post op */
#endif
  kaapi_io_cbk_fnc_t cbk;
  void* arg0; void* arg1; void* arg2;
} host_register_request_t;


typedef struct host_register_queue {
  #define KAAPI_MAX_REGLIST 256
  pthread_t         thread;
  volatile uint64_t posw;  /* index of the next request to store */
  volatile uint64_t posr;  /* index of the next request to read */
  pthread_mutex_t   lock;
  pthread_cond_t    cond;
  pthread_cond_t    cond_waitall;  /* broadcasted when request is processed */
  host_register_request_t req[KAAPI_MAX_REGLIST];
} host_register_queue_t __attribute__ ((aligned (KAAPI_CACHE_LINE_SIZE)));


/* array of lists of requests */
static int all_rrl_size = 0;
static host_register_queue_t* all_rrl = 0;

static void kaapi_cuda_init_reqreg_list( host_register_queue_t* rrl )
{
  rrl->posw = 0;
  rrl->posr = 0;
  kaapi_assert(0 == pthread_mutex_init(&rrl->lock, 0));
  kaapi_assert(0 == pthread_cond_init(&rrl->cond, 0));
  kaapi_assert(0 == pthread_cond_init(&rrl->cond_waitall, 0));
  memset(rrl->req, 0, sizeof(rrl->req));
  for (int i=0; i<KAAPI_MAX_REGLIST; ++i)
  {
    kaapi_assert(0 == pthread_mutex_init(&rrl->req[i].lock, 0));
    kaapi_assert(0 == pthread_cond_init(&rrl->req[i].cond, 0));
    rrl->req[i].state = REQUEST_INIT;
  }
};

/* daemon thread, one per queue
*/
void* kaapi_cuda_register_thread(void* dummy )
{
  int tid = (int)(uintptr_t)dummy;
  kaapi_assert( tid < KAAPI_MAX_THREAD_CUDA_COUNT );
  host_register_queue_t* rrl = &all_rrl[tid];

  kaapi_assert( 0 == pthread_mutex_lock(&rrl->lock) );
  while (1)
  {
    /* wait for request until plugin is finished */
    while (plugin_initialized && (rrl->posw == rrl->posr))
      kaapi_assert(0 == pthread_cond_wait(&rrl->cond, &rrl->lock));

    /* yeh, one request ? */
    if (rrl->posw > rrl->posr )
    {
      uint64_t index = rrl->posr % KAAPI_MAX_REGLIST;

      /* recopy request */
      host_register_request_t req = rrl->req[index];
      req.err = 0;

      kaapi_assert(0 == pthread_mutex_unlock(&rrl->lock));
#if KAAPI_USE_PERFCOUNTER
      double t0p = kaapi_get_elapsedtime();
#endif
      cudaError_t err = cudaSuccess;
      if (req.size >0)
      {
        if (req.op == DEVICE_REGISTER_REQUEST)
        {
          err = cudaHostRegister(req.ptr, req.size, cudaHostRegisterPortable);
          if (!( (cudaSuccess == err) || (cudaErrorHostMemoryAlreadyRegistered == err)))
          //CUresult err = cuMemHostRegister( ptr, size, CU_MEMHOSTREGISTER_PORTABLE );
          //if ((err != CUDA_SUCCESS) && (err != CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED))
          {
            printf("***[%s]: cudaHostRegister error: %i\n", __func__, err);
            req.err = EALREADY;
          }
        }
        else if (req.op == DEVICE_UNREGISTER_REQUEST)
        {
          err = cudaHostUnregister(req.ptr);
          if (!( (cudaSuccess == err) || (cudaErrorHostMemoryNotRegistered == err)))
          {
            printf("***[%s]: cudaHostUnregister error: %i\n", __func__, err);
            req.err = EALREADY;
          }
        }
      }
#if KAAPI_USE_PERFCOUNTER
      double t1 = kaapi_get_elapsedtime();
#endif

      if (req.cbk !=0)
      {
        kaapi_io_status_t ios = {0, req.err };
        req.cbk(ios, 0, req.arg0, req.arg1, req.arg2);
      }
#if KAAPI_USE_PERFCOUNTER
      double t1p = kaapi_get_elapsedtime();

      //TODO: toverhead += (t0p-rrl->req[index].t0) + (t1p-t1);
      if (req.op == DEVICE_REGISTER_REQUEST)
        kaapi_perthread_asyncpin[tid].dcounter[KAAPI_TIME_OS_PIN] += t1-t0p;
      else
        kaapi_perthread_asyncpin[tid].dcounter[KAAPI_TIME_OS_UNPIN] += t1-t0p;
      kaapi_perthread_asyncpin[tid].dcounter[KAAPI_TIME_OVERHEAD_PIN]
          += (t0p-req.t0) + (t1p-t1);
#endif

      kaapi_assert(0 == pthread_mutex_lock(&rrl->lock));

      /* request lock */
      kaapi_assert(0 == pthread_mutex_lock(&rrl->req[index].lock));
      request_state_t state = rrl->req[index].state;
      rrl->req[index].err = req.err;
      rrl->req[index].state = REQUEST_DONE;
      if (state == REQUEST_WAIT)
        kaapi_assert(0 == pthread_cond_signal( &rrl->req[index].cond ));
      kaapi_assert(0 == pthread_mutex_unlock( &rrl->req[index].lock));
      kaapi_assert_debug( rrl->posr % KAAPI_MAX_REGLIST == index );
      ++rrl->posr;
      /* always broadcast to waiters on all requests: may be optimized */
      kaapi_assert(0 == pthread_cond_broadcast( &rrl->cond_waitall ));
    }
    /* if queue empty and plugin dinitialized then exit */
    else if (!plugin_initialized) break;
  }
  kaapi_assert(0 == pthread_mutex_unlock(&rrl->lock));
  return 0;
}



static char* name_io[] = {
  "IO_NOP",
  "IO_BEGIN",
  "IO_END",
  "IO_COPY_H2H",
  "IO_COPY_H2D",
  "IO_COPY_D2H",
  "IO_COPY_D2D",
  "IO_BARRIER",
  "IO_KERN"
};


/*
 */
static int cuda_stream_decode_ioinstruction(
    kaapi_device_t* dev,
    kaapi_io_stream_t* ios,
    kaapi_io_instruction_t* instr
)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;

  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_MSG("%s: instr '%s'\n", __FUNCTION__, name_io[instr->type]);

  cudaError_t res = CUDA_SUCCESS;
  kaapi_cuda_io_stream_t* cios = (kaapi_cuda_io_stream_t*)ios;
  CUstream* stream = 0;

  switch (instr->type)
  {
    case KAAPI_IO_NOP:
    case KAAPI_IO_BEGIN:
    case KAAPI_IO_END:
      return 0;

    case KAAPI_IO_COPY_H2H:
    case KAAPI_IO_COPY_H2D:
    case KAAPI_IO_COPY_D2H:
    case KAAPI_IO_COPY_D2D:
    {
#if KAAPI_HAVE_IO_THREADS
      kaapi_assert_debug( (thread_type == 1) || (thread_type == 2) );
#endif

      if (instr->type == KAAPI_IO_COPY_D2D)
        stream = &cios->stream_low;
      else
        stream = &cios->stream;
      kaapi_assert_debug(*stream !=0);

#if 0
      /* todo in an efficient way: implicit synchro between host_register_async and here */
      /* wait end of pining operation if any */
      while (rrl->posw != rrl->reg_sig)
        kaapi_slowdown_cpu();
#endif

#if CONFIG_USE_EVENT && KAAPI_USE_PERFCOUNTER
      instr->t1 = kaapi_get_elapsedtime();
      res = cuEventRecord(cios->start_events[ ios->pos_wp % ios->count ], *stream );
      kaapi_assert(res == CUDA_SUCCESS);
      //printf("Start event recorded for IO %s at pos: %i\n", name_io[instr->type], ios->pos_wp );
#endif

      struct kaapi_io_copy* op = &instr->inst.c_io;

      /* switch among view_src type (1D, 2D or 3D).
         May be some redistribution may be implemented here ?
       */
      size_t size = kaapi_memory_view_size(op->view_src);
      kaapi_assert( size == kaapi_memory_view_size(op->view_dest));
      kaapi_assert_debug( size == kaapi_memory_view_size(op->view_dest));
      uint8_t type = op->view_src->type;
      kaapi_assert_debug( type == op->view_dest->type);
      uint8_t storage = op->view_src->storage;
      kaapi_assert_debug( storage == op->view_dest->storage);
      int test = kaapi_memory_view_iscontiguous(op->view_src) && kaapi_memory_view_iscontiguous(op->view_dest);

      if (test)
      {
        type = KAAPI_MEMORY_VIEW_1D;
      }
      kaapi_assert_debug( test || (instr->type != KAAPI_IO_COPY_D2D) );

      void* src  = kaapi_memory_view2pointer((void*)op->src, op->view_src);
      void* dest = kaapi_memory_view2pointer((void*)op->dest, op->view_dest);
      
      switch (type)
      {
        case KAAPI_MEMORY_VIEW_1D:
        {
          KAAPI_PLUGIN_TRACE_MSG("%s: instr '%s' 1D data\n", __FUNCTION__, name_io[instr->type]);
          //printf("%f: instr '%s' 1D data\n", kaapi_get_elapsedtime(), name_io[instr->type]);
          switch (instr->type)
          {
            case KAAPI_IO_COPY_H2H:
              memcpy( dest, src, size );
              res = 0;
            break;
            case KAAPI_IO_COPY_H2D:
              res = cuMemcpyHtoDAsync( (CUdeviceptr)dest,
                                       src,
                                       size,
                                       *stream);
              COUNTER_CNT_H2D++;
              COUNTER_SIZE_H2D+= size;
            break;
            case KAAPI_IO_COPY_D2H:
              res = cuMemcpyDtoHAsync( dest,
                                       (CUdeviceptr)src,
                                       size,
                                       *stream);
              COUNTER_CNT_D2H++;
              COUNTER_SIZE_D2H+= size;
            break;

            case KAAPI_IO_COPY_D2D:
              res = cuMemcpyPeerAsync( (CUdeviceptr)dest,
                                       ((kaapi_device_cuda_t*)op->dev_dest->device)->ctx,
                                       (CUdeviceptr)src,
                                       ((kaapi_device_cuda_t*)op->dev_src->device)->ctx,
                                       size,
                                       *stream);
              COUNTER_CNT_D2D++;
              COUNTER_SIZE_D2D+= size;
            break;
            default:
              kaapi_assert_debug(0);
          };
          CudaCheckError(res);
        } break;

        case KAAPI_MEMORY_VIEW_2D:
        {
          CUDA_MEMCPY2D pcopy;
          pcopy.dstXInBytes = 0; /* no offset */
          pcopy.dstY        = 0;
          pcopy.dstPitch    = op->view_dest->ld * op->view_dest->wordsize;

          pcopy.srcXInBytes = 0;  /* */
          pcopy.srcY        = 0;
          pcopy.srcPitch    = op->view_src->ld * op->view_src->wordsize;
          switch (instr->type)
          {
            case KAAPI_IO_COPY_H2H:
              pcopy.srcMemoryType = CU_MEMORYTYPE_HOST;
              pcopy.srcHost       = src;
              pcopy.dstMemoryType = CU_MEMORYTYPE_HOST;
              pcopy.dstHost       = dest;
            break;
            case KAAPI_IO_COPY_H2D:
              pcopy.srcMemoryType = CU_MEMORYTYPE_HOST;
              pcopy.srcHost       = src;
              pcopy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
              pcopy.dstDevice     = (CUdeviceptr)dest;
              COUNTER_CNT_H2D++;
              COUNTER_SIZE_H2D   += size;
            break;
            case KAAPI_IO_COPY_D2H:
              pcopy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
              pcopy.srcDevice     = (CUdeviceptr)src;
              pcopy.dstMemoryType = CU_MEMORYTYPE_HOST;
              pcopy.dstHost       = dest;
              COUNTER_CNT_D2H++;
              COUNTER_SIZE_D2H   += size;
            break;
            case KAAPI_IO_COPY_D2D:
              pcopy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
              pcopy.srcDevice     = (CUdeviceptr)src;
              pcopy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
              pcopy.dstDevice     = (CUdeviceptr)dest;
              COUNTER_CNT_D2D++;
              COUNTER_SIZE_D2D   += size;
            break;
            default:
              kaapi_assert(0);
          };
          if (storage == KAAPI_MEMORY_STORAGE_ROWMAJOR)
          {
            pcopy.WidthInBytes = op->view_dest->size[1] * op->view_dest->wordsize;
            pcopy.Height       = op->view_dest->size[0];
          }
          else if (storage == KAAPI_MEMORY_STORAGE_COLMAJOR)
          {
            pcopy.WidthInBytes = op->view_dest->size[0] * op->view_dest->wordsize;
            pcopy.Height       = op->view_dest->size[1];
          } else {
            kaapi_abort( __LINE__, __FILE__, "Invalid storage");
          }

          res = cuMemcpy2DAsync( &pcopy, *stream );
          CudaCheckErrorWithDump(res, 
            printf("%p:: tid: %i,  type: %s, %p -> %p, size:%i\n", 
               pthread_self(), device->inherited.ctxt->tid, name_io[instr->type], 
               src, dest, (int)size
            )
          );
        } break;

        case KAAPI_MEMORY_VIEW_3D:
        default:
          kaapi_assert(false); 
          break;
      };

#if CONFIG_SYNCHRONOUS_COPY
      res = cuStreamSynchronize( *stream );
      CudaCheckError(res);
      ++ios->ok_p;
#elif CONFIG_USE_EVENT 
      res = cuEventRecord( cios->end_events[ ios->pos_wp % ios->count ], *stream );
      CudaCheckError(res);
#else // no use event, no synchronous == synchronous
      #error "Unsupported configuration"
#endif
      KAAPI_PLUGIN_TRACE_MSG("%s: stream %p instr '%s' src:%p, dest:%p size:%zu\n", __FUNCTION__,
          (void*)*stream,
          name_io[instr->type],
          (void*)src,
          (void*)dest,
          size
      );
    } break;

    case KAAPI_IO_BARRIER:
      res = cuStreamSynchronize( cios->stream );
      kaapi_assert(res == CUDA_SUCCESS);
      res = cuStreamSynchronize( cios->stream_low );
      kaapi_assert(res == CUDA_SUCCESS);
      ++ios->ok_p;
      break;

    case KAAPI_IO_KERN:
    {
#if KAAPI_HAVE_IO_THREADS
      kaapi_assert_debug( thread_type == 0 );
#endif
      /* same as cublas */
      stream = &cios->stream;
      struct kaapi_io_kernel* op = &instr->inst.k_io;
      KAAPI_PLUGIN_TRACE_MSG("%s: instr '%s' exec task:%p, stream: %p\n", __FUNCTION__,
          name_io[instr->type],
          op->task,
          (void*)*stream
      );
#if KAAPI_USE_PERFCOUNTER
      instr->t1 = kaapi_get_elapsedtime();
#if CONFIG_USE_EVENT
      res = cuEventRecord(cios->start_events[ ios->pos_wp % ios->count ], *stream );
      kaapi_assert(res == CUDA_SUCCESS);
#endif
#endif
      kaapi_offload_device_execute_task(
        &device->inherited,
        op->task,
        cios->handle
      );
#if CONFIG_SYNCHRONOUS_KERNEL
      res = cuStreamSynchronize( *stream );
      kaapi_assert(res == CUDA_SUCCESS);
      ++ios->ok_p;
#elif CONFIG_USE_EVENT 
      res = cuEventRecord(cios->end_events[ ios->pos_wp % ios->count ], *stream );
      kaapi_assert(res == CUDA_SUCCESS);
#else // no use event, no synchronous
      #error "Unsupported configuration"
#endif
    }
  }

  KAAPI_PLUGIN_TRACE_OUT
  return EINPROGRESS;
}


/* The purpose of this function is to increase ios->ok_p
   but whithout calling callback
 */
static int cuda_stream_advance_pending(
    kaapi_device_t* device,
    kaapi_io_stream_t* ios,
    int blocking
)
{
  KAAPI_PLUGIN_TRACE_IN

  CUresult res;
  kaapi_cuda_io_stream_t* cios = (kaapi_cuda_io_stream_t*)ios;
  if (kaapi_io_stream_emptypending(ios))
    return 0;

#if CONFIG_USE_EVENT 
  if (blocking)
  {
    res = cuStreamSynchronize( cios->stream );
    CudaCheckError(res);
    res = cuStreamSynchronize( cios->stream_low );
    CudaCheckError(res);
    ios->ok_p = ios->pos_wp;
    return 0;
  }

  size_t len_p = kaapi_io_stream_sizepending(ios);

#if 0 // best
  int queue_max[4];
  queue_max[KAAPI_IO_STREAM_H2D]  = 9; 
  queue_max[KAAPI_IO_STREAM_KERN] = 2; //kaapi_default_param.cuda_conc_kernel; 
  queue_max[KAAPI_IO_STREAM_D2H]  = 6; 
#if KAAPI_USE_STREAM_D2D
  queue_max[KAAPI_IO_STREAM_D2D]  = 4; 
#endif
  if ((len_p >1) && (len_p> queue_max[ios->type]))
  {
    int shift = 0; //(ios->type == KAAPI_IO_STREAM_KERN ? 0: len_p/2-1);
    int idx = (ios->ok_p + shift)% ios->count;
    res = cuEventSynchronize(cios->end_events[idx]);
  }
#endif
  /* ios->ok_p is past the last ok pending request: test from ok_p to pos_wp */
  int cnt;
  uint64_t ios_okp = ios->ok_p; 
  int prev_iosokp = ios_okp-1;
  while (ios_okp < ios->pos_wp)
  {
    int idx = ios_okp % ios->count;
    kaapi_io_instruction_t* op = &ios->pending[idx];
    res = CUDA_SUCCESS;
    switch (op->type)
    {
      case KAAPI_IO_KERN:
      case KAAPI_IO_COPY_H2H:
      case KAAPI_IO_COPY_H2D:
      case KAAPI_IO_COPY_D2H:
      case KAAPI_IO_COPY_D2D:
        for (int cnt=0; cnt<5; ++cnt)
        {
          res = cuEventQuery( cios->end_events[idx] );
          kaapi_assert_debug((res == CUDA_ERROR_NOT_READY)  || (res == CUDA_SUCCESS));
          if (res == CUDA_ERROR_NOT_READY) 
            pthread_yield();
          else
            if (prev_iosokp+1 == ios_okp) ++prev_iosokp;
        }
  
#if KAAPI_USE_PERFCOUNTER
        if (res == CUDA_SUCCESS)
          op->t2 = kaapi_get_elapsedtime();
#endif

#if LOG_DBG
        printf("%s:: instruction pos:%i, instr '%s' ok\n", __func__, ios_okp,  name_io[op->type]);
#endif

      case KAAPI_IO_END:
      case KAAPI_IO_BARRIER:
      case KAAPI_IO_NOP:
        ++ios_okp;
        break;

      default:
        fprintf(stderr, "%i:: bad instruction type at pos:%li\n", device->ld->idx, ios_okp);
        kaapi_assert(0);
        break;
    }
  }
  /* all events have been tested, test the prev_iosokp has been incremented */
  ios_okp = ios->ok_p;
  if (prev_iosokp != ios_okp-1) 
  {
    ios->ok_p = prev_iosokp+1;
    return 0;
  }
  return EINPROGRESS;

#elif CONFIG_SYNCHRONOUS_COPY && CONFIG_SYNCHRONOUS_KERNEL
  /* if synchronous call then advance automatically at the end of the operation */ 
  ;
#else
  /* if do not use event */
  if (blocking)
  {
    res = cuStreamSynchronize( cios->stream );
    CudaCheckError(res);
    res = cuStreamSynchronize( cios->stream_low );
    CudaCheckError(res);
    ios->ok_p = ios->pos_wp;
    return 0;
  }
  else
  {
    res = cuStreamQuery( cios->stream );
    if (res == CUDA_ERROR_NOT_READY)
    {
      KAAPI_PLUGIN_TRACE_OUT
      return EINPROGRESS;
    }
    CudaCheckError(res);
    res = cuStreamQuery( cios->stream_low );
    if (res == CUDA_ERROR_NOT_READY)
    {
      KAAPI_PLUGIN_TRACE_OUT
      return EINPROGRESS;
    }
    CudaCheckError(res);

    ios->ok_p = ios->pos_wp;
  }
#endif
  KAAPI_PLUGIN_TRACE_OUT
}



/*
 */
static int cuda_stream_process_pending(
    kaapi_device_t* device,
    kaapi_io_stream_t* ios,
    int blocking
)
{
  kaapi_cuda_io_stream_t* cios = (kaapi_cuda_io_stream_t*)ios;

  do {
    cuda_stream_advance_pending(device, ios, blocking );

    KAAPI_PLUGIN_TRACE_IN

    kaapi_io_status_t status = {0,0};

    /* call callback functions */
    for (uint64_t pos = ios->pos_rp; pos<ios->ok_p; ++pos)
    {
      int idx = pos % ios->count;
      kaapi_io_instruction_t* op = &ios->pending[idx];
      switch (op->type)
      {
        case KAAPI_IO_KERN:
        case KAAPI_IO_COPY_H2H:
        case KAAPI_IO_COPY_H2D:
        case KAAPI_IO_COPY_D2H:
        case KAAPI_IO_COPY_D2D:
        case KAAPI_IO_END:
        case KAAPI_IO_BARRIER:
        {
#if KAAPI_USE_PERFCOUNTER
          CUresult res;
#if KAAPI_DEBUG
          CUresult res_dbg;
          res_dbg = cuEventQuery( cios->start_events[idx] );
          if (res_dbg != CUDA_SUCCESS)
            printf("   invalid start_event state at: %lu \n", idx );
#endif
          res = cuEventElapsedTime ( &status.gpu_delay, cios->start_events[idx], cios->end_events[idx] );
          if (res != CUDA_SUCCESS) {
            printf("   invalid Cuda event state at: %lu non fifo order ?\n", idx );
            status.gpu_delay = 0;
            kaapi_assert(0);
          }
          status.gpu_delay *= 1e-3;
          status.cpu_delay = op->t2-op->t1; 
#endif
          if (op->inst.cbk.fnc)
            op->inst.cbk.fnc(status, ios, op->inst.cbk.arg[0], op->inst.cbk.arg[1], op->inst.cbk.arg[2]);
  
#if KAAPI_USE_PERFCOUNTER
          if (op->inst.cbk.fnc) op->t3 = kaapi_get_elapsedtime();
          else op->t3 = op->t2;
          if (op->type == KAAPI_IO_KERN)
          {
            kaapi_context_t* ctxt = device->ctxt;
            kaapi_perthread_stat[ctxt->tid].dcounter[KAAPI_CNT_TASK_WORK_OVERHEAD_CPU]
                += (op->t1-op->t0)+(op->t3-op->t2);
          }
#endif
          op->type = KAAPI_IO_NOP;
        }
  
        case KAAPI_IO_NOP:
          /* commit index for the next event */
          ++ios->pos_rp;
          //return 0;
          break;
  
        default:
          fprintf(stderr, "%i:: bad instruction type at pos:%li\n", device->ld->idx, pos);
          kaapi_assert(0);
          break;
      }
    }
  } while (0); 
  KAAPI_PLUGIN_TRACE_OUT
  return 0;
}


/* Return the source device to send data to device in the destination memory 'dev'.
 */
static uint16_t cuda_get_source(
    kaapi_memory_device_t* dev,
    uint16_t lid0,
    KAAPI_MEMORY_VALUE_TYPE valid_bit,
    KAAPI_MEMORY_VALUE_TYPE xfer_bit
)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev->device;
  int device_id_dest = kaapi_device_ids[dev->device->device_id];
  uint16_t lid_dest = kaapi_memory_asid_get_lid(dev->asid);
  uint16_t lid_src;
  kaapi_assert_debug((valid_bit !=0) || (xfer_bit !=0));

  /* return the device with the higher affinity rank with lid_dest
     - does not consider the last performance rank
   */
#if 1 // original
  for (int rank = 0; rank < cuda_count_perfrank-1; ++rank)
  {
    if (valid_bit !=0)
      lid_src = KAAPI_MEMORY_FFS( valid_bit &device->affinity[rank] );
    else /* xfer !=0: pre-cond of the function */
      lid_src = KAAPI_MEMORY_FFS( xfer_bit &device->affinity[rank]);
    if (lid_src !=0)
    {
      --lid_src;
      kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
      return lid_src;
    }
  }
#else /* first return a valid data, else an xfer data */
  if (valid_bit !=0)
    for (int rank = 0; rank < cuda_count_perfrank-1; ++rank)
    {
      lid_src = KAAPI_MEMORY_FFS( valid_bit &device->affinity[rank] );
      if (lid_src !=0)
      {
        --lid_src;
        kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
        return lid_src;
      }
    }

  if (xfer_bit !=0)
    for (int rank = 0; rank < cuda_count_perfrank-1; ++rank)
    {
      lid_src = KAAPI_MEMORY_FFS( xfer_bit &device->affinity[rank]);
      if (lid_src !=0)
      {
        --lid_src;
        kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
        return lid_src;
      }
    }
#endif

  return (uint16_t)-1;
}


#if KAAPI_HAVE_IO_THREADS
static int kaapi_cuda_io_thread( kaapi_device_cuda_t* device, kaapi_io_stream_type_t type )
{
  CUresult res;
  CUcontext ctx;
  res = cuCtxGetCurrent(&ctx);
  if (res != CUDA_ERROR_INVALID_CONTEXT)
    cuCtxPopCurrent(&ctx);

  res = cuCtxCreate(&ctx, CU_CTX_SCHED_AUTO, device->cu_device);
  kaapi_assert( res == CUDA_SUCCESS );

  res = cuCtxSetCurrent( ctx );
  kaapi_assert( res == CUDA_SUCCESS );

  /* initialize all IO cuda stream within the current context */
  kaapi_offload_stream_t* const stream = &device->inherited.stream;
  for (unsigned int i=0; i< stream->count[type]; ++i)
    kaapi_cuda_init_cuda_stream( (kaapi_cuda_io_stream_t*)stream->ios[type][i], type, stream->ios[type][i]->count );

  /* infinite loop */
  while (!device->inherited.finalize)
  {
    int count = 0;
    kaapi_offload_stream_process_instruction( stream, type );
    for (unsigned int i=0; i< stream->count[type]; ++i)
    {
      uint64_t old_ok_p = stream->ios[type][i]->ok_p;
      cuda_stream_advance_pending( &device->inherited, stream->ios[type][i], 0 );
      if (old_ok_p != stream->ios[type][i]->ok_p) ++count;
    }
    if (count ==0) pthread_yield();
  }

  cuCtxDestroy(ctx);
}


static void* kaapi_cuda_H2D_io_thread( void* arg )
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)arg;
  /* for debug */
  thread_type = 1;

  kaapi_cuda_io_thread(device, KAAPI_IO_STREAM_H2D);
  return 0;
}

static void* kaapi_cuda_D2H_io_thread( void* arg )
{ 
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)arg;
  /* for debug */
  thread_type = 2;

  kaapi_cuda_io_thread(device, KAAPI_IO_STREAM_D2H);
  return 0;
}
#endif



/*
*/
KAAPI_CLASS_ENTRYPOINT const char *
KAAPI_PLUGIN_ENTRYPOINT(get_name)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return "cuda";
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int 
KAAPI_PLUGIN_ENTRYPOINT(get_flags)(void)
{
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int 
KAAPI_PLUGIN_ENTRYPOINT(get_type)(void)
{
  return KAAPI_PROC_TYPE_CUDA;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int 
KAAPI_PLUGIN_ENTRYPOINT(get_number)(void)
{
  assert(plugin_initialized == true);
  return kaapi_device_count;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int 
KAAPI_PLUGIN_ENTRYPOINT(init)(void)
{
  CUresult res;
  
  kaapi_cuda_plugin_lock();
  if(plugin_initialized == true){
      kaapi_cuda_plugin_unlock();
      return 0;
  }
  
  res = cuInit(0);
  CudaCheckError(res);

  int device_count;
  res = cuDeviceGetCount(&device_count);
  CudaCheckError(res);

  kaapi_device_list = (kaapi_device_cuda_t**)calloc( device_count, sizeof(kaapi_device_cuda_t) );
  kaapi_default_param.sys_ngpus = device_count;

  if (kaapi_default_param.ngpus == (uint8_t)-1)
  {
    kaapi_default_param.ngpus = device_count;
    kaapi_default_param.gpu_set = (1<<device_count)-1;
  }

  /* bad number of GPUS ? */
  if (kaapi_default_param.ngpus > device_count)
  {
    printf("[%s] too many GPUs requested: %i. Use system default count: %i\n",__func__, kaapi_default_param.ngpus, device_count);
    kaapi_default_param.ngpus = device_count;
  }

  kaapi_device_count = kaapi_default_param.ngpus;
  kaapi_device_ids = (int*)malloc( kaapi_default_param.ngpus*sizeof(int) );
  int gpuset = kaapi_default_param.gpu_set;
  for (int i=0; i<kaapi_default_param.ngpus; ++i)
  {
    int idx = __builtin_ffs((unsigned int)gpuset);
    kaapi_assert( idx != 0);
    --idx;
    gpuset &= ~(1<<idx);
    kaapi_device_ids[i] = idx;
    //fprintf(stdout,"[%s] take GPU id:%2i= device_id:%i\n", __FUNCTION__, i, idx);
  }

  kaapi_device_count = kaapi_default_param.ngpus;
  plugin_initialized = true;
  kaapi_cuda_plugin_unlock();
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: cuda init with %d devices\n", __FUNCTION__, kaapi_device_count);
#endif

#if KAAPI_USE_HWLOC
  hwloc_topology_init(&topology);
  //hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_IMPORTANT);
  hwloc_topology_load(topology);
#endif

#if KAAPI_CUDA_USE_NVLINK_TOPO
  _kaapi_get_gpu_topo();
#endif

  int nrrl = getenv("KAAPI_RRL_SIZE") ? atoi(getenv("KAAPI_RRL_SIZE")) : 1;
  if (nrrl <=0) nrrl = 1;
  if (nrrl >= kaapi_default_param.ngpus) nrrl = kaapi_default_param.ngpus;
  //printf("*** NEW: #rrl=%i\n", nrrl);
  all_rrl_size =nrrl;
  all_rrl = (host_register_queue_t*)malloc(all_rrl_size* sizeof(host_register_queue_t));
  for (int i=0; i<nrrl; ++i)
  {
    kaapi_cuda_init_reqreg_list(&all_rrl[i]);
    int err = pthread_create(&all_rrl[i].thread, 0, kaapi_cuda_register_thread, (void*)(uintptr_t)i );
    kaapi_assert(err ==0);
  }

  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT void 
KAAPI_PLUGIN_ENTRYPOINT(finalize)(void)
{
  kaapi_cuda_plugin_lock();
  if (plugin_initialized == true)
  {
    plugin_initialized = false;
#if _PLUGIN_DEBUG
    fprintf(stdout, "cuda:%s: cuda finalize\n", __FUNCTION__);
#endif
  }
  kaapi_cuda_plugin_unlock();

  /* */
  void* tmp;
  for (int i=0; i<all_rrl_size; ++i)
  {
    kaapi_assert(0 == pthread_cond_signal( &all_rrl[i].cond ));
    kaapi_assert(0 == pthread_join( all_rrl[i].thread, &tmp ));
  }
  free( all_rrl );
  all_rrl_size = 0;
  all_rrl = 0;

  free(kaapi_device_ids);
  kaapi_device_ids = 0;

#if KAAPI_USE_HWLOC
  hwloc_topology_destroy(topology);
#endif

  free(  kaapi_device_list );
  kaapi_device_list = 0;
}


static uint64_t post_request(
    host_register_request_op_t op,
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  if (ptr ==0) return (uint64_t)-1;

  /* Hash function from self_thread to get its request register list */
  int hash = kaapi_hash_ulong( pthread_self() ) % all_rrl_size;
  host_register_queue_t* rrl = &all_rrl[hash];
  kaapi_assert(0 == pthread_mutex_lock( &rrl->lock ));
  while (rrl->posw - rrl->posr >= KAAPI_MAX_REGLIST)
    pthread_cond_wait( &rrl->cond_waitall, &rrl->lock );
  uint64_t index = rrl->posw;
  ++rrl->posw;
  int idx = index % KAAPI_MAX_REGLIST;
  rrl->req[idx].op   = op;
  rrl->req[idx].state= REQUEST_POST;
  rrl->req[idx].size = size;
  rrl->req[idx].cbk  = cbk;
  rrl->req[idx].arg0 = arg0;
  rrl->req[idx].arg1 = arg1;
  rrl->req[idx].arg2 = arg2;
  rrl->req[idx].ptr  = ptr;
#if KAAPI_USE_PERFCOUNTER
  rrl->req[idx].t0   = kaapi_get_elapsedtime();
#endif
  /* signal daemon thread */
  kaapi_assert(0 == pthread_cond_signal( &rrl->cond ));
  kaapi_assert(0 == pthread_mutex_unlock( &rrl->lock ));
  return index;
}

/*
*/
KAAPI_CLASS_ENTRYPOINT
uint64_t KAAPI_PLUGIN_ENTRYPOINT(host_register)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  if (ptr ==0) return (uint64_t)-1;
  return post_request( DEVICE_REGISTER_REQUEST, ptr, size, cbk, arg0, arg1, arg2 );
}

/*
*/
KAAPI_CLASS_ENTRYPOINT
uint64_t KAAPI_PLUGIN_ENTRYPOINT(host_unregister)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  if (ptr ==0) return (uint64_t)-1;
  return post_request( DEVICE_UNREGISTER_REQUEST, ptr, size, cbk, arg0, arg1, arg2 );
}


/*
*/
KAAPI_CLASS_ENTRYPOINT
int KAAPI_PLUGIN_ENTRYPOINT(host_register_testwait)(
    uint64_t index,
    int flag
)
{
  /* Hash function from the thread id to request register list */
  int hash = kaapi_hash_ulong( pthread_self() ) % all_rrl_size;
  host_register_queue_t* rrl = &all_rrl[hash];

  if ((flag != 2) && ((index == (uint64_t)-1) || (index >= rrl->posw)))
    return EINVAL;

  switch (flag) 
  {
    case 0: /* test: do not lock if test is true, else may it is a false negative */
      if (index <= rrl->posr) return 0;
      return EINPROGRESS;

    case 1: /* wait */
      /*TODO: cannot compute the time between T0 of the post_request and now if the index < rrl->posr
        because it means that requests have been proceed but we lost t0 stored in the request struct.
        One solution: sum t0, sum twait here and at the end returns sum twait - sum t0 as the sum of the delay.
        One case should be take into account: is post without corresponding wait=> each wait should account for all request between rrl->posr until the current index !.
        Other solution: keep the list of waiting requests index increment on wait_request.
      */
      kaapi_assert(0 == pthread_mutex_lock( &rrl->lock ));
      if (index >= rrl->posr)
      {
        int idx = index % KAAPI_MAX_REGLIST;
        kaapi_assert(0 == pthread_mutex_lock( &rrl->req[idx].lock ));
        kaapi_assert(0 == pthread_mutex_unlock( &rrl->lock ));
        kaapi_assert_debug((rrl->req[idx].state == REQUEST_POST) || (rrl->req[idx].state == REQUEST_DONE));
        if (rrl->req[idx].state == REQUEST_POST)
        {
          rrl->req[idx].state = REQUEST_WAIT;
          kaapi_assert(0 == pthread_cond_wait( &rrl->req[idx].cond, &rrl->req[idx].lock ));
          kaapi_assert_debug( rrl->req[idx].state == REQUEST_DONE );
          kaapi_assert(0 == pthread_mutex_unlock( &rrl->req[idx].lock ));
        }
        return 0;
      }
      else {
        kaapi_assert(0 == pthread_mutex_unlock( &rrl->lock ));
        return 0;
      }

    case 2: /* wait all */
    {
      kaapi_assert(0 == pthread_mutex_lock( &rrl->lock ));
      uint64_t pos = rrl->posw;
      while (pos > rrl->posr)
        kaapi_assert(0 == pthread_cond_wait( &rrl->cond_waitall, &rrl->lock ));
      kaapi_assert(0 == pthread_mutex_unlock( &rrl->lock ));
      return 0;
    }
    default: 
      return EINVAL;
  } 
  return 0;
}



/*
*/
KAAPI_CLASS_ENTRYPOINT kaapi_device_t* 
KAAPI_PLUGIN_ENTRYPOINT(device_create)(int dev)
{
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d init\n", __FUNCTION__, dev);
#endif
  kaapi_device_cuda_t* cudadevice = (kaapi_device_cuda_t*)malloc(sizeof(kaapi_device_cuda_t));
  memset(cudadevice, 0, sizeof(kaapi_device_cuda_t) );
  cudadevice->inherited.device_id = dev;
  return (kaapi_device_t*)cudadevice;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int 
KAAPI_PLUGIN_ENTRYPOINT(device_destroy)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %lu init\n", __FUNCTION__, (uintptr_t)device);
#endif
  kaapi_localitydomain_destroy(device->inherited.ld);
  free(device->inherited.ld);
  free(device);
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int 
KAAPI_PLUGIN_ENTRYPOINT(device_init)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d in\n", __FUNCTION__, dev->device_id);
#endif
  int err = 0;
  int pi;
  CUresult res;
  CUcontext ctx;

  if ((plugin_initialized != true) || (dev == 0))
  { 
    err = EINVAL;
    goto out;
  }

  if (kaapi_cuda_device_is_initialized(device))
  {
    err = 0;
    goto out;
  }

  kaapi_cuda_plugin_lock();

  res = cuDeviceGet(&device->cu_device, kaapi_device_ids[dev->device_id]);
  CudaCheckError(res);

  /* thread has already a context, pop it */
  res = cuCtxGetCurrent(&ctx);
  CudaCheckError(res);

  res = cuCtxCreate(&ctx, CU_CTX_SCHED_AUTO, device->cu_device);
  CudaCheckError(res);
  device->ctx = ctx;

  res = cuDeviceGetAttribute (&pi, CU_DEVICE_ATTRIBUTE_GPU_OVERLAP, device->cu_device);
  CudaCheckError(res);
  device->prop.overlap = pi;

  res = cuDeviceGetAttribute (&pi, CU_DEVICE_ATTRIBUTE_INTEGRATED, device->cu_device);
  CudaCheckError(res);
  device->prop.integrated = pi;

  res = cuDeviceGetAttribute (&pi, CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY, device->cu_device);
  CudaCheckError(res);
  device->prop.map = pi;

  res = cuDeviceGetAttribute (&pi, CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS, device->cu_device);
  CudaCheckError(res);
  device->prop.concurrent = pi;

  res = cuDeviceGetAttribute (&pi, CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT, device->cu_device);
  if(res != CUDA_SUCCESS)
    pi = 1;
  device->prop.async_engines = pi;

  res = cuDeviceTotalMem(&device->prop.mem_total, device->cu_device);
  CudaCheckError(res);

  memset(device->prop.name, 0, 64*sizeof(char));
  res = cuDeviceGetName(device->prop.name, 64, device->cu_device);
  CudaCheckError(res);

  /* */
  cuCtxSynchronize();

  /* memory device */
  device->size_alloc = 0;
  device->size_free = 0;
  device->free_mem = 0;
#if KAAPI_CUDA_CACHE
  dev->memdev.f_alloc = cuda_mem_alloc_cache;
  dev->memdev.f_free = cuda_mem_free_cache;
  if (getenv("KAAPI_NO_GPUALLOCATOR"))
  {
    dev->memdev.f_alloc = cuda_alloc;
    dev->memdev.f_free = cuda_free;
  }
#else
  if (getenv("KAAPI_NO_GPUALLOCATOR"))
  {
    printf("[XKAAPI] KAAPI_NO_GPUALLOCATOR but code do not compile for this option\n");
  }
  dev->memdev.f_alloc = cuda_alloc;
  dev->memdev.f_free = cuda_free;
#endif

  dev->memdev.f_copy = cuda_copy;
  dev->memdev.f_memsync = cuda_memsync;
  dev->memdev.f_get_mem_info = cuda_get_mem_info;
  dev->memdev.f_get_free_mem = cuda_get_free_mem;
  {
    size_t free;
    size_t total;
    res = cuMemGetInfo(&free, &total);
    CudaCheckError(res);

    device->free_mem = (size_t)free;
  }
  /* limit the memory allocation: reserve about 180MB for runing something */
  device->mem_limit = (size_t)((double)kaapi_default_param.cuda_cache_limit
          * (double)(device->free_mem-180UL*1024UL*1024UL));
  dev->memdev.f_get_source = cuda_get_source;

#if KAAPI_CUDA_CACHE
  if (!getenv("KAAPI_NO_GPUALLOCATOR"))
    cuda_mem_cache_init(device);
#endif

  /* stream device */
  dev->stream.f_stream_free = cuda_stream_free;
  dev->stream.f_stream_alloc = cuda_stream_alloc;
  dev->stream.f_stream_process_pending = cuda_stream_process_pending;
  dev->stream.f_stream_decode_ioinstruction = cuda_stream_decode_ioinstruction;

  kaapi_localitydomain_t* ld = malloc(sizeof(kaapi_localitydomain_t));
  kaapi_localitydomain_init(ld, &device->inherited);
  device->inherited.ld = ld;
  kaapi_localitydomain_attach( KAAPI_LD_GPU, 0, ld );

  kaapi_dsm_register_device(&kaapi_the_dsm, &dev->memdev, dev->driver->f_get_type() );

  kaapi_device_list[ dev->device_id ] = device;

  res = cuCtxPopCurrent(&device->ctx);
  CudaCheckError(res);

#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: cuda %d out\n", __FUNCTION__, dev->device_id);
#endif
  kaapi_cuda_plugin_unlock();
out:
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}



/* Call on all devices of the driver after they have been initialized
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_commit)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d start\n", __FUNCTION__, dev->device_id);
#endif

  /* all other devices 'peer' context have been initialized, enable peer */
#if CONFIG_USE_P2P
  int verboseok = 0;
  CUresult res;
  res = cuCtxPushCurrent(device->ctx);
  kaapi_assert(res == CUDA_SUCCESS);

  /* similar to cuda_perf_device but with ldid index in place of cuda device number */
  kaapi_localitydomain_t* ld = device->inherited.ld;
  kaapi_assert(ld !=0);
  ld->perfrank = cuda_count_perfrank-1;
  ld->affinity = (uint64_t*)malloc( sizeof(uint64_t)* ld->perfrank);
  device->affinity = (uint64_t*)malloc( sizeof(uint64_t)* cuda_count_perfrank-1);
  for (int i=0; i<cuda_count_perfrank-1; ++i)
  {
    ld->affinity[i] = 0;
    device->affinity[i] = 0;
  }

  for (int j=0; j<kaapi_device_count; j++)
  {
    if ( device != kaapi_device_list[j] )
    {
      int access;
      res = cuDeviceCanAccessPeer(&access, device->cu_device, kaapi_device_list[j]->cu_device);
      kaapi_assert( res == CUDA_SUCCESS );
      if (access)
      {
        res = cuCtxEnablePeerAccess(kaapi_device_list[j]->ctx, 0 );
        kaapi_assert( res == CUDA_SUCCESS );
        verboseok = 1;
      }
      int device1 = kaapi_device_ids[device->inherited.device_id];
      int device2 = kaapi_device_ids[kaapi_device_list[j]->inherited.device_id];
      int rank = cuda_perf_topo[device1*cuda_device_count+device2];
      kaapi_assert_debug(rank !=0);
      if (cuda_perf_device[ device1*cuda_count_perfrank+ rank] & (1<<device2))
      {
        device->affinity[rank-1] |= (1UL<<kaapi_memory_asid_get_lid(kaapi_device_list[j]->inherited.memdev.asid));
        ld->affinity[rank-1] |= (1UL<<kaapi_memory_asid_get_lid(kaapi_device_list[j]->inherited.memdev.asid));
      }
    }
    else
    { /* add device with itself */
      device->affinity[0] |= (1UL<<kaapi_memory_asid_get_lid(kaapi_device_list[j]->inherited.memdev.asid));
      ld->affinity[0] |= (1UL<<kaapi_memory_asid_get_lid(kaapi_device_list[j]->inherited.memdev.asid));
    }
  }
  CUcontext ctx;
  res = cuCtxPopCurrent(&ctx);
  kaapi_assert(res ==CUDA_SUCCESS);
#endif
}


/*
*/
KAAPI_CLASS_ENTRYPOINT const char* KAAPI_PLUGIN_ENTRYPOINT(device_info)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
  static char buffer[256];
  static char buf1[16];
  static char buf2[16];
  static char buf3[16];
  buf1[10] = 0;
  buf2[10] = 0;
  buf3[10] = 0;
  _print_mask(buf1, 10, device->affinity[0]);
  _print_mask(buf2, 10, device->affinity[1]);
  _print_mask(buf3, 10, device->affinity[2]);
  snprintf(buffer, 256, "%s, %i async engine(s), %.2f (GB), cache limit %.2f (GB), affinity: %s,%s,%s",
    device->prop.name,
    device->prop.async_engines,
    ((double)device->prop.mem_total)/1024.0/1024.0/1024.0,
    ((double)device->mem_limit)/1024.0/1024.0/1024.0,
    buf1, buf2, buf3
  );
  return buffer;
}

/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_start)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  int err;
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d start\n", __FUNCTION__, dev->device_id);
#endif

  pthread_attr_t attr;
  pthread_attr_init(&attr);

#if KAAPI_USE_HWLOC
  hwloc_cpuset_t cpuset;
  hwloc_obj_t obj;
  cpu_set_t save_schedset;
  cpu_set_t schedset;
  cpu_set_t schedset_map;

  CPU_ZERO(&schedset);
  cpuset = hwloc_bitmap_alloc();
  err = hwloc_cudart_get_device_cpuset( topology, kaapi_device_ids[dev->device_id], cpuset );
  if (err == 0)
  {
#if 0
    /* find package obj */
    hwloc_obj_t curr = obj->parent;
    while (curr !=0)
    {
      if (curr->type == HWLOC_OBJ_PACKAGE) break;
      curr = curr->parent;
    }
    if (curr !=0)
#endif
    {
      err = hwloc_cpuset_to_glibc_sched_affinity (topology, cpuset, &schedset, sizeof(cpu_set_t));
      kaapi_assert(err == 0);
      CPU_ZERO(&schedset_map);
#if KAAPI_DEBUG
      char buffer[512];
      ssize_t sb = 0;
#endif
      for (int i=0; i<128; ++i)
      {
        if (CPU_ISSET(i, &schedset))
        {
#if KAAPI_DEBUG
          sb += snprintf(buffer+sb, 256, "%i ", (int)i);
#endif
          CPU_SET(i, &schedset_map);
        }
      }
#if KAAPI_DEBUG
      printf("**** Mapping CUDA thread for device %i on core: %s\n", device->cu_device, buffer);
#endif

      pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &save_schedset);
      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &schedset_map);
      for (int i=0; i<10; ++i) sched_yield();
      err = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &schedset_map);
      kaapi_assert(err == 0);
    }
  }
#else
#endif

  err = pthread_create(&dev->tid, &attr, kaapi_offload_device_thread, dev);
  kaapi_assert(err ==0);
#if KAAPI_HAVE_IO_THREADS
  printf("[kaapi]: plug cuda create helper threads H2D and D2H\n");
  err = pthread_create(&device->tidio[0], &attr, kaapi_cuda_H2D_io_thread, dev);
  kaapi_assert(err ==0);
  err = pthread_create(&device->tidio[1], &attr, kaapi_cuda_D2H_io_thread, dev);
  kaapi_assert(err ==0);
#endif
#if KAAPI_USE_HWLOC
  hwloc_bitmap_free(cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &save_schedset);
#endif

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}




/*
*/
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_stop)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN

  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d in\n", __FUNCTION__, dev->device_id);
#endif
  kaapi_assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

  /* stop thread device */
  device->inherited.finalize = true;
  /* may be wakeup thread */
  kaapi_offload_device_wakeup(&device->inherited);
  void* dummy;
  pthread_join( device->inherited.tid, &dummy );
#if KAAPI_HAVE_IO_THREADS
  pthread_join( device->tidio[0], &dummy );
  pthread_join( device->tidio[1], &dummy );
#endif

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}

/*
*/
KAAPI_CLASS_ENTRYPOINT void 
KAAPI_PLUGIN_ENTRYPOINT(device_finalize)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d in\n", __FUNCTION__, dev->device_id);
#endif
  kaapi_assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

  kaapi_cuda_plugin_lock();
  CUresult res;
  res = cuCtxPushCurrent(device->ctx);
  CudaCheckError(res);

  kaapi_dsm_unregister_device(&kaapi_the_dsm, &dev->memdev);
  kaapi_localitydomain_deattach( KAAPI_LD_GPU, dev->ld );

#if KAAPI_CUDA_CACHE
  if (!getenv("KAAPI_NO_GPUALLOCATOR"))
    cuda_mem_cache_destroy(device);
#endif

  res = cuCtxPopCurrent(&device->ctx);
  CudaCheckError(res);

  res = cuCtxDestroy(device->ctx);
  CudaCheckError(res);

#if KAAPI_DEBUG
  if (getenv("KAAPI_VERBOSE"))
  {
    printf("%i, MEM, %li, %li\n", device->cu_device, device->size_alloc, device->size_free);
    printf("%i, H2D, %li, %li\n", device->cu_device, COUNTER_CNT_H2D, COUNTER_SIZE_H2D);
    printf("%i, D2H, %li, %li\n", device->cu_device, COUNTER_CNT_D2H, COUNTER_SIZE_D2H);
    printf("%i, D2D, %li, %li\n", device->cu_device, COUNTER_CNT_D2D, COUNTER_SIZE_D2D);
  }
#endif

  kaapi_cuda_plugin_unlock();
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: cuda %d finalize\n", __FUNCTION__, dev->device_id);
#endif
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int 
KAAPI_PLUGIN_ENTRYPOINT(device_attach)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: cuda %d attach\n", __FUNCTION__, dev->device_id);
#endif
  CUresult res;
  assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

  res = cuCtxPushCurrent(device->ctx);
  CudaCheckError(res);

  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int 
KAAPI_PLUGIN_ENTRYPOINT(device_detach)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: cuda %d detach\n", __FUNCTION__, dev->device_id);
#endif
  assert(plugin_initialized == true);
  kaapi_assert(kaapi_cuda_device_is_initialized(device));

  CUcontext ctx;
  CUresult res;

  res = cuCtxPopCurrent(&ctx);
  CudaCheckError(res);

  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT void* 
KAAPI_PLUGIN_ENTRYPOINT(get_cublas_handle)(kaapi_device_t* dev)
{
  kaapi_device_cuda_t* device = (kaapi_device_cuda_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "cuda:%s: device %d cublas_handle\n", __FUNCTION__, dev->device_id);
#endif
  return device->inherited.handle;
}


#if KAAPI_USE_DYNLOADER==0
  /* */
#define EP(func)                                                     \
    driver->f_##func = KAAPI_PLUGIN_ENTRYPOINT(func)

void KAAPI_PLUGIN_ENTRYPOINT(get_cuda_driver)(kaapi_driver_t* driver)
{
  /* */
  EP (get_name);
  EP (get_flags);
  EP (get_type);
  EP (get_number);
  EP (init);
  EP (finalize);
  EP (host_register);
  EP (host_register_testwait);
  EP (host_unregister);

  EP (device_create);
  EP (device_destroy);
  EP (device_info);
  EP (device_init);
  EP (device_commit);
  EP (device_start);
  EP (device_stop);
  EP (device_finalize);
  EP (device_attach);
  EP (device_detach);
  EP (get_cublas_handle);
}
#endif
