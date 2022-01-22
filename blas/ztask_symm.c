/**
 *
 * @file parsec/codelet_zsymm.c
 *
 * @copyright 2009-2015 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zsymm PaRSEC codelet
 *
 * @version 1.0.0
 * @author Reazul Hoque
 * @author Thierry Gautier
 * @precisions normal z -> c d s
 *
 */
#include "common.h"
#include "xkblas.h"
#include "ztask.h"
#include "ztask_internal.h"

#define ROWDIM(v,m,n) ((v) == CblasLeft ? m : n)
#define COLDIM(v,m,n) ((v) == CblasLeft ? n : m)


#define STR_EXPAND(tok) #tok
#define TASK_NAME zsymm
#define STRNAME  "zsymm"
#define NAME(x) x##_##zsymm
#define PNAME(x) zsymm##_##x
#define SIZE_NPARAM 3
#define NPARAM 3
#define MODE_PARAM {KAAPI_ACCESS_MODE_R,KAAPI_ACCESS_MODE_R,arg->beta == 0.0 ? KAAPI_ACCESS_MODE_W : KAAPI_ACCESS_MODE_RW}
#define ADDR_PARAM {&arg->A, &arg->B, &arg->C}
#define VIEW_PARAM {\
    { ROWDIM(arg->side, &arg->m, &arg->n), ROWDIM(arg->side, &arg->m, &arg->n), &arg->lda},\
    { &arg->m, &arg->n, &arg->ldb},\
    { &arg->m, &arg->n, &arg->ldc}}
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "chartreuse1"
#define TASK_FLOPS FLOPS_ZSYMM(arg->side, arg->m, arg->n)
#define TASK_DATA  DATA_ZSYMM(arg->side,arg->m,arg->n)

/**
 *
 * @ingroup CORE_Complex64_t
 *
 */
 typedef struct {
  int side;
  int uplo;
  size_t m;
  size_t n;
  Complex64_t alpha;
  kaapi_access_t A;
  size_t lda;
  kaapi_access_t B;
  size_t ldb;
  Complex64_t beta;
  kaapi_access_t C;
  size_t ldc;
  xkblas_mode_math_t mm;
} NAME(Arg);

static kaapi_format_id_t NAME(task_fmtid) = 0;


void INSERT_TASK_zsymm(
    int side, int uplo,
    size_t m, size_t n, 
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, int Am, int An, int lda,
                       xkblas_matrix_descr_t *Bh, int Bm, int Bn, int ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *Ch, int Cm, int Cn, int ldc)
{
    kaapi_task_t* task;
    xkblas_context_t* ctxt = xkblas_context_get();
    kaapi_thread_t* thread = ctxt->kthread;
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_withperfcnt_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst((kaapi_task_withperfcnt_t*)task,NAME(Arg));

    taskarg->side = side;
    taskarg->uplo = uplo;
    taskarg->m = m;
    taskarg->n = n;
    taskarg->alpha = alpha;
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah, Am, An));
    taskarg->lda = lda;
    kaapi_update_dependencies(thread, &taskarg->B, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Bh, Bm, Bn));
    taskarg->ldb = ldb;
    kaapi_update_dependencies(thread, &taskarg->C, task,
        KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Ch, Cm, Cn));
    taskarg->beta = beta;
    taskarg->ldc = ldc;
    taskarg->mm = xkblas_get_modemath();
#if KAAPI_USE_OCR
    /* OCR on the third parameter */
    kaapi_task_set_ld(task, KAAPI_TASK_OCR_PARAM, 2);
#else
    uint16_t ldid = xkblas_get_ld(Ch, Cm, Cn);
    kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, ldid);
#endif
    kaapi_taskflag_set(task, KAAPI_TASK_PERFCNT);
    kaapi_task_commit( thread, task );
}


static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  xkblas_zsymm_native(
      arg->side, arg->uplo,
      arg->m, arg->n,
      &arg->alpha,
      (Complex64_t*)arg->A.data, arg->lda,
      (Complex64_t*)arg->B.data, arg->ldb,
      &arg->beta,
      (Complex64_t*)arg->C.data, arg->ldc
  );
}

#if KAAPI_USE_CUDA||KAAPI_USE_HIP
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  cublasZsymm((cublasHandle_t)handle,
      cblas2cublas_side(arg->side), cblas2cublas_uplo(arg->uplo),
      arg->m, arg->n, 
      (const cuDoubleComplex*)&arg->alpha,
      (const cuDoubleComplex*)arg->A.data, arg->lda,
      (const cuDoubleComplex*)arg->B.data, arg->ldb,
      (const cuDoubleComplex*)&arg->beta,
      (cuDoubleComplex*)arg->C.data, arg->ldc
  );
}
#endif

#include "task_format.h"

