/**
 *
 * @file quark/codelet_ztrsm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon ztrsm Quark codelet
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Hatem Ltaief
 * @author Jakub Kurzak
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Philippe Virouleau
 * @author Thierry Gautier
 * @date 2018-11-20
 * @precisions normal z -> c d s
 *
 */
#include "common.h"
#include "xkblas.h"
#include "task_z.h"
#include "task_z_internal.h"

#ifdef KAAPI_DEBUG
#undef KAAPI_DEBUG
#endif

#define ROWDIM(v,m,n) ((v) == CblasNoTrans ? m : n)
#define COLDIM(v,m,n) ((v) == CblasNoTrans ? n : m)

#define STR_EXPAND(tok) #tok
#define TASK_NAME zsyr2k
#define STRNAME  "zsyr2k"
#define NAME2(x,s) x##_##s
#define NAME(x) NAME2(x,zsyr2k)
#define PNAME(x) zsyr2k##_##x
#define NPARAM 3
#define MODE_PARAM {KAAPI_ACCESS_MODE_R, KAAPI_ACCESS_MODE_R, arg->beta == 0.0 ? KAAPI_ACCESS_MODE_W : KAAPI_ACCESS_MODE_RW }
#define ADDR_PARAM {&arg->A, &arg->B, &arg->C}
#define VIEW_PARAM {\
    {ROWDIM(arg->trans, &arg->n, &arg->k), COLDIM(arg->trans, &arg->n, &arg->k), &arg->lda}, \
    {ROWDIM(arg->trans, &arg->n, &arg->k), COLDIM(arg->trans, &arg->n, &arg->k), &arg->ldb}, \
    {&arg->n, &arg->n, &arg->ldc}}
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "darkorchid4"
#define TASK_FLOPS FLOPS_ZSYR2K(arg->n,arg->k)
#define TASK_DATA  DATA_ZSYR2K(arg->n,arg->k)

/**
 *
 * @ingroup CORE_Complex64_t
 *
 */
 typedef struct {
  int uplo;
  int trans;
  size_t n;
  size_t k;
  Complex64_t alpha;
  kaapi_access_t A;
  size_t lda;
  Complex64_t beta;
  kaapi_access_t B;
  size_t ldb;
  kaapi_access_t C;
  size_t ldc;
  xkblas_mode_math_t mm;
} NAME(Arg);

static kaapi_format_id_t NAME(task_fmtid) = 0;

void INSERT_TASK_zsyr2k(
    int uplo, int trans,
    size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *Ch, size_t Cm, size_t Cn, size_t ldc
)
{
    kaapi_task_t* task;
    xkblas_context_t* ctxt = xkblas_context_get();
    kaapi_thread_t* thread = ctxt->kthread;
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_withperfcnt_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst((kaapi_task_withperfcnt_t*)task,NAME(Arg));

    taskarg->uplo = uplo;
    taskarg->trans = trans;
    taskarg->n = n;
    taskarg->k = k;
    taskarg->alpha = alpha;
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah, Am, An));
    taskarg->lda = lda;
    taskarg->beta = beta;
    kaapi_update_dependencies(thread, &taskarg->B, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Bh, Bm, Bn));
    taskarg->ldb = ldb;
    kaapi_update_dependencies(thread, &taskarg->C, task,
        beta == 0.0 ? KAAPI_ACCESS_MODE_W : KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Ch, Cm, Cn));
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
  xkblas_zsyr2k_native(
      arg->uplo, arg->trans,
      arg->n, arg->k,
      &arg->alpha, arg->A.data, arg->lda, arg->B.data, arg->ldb,
      &arg->beta,  arg->C.data, arg->ldc
  );
}

#if KAAPI_USE_CUDA
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  cublasZsyr2k((cublasHandle_t)handle,
        cblas2cublas_uplo(arg->uplo), cblas2cublas_op(arg->trans),
        arg->n, arg->k,
        (const cuDoubleComplex*)&arg->alpha, arg->A.data, arg->lda, arg->B.data, arg->ldb,
        (const cuDoubleComplex*)&arg->beta,  arg->C.data, arg->ldc
  );
}
#endif

#include "task_format.h"
