/**
 *
 * @file quark/codelet_ztrmm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon ztrmm Quark codelet
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

#define ROWDIM(v,m,n) ((v) == CblasLeft ? m : n)
#define COLDIM(v,m,n) ((v) == CblasLeft ? n : m)

#define STR_EXPAND(tok) #tok
#define TASK_NAME ztrmm
#define STRNAME  "ztrmm"
#define NAME2(x,s) x##_##s
#define NAME(x) NAME2(x,ztrmm)
#define PNAME(x) ztrmm##_##x
#define NPARAM 2
#define MODE_PARAM {KAAPI_ACCESS_MODE_R,KAAPI_ACCESS_MODE_RW }
#define ADDR_PARAM {&arg->A, &arg->B}
#define VIEW_PARAM {\
    {ROWDIM(arg->side, &arg->m, &arg->n), ROWDIM(arg->side, &arg->m, &arg->n), &arg->lda}, \
    {&arg->m, &arg->n, &arg->ldb}}
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "darkorchid2"
#define TASK_FLOPS FLOPS_ZTRMM(arg->side,arg->m,arg->n)
#define TASK_DATA  DATA_ZTRMM(arg->side,arg->m,arg->n)

/**
 *
 * @ingroup CORE_Complex64_t
 *
 */
 typedef struct {
  int side;
  int uplo;
  int transA;
  int diag;
  size_t m;
  size_t n;
  Complex64_t alpha;
  kaapi_access_t A;
  size_t lda;
  kaapi_access_t B;
  size_t ldb;
#if defined(KAAPI_DEBUG)
  /* debug */
  size_t Am;
  size_t An;
  size_t Bm;
  size_t Bn;
#endif
  xkblas_mode_math_t mm;
 } NAME(Arg);

static kaapi_format_id_t NAME(task_fmtid) = 0;

void INSERT_TASK_ztrmm(
    int side, int uplo, int transA, int diag,
    size_t m, size_t n, 
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda,
    xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldb)
{
    kaapi_task_t* task;
    xkblas_context_t* ctxt = xkblas_context_get();
    kaapi_thread_t* thread = ctxt->kthread;
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_withperfcnt_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst((kaapi_task_withperfcnt_t*)task,NAME(Arg));

    taskarg->side = side;
    taskarg->uplo = uplo;
    taskarg->transA = transA;
    taskarg->diag = diag;
    taskarg->m = m;
    taskarg->n = n;
#if defined(KAAPI_DEBUG)
    taskarg->Am = Am;
    taskarg->An = An;
    taskarg->Bm = Bm;
    taskarg->Bn = Bn;
#endif
    taskarg->alpha = alpha;
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah, Am, An));
    taskarg->lda = lda;
    kaapi_update_dependencies(thread, &taskarg->B, task,
        KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Bh, Bm, Bn));
    taskarg->ldb = ldb;
    taskarg->mm = xkblas_get_modemath();
#if KAAPI_USE_OCR
    /* OCR on the third parameter */
    kaapi_task_set_ld(task, KAAPI_TASK_OCR_PARAM, 1);
#else
    kaapi_ldid_t ldid = xkblas_get_ld(Bh, Bm, Bn);
    kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, ldid);
#endif
    kaapi_taskflag_set(task, KAAPI_TASK_PERFCNT);
    kaapi_task_commit( thread, task );
}


static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
#if defined(KAAPI_DEBUG)
  printf("%s: %s x = %s \n",__func__,
      kaapi_dbg_get_name(arg->A.data),
      kaapi_dbg_get_name(arg->B.data)
  );
#endif
  xkblas_ztrmm_native(
      arg->side, arg->uplo, arg->transA, arg->diag,
      arg->m, arg->n,
      &arg->alpha, arg->A.data, arg->lda,
      arg->B.data, arg->ldb
  );
}

#if KAAPI_USE_CUDA
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
#if defined(KAAPI_DEBUG)
  printf("%s: A(%i,%i) X = B(%i,%i)\n",__func__,
      arg->Am, arg->An, arg->Bm, arg->Bn
  );
#endif
  cublasZtrmm((cublasHandle_t)handle,
        cblas2cublas_side(arg->side), cblas2cublas_uplo(arg->uplo),
        cblas2cublas_op(arg->transA), cblas2cublas_diag(arg->diag),
        arg->m, arg->n,
        (const cuDoubleComplex*)&arg->alpha,
        arg->A.data, arg->lda,
        arg->B.data, arg->ldb,
        arg->B.data, arg->ldb);
}
#endif

#include "task_format.h"
