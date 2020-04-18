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

#ifndef _kaapi_benchmark_common_h_
#define _kaapi_benchmark_common_h_

#if defined(__STDC_NO_COMPLEX__)
# error "Compiler support for complex number is required."
#else
# include <complex.h>
typedef float complex Complex32_t;
typedef double complex Complex64_t;
typedef double CFloat64_t;
#endif

#ifndef KAAPI_NO_INCLUDE_BLAS_H
#if defined(KAAPI_BLAS_USE_OPENBLAS)
#  include <cblas.h>
#  include <lapacke.h>
#elif defined(KAAPI_BLAS_USE_MKL)
#  undef lapack_complex_float
#  undef lapack_complex_double
#  define lapack_complex_float Complex32_t
#  define lapack_complex_double Complex64_t
#  include <mkl.h>
#  include <mkl_types.h>
#  include <mkl_cblas.h>
#  include <mkl_lapacke.h>
#else
#  error "Blas library undefined"
#endif
#define KAAPI_NO_DEFAULT_BLAS_ENUM
#endif

#include "xkblas.h"
#include "flops.h"
#if KAAPI_USE_CUDA
#include <cublas_v2.h>
#endif
#include "kaapi_impl.h"
#include "kaapi_offload.h"

/* use full to have the interger required by MKL */
#if KAAPI_BLAS_USE_MKL
#define KBLAS_INT MKL_INT
#else
#define KBLAS_INT int
#endif

/* Get the mode math for the next kernel.
*/
extern xkblas_mode_math_t xkblas_get_modemath(void);

/* perfcounter per thread of tasks performed by thread
   Note that all fields are not available for all context.
*/
typedef struct  {
  uint64_t          spawn;
  uint64_t          cpu_exec;
  uint64_t          gpu_exec;
  double            flops;    /* sum of flops */
  double            ai;       /* sum of arithmetic intensity */
} xkblas_task_perfcounter_t;

/* per thread counter
*/
typedef struct {
  size_t sz_alloc;
  size_t sz_free;
  size_t max_size;
  /* kaapi_format_id_t is uint8_t */
  xkblas_task_perfcounter_t task[KAAPI_FORMAT_MAX];
} xkblas_perfcounter_t;

/*
*/
struct xkblas_context {
  /* hash map: matrix -> xkblas_matrix_descr_t */
  kaapi_thread_t*         kthread;
  kaapi_team_t*           kteam;
  kaapi_hashmap_t         xkblas_ptr2handle;
  kaapi_hashentries_t*    xkblas_mapentries[1ULL<<KAAPI_SIZE_DSM_MAP];

  kaapi_handle_t*         xkblas_list_sync0; /* re-used unused sync0->sync field as ptr to handle */
  uint32_t                xkblas_generation_cache;
  xkblas_matrix_descr_t*  xkblas_matrix_descr_list;
  xkblas_mode_math_t      xkblas_modemath;
  xkblas_perfcounter_t*   perfcnt;
  struct xkblas_context** self; /* point to its own _xkblas_self_context */
  struct xkblas_context*  next;
};


/* Common Macro... be carrefull, name not so clear */
#define _XKBLAS_STR_EXPAND2(tok) #tok
#define _XKBLAS_STR_EXPAND(tok) _XKBLAS_STR_EXPAND2(tok)
#define _XKBLAS_XCAT2(w,x,y,z) w##x##y##z
#define _XKBLAS_XCAT(w,x,y,z) _XKBLAS_XCAT2(w,x,y,z)
#define BLAS_NAME(name)   _XKBLAS_XCAT(,name,_,)
#define SYMBLAS_NAME(name) _XKBLAS_STR_EXPAND(_XKBLAS_XCAT(,name,_,))

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
  KERN_HER2K
} xkblas_kernel_t;


/* Internal matrix data structure
   From application pointer to a matrix, xkblas keep track of the internal matrix descriptor.
   The matrix descriptor contains meta information about each tile (kaapi_handle_t) and it's
   prefereed location in ldid.
   Both 2D arrays ldid and handle have dimension mtxnt and are stored in RowMajor format.
*/
struct xkblas_matrix_descr {
  void*            addr;   // matrix address
  size_t           eltsize;//
  size_t           ld;     // leading dimension
  size_t           M;      // matrix dimension, #row M
  size_t           N;      // matrix dimension, #col N
  size_t           mb;     // tile size (M)
  size_t           nb;     // tile size (N)
  size_t           mt;     // number of rows of tile
  size_t           nt;     // number of columns of tile
  uint16_t*        ldid;   // array (rowmajor) of mapping attribute or 0
  kaapi_handle_t*  handle; // array (rowmajor) of size MBxNB of handle
  size_t           capacity;// mt*nt size for handle allocation
  uint32_t         gen;    // generation number of the entry
  void*            entry;   // entry in hashtable for find
#if KAAPI_DEBUG
  pthread_t        owner;
#endif
  struct xkblas_matrix_descr* next; // to free
};
typedef struct xkblas_matrix_descr xkblas_matrix_descr_t;

#define xkblas_num_of_tiles(M,NB) (((M)+(NB)-1)/(NB))

extern int xkblas_matrix_descr_isinit(
  xkblas_matrix_descr_t* Ah
);

extern kaapi_handle_t* xkblas_get_handle(
  xkblas_matrix_descr_t* Ah,
  size_t m,
  size_t n);

/* Return ldid info for the tile A(i,j) i,j are tile indexes.
 */
extern uint16_t xkblas_get_ld(
  xkblas_matrix_descr_t* Mh,
  size_t i, size_t j
);

/* Automap block cyclic mapping for matrix of size M,N
*/
extern int xkblas_auto_map(
  xkblas_kernel_t kernel, xkblas_matrix_descr_t* Ah
);


/* Auto compute tile size
   M,N,K are the input problem sizes.
   Some kernel does not required 3 sizes and K is ignored
*/
extern size_t xkblas_auto_tilesize(
  xkblas_kernel_t kernel, size_t M, size_t N, size_t K
);


#if KAAPI_USE_CUDA
static inline cublasOperation_t cblas2cublas_op( int trans )
{
  switch (trans)
  {
    case CblasNoTrans: return CUBLAS_OP_N;
    case CblasTrans:  return CUBLAS_OP_T;
    case CblasConjTrans: return CUBLAS_OP_C;
  }
  abort();
}

static inline cublasOperation_t cblas2cublas_side( int side )
{
  switch (side)
  {
    case CblasLeft: return CUBLAS_SIDE_LEFT;
    case CblasRight: return CUBLAS_SIDE_RIGHT;
  }
  abort();
}

static inline cublasOperation_t cblas2cublas_uplo( int uplo )
{
  switch (uplo)
  {
    case CblasUpper: return CUBLAS_FILL_MODE_UPPER;
    case CblasLower: return CUBLAS_FILL_MODE_LOWER;
  }
  abort();
}

static inline cublasOperation_t cblas2cublas_diag( int diag )
{
  switch (diag)
  {
    case CblasNonUnit: return CUBLAS_DIAG_NON_UNIT;
    case CblasUnit: return CUBLAS_DIAG_UNIT;
  }
  abort();
}

#endif

/*
*/
static inline char cblas2blas_op( int trans )
{
  switch (trans)
  {
    case CblasNoTrans: return 'N';
    case CblasTrans:  return 'T';
    case CblasConjTrans: return 'C';
  }
  abort();
}

/*
*/
static inline char cblas2blas_side( int side )
{
  switch (side)
  {
    case CblasLeft: return 'L';
    case CblasRight: return 'R';
  }
  abort();
}

/*
*/
static inline char cblas2blas_diag( int diag )
{
  switch (diag)
  {
    case CblasNonUnit: return 'N';
    case CblasUnit: return 'U';
  }
  abort();
}

/*
*/
static inline char cblas2blas_fill( int uplo )
{
  switch (uplo) {
    case CblasLower: return 'L';
    case CblasUpper: return 'U';
    default:
      abort();
  }
}


static inline size_t kaapi_max( size_t a, size_t b)
{ return a<b ? b: a; }
static inline size_t kaapi_min( size_t a, size_t b)
{ return a>b ? b: a; }

static void kaapi_error( const char* s1, const char* s2)
{
  fprintf(stderr, "In %s: %s\n", s1, s2);
  abort();
}

/*
*/
extern uint32_t xkblas_context_get_generation(void);


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

/*
*/
static inline kaapi_thread_t* xkblas_self_thread()
{
  return xkblas_context_get()->kthread;
}

/* Update load information
   GPU: GPUlid
   startend: 1 start, 0 end
   flops,data: complexities (arith, byte) of the kernel
   kern: the kernel
*/
extern int xkbkas_update_load(
  int GPU,
  int startend,
  double flops,
  double data,
  xkblas_kernel_t kern
);

#if KAAPI_DEBUG
static inline void print_dmatrix(int M, int N, double*  A, int ldA)
{
  printf("%p\nmatrix( c( ", (void*)A);
  int i, j;
  for ( i = 0; i < M; i++)
  {
    for (  j = 0; j < N; j++)
    {
      char sep = ',';
      if ((i == M-1) && (j == N-1)) sep =' ';
      printf("%.15f%c", A[i+ldA*j],sep);
    }
    printf("  ");
  }
  printf(" ), nrow=%i, ncol=%i, byrow=TRUE);\n", M, N);
}

static inline void print_smatrix(int M, int N, float*  A, int ldA)
{
  printf("matrix( c( ");
  int i, j;
  for ( i = 0; i < M; i++)
  {
    for (  j = 0; j < N; j++)
    {
      char sep = ',';
      if ((i == M-1) && (j == N-1)) sep =' ';
      printf("%3f%c", A[i+ldA*j],sep);
    }
    printf("  ");
  }
  printf(" ), nrow=%i, ncol=%i, byrow=TRUE);\n", M, N);
}
#endif

#endif /* _kaapi_benchmark_common_h_ */
