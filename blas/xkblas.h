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

/* xkblas is 'blas over kaapi' (kblas was already known as kaust blas).
 * xkblas is subset of blas subroutines over multi-GPU architecture.
 * It was provided as extended set of routines to launch asynchronous call
 * to blas subroutine and a set of functions to distribute or get results.
 * xkblas also include a drop-in replacement of blas library.
 */

#ifndef _xkblas_h_
#define _xkblas_h_
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* */
#if defined(__STDC_NO_COMPLEX__)
# error "Compiler support for complex number is required."
#else
# include <complex.h>
typedef float complex Complex32_t;
typedef double complex Complex64_t;
typedef double CFloat64_t;
#endif

/* xkblas context
*/
struct xkblas_context;
typedef struct xkblas_context xkblas_context_t;

/*
*/
extern __thread xkblas_context_t* _xkblas_self_context;
extern xkblas_context_t* xkblas_context_alloc(void);
static inline xkblas_context_t* xkblas_context_get(void)
{
  if (_xkblas_self_context ==0)
    xkblas_context_alloc();
  return _xkblas_self_context;
}


/* Xblas mode math
*/
typedef enum {
  XKBLAS_DEFAULT_MATH,
  XKBLAS_TENSOR_OP_MATH
} xkblas_mode_math_t;

/* Set the mode math for the next kernels.
*/
extern void xkblas_set_modemath( xkblas_mode_math_t );

/* Here we include the header for the underlayer BLAS library.
   Should really be ?
*/
#ifndef KAAPI_NO_DEFAULT_BLAS_ENUM
/* traditional definitions: before xkblas.h */
typedef enum {CblasRowMajor=101, CblasColMajor=102} CBLAS_ORDER;
typedef enum {CblasNoTrans=111, CblasTrans=112, CblasConjTrans=113, CblasConjNoTrans=114} CBLAS_TRANSPOSE;
typedef enum {CblasUpper=121, CblasLower=122} CBLAS_UPLO;
typedef enum {CblasNonUnit=131, CblasUnit=132} CBLAS_DIAG;
typedef enum {CblasLeft=141, CblasRight=142} CBLAS_SIDE;
typedef CBLAS_ORDER CBLAS_LAYOUT;
#endif


/* List of kernel implemented in the runtime */
typedef enum {
  KERN_VOID,
  KERN_GEMM,
  KERN_GEMMT,
  KERN_TRMM,
  KERN_TRSM,
  KERN_SYMM,
  KERN_SYRK,
  KERN_SYR2K,
  KERN_HEMM,
  KERN_HERK,
  KERN_HER2K,
  KERN_SWAP

} xkblas_kernel_t;

/*
*/
static inline int xkblas_blas2cblas_trans( const char* trans )
{
  switch (trans[0]) {
    case 'n':
    case 'N': return CblasNoTrans;
    case 't':
    case 'T': return CblasTrans;
    case 'c':
    case 'C': return CblasConjTrans;
    default:
      abort();
  }
}

/*
*/
static inline int xkblas_blas2cblas_side( const char* side )
{
  switch (side[0]) {
    case 'l':
    case 'L': return CblasLeft;
    case 'r':
    case 'R': return CblasRight;
    default:
      abort();
  }
}


/*
*/
static inline int xkblas_blas2cblas_fill( const char* uplo )
{
  switch (uplo[0]) {
    case 'l':
    case 'L': return CblasLower;
    case 'u':
    case 'U': return CblasUpper;
    default:
      abort();
  }
}

/*
*/
static inline int xkblas_blas2cblas_diag( const char* diag )
{
  switch (diag[0]) {
    case 'n':
    case 'N': return CblasNonUnit;
    case 'u':
    case 'U': return CblasUnit;
    default:
      abort();
  }
}

/*
*/
extern int xkblas_init(void);

/*
*/
extern int xkblas_finalize(void);

/*
*/
extern void xkblas_load_sym(void** ptr, const char* name);

/*
*/
extern int xkblas_sync(void);

/*
 */
struct xkblas_matrix_descr;
typedef struct xkblas_matrix_descr xkblas_matrix_descr_t;

/* Returns the matrix handle for matrix A or 0 if not found
 */
extern xkblas_matrix_descr_t* xkblas_find( const void* A );

/*
*/
extern int xkblas_matrix_descr_isinit(
  xkblas_matrix_descr_t* Ah
);

/*
 */
extern int xkblas_init_matrix_handle( xkblas_matrix_descr_t* Ah,
  void* A, size_t M, size_t N, size_t LD, size_t eltsize, size_t MB, size_t NB
);


/* Auto compute tile size
   M,N,K are the input problem sizes.
   Some kernel does not required 3 sizes and K is ignored
*/
extern size_t xkblas_auto_tilesize(
  xkblas_kernel_t kernel, size_t M, size_t N, size_t K
);


/* Allocate a Host registered bloc of sz byte if CUDA is configured on.
   Else return a bloc as if returned by malloc.
 */
extern void* xkblas_malloc( size_t sz );

/* Free a Host registered bloc allocated by xkblas_malloc.
 */
extern void xkblas_free( void* ptr, size_t sz );

/* Non blocking call host registration of the memory block (ptr, sz).
   Returns an handle that could be employed for wait or testing
   the completion of the request.
 */
extern uint64_t xkblas_register_memory_async( void* ptr, size_t sz );

/* Non blocking call to host unregistration of the memory block (ptr, sz).
   Memory bloc (ptr,sz) should have been previously registered with 
   xkblas_register_memory_async or xkblas_register_memory.
   Returns an handle that could be employed for wait or testing
   the completion of the request.
 */
extern uint64_t xkblas_unregister_memory_async( void* ptr, size_t sz );

/* Test completion of the asynchronous host registration operation with
   given handle. Handle should has been returned by previous call to
   xkblas_register_memory_async.
   Returns 0 if operation is not completed, else returns !=0 value.
   If the test is true, then the memory block specified in the host registration
   operation has been 'host registered'.
 */
extern int xkblas_register_memory_test( uint64_t );

/* Wait completion of the host registration operation with handle
   returned by previous call to xkblas_register_memory_async.
   On return the memory block has been 'host registered'.
 */
extern int xkblas_register_memory_wait( uint64_t );

/* Wait all previously non blocking host registration operations.
 */
extern int xkblas_register_memory_waitall( );

/* Blocking version of xkblas_register_memory_async.
   Equivalent to xkblas_register_memory_async followed by call to
   xkblas_register_memory_wait.
 */
extern int xkblas_register_memory( void* ptr, size_t sz );

/* Blocking version of xkblas_unregister_memory_async
 */
extern int xkblas_unregister_memory( void* ptr, size_t sz );

/* Give name of tile for various output (graph, debug).
   Each tile will be display as name(i,j) where i,j is the position in A.
   If mb or nb ==0 then use the value of xkblas_get_param()
*/
extern int xkblas_dbg_setname(
  const char* name,
  xkblas_matrix_descr_t* Mh
);

/* Dump the DAG corresponding to the current spawned task.
*/
extern void xkblas_dbg_dump_graph( const char* name );

/* Recall to map the tiles of matrix A in a 2D bloc cyclic mapping fashion
   with blocking factor (Bp,Bq) on the ressource grid (Gp,Gq).
   If force !=0 then force remapping of the tile even if mapping is already defined.
 */
extern int xkblas_map_2Dblock_cyclic(
  int hlevel, int storage, size_t m, size_t n,
  const void* A, size_t ld, size_t eltsize,
  size_t Bp, size_t Bq, /* blocking size */
  size_t Gp, size_t Gq,  /* grid size */
  int force
);

/* Recall to map the tiles of matrix A in a 1D bloc cyclic mapping fashion
   with blocking factor B on the G ressources.
   If colrow is 0 then do mapping by column. Else it is row mapping which is considered.
   If force !=0 then force remapping of the tile even if mapping is already defined.
*/
extern int xkblas_map_1Dblock_cyclic(
  int hlevel, int storage, int colrow, size_t m, size_t n,
  const void* A, size_t ld, size_t eltsize,
  size_t B, size_t G,    /* grid size */
  int force
);

/* Distribute the tiles of A among the ressource gird (Gp,Gq) using a 2D block cyclic distribution
   with blocking factor (Bp,Bq).
*/
extern int xkblas_distribute_2Dblock_cyclic_async(
  int hlevel, int storage, int uplo, size_t NB,
  size_t m, size_t n, const void* A, size_t ld, size_t eltsize,
  size_t Bp, size_t Bq, /* blocking size */
  size_t Gp, size_t Gq  /* grid size */
);

/* Distribute the tiles of A among the G ressources using a 1D block cyclic distribution
   with blocking factor B.
*/
extern int xkblas_distribute_1Dblock_cyclic_async(
  int hlevel, int storage, int uplo, int colrow, 
  size_t NB, size_t m, size_t n, const void* A, size_t ld, size_t eltsize,
  size_t B, size_t G    /* grid size */
);


/* Make coherent view of the matrix with respect to copies located on devices.
   This is a non blocking method. xkblas_sync() should be called to ensure completion
   of operations.
   - if uplo is non null, synchronize either the upper (CblasUpper) or lower (CblasLower)
   part of the matrix.
   - if memflag is 1 (strict mode) then other part of the matrix is marked
   as "non usefull" and could not be recover.
   - if memflag is 0 then the other part is keep untouch in the dsm.
 */
extern int xkblas_memory_coherent_async(
  int uplo, int memflag,
  size_t M, size_t N,
  void* A, size_t ld, size_t elsize
);

/* Wait until all tasks have been executed and memory on the host is coherent with copies on
   different devices.
   Equivalent to well formed calls to xkblas_memory_sync on all matrices followed
   by a call to xkblas_sync.
*/
extern int xkblas_memory_syncall(void);


/* Invalidate all entries in cache in order to force transfer back between host and device
   for tile of the matrix A.
*/
extern int xkblas_memory_invalidate(const void* A);

/* Invalidate all data in cache previously used by the current thread.
*/
extern int xkblas_memory_invalidate_caches(void);

/* Free all internal data structure related to memory management
*/
extern int xkblas_memory_free(void);

/* Get the tile size
*/
extern size_t xkblas_get_param(void);

/* Get the number of GPUs 
*/
extern int xkblas_get_ngpus(void);

/* Set the number of GPUs
*/
extern int xkblas_set_ngpus(int ngpus);

/* Set the tile size and the element
 */
extern void xkblas_set_param(size_t nb, size_t p );

/** kaapi_get_elapsedtime
    The function kaapi_get_elapsedtime() will return the elapsed time in second
    since an epoch.
    Default (generic) function is based on system clock (gettimeofday).
    Optimized function is based on clock_gettime.
*/
extern uint64_t xkblas_elapsedns(void);

/** kaapi_get_elapsedns
    The function kaapi_get_elapsedtime() will return the elapsed time since an epoch
    in nano second unit.
*/
extern double xkblas_elapsedtime(void);


#include "zxkblas.h"
#include "cxkblas.h"
#include "dxkblas.h"
#include "sxkblas.h"

#endif /* _xkblas_h_ */
