/**
 *
 * @file quark/codelet_zpotrf.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zpotrf Quark codelet
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Hatem Ltaief
 * @author Jakub Kurzak
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
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
#define TASK_NAME zpotrf
#define STRNAME  "zpotrf"
#define NAME2(x,s) x##_##s
#define NAME(x) NAME2(x,zpotrf)
#define PNAME(x) zpotrf##_##x
#define NPARAM 1
#define MODE_PARAM { KAAPI_ACCESS_MODE_RW }
#define ADDR_PARAM {&arg->A}
#define VIEW_PARAM {\
    {&arg->n, &arg->n, &arg->lda} }
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "darkorchid2"

/**
 *
 * @ingroup CORE_Complex64_t
 *
 */
 typedef struct {
  int uplo;
  size_t n;
  kaapi_access_t A;
  size_t lda;
  xkblas_mode_math_t mm;
 } NAME(Arg);

static kaapi_format_id_t NAME(task_fmtid) = 0;

void INSERT_TASK_zpotrf(
    int uplo,
    size_t n,
    xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda
)
{
    kaapi_task_t* task;
    kaapi_thread_t* thread = xkblas_self_thread();
    kaapi_context_t* kctxt = kaapi_thread2context(thread);
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst(task,NAME(Arg));

    taskarg->uplo = uplo;
    taskarg->n = n;
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah, Am, An));
    taskarg->lda = lda;
    taskarg->mm = xkblas_get_modemath();
    kaapi_task_set_ld(task, 0, xkblas_get_ld(Ah, Am, An ));
    kaapi_task_commit( thread, task );
}


static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  char uplo = cblas2blas_fill(arg->uplo);
  LAPACKE_zpotrf_work(
      LAPACK_COL_MAJOR,
      uplo,
      arg->n,
      arg->A.data, arg->lda);
}

#define NO_GPU_IMPL 1
#include "task_format.h"
