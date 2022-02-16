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

#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "common.h"
#include "kaapi_impl.h"
#include "kaapi_offload.h"

#if KAAPI_USE_CUDA || KAAPI_USE_HIP
#    include <cublas_v2.h>
//#include <cuda.h>
#include <cuda_runtime_api.h>
#endif

#include "internal_register.h"

/* 2^KAAPI_SIZE_DSM_MAP is the size of the hash map */
#define KAAPI_SIZE_DSM_MAP 20

static kaapi_team_t*       _xkblas_global_team = 0;
static kaapi_atomic_t      _xkblas_thread_idx = {0};
__thread xkblas_context_t* _xkblas_self_context = 0;
/* deprecated variable */
__thread kaapi_thread_t*   _xkblas_self_thread = 0;

/* list of all contexts declared */
static kaapi_lock_t        _xkblas_list_lock = KAAPI_LOCK_INITIALIZER;
static xkblas_context_t*   _xkblas_list_context = 0;

static xkblas_mode_math_t xkblas_default_math = XKBLAS_DEFAULT_MATH;


static const char* get_xkblas_info(void);

/* Return the current xkblas_context (
*/
xkblas_context_t* xkblas_context_alloc(void)
{
  if (_xkblas_self_context ==0)
  {
    /* todo: data structure compaction: between kaapi context and xkblas context
         |------------------|
         |                  |
         | kaapi_context_t  |
         |                  |
         |------------------|
         |                  |
         | xkblas_context_t |
         |                  |
         |------------------|

      Todo that-> use user_size extra context in thread_bind.
    */
    kaapi_context_t* kctxt = kaapi_self_context();
    kaapi_thread_t* kthread = kaapi_context2thread(kctxt);
    kaapi_assert( kthread != 0);
    _xkblas_self_thread = kthread;
    xkblas_context_t* ctxt = (xkblas_context_t*)malloc(sizeof(xkblas_context_t));
    int err = kaapi_hashmap_init(&ctxt->xkblas_ptr2handle, ctxt->xkblas_mapentries, KAAPI_SIZE_DSM_MAP, 0);
    kaapi_assert(err ==0);
    ctxt->xkblas_list_sync0 = 0;
    ctxt->xkblas_list_sync0_tail = 0;
    ctxt->xkblas_generation_cache = 0;
    ctxt->xkblas_matrix_descr_list = 0;
    ctxt->xkblas_modemath = xkblas_default_math;
    ctxt->kthread = kthread;

    /* create default kaapi context & thread */
    int idx = KAAPI_ATOMIC_INCR(&_xkblas_thread_idx);
    ctxt->kteam = kaapi_team_alloc();
    kaapi_team_attach(ctxt->kteam, kthread, 0 ); /* idx. But with 1 team per thread: all threads have idx 0 */
    kaapi_begin_dfg( kthread, KAAPI_FRAME_FLAG_DFG_OK );

    /* link all context */
    kaapi_atomic_lock(&_xkblas_list_lock);
    ctxt->next = _xkblas_list_context;
    _xkblas_list_context = ctxt;
    ctxt->self = &_xkblas_self_context;
    kaapi_atomic_unlock(&_xkblas_list_lock);
    _xkblas_self_context = ctxt;
  }
  return _xkblas_self_context;
}



/*
*/
uint32_t xkblas_context_get_generation()
{ return xkblas_context_get()->xkblas_generation_cache; }

/*
*/
void xkblas_set_modemath( xkblas_mode_math_t m)
{
  xkblas_context_get()->xkblas_modemath = m;
}

/*
*/
xkblas_mode_math_t xkblas_get_modemath(void)
{
  return xkblas_context_get()->xkblas_modemath;
}

/*
*/
extern kaapi_handle_t* xkblas_context_get_list_sync0(void)
{
  return xkblas_context_get()->xkblas_list_sync0;
}

/*
*/
static void* handle_cpublas = 0;

/*
*/
kaapi_handle_t* xkblas_get_handle(
  xkblas_matrix_descr_t* Ah,
  size_t m,
  size_t n)
{ 
  kaapi_assert_debug( (m<Ah->mt) && (n<Ah->nt) );
  return &Ah->handle[m*Ah->nt+n];
}

/*
*/
uint16_t xkblas_get_ldid(
  xkblas_matrix_descr_t* Ah,
  size_t m,
  size_t n)
{
  kaapi_assert_debug( (m<Ah->mt) && (n<Ah->nt) );
  return Ah->ldid[m*Ah->nt+n];
}

/*
*/
uint16_t xkblas_set_ldid(
  xkblas_matrix_descr_t* Ah,
  size_t m,
  size_t n,
  uint16_t val)
{
  kaapi_assert_debug( (m<Ah->mt) && (n<Ah->nt) );
  return Ah->ldid[m*Ah->nt+n] = val;
}


/* Init if also same generation
*/
int xkblas_matrix_descr_isinit(
  xkblas_matrix_descr_t* Ah
)
{
  return (Ah->addr != 0);
}


/* Find the internal descriptor for A
*/
xkblas_matrix_descr_t* xkblas_find( const void* A )
{
  xkblas_matrix_descr_t** me;
  kaapi_hashentries_t* entry;
  xkblas_context_t* ctxt = xkblas_context_get();

  //printf("xkblas_find:%i\n", ++cnt_xkblas_find);
  entry = kaapi_hashmap_findinsert( &ctxt->xkblas_ptr2handle, A );
  me = KAAPI_HASHENTRIES_GETREF(entry, xkblas_matrix_descr_t*);
  if (*me == 0)
  {
    xkblas_matrix_descr_t* Ah = (xkblas_matrix_descr_t*)malloc(sizeof(struct xkblas_matrix_descr));
    /* marker for xkblas_matrix_descr_isinit return false */
    Ah->handle = 0;
    Ah->addr = 0;
    Ah->capacity = 0;
    Ah->next = ctxt->xkblas_matrix_descr_list;
    Ah->entry = (void*)entry;
    ctxt->xkblas_matrix_descr_list = Ah;
#if KAAPI_DEBUG
    Ah->owner = pthread_self();
#endif
    *me = Ah;
  }
#if KAAPI_DEBUG
  else {
    kaapi_assert((*me)->owner == pthread_self());
  }
#endif
  return *me;
}


int xkblas_init_matrix_handle( xkblas_matrix_descr_t* Ah,
  void* A, size_t M, size_t N, size_t LD, size_t eltsize, size_t MB, size_t NB
)
{
  size_t default_tilesize = xkblas_get_param();
  xkblas_context_t* ctxt = xkblas_context_get();

  Ah->addr = A;
  Ah->eltsize = eltsize;
  Ah->ld = LD;
  Ah->M  = M;
  Ah->N  = N;
  Ah->mb = MB;
  Ah->nb = NB;
#if KAAPI_DEBUG
  Ah->owner = pthread_self();
#endif

  size_t mt = xkblas_num_of_tiles(M,MB);
  size_t nt = xkblas_num_of_tiles(N,NB);
  if (mt * nt >= Ah->capacity)
  {
    if (Ah->handle) free(Ah->handle);
    Ah->handle = malloc( (sizeof(uint16_t) + sizeof(kaapi_handle_t))* mt * nt );
    Ah->capacity = mt*nt;
  }
  Ah->ldid   = (uint16_t*)(Ah->handle + mt * nt);
  Ah->mt = mt;
  Ah->nt = nt;

  for (size_t m=0; m< Ah->mt; ++m)
    for (size_t n=0; n< Ah->nt; ++n)
    {
#define ADDR_(A, m, n) ((char*)(A)+ ((m) * MB + (n) * NB * LD)*eltsize)
      kaapi_handle_t*  handle = &Ah->handle[m*Ah->nt+n];
      kaapi_handle_init(ctxt->kthread, handle, ADDR_(A,m,n), 0);
#if 0//DEBUG
char* name =kaapi_dbg_get_name(Ah);
printf("New handle (%i,%i) / %s\n",m,n, name == 0 ? "" : name );
#endif
#if 1
      handle->sync0.sync = 0;
      if (ctxt->xkblas_list_sync0_tail !=0)
        ctxt->xkblas_list_sync0_tail->sync0.sync = (kaapi_access_t*)handle;
      else
        ctxt->xkblas_list_sync0 = handle;
      ctxt->xkblas_list_sync0_tail = handle;
#else
      handle->sync0.sync = (kaapi_access_t*)ctxt->xkblas_list_sync0;
      ctxt->xkblas_list_sync0 = handle;
#endif
      Ah->ldid[m*Ah->nt+n] = (uint16_t)-1;
    }

  /* to debug... */
  Ah->gen = ctxt->xkblas_generation_cache;
}


/* Find the mapping for the tile A(i,j)
*/
uint16_t xkblas_get_ld(
  xkblas_matrix_descr_t* Ah,
  size_t i, size_t j
)
{
  uint16_t lid0 = kaapi_memory_asid_get_lid( kaapi_local_asid );
  uint16_t lid;

  if (Ah ==0) return (uint16_t)-1;

  kaapi_assert_debug( (i<Ah->mt) && (j<Ah->nt) );
  lid = Ah->ldid[ i*Ah->nt + j ];

  if (lid != (uint16_t)-1)
    return lid;

  unsigned int count = kaapi_localitydomain_count(KAAPI_LD_GPU);
  if (count ==0)
  {
    lid = lid0;
    goto retval;
  }

  kaapi_handle_t* h = xkblas_get_handle(Ah, i, j);
  kaapi_metadata_info_t* mdi = h->last->mdi;
  if (mdi !=0)
  {
    int idxvalid[KAAPI_MEMORY_MAX_NODES];
    int count = 0;
    KAAPI_MEMORY_VALUE_TYPE valid_bit= KAAPI_ATOMIC_READ(&mdi->valid);
    valid_bit &= ~(1<<lid0);
    while (valid_bit !=0)
    {
      KAAPI_MEMORY_VALUE_TYPE lid = KAAPI_MEMORY_FFS(valid_bit);
      --lid;
      if (kaapi_memory_replica_is_valid(mdi, lid))
        idxvalid[count++] = lid;
      valid_bit &= ~(1<<lid);
    }
//TG: to do here avoid to talk of ldid : locality domain id and lid: local id of the adress space. Should be ~ the same ?
    kaapi_assert(0);
    if (count ==1)
      return 1+idxvalid[0];  
    else if (count >0)
      return 1+idxvalid[rand() % count];  
  }
  lid = (uint16_t)-1;

retval:
  Ah->ldid[ i*Ah->nt + j ] = lid;
  return lid;
}


/* NB = tile size. 0 == value computed at runtime
*/
static size_t NB = 0;
void xkblas_set_param(size_t nb, size_t p)
{
  //printf("In %s: nb:%li, p: %li\n", __func__, nb, p);
  NB=nb;
  if (p > sizeof(double)) /* max precision */
    p = 16;
  kaapi_memory_set_info( KAAPI_MEMORY_EXPECTED_BLOCK, nb*nb*p );
}


/*
*/
size_t xkblas_get_param(void)
{
  return NB;
}


int xkblas_get_devicecount(void)
{
}


/*
*/
int xkblas_get_ngpus(void)
{
  return (int)kaapi_default_param.ngpus;
}

/*
*/
int xkblas_set_ngpus(int ngpus)
{
  int err = 0;
  if (ngpus <0) return EINVAL;
  if (ngpus > kaapi_default_param.sys_ngpus)
  {
    err = EDOM;
    ngpus = kaapi_default_param.sys_ngpus;
  }
  kaapi_default_param.ngpus = ngpus;
  return err;
}

/*
 */
void* xkblas_malloc( size_t size )
{
#if KAAPI_USE_HIP
  void* ptr = 0;
  kaapi_assert_m(hipSuccess== hipHostMalloc(&ptr, size, hipHostMallocPortable),"hipHostAlloc failed");
  return ptr;
#elif KAAPI_USE_CUDA  
  void* ptr = 0;
  //CUresult err = cuMemHostAlloc(&ptr, size, CU_MEMHOSTALLOC_PORTABLE);
  //kaapi_assert_m(CUDA_SUCCESS== err, "cuMemHostAlloc failed");
  kaapi_assert_m(cudaSuccess== cudaHostAlloc(&ptr, size, cudaHostAllocPortable),"cudaHostAlloc failed");
  return ptr;
#else
  return malloc(size);
#endif
}

/*
 */
void xkblas_free( void* ptr, size_t sz )
{
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
  //kaapi_assert_m(CUDA_SUCCESS==  cuMemFreeHost(ptr),"cuMemFreeHost failed");
  kaapi_assert_m(cudaSuccess== cudaFreeHost(ptr),"cudaFreeHost failed");
#else
  free(ptr);
#endif
}


/* Asynchronous operation, return handle to the request
 */
uint64_t xkblas_register_memory_async( void* ptr, size_t sz )
{
#if KAAPI_USE_CUDA||KAAPI_USE_HIP
// warning in this version, if USE_HIP is defined, then also is USE_CUDA 
// (the file is hipyfied to be compiled with hip)
#if KAAPI_USE_HIP
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_HIP );
#elif KAAPI_USE_CUDA 
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_CUDA );
#endif
  if (driver ==0) return 0;
  return driver->f_host_register( ptr, sz, 0, 0, 0, 0);
#endif
  return 0;
}


/*
*/
uint64_t xkblas_unregister_memory_async( void* ptr, size_t sz )
{
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
// warning in this version, if USE_HIP is defined, then also is USE_CUDA 
// (the file is hipyfied to be compiled with hip)
#if KAAPI_USE_HIP
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_HIP );
#elif KAAPI_USE_CUDA 
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_CUDA );
#endif
  if (driver ==0) return 0;
  return driver->f_host_unregister( ptr, sz, 0, 0, 0, 0);
#endif
  return 0;
}


/* Test if the request is completed
*/
int xkblas_register_memory_test( uint64_t handle )
{
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
// warning in this version, if USE_HIP is defined, then also is USE_CUDA 
// (the file is hipyfied to be compiled with hip)
#if KAAPI_USE_HIP
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_HIP );
#elif KAAPI_USE_CUDA 
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_CUDA );
#endif
  if (driver ==0) return 1; /* always completed */
  return driver->f_host_register_testwait( handle, 0 );
#endif
  return 0;
}


/* Return the error code of the memory registration request handle
*/
int xkblas_register_memory_wait( uint64_t handle )
{
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
// warning in this version, if USE_HIP is defined, then also is USE_CUDA 
// (the file is hipyfied to be compiled with hip)
#if KAAPI_USE_HIP
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_HIP );
#elif KAAPI_USE_CUDA 
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_CUDA );
#endif
  if (driver ==0) return 1; /* always completed */
  return driver->f_host_register_testwait( handle, 1 );
#endif
  return 0;
}


/*
*/
int xkblas_register_memory_waitall( )
{
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
// warning in this version, if USE_HIP is defined, then also is USE_CUDA 
// (the file is hipyfied to be compiled with hip)
#if KAAPI_USE_HIP
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_HIP );
#elif KAAPI_USE_CUDA 
  kaapi_driver_t* driver = kaapi_offload_driver_bytype( KAAPI_PROC_TYPE_CUDA );
#endif
  if (driver ==0) return 1; /* always completed */
  return driver->f_host_register_testwait( (uint64_t)-1, 2 );
#endif
  return 0;
}


/*
 */
int xkblas_register_memory( void* ptr, size_t sz )
{
  return xkblas_register_memory_wait( xkblas_register_memory_async(ptr, sz) );
}

/*
 */
int xkblas_unregister_memory( void* ptr, size_t sz )
{
  uint64_t handle = xkblas_unregister_memory_async(ptr, sz); 
  xkblas_register_memory_waitall();
}

/*
*/
#if 00
static void xkblas_free_curr_blochandle(void)
{
  while (curr_blochandle !=0)
  {
    kaapi_stack_bloc_t* bloc = curr_blochandle;
    curr_blochandle = bloc->next;
    bloc->next = freelist_blochandle;
    freelist_blochandle = bloc;
  }
}
#endif

/* Do not implement type== ALL
   store bloc (i,j) on a grid of ressource GpxGq (i/Bp)%Gp,(j/Bq)%Gq
   If matrix is not found return EINVAL
*/
int xkblas_map_2Dblock_cyclic(
  int hlevel, int storage, size_t m, size_t n,
  const void* A, size_t lda, size_t eltsize,
  size_t Bp, size_t Bq, /* blocking size */
  size_t Gp, size_t Gq,  /* grid size */
  int force
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) return EINVAL;

  kaapi_ld_type_t type;
  switch (hlevel) {
    case 0: type = KAAPI_LD_BOARD; break;
    case 1: type = KAAPI_LD_GPU; break;
    case 2: type = KAAPI_LD_CORE; break;
    default:
      printf("[%s] unknown type, returns immediatly\n", __func__);
#if KAAPI_DEBUG
      abort();
#endif
      return EINVAL;
  };

  unsigned int count = kaapi_localitydomain_count(type);
  if (count ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;

  for (size_t i=0; i<Amt; ++i)
  {
    for (size_t j=0; j<Ant; ++j)
    {
      uint16_t ldid = xkblas_get_ldid(Ah, i, j );
      if ((ldid ==(uint16_t)-1) || force)
      {
        int r = ( ((i/Bp)%Gp)*Gq + (j/Bq)%Gq ) %count;
        kaapi_localitydomain_t* ld = kaapi_localitydomain_get_bytype(type, r);
        xkblas_set_ldid(Ah, i, j, ldid = ld->ldid);
#if KAAPI_USE_OCR
        kaapi_assert( ldid == kaapi_memory_asid_get_lid(ld->device->memdev.asid) );
        kaapi_assert(0 == kaapi_dsm_wish_distribution(
              &kaapi_the_dsm,
              ld->device->memdev.asid,
              xkblas_get_handle(Ah, i, j)
        ));
#endif
      }
    }
  }
  return 0;
}

/* Do not implement type== ALL
   store bloc [i,.] on a ressource (i/B)%G
   colrow = 0 -> col mapping
   colrow = 1 -> row mapping
*/
int xkblas_map_1Dblock_cyclic(
  int hlevel, int storage, int colrow, size_t m, size_t n,
  const void* A, size_t lda, size_t eltsize,
  size_t B, size_t G,    /* grid size */
  int force
)
{
  size_t MB,NB;
  MB = NB = xkblas_get_param();
  if (colrow == 0)
  {
    return xkblas_map_2Dblock_cyclic(
      hlevel, storage, m, n, A, lda, eltsize,
      (m+MB-1)/MB, B,
      1, G,
      force
    );
  }
  else
  {
    return xkblas_map_2Dblock_cyclic(
      hlevel, storage, m, n, A, lda, eltsize,
      B, (n+NB-1)/NB,
      G, 1,
      force
    );
  }
#if KAAPI_DEBUG
  abort();
#endif
  return EINVAL;
}


int xkblas_map_cyclic(
  int hlevel, int storage, size_t m, size_t n,
  const void* A, size_t lda, size_t eltsize,
  int force
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) return EINVAL;

  kaapi_ld_type_t type;
  switch (hlevel) {
    case 0: type = KAAPI_LD_BOARD; break;
    case 1: type = KAAPI_LD_GPU; break;
    case 2: type = KAAPI_LD_CORE; break;
    default:
      printf("[%s] unknown type, returns immediatly\n", __func__);
#if KAAPI_DEBUG
      abort();
#endif
      return EINVAL;
  };

  unsigned int count = kaapi_localitydomain_count(type);
  if (count ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  char* ptr = (char*)A;
  void* addr;

  for (size_t i=0; i<Amt; ++i)
  {
    for (size_t j=0; j<Ant; ++j)
    {
      uint16_t ldid = xkblas_get_ldid(Ah, i, j );
      if ((ldid ==(uint16_t)-1) || force)
      {
        int r = (i*Ant+j)%count;
        xkblas_set_ldid(Ah, i, j, ldid = kaapi_localitydomain_get_num(type, r));
      }
    }
  }
  return 0;
}


int xkblas_map_ij_cyclic(
  int hlevel, int storage, size_t m, size_t n,
  const void* A, size_t lda, size_t eltsize,
  int force
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) return EINVAL;

  kaapi_ld_type_t type;
  switch (hlevel) {
    case 0: type = KAAPI_LD_BOARD; break;
    case 1: type = KAAPI_LD_GPU; break;
    case 2: type = KAAPI_LD_CORE; break;
    default:
      printf("[%s] unknown type, returns immediatly\n", __func__);
#if KAAPI_DEBUG
      abort();
#endif
      return EINVAL;
  };

  unsigned int count = kaapi_localitydomain_count(type);
  if (count ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  char* ptr = (char*)A;
  void* addr;

  for (size_t i=0; i<Amt; ++i)
  {
    for (size_t j=0; j<Ant; ++j)
    {
      uint16_t ldid = xkblas_get_ldid(Ah, i, j );
      if ((ldid ==(uint16_t)-1) || force)
      {
        int r = (i+j)%count;
        xkblas_set_ldid(Ah, i, j, ldid = kaapi_localitydomain_get_num(type, r));
      }
    }
  }
  return 0;
}

int xkblas_map_test(
  int hlevel, int storage, size_t m, size_t n,
  const void* A, size_t lda, size_t eltsize,
  int force
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) return EINVAL;

  kaapi_ld_type_t type;
  switch (hlevel) {
    case 0: type = KAAPI_LD_BOARD; break;
    case 1: type = KAAPI_LD_GPU; break;
    case 2: type = KAAPI_LD_CORE; break;
    default:
      printf("[%s] unknown type, returns immediatly\n", __func__);
#if KAAPI_DEBUG
      abort();
#endif
      return EINVAL;
  };

  unsigned int count = kaapi_localitydomain_count(type);
  if (count ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  char* ptr = (char*)A;
  void* addr;
  size_t B = (Ant+2*count-1)/(2*count);

  for (size_t i=0; i<Amt; ++i)
  {
    int r = i % count;
    uint16_t ldid = xkblas_get_ldid(Ah, i, i );
    if ((ldid ==(uint16_t)-1) || force)
      xkblas_set_ldid(Ah, i, i, ldid = kaapi_localitydomain_get_num(type, r));

    size_t c = i+1;
    for (size_t j=0; j<Ant; ++j)
    {
      if (i ==j) continue;
      int r = c % count;
      ++c;
      uint16_t ldid = xkblas_get_ldid(Ah, i, j );
      if ((ldid ==(uint16_t)-1) || force)
        xkblas_set_ldid(Ah, i, j, ldid = kaapi_localitydomain_get_num(type, r));
    }
  }
  return 0;
}



/*
*/
#define STRNAME  "writeback"
#define NAME(x) kaapi_##x##_writeback
#define PNAME(x) kaapi_writeback_##x
#define SIZE_NPARAM 1
#define NPARAM 1
#define MODE_PARAM {KAAPI_ACCESS_MODE_R}
#define ADDR_PARAM {&arg->a}
#define VIEW_PARAM {{ &arg->m, &arg->n, &arg->ld}}
#define FORMAT_TYPE kaapi_char_format
#define SIZEOF_TYPE arg->eltsize
#define DOT_COLOR "gray"
#define DOT_SHAPE "octagon"
#define TASK_FLOPS 0
#define TASK_DATA  0


typedef struct {
  kaapi_access_t a;
  size_t m;
  size_t n;
  size_t ld;
  size_t eltsize;
  kaapi_frame_t* frame;
} NAME(Arg);
static kaapi_format_id_t NAME(task_fmtid) = 0;

/*
*/
static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  //printf("[%s] should never be call on CPU !!!\n", __func__);
  //abort();
}

/*
*/
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
#if KAAPI_DEBUG
kaapi_atomic_t spawn_writeback={0};
kaapi_atomic_t pending_writeback={0};
kaapi_atomic_t received_writeback={0};
#endif
static void callback_epilogue_writeback(
    kaapi_io_status_t status,
    kaapi_io_stream_t* ios,
    void* arg0, void* arg1, void* arg2
)
{
  kaapi_metadata_info_t* mdi __attribute__((unused)) = (kaapi_metadata_info_t*)arg0;
  kaapi_frame_t* frame = (kaapi_frame_t*)arg1;
#if KAAPI_DEBUG
  KAAPI_ATOMIC_INCR(&received_writeback);
#endif
  KAAPI_ATOMIC_INCR(&frame->exec_count);
  //printf("Receive callback coherent: %p, count:%li\n", frame, KAAPI_ATOMIC_INCR(&frame->exec_count));
}

static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  /* Make execution as if task_body_gpu spawn continuation (reception of communication)
     in order to detect that end of tasks execution
  */
//printf("----------- Writeback task\n");
  NAME(Arg)* taskarg = kaapi_task_getargst(task,NAME(Arg));
#if KAAPI_DEBUG
  KAAPI_ATOMIC_INCR(&pending_writeback);
#endif
  int err = kaapi_dsm_prefetch_on( &kaapi_the_dsm, kaapi_local_asid,
    taskarg->a.mdi, 
    callback_epilogue_writeback, taskarg->a.mdi, taskarg->frame, taskarg
  );
  kaapi_assert((err==0) || (err== EINPROGRESS));
}
#endif

#include "task_format.h"


/* Create write back task for the tile A(m,n) of size compute in the function
*/
static void xkblas_create_taskwriteback(
  xkblas_matrix_descr_t* Ah,
  size_t m, size_t n,
  size_t ld,
  size_t eltsize
)
{
  kaapi_task_t* task;
  kaapi_thread_t* thread = xkblas_self_thread();
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  size_t tasksize = sizeof(kaapi_Arg_writeback) + sizeof(kaapi_task_t);
  task = kaapi_task_alloc( thread, kaapi_task_fmtid_writeback, tasksize );
  kaapi_Arg_writeback* taskarg = kaapi_task_getargst(task,kaapi_Arg_writeback); // == NAME(ARG)

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  taskarg->m = m == Amt-1 ? Ah->M - m*Ah->mb : Ah->mb;
  taskarg->n = n == Ant-1 ? Ah->N - n*Ah->nb : Ah->nb;
  taskarg->eltsize = eltsize;
  taskarg->ld = ld;
  taskarg->frame = ctxt->unlink;
  kaapi_update_dependencies(thread, &taskarg->a, task,
      KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah,m,n));
  kaapi_assert_debug( taskarg->a.mdi==0 );
  kaapi_taskflag_set(task, KAAPI_TASK_FLAG_INCOM);

#if KAAPI_USE_OCR
  /* OCR on the first parameter */
  kaapi_task_set_ld(task, KAAPI_TASK_OCR_PARAM, 0);

#if KAAPI_DEBUG
  kaapi_ldid_t ldid0 = kaapi_dsm_get_wish_distribution(
      &kaapi_the_dsm,
      xkblas_get_handle(Ah,m,n));
  uint16_t ldid1 = xkblas_get_ld(Ah,m,n);
  kaapi_assert( ldid0 == ldid1 );
#endif

#else
  uint16_t ldid = xkblas_get_ld(Ah,m,n);
  kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, ldid);
#endif


#if KAAPI_DEBUG
  KAAPI_ATOMIC_INCR(&spawn_writeback);
#endif
  /* incr spawn_count: spawned task should be considered completed when epilogue is called */
  KAAPI_ATOMIC_INCR(&taskarg->frame->spawn_count);
  kaapi_task_commit( thread, task );
}


/*
*/
#define STRNAME  "invalidate"
#define NAME(x) kaapi_##x##_invalidate
#define PNAME(x) kaapi_invalidate_##x
#define SIZE_NPARAM 1
#define NPARAM 1
#define MODE_PARAM {KAAPI_ACCESS_MODE_RW}
#define ADDR_PARAM {&arg->a}
#define VIEW_PARAM {{ &arg->m, &arg->n, &arg->ld}}
#define FORMAT_TYPE kaapi_char_format
#define SIZEOF_TYPE arg->eltsize
#define DOT_COLOR "gray0"
#define DOT_SHAPE "invtriangle"
#define TASK_FLOPS 0
#define TASK_DATA  0

typedef struct {
  kaapi_access_t a;
  size_t m;
  size_t n;
  size_t ld;
  size_t eltsize;
  kaapi_frame_t* frame;
} NAME(Arg);
static kaapi_format_id_t NAME(task_fmtid) = 0;

/*
*/
static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  printf("[%s] should never be call on CPU !!!\n", __func__);
  abort();
}

/*
*/
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  kaapi_assert_debug( ctxt->device !=0);
  uint16_t lidhost = kaapi_memory_asid_get_lid(kaapi_local_asid);
  kaapi_assert_debug(lidhost < KAAPI_MEMORY_MAX_NODES);
  uint16_t lid = kaapi_memory_asid_get_lid(ctxt->device->memdev.asid);
  kaapi_assert_debug(lid < KAAPI_MEMORY_MAX_NODES);
  if (lid == lidhost) return;
  NAME(Arg)* taskarg = kaapi_task_getargst(task,NAME(Arg));
  kaapi_memory_cache_invalidate_bloc(&ctxt->device->memdev, 0, taskarg->a.mdi);
}
#endif

#include "task_format.h"

/*
*/
static void xkblas_create_taskinvalidate(
  xkblas_matrix_descr_t* Ah,
  size_t m, size_t n,
  size_t ld,
  size_t eltsize
)
{
  kaapi_task_t* task;
  kaapi_thread_t* thread = xkblas_self_thread();
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  size_t tasksize = sizeof(kaapi_Arg_invalidate) + sizeof(kaapi_task_t);
  task = kaapi_task_alloc( thread, kaapi_task_fmtid_invalidate, tasksize );
  kaapi_Arg_invalidate* taskarg = kaapi_task_getargst(task,kaapi_Arg_invalidate); // == NAME(ARG)
  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  taskarg->m = m == Amt-1 ? Ah->M - m*Ah->mb : Ah->mb;
  taskarg->n = n == Ant-1 ? Ah->N - n*Ah->nb : Ah->nb;
  taskarg->eltsize = eltsize;
  taskarg->ld = ld;
  taskarg->frame = ctxt->unlink;
  kaapi_update_dependencies(thread, &taskarg->a, task,
      KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Ah,m,n));
  uint16_t lid = xkblas_get_ld(Ah, m, n);
  kaapi_taskflag_set(task, KAAPI_TASK_FLAG_INCOM);
  if (lid != (uint16_t)-1)
    kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, lid);
  kaapi_task_commit( thread, task );
}




/*
*/
#define STRNAME  "distribute"
#define NAME(x) kaapi_##x##_distribute
#define PNAME(x) kaapi_distribute_##x
#define SIZE_NPARAM 1
#define NPARAM 1
#define MODE_PARAM {KAAPI_ACCESS_MODE_RW}
#define ADDR_PARAM {&arg->a}
#define VIEW_PARAM {{ &arg->m, &arg->n, &arg->ld}}
#define FORMAT_TYPE kaapi_char_format
#define SIZEOF_TYPE arg->eltsize
#define DOT_COLOR "gray1"
#define DOT_SHAPE "triangle"
#define TASK_FLOPS 0
#define TASK_DATA  0

typedef struct {
  kaapi_access_t a;
  size_t m;
  size_t n;
  size_t ld;
  size_t eltsize;
  kaapi_frame_t* frame;
} NAME(Arg);
static kaapi_format_id_t NAME(task_fmtid) = 0;

/*
*/
static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
}

/*
*/
#if KAAPI_USE_CUDA || KAAPI_USE_HIP
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
}
#endif

#include "task_format.h"

/* Task to force copy of data on a given ldid
*/
static void xkblas_create_distribute(
  xkblas_matrix_descr_t* Ah,
  size_t m, size_t n,
  size_t ld,
  size_t eltsize,
  kaapi_ldid_t ldid
)
{
  kaapi_task_t* task;
  kaapi_thread_t* thread = xkblas_self_thread();
  kaapi_context_t* ctxt = kaapi_thread2context(thread);
  size_t tasksize = sizeof(kaapi_Arg_distribute) + sizeof(kaapi_task_t);
  task = kaapi_task_alloc( thread, kaapi_task_fmtid_distribute, tasksize );
  kaapi_Arg_distribute* taskarg = kaapi_task_getargst(task,kaapi_Arg_distribute); // == NAME(ARG)
  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  taskarg->m = m == Amt-1 ? Ah->M - m*Ah->mb : Ah->mb;
  taskarg->n = n == Ant-1 ? Ah->N - n*Ah->nb : Ah->nb;
  taskarg->eltsize = eltsize;
  taskarg->ld = ld;
  taskarg->frame = ctxt->unlink;
  kaapi_update_dependencies(thread, &taskarg->a, task,
      KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah,m,n));
  kaapi_taskflag_set(task, KAAPI_TASK_FLAG_OUTCOM);
  kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, ldid);
  kaapi_task_commit( thread, task );
}



/*
*/
static int xkblas_ismode_math_tc(void)
{
  return xkblas_default_math == XKBLAS_TENSOR_OP_MATH;
}


/*
*/
int xkblas_get_device_count(int* count)
{ 
  if (count ==0) return EINVAL;
  *count = kaapi_offload_ndevices(); 
  return 0;
}


/*
*/
static int init_count = 0;
int xkblas_init(void)
{
  if (init_count++ !=0) return 0;

  if (getenv("XKBLAS_TILE_SIZE") || getenv("XKBLAS_BLOC_SIZE") || getenv("XKBLAS_PRECISION"))
  {
    size_t tile_size = 0;
    if (getenv("XKBLAS_TILE_SIZE") ==0)
      tile_size = xkblas_get_param();
    else
      tile_size = atoi(getenv("XKBLAS_TILE_SIZE"));

    size_t precision = sizeof(double);
    if (getenv("XKBLAS_PRECISION") ==0)
      precision = sizeof(double);
    else {
      char* p = getenv("XKBLAS_PRECISION");
      if ((strcmp(p,"float") ==0) || (strcmp(p, "real*4") ==0))
        precision = sizeof(float);
      else if ((strcmp(p,"double") ==0) || (strcmp(p, "real*8") ==0))
        precision = sizeof(double);
      else if ((strcmp(p,"complex32") ==0) || (strcmp(p, "complex*4") ==0))
        precision = sizeof(Complex32_t);
      else if ((strcmp(p,"complex64") ==0) || (strcmp(p, "complex*8") ==0))
        precision = sizeof(Complex64_t);
      else {
        fprintf(stderr,"[XKBlas] XKBLAS_PRECISION must be = float|real*4|double|read*8|complex|complex32|complex*4|complex64|complex*8.\n");
        exit(1); 
      }
    }
    if (getenv("KAAPI_VERBOSE"))
    {
      printf("xe size: %lu, precision: %lu\n",tile_size, precision);
    }
    xkblas_set_param( tile_size, precision );
  }

  if (getenv("XKBLAS_NGPUS"))
    setenv("KAAPI_NUM_GPUS",getenv("XKBLAS_NGPUS"),1);
  else if (getenv("XKBLAS_NGPU"))
    setenv("KAAPI_NUM_GPUS",getenv("XKBLAS_NGPU"),1);

  if (getenv("XKBLAS_GPUSET"))
    setenv("KAAPI_GPUSET",getenv("XKBLAS_GPUSET"),1);

  if (getenv("XKBLAS_NCUDA_STREAMS"))
  {
    if (!getenv("XKBLAS_NSTREAMS"))
      printf("[XKBlas] deprecated 'XKBLAS_NCUDA_STREAMS' use 'XKBLAS_NSTREAMS'\n");
    setenv("KAAPI_CUDA_KERNEL_STREAM_NUMS",getenv("XKBLAS_NCUDA_STREAMS"),1);
  } 
  if (getenv("XKBLAS_NSTREAMS"))
  {
    if (getenv("XKBLAS_NCUDA_STREAMS"))
      printf("[XKBlas] deprecated 'XKBLAS_NCUDA_STREAMS' also defined, use 'XKBLAS_NSTREAMS'\n");
    setenv("KAAPI_CUDA_KERNEL_STREAM_NUMS",getenv("XKBLAS_NSTREAMS"),1);
  }

  if (getenv("XKBLAS_NKERNELS_PER_STREAM"))
  {
    if (!getenv("XKBLAS_NKERNELS"))
      printf("[XKBlas] deprecated 'XKBLAS_NKERNELS_PER_STREAM' use 'XKBLAS_NKERNELS'\n");
    setenv("KAAPI_CUDA_KERNEL_PER_STREAM",getenv("XKBLAS_NKERNELS_PER_STREAM"),1);
  }
  if (getenv("XKBLAS_NKERNELS"))
  {
    if (getenv("XKBLAS_NKERNELS_PER_STREAM"))
      printf("[XKBlas] deprecated 'XKBLAS_NKERNELS_PER_STREAM' also defined, use 'XKBLAS_NKERNELS'\n");
    setenv("KAAPI_CUDA_KERNEL_PER_STREAM",getenv("XKBLAS_NKERNELS"),1);
  }

  if (getenv("XKBLAS_CACHE_LIMIT"))
    setenv("KAAPI_CUDA_CACHE_LIMIT",getenv("XKBLAS_CACHE_LIMIT"),1);

  const char* m;
  if (m = getenv("XKBLAS_DEFAULT_MATH"))
  {
    if ((strcasecmp(m,"TC") ==0)||(strcasecmp(m,"tensorcore") ==0)||(strcasecmp(m,"mix1632") ==0))
      xkblas_default_math = XKBLAS_TENSOR_OP_MATH;
    else if ((strcasecmp(m,"default") ==0)||(strcasecmp(m,"notc") ==0))
      xkblas_default_math = XKBLAS_DEFAULT_MATH;
    else
      printf("[XKBlas] unkown math mode '%s', use default\n", m);
  }
  xkblas_register_task_format();
  kaapi_register_format_writeback();
  kaapi_register_format_invalidate();
  kaapi_register_format_distribute();
  KAAPI_ATOMIC_WRITE(&_xkblas_thread_idx,0);
  kaapi_init();

  //_xkblas_global_team = kaapi_team_alloc();


  if (getenv("KAAPI_VERBOSE")||getenv("XKBLAS_VERBOSE"))
  {
    extern const char* get_kaapi_version(void);
    extern const char* get_kaapi_info(void);
    printf("[XKBlas info]\n%s%s[XKBlas info]\n", get_kaapi_info(), get_xkblas_info() );

    /* Some information about hierarchy
    */
    printf("#KAAPI_LD_NUMA=%i\n", kaapi_localitydomain_count(KAAPI_LD_NUMA) );

    if ((kaapi_localitydomain_count(KAAPI_LD_NUMA) != -1)
     && (kaapi_localitydomain_count(KAAPI_LD_NUMA) >0))
    {
      for (size_t i=0; i<kaapi_localitydomain_count(KAAPI_LD_NUMA); ++i)
        printf("  lid[%i]=%i\n", (int)i, (int)kaapi_localitydomain_get_num(KAAPI_LD_NUMA, i) );
    }
    printf("#KAAPI_LD_GPU=%i\n", kaapi_localitydomain_count(KAAPI_LD_GPU) );
    if ((kaapi_localitydomain_count(KAAPI_LD_GPU) != -1)
     && (kaapi_localitydomain_count(KAAPI_LD_GPU) >0))
    {
    for (size_t i=0; i<kaapi_localitydomain_count(KAAPI_LD_GPU); ++i)
      printf("  lid[%i]=%i,  %s\n", (int)i, (int)kaapi_localitydomain_get_num(KAAPI_LD_GPU, i), kaapi_localitydomain_info(KAAPI_LD_GPU, i) );
    }
  }

  /* */
  kaapi_thread_t* thread = xkblas_self_thread();
  kaapi_begin_dfg( thread, KAAPI_FRAME_FLAG_DFG_OK );
}


/*
*/
int xkblas_finalize(void)
{
  if (--init_count !=0) return 0;

  int err;

  /* TG: with several thread calling xkblas it is not possible to form the correct team
     only call end_dfg for local synchronisation
  */
  kaapi_end_dfg( _xkblas_self_context->kthread );
  kaapi_memory_synchronize();

  /* */
  kaapi_atomic_lock(&_xkblas_list_lock);

  int verbose=0;
  if (getenv("KAAPI_VERBOSE")||getenv("XKBLAS_VERBOSE"))
    verbose = 1;
 
#if KAAPI_USE_PERFCOUNTER
  int disphead = 0;
  kaapi_offload_perfcounter_t cumul;
  char* task_names[KAAPI_FORMAT_MAX];
  if (verbose)
  {
    memset(&cumul, 0, sizeof(cumul));
    memset(&task_names, 0, sizeof(task_names));
    char tmp[12];
    for (int d=0; d<kaapi_offload_get_num_devices(); ++d)
    {
      kaapi_device_t* device = kaapi_offload_device(d);
      int dispdevice = 0;
      uint64_t spawn_count = 0;
      double time_count = 0, flops_count = 0;
      for (kaapi_format_id_t i=0; i<KAAPI_FORMAT_MAX; ++i)
      {
        if (device->perfcnt.task[i].spawn >0)
        {
          if (disphead ==0)
          {
            printf("[XKBlas stats]\n");
            disphead = 1;
          }
          if (dispdevice ==0)
          {
            printf("\t*device: %i\n", d);
            dispdevice =1;
          }
          kaapi_format_t* fmt = kaapi_format_resolve_byfmid(i);
          kaapi_format_get_name(fmt, 0, tmp, sizeof(tmp));
          task_names[i] = strdup(tmp);
          printf("\t[%12s]: count=%12li, time=%8e, flops=%10e, ai=%10e bar{ai}=%10e\n",
            tmp,
            device->perfcnt.task[i].spawn,
            device->perfcnt.task[i].time,
            device->perfcnt.task[i].flops,
            device->perfcnt.task[i].ai,
            device->perfcnt.task[i].ai/device->perfcnt.task[i].spawn
          );
          spawn_count+= device->perfcnt.task[i].spawn;
          time_count+= device->perfcnt.task[i].time;
          flops_count+= device->perfcnt.task[i].flops;
  
          cumul.task[i].spawn += device->perfcnt.task[i].spawn;
          cumul.task[i].time += device->perfcnt.task[i].time;
          cumul.task[i].flops += device->perfcnt.task[i].flops;
          cumul.task[i].ai += device->perfcnt.task[i].ai;
        }
      }
      if (dispdevice)
      {
        printf("\t[%12s]: count=%12li, time=%8e, flops=%10e\n",
          "sum -->",
          spawn_count,
          time_count,
          flops_count
        );
      }
    }
  }
#endif

  while (_xkblas_list_context !=0)
  {
    xkblas_context_t* xkblas_ctxt = _xkblas_list_context;
    kaapi_team_deattach(xkblas_ctxt->kteam, xkblas_ctxt->kthread);
    kaapi_thread_unbind(xkblas_ctxt->kthread);
    kaapi_team_dealloc( xkblas_ctxt->kteam );
    xkblas_ctxt->kteam = 0;

    err = kaapi_hashmap_clear(&xkblas_ctxt->xkblas_ptr2handle);
    kaapi_assert(err ==0);
    err = kaapi_hashmap_destroy(&xkblas_ctxt->xkblas_ptr2handle);
    kaapi_assert(err ==0);

//TG: thread may leave and its __thread data specific deallocated?
//     *xkblas_ctxt->self = 0;
    _xkblas_list_context = xkblas_ctxt->next;
    free(xkblas_ctxt);
  }

  kaapi_atomic_unlock(&_xkblas_list_lock);

  /* can only doit for the main thread ! */
  _xkblas_self_context = 0;
  //kaapi_team_dealloc(_xkblas_global_team);
  //_xkblas_global_team = 0;

#if 00
  xkblas_free_curr_blochandle();
  while (freelist_blochandle !=0)
  {
    kaapi_stack_bloc_t* bloc = freelist_blochandle;
    freelist_blochandle = freelist_blochandle->next;
    kaapi_stackallocator_dealloc(&ctxt->st_allocator, bloc );
  }
#endif


  if (handle_cpublas != 0) 
    dlclose(handle_cpublas);
  handle_cpublas = 0;

  kaapi_finalize();
}


/*
*/
#if KAAPI_DEBUG
size_t cnt_activated_handle = 0;
size_t count = 0;
#endif
int xkblas_sync(void)
{
  xkblas_context_t* xk_ctxt = xkblas_context_get();
  kaapi_context_t* ctxt = kaapi_thread2context(xk_ctxt->kthread);

  /* activate first synchronisation access */
  kaapi_handle_t* curr = xk_ctxt->xkblas_list_sync0;
  while (curr != 0)
  {
    kaapi_handle_t* next = (kaapi_handle_t*)curr->sync0.sync;
    if (curr->sync != 0)
    {
#if KAAPI_DEBUG
      count += 
#endif
        kaapi_sched_activate_syncpoint(xk_ctxt->kthread, &curr->sync0);
#if KAAPI_DEBUG
      ++cnt_activated_handle;
#endif
    }
    curr = next;
  }

//#undef KAAPI_DEBUG
//#define KAAPI_DEBUG 0
//printf("------------ xkblas_sync: %i -> #handle: %i, #count activated: %i\n",xkblas_generation_cache, cnt_activated_handle, count);

  /* also do sync */
  //printf("--- [thread: %i] xkblas_sync: #spawn task: %li\n", kaapi_thread_kid(xk_ctxt->kthread), xk_ctxt->kthread->cnt);
  kaapi_end_dfg( xk_ctxt->kthread );
  kaapi_assert( xk_ctxt == xkblas_context_get() );

#define OLD_FLUSH 0
#if OLD_FLUSH // see xkblas_memory_invalidate_caches below
  xkblas_free_curr_blochandle();
  kaapi_hashmap_clear(&xk_ctxt->xkblas_ptr2handle);
  xk_ctxt->xkblas_list_sync0 = 0;
  xk_ctxt->xkblas_list_sync0_tail = 0;
#else
  /* reset all entries in ctxt->xkblas_list_sync0 */
  curr = xk_ctxt->xkblas_list_sync0;
  xk_ctxt->xkblas_list_sync0 = 0;
  xk_ctxt->xkblas_list_sync0_tail = 0;
  while (curr != 0)
  {
    kaapi_handle_t* next = (kaapi_handle_t*)curr->sync0.sync;
    kaapi_metadata_info_t* mdi = curr->mdi;
    if (mdi == 0)
      mdi = (curr->last->mode == KAAPI_ACCESS_SYNC ? 0: curr->last->mdi);
    kaapi_handle_init(xk_ctxt->kthread, curr, curr->sync0.data, mdi);
    curr->sync0.sync = (kaapi_access_t*)xk_ctxt->xkblas_list_sync0;
    xk_ctxt->xkblas_list_sync0 = curr;
    curr = next;
  }
#endif

  kaapi_begin_dfg( xk_ctxt->kthread, KAAPI_FRAME_FLAG_DFG_OK );
}


/*
 */
int xkblas_memory_invalidate_caches(void)
{
  //kaapi_memory_invalidate_caches();
  xkblas_context_t* ctxt = xkblas_context_get();
#if OLD_FLUSH //see just above
  xkblas_free_curr_blochandle();
  kaapi_hashmap_clear(&_kblas_ptr2handle);
  ctxt->xkblas_list_sync0 = 0;
  ctxt->xkblas_list_sync0_tail = 0;
#else
  ++ctxt->xkblas_generation_cache;

  /* invalidate each block of matrix handle list */
  xkblas_matrix_descr_t* curr = ctxt->xkblas_matrix_descr_list;
  while (curr !=0)
  {
#if KAAPI_DEBUG
    kaapi_assert(curr->owner == pthread_self());
#endif
    if (xkblas_matrix_descr_isinit(curr))
    {
      size_t Amt = curr->mt;
      size_t Ant = curr->nt;

      for (size_t m = 0; m < Amt; ++m)
        for (size_t n = 0; n < Ant; ++n)
        {
          kaapi_handle_t* h = xkblas_get_handle(curr, m, n);
          kaapi_metadata_info_t* mdi = h->mdi;
          /* if data not touch, may be mdi ==0 */
          if (mdi !=0) 
            kaapi_memory_cache_invalidate_data( mdi );
        }
    }
    KAAPI_HASHENTRIES_SET((kaapi_hashentries_t*)curr->entry, 0, xkblas_matrix_descr_t*);
    xkblas_matrix_descr_t* ncurr = curr->next;
    free(curr);
    curr = ncurr;
  }

#if KAAPI_DEBUG
  /* here check all data in cache: it should not exist trace of data owned by the current thread */
  /* verify that their is no data with owner myself in all the caches */
  _kaapi_memory_cache_verify_notself();
#endif
  ctxt->xkblas_matrix_descr_list = 0;
#endif
}


/*
 */
int xkblas_memory_free(void)
{
  kaapi_memory_invalidate_caches();
  xkblas_context_t* ctxt = xkblas_context_get();
  /* free matrix handle list */
  while (ctxt->xkblas_matrix_descr_list !=0)
  {
    xkblas_matrix_descr_t* next = ctxt->xkblas_matrix_descr_list->next;
    free(ctxt->xkblas_matrix_descr_list->handle);
    free(ctxt->xkblas_matrix_descr_list);
    ctxt->xkblas_matrix_descr_list = next;
  }
  /* kaapi_handle_t are store in matrix descriptor */
  ctxt->xkblas_list_sync0 = 0;
  ctxt->xkblas_list_sync0_tail = 0;
  kaapi_hashmap_clear(&ctxt->xkblas_ptr2handle);

#if OLD_FLUSH //see just above
  xkblas_free_curr_blochandle();
  kaapi_hashmap_clear(&ctxt->xkblas_ptr2handle);
  ctxt->xkblas_list_sync0 = 0;
  ctxt->xkblas_list_sync0_tail = 0;
#endif
}



/*
*/
int xkblas_memory_syncall(void)
{
  //double t0 = kaapi_get_elapsedtime();
  xkblas_sync();
  kaapi_memory_synchronize();
  //double t1 = kaapi_get_elapsedtime();
  //printf("Time(s) sync + memory synchronise:%f\n",t1-t0);
}

/*
*/

int xkblas_memory_coherent_async(
  int uplo, int memflag,
  size_t M, size_t N,
  void* A, size_t LD, size_t eltsize
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) /* unknown matrix, return except if debug mode because it is strange to call this
  function with unknown matrix */
  {
    kaapi_assert_debug(xkblas_matrix_descr_isinit(Ah));
    return 0;
  }

  size_t A_MB = Ah->mb;
  size_t A_NB = Ah->nb;

  size_t LDA = LD;
  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt =kaapi_self_context();
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"coherent",8);
      evt->u.s.d1.u = Ah->M;
      evt->u.s.d2.u = Ah->N;
      evt->u.s.d3.u = A_MB;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
    evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 2 /*info*/ );
    if (evt)
    {
      evt->u.s.d0.u = A_NB;
      evt->u.s.d1.u = uplo;
      evt->u.s.d2.u = memflag;
      evt->u.s.d3.u = 0;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
#endif


  /* tile iteration */
  if (uplo ==0)
  {
    for (size_t m = 0; m < Amt; m++)
      for (size_t n = 0; n < Ant; n++)
        xkblas_create_taskwriteback( Ah, m, n, LD, eltsize );
  }
  else if (uplo == CblasUpper)
  {
    for (size_t m = 0; m < Amt; m++)
      for (size_t n = m; n < Ant; n++)
        xkblas_create_taskwriteback( Ah, m, n, LD, eltsize );

    /* invalidated the complementary part if memflag == 1*/
    if (memflag ==1)
    {
      for (size_t m = 0; m < Amt; m++)
        for (size_t n = 0; n < m; n++)
          xkblas_create_taskinvalidate( Ah, m, n, LD, eltsize );
    }
  }
  else if (uplo == CblasLower)
  {
    for (size_t m = 0; m < Amt; m++)
    {
      size_t min = m+1 < Ant ? m+1 : Ant;
      for (size_t n = 0; n < min; n++)
        xkblas_create_taskwriteback( Ah, m, n, LD, eltsize );
    }

    /* invalidated the complementary part if memflag == 1*/
    if (memflag ==1)
    {
      for (size_t m = 0; m < Amt; m++)
      {
        size_t Am = m == Amt-1 ? M - m*A_MB : A_MB;
        for (size_t n = m+1; n < Ant; n++)
          xkblas_create_taskinvalidate( Ah, m, n, LD, eltsize );
      }
    }
  }
  else
  {
    printf("[%s]: invalid argument uplo\n", __func__);
    abort();
  }

  return 0;
}


int xkblas_memory_invalidate_async(const void* A)
{
  // TODO
  return 0;
}



/* 2D data distribute
   store bloc (i,j) on a grid of ressource GpxGq (i/Bp)%Gp,(j/Bq)%Gq
   If matrix is not found return EINVAL
*/
int xkblas_distribute_2Dblock_cyclic_async(
  int hlevel, int storage, int uplo, size_t NB,
  size_t m, size_t n, const void* A, size_t lda, size_t eltsize,
  size_t Bp, size_t Bq, /* blocking size */
  size_t Gp, size_t Gq  /* grid size */
)
{
  xkblas_matrix_descr_t* Ah = xkblas_find(A);
  if (!xkblas_matrix_descr_isinit(Ah)) 
  {
    xkblas_init_matrix_handle(Ah, (void*)A, m, n, lda, eltsize, NB, NB);
  }

  kaapi_ld_type_t type;
  switch (hlevel) {
    case 0: type = KAAPI_LD_BOARD; break;
    case 1: type = KAAPI_LD_GPU; break;
    case 2: type = KAAPI_LD_CORE; break;
    default:
      printf("[%s] unknown type, returns immediatly\n", __func__);
#if KAAPI_DEBUG
      abort();
#endif
      return EINVAL;
  };

  unsigned int count = kaapi_localitydomain_count(type);
  if (count ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  char* ptr = (char*)A;
  void* addr;

#if 0
  printf("Distribute on grid: %i x %i\n", Gp, Gq);
#endif
  for (size_t i=0; i<Amt; ++i)
  {
    for (size_t j=0; j<Ant; ++j)
    {
      int r = ( ((i/Bp)%Gp)*Gq + (j/Bq)%Gq ) %count;
#if 0
printf("%i ", r );
#endif
      kaapi_ldid_t ldid = kaapi_localitydomain_get_num(type, r);
      xkblas_create_distribute( Ah, i, j, lda, eltsize, ldid );
      xkblas_set_ldid(Ah, i, j, ldid );
    }
#if 0
printf("\n");
#endif
  }
  return 0;
}


/* 1D distribution of tiles
   store bloc [i,.] on a ressource (i/B)%G
   colrow = 0 -> col mapping
   colrow = 1 -> row mapping
*/
int xkblas_distribute_1Dblock_cyclic_async(
  int hlevel, int storage, int colrow, int uplo, size_t NB, 
  size_t m, size_t n, const void* A, size_t lda, size_t eltsize,
  size_t B, size_t G    /* grid size */
)
{
  size_t MB;
  MB = NB;
  if (colrow == 0)
  {
    return xkblas_distribute_2Dblock_cyclic_async(
      hlevel, storage, uplo, NB, m, n, A, lda, eltsize,
      (m+MB-1)/MB, B,
      1, G
    );
  }
  else
  {
    return xkblas_distribute_2Dblock_cyclic_async(
      hlevel, storage, uplo, NB, m, n, A, lda, eltsize,
      B, (n+NB-1)/NB,
      G, 1
    );
  }
  return EINVAL;
}


/* Return the bloc size:
   - M,N should be the dimension of the result. K depends of the Kernel.
   Return NB such that (M,N) is computed in FACTOR*NGPU blocs
*/
size_t xkblas_auto_tilesize(
  xkblas_kernel_t kernel, size_t M, size_t N, size_t K
)
{

#if 0
{
xkblas_context_t* ctxt = xkblas_context_get();
int ngpu= kaapi_localitydomain_count(KAAPI_LD_GPU);
float load[ngpu];
int lmin, lmax;
float avrg;
float delta;
int imax_gpu[ngpu];
int imax_count;
imax_count = _kaapi_compute_load_device(
    &lmin,
    &lmax,
    &avrg,
    &delta,
    imax_gpu,
    load);
char tmp[256];
ssize_t ssize = snprintf(tmp,256,"--- ctxt:%p #Lmax: %i Load: ", ctxt,imax_count);
for (int i=0; i<ngpu; ++i)
  ssize += snprintf(tmp+ssize, 256-ssize, "%f ", load[i]);
ssize += snprintf(tmp+ssize, 256-ssize,"\n");
printf(tmp);
}
#endif

  /* get default tile size and initialize internal descriptor if not yet */
  size_t NB = xkblas_get_param();
  if (NB !=0) return NB;
  size_t ngpu = xkblas_get_ngpus();
  size_t fact = 1;

  switch (kernel)
  {
    case KERN_HERK:
    case KERN_SYRK:
      {
        fact = 2;
redo_syrk:
        NB = M / (fact*ngpu);
        if (NB >= 2048)
          NB= NB & ~2047UL;
        if ((NB <= 1024) && (fact==2)) 
        { fact=1; goto redo_syrk;} 
        if (NB >4096) NB = M / (2*fact*ngpu);
        if (NB <512) NB = 512;
      }
      break;

    case KERN_HER2K:
    case KERN_SYR2K:
      {
        fact = 4;
redo_syr2k:
        NB = M / (fact*ngpu);
        NB= (NB +63UL)& ~63UL;
        if ((NB <= 2048) && (fact>2))
        { --fact; goto redo_syr2k;}
        if (NB >4096) NB = M / (2*fact*ngpu);
        if (NB <512) NB = 512;
      }
      break;

    case KERN_HEMM:
    case KERN_SYMM:
      {
        fact = 4;
        NB = M / (fact*ngpu);
        NB= NB & ~1023UL;
        if (NB >4096) NB = M / (2*fact*ngpu);
        if (NB <1024) NB = 1024;
      } 
      break;

    case KERN_TRMM:
    case KERN_TRSM:
      {
        fact = 2;
        NB = M / (fact*ngpu);
        if (NB <1024) NB = 1024;
        NB = (NB + 63) & ~63UL;
      }
      break;

    case KERN_GEMMT:
    case KERN_GEMM:
      { 
        fact = 1;
        NB = M / (fact*ngpu);
        if (M >ngpu)
        {
          if ((M> 16384)&&(NB<2048)) NB=2048;
        }
        if (NB >4096) NB = M / (2*fact*ngpu);
        if (NB <512) NB = 512;
        NB = (NB + 63) & ~63UL;
      }
      break;

    default:
      { 
        fact = 1;
        NB = M / (fact*ngpu);
        if (NB >=4096) NB = M / (2*fact*ngpu);
        if (NB <1024) NB = 1024;
        NB = (NB + 63) & ~63UL;
      }
      break;
  }

//printf("Mat size: %i tilesize: %i\n",(int)M, NB);
  return NB;
}


/* Map tile of matrix descriptor.
   Hardcoded selection between 1D or 2D mapping depending of the type of kernel
*/
int xkblas_auto_map(
  xkblas_kernel_t kernel,
  xkblas_matrix_descr_t* Ah
)
{
  switch (kernel)
  {
    case KERN_SYRK:
#if 0
    {
      xkblas_map_ij_cyclic(
        1, CblasColMajor,
        Ah->M, Ah->N, Ah->addr, Ah->ld, Ah->eltsize,
        0
      );
    } break;
#endif

    case KERN_SYR2K:
    case KERN_SYMM:
    case KERN_GEMM:
    case KERN_TRMM:
    case KERN_GEMMT:
    case KERN_TRSM:
    case KERN_HEMM:
    case KERN_HERK:
    case KERN_HER2K:
    { /* 2D Bloc cyclic */
      size_t Blkm = 1;
      size_t Blkn = 1;
      size_t ngpu = xkblas_get_ngpus();
      size_t Gm = sqrt(ngpu);
      size_t Gn = Gm;
      if (Gm ==0) { Gn = ngpu; Gm = 1; }
      else {
        /* find the most square decomposition of ngpu in Gm x Gn */
        size_t g;
        for (g = Gm+1; g>0; --g)
           if (ngpu % g == 0) break;
        if (g==0) { Gm = ngpu; Gn = 1; }
        //if (g==0) { Gm = 1; Gn = ngpu; }
        else { Gm = g; Gn = ngpu/g; }
      }
      //printf("Block2D cyclic: Blkm: %i, Blkn: %i, Gm: %i, Gn: %i\n", Blkm, Blkn, Gm, Gn);
      xkblas_map_2Dblock_cyclic(
        1, CblasColMajor,
        Ah->M, Ah->N, Ah->addr, Ah->ld, Ah->eltsize,
        Blkm, Blkn, Gm, Gn, 0
      );
    } break;

    case KERN_VOID:
      break;
  }
}




/* kaapi_get_elapsedtime
*/
uint64_t xkblas_elapsedns(void)
{
  return kaapi_get_elapsedns();
}

/* kaapi_get_elapsedns
*/
double xkblas_elapsedtime(void)
{
  return kaapi_get_elapsedtime();
}

/* Load symbol in the sub cpu BLAS library
*/
extern void xkblas_load_sym(void** ptr, const char* name)
{
  if (handle_cpublas ==0)
  {
    handle_cpublas = dlopen(XKBLAS_BLASLIB,RTLD_LAZY);
    if (handle_cpublas ==0)
    {
      printf("[xkblas]: cannot load liblas '%s'\n", XKBLAS_BLASLIB);
      abort();
    }
  }
  *ptr = dlsym( handle_cpublas, name );
  if (*ptr ==0)
  {
    fprintf(stderr,"*** Error: [xkblas] cannot load symbol '%s' from '%s'\n", name, XKBLAS_BLASLIB);
    abort();
  }
}


/* How it was compiled */
const char* get_xkblas_info(void)
{ 
  static char buffer[8192];
  static int isinit = 0;
  if (isinit ==0)
    snprintf( buffer, 8192, 
            "  TILE_SIZE: %i\n"
            "  MODE_MATH: %s\n",
         xkblas_get_param(),
         (xkblas_default_math == XKBLAS_TENSOR_OP_MATH ? "TensorCore" : "Default")
    );
  return buffer; 
}

