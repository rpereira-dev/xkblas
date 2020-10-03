/**
 *
 * @file codelet_zgemm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2016 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zgemm StarPU codelet
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
#define TASK_NAME zgemm
#define STRNAME  "zgemm"
#define NAME(x) x##_##zgemm
#define PNAME(x) zgemm##_##x
#define NPARAM 3
#define MODE_PARAM {KAAPI_ACCESS_MODE_R,KAAPI_ACCESS_MODE_R,arg->beta == 0.0 ? KAAPI_ACCESS_MODE_W : KAAPI_ACCESS_MODE_RW}
#define ADDR_PARAM {&arg->A, &arg->B, &arg->C}
#define VIEW_PARAM {\
    { ROWDIM(arg->transA, &arg->m, &arg->k), COLDIM(arg->transA, &arg->m, &arg->k), &arg->lda},\
    { ROWDIM(arg->transB, &arg->k, &arg->n), COLDIM(arg->transB, &arg->k, &arg->n), &arg->ldb},\
    { &arg->m, &arg->n, &arg->ldc}}
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "chartreuse3"
#define TASK_FLOPS FLOPS_ZGEMM(arg->m,arg->n,arg->k)
#define TASK_DATA  DATA_ZGEMM(arg->m,arg->n,arg->k)


/**
 *
 * @ingroup CORE_Complex64_t
 *
 */
 typedef struct {
  int transA;
  int transB;
  size_t m;
  size_t n;
  size_t k;
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

void INSERT_TASK_zgemm(
    int transA, int transB,
    size_t m, size_t n, size_t k, 
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *Ch, size_t Cm, size_t Cn, size_t ldc)
{
    kaapi_task_t* task;
    xkblas_context_t* ctxt = xkblas_context_get();
    kaapi_thread_t* thread = ctxt->kthread;
    kaapi_context_t* kctxt = kaapi_thread2context(thread);
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_withperfcnt_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst((kaapi_task_withperfcnt_t*)task,NAME(Arg));

    taskarg->transA = transA;
    taskarg->transB = transB;
    taskarg->m = m;
    taskarg->n = n;
    taskarg->k = k;
    taskarg->alpha = alpha;
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Ah, Am, An));
    taskarg->lda = lda;
    kaapi_update_dependencies(thread, &taskarg->B, task,
        KAAPI_ACCESS_MODE_R, xkblas_get_handle(Bh, Bm, Bn));
    taskarg->ldb = ldb;
    kaapi_update_dependencies(thread, &taskarg->C, task,
        beta == 0.0 ? KAAPI_ACCESS_MODE_W : KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Ch, Cm, Cn));
    taskarg->beta = beta;
    taskarg->ldc = ldc;
    taskarg->mm = xkblas_get_modemath();
    kaapi_ldid_t ldid = xkblas_get_ld(Ch, Cm, Cn );
    kaapi_task_set_ld(task, 0, ldid);
    kaapi_taskflag_set(task, KAAPI_TASK_PERFCNT);
    kaapi_task_commit( thread, task );

#if KAAPI_DEBUG
  printf("%s: @:%p %s[%lu,%lu, ld:%lu]%s x @:%p %s[%lu,%lu, ld:%lu]%s -> @:%p %s[%lu,%lu, ld:%lu]\n",__func__, 
      A, kaapi_dbg_get_name(A), m, k, lda, (transA==CblasNoTrans ? "":"^t"),
      B, kaapi_dbg_get_name(B), k, n, ldb, (transB==CblasNoTrans ? "":"^t"),
      C, kaapi_dbg_get_name(C), m, n, ldc
  );
#endif
}


static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
#if KAAPI_DEBUG
  printf("%s: @:%p %s[%lu,%lu, ld:%lu]%s x @:%p %s[%lu,%lu, ld:%lu]%s -> @:%p %s[%lu,%lu, ld:%lu]\n",__func__, 
      arg->A.data, kaapi_dbg_get_name(arg->A.data), arg->m, arg->k, arg->lda, (arg->transA==CblasNoTrans ? "":"^t"),
      arg->B.data, kaapi_dbg_get_name(arg->B.data), arg->k, arg->n, arg->ldb, (arg->transB==CblasNoTrans ? "":"^t"),
      arg->C.data, kaapi_dbg_get_name(arg->C.data), arg->m, arg->n, arg->ldc
  );
#endif
  xkblas_zgemm_native(
      arg->transA, arg->transB,
      arg->m, arg->n, arg->k,
      &arg->alpha,
      (Complex64_t*)arg->A.data, arg->lda,
      (Complex64_t*)arg->B.data, arg->ldb,
      &arg->beta,
      (Complex64_t*)arg->C.data, arg->ldc
  );
#if 0
  cblas_zgemm(
      CblasColMajor,
      arg->transA, arg->transB,
      arg->m, arg->n, arg->k,
      CBLAS_SADDR(arg->alpha),
      (Complex64_t*)arg->A.data, arg->lda,
      (Complex64_t*)arg->B.data, arg->ldb,
      CBLAS_SADDR(arg->beta),
      (Complex64_t*)arg->C.data, arg->ldc
  );
#endif

}

#if KAAPI_USE_CUDA
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
#if KAAPI_DEBUG
  printf("%s: @:%p %s[%lu,%lu, ld:%lu]%s x @:%p %s[%lu,%lu, ld:%lu]%s -> @:%p %s[%lu,%lu, ld:%lu]\n",__func__, 
      arg->A.data, kaapi_dbg_get_name(arg->A.data), arg->m, arg->k, arg->lda, (cblas2cublas_op(arg->transA)==CUBLAS_OP_N ? "":"^t"),
      arg->B.data, kaapi_dbg_get_name(arg->B.data), arg->k, arg->n, arg->ldb, (cblas2cublas_op(arg->transB)==CUBLAS_OP_N ? "":"^t"),
      arg->C.data, kaapi_dbg_get_name(arg->C.data), arg->m, arg->n, arg->ldc
  );
#endif
  double flops = FLOPS_ZGEMM(arg->m,arg->n,arg->k);
#if KAAPI_USE_PERFCOUNTER
  kaapi_stat_internal_t* kpi = &kaapi_perthread_stat[kaapi_offload_self_device()->ctxt->tid];
#endif
  cublasStatus_t res;
#if defined(PRECISION_s)
  if (arg->mm == XKBLAS_TENSOR_OP_MATH)
  {
    res = cublasSetMathMode((cublasHandle_t)handle, CUBLAS_TENSOR_OP_MATH);
#if KAAPI_USE_PERFCOUNTER
    if (
        (arg->m % 4 == 0)
     && (arg->k % 8 == 0)
     && (((intptr_t)arg->A.data) % 16 == 0)
     && (((intptr_t)arg->B.data) % 16 == 0)
     && (((intptr_t)arg->C.data) % 16 == 0)
     && (arg->lda % 4 == 0)
     && (arg->ldb % 4 == 0)
     && (arg->ldc % 4 == 0)
    )
    {
      ++kpi->counter[KAAPI_CNT_GEMM_ONTC];
      kpi->dcounter[KAAPI_FLOPS_GEMM_ONTC] += flops;
    }
    else
    {
      ++kpi->counter[KAAPI_CNT_GEMM_NOTONTC];
      kpi->dcounter[KAAPI_FLOPS_GEMM_NOTONTC] += flops;
    }
#endif

#if 0//KAAPI_DEBUG
  #if 1
    int warn = 0;
    /* emit warning if constraints defined in CUDA-10.1 are not satisfied */
    warn |= kaapi_assert_warning(arg->m % 4 == 0);
    warn |= kaapi_assert_warning(arg->k % 8 == 0);
    warn |= kaapi_assert_warning(((intptr_t)arg->A.data) % 16 == 0);
    warn |= kaapi_assert_warning(((intptr_t)arg->B.data) % 16 == 0);
    warn |= kaapi_assert_warning(((intptr_t)arg->C.data) % 16 == 0);
    warn |= kaapi_assert_warning(arg->lda % 4 == 0);
    warn |= kaapi_assert_warning(arg->ldb % 4 == 0);
    warn |= kaapi_assert_warning(arg->ldc % 4 == 0);
    if (warn) {
      printf("*** warning: %s: @:%p %s[%lu,%lu, ld:%lu]%s x @:%p %s[%lu,%lu, ld:%lu]%s -> @:%p %s[%lu,%lu, ld:%lu]\n",__func__,
        arg->A.data, kaapi_dbg_get_name(arg->A.data), arg->m, arg->k, arg->lda, (cblas2cublas_op(arg->transA)==CUBLAS_OP_N ? "":"^t"),
        arg->B.data, kaapi_dbg_get_name(arg->B.data), arg->k, arg->n, arg->ldb, (cblas2cublas_op(arg->transB)==CUBLAS_OP_N ? "":"^t"),
        arg->C.data, kaapi_dbg_get_name(arg->C.data), arg->m, arg->n, arg->ldc
      );
    }
  #else
    kaapi_assert(arg->m % 4 == 0);
    kaapi_assert(arg->k % 8 == 0);
    kaapi_assert(((intptr_t)arg->A.data) % 16 == 0);
    kaapi_assert(((intptr_t)arg->B.data) % 16 == 0);
    kaapi_assert(((intptr_t)arg->C.data) % 16 == 0);
    kaapi_assert(arg->lda % 4 == 0);
    kaapi_assert(arg->ldb % 4 == 0);
    kaapi_assert(arg->ldc % 4 == 0);
  #endif
#endif // KAAPI_DEBUG
  }
  else
#endif // defined(PRECISION_s)
  {
#if KAAPI_USE_PERFCOUNTER
    ++kpi->counter[KAAPI_CNT_GEMM_NOTONTC];
    kpi->dcounter[KAAPI_FLOPS_GEMM_NOTONTC]+= flops;
#endif
    res = cublasSetMathMode((cublasHandle_t)handle, CUBLAS_DEFAULT_MATH);
  }
  kaapi_assert(res == CUBLAS_STATUS_SUCCESS);

  cublasZgemm((cublasHandle_t)handle,
      cblas2cublas_op(arg->transA), cblas2cublas_op(arg->transB),
      arg->m, arg->n, arg->k,
      (const cuDoubleComplex*)&arg->alpha,
      (const cuDoubleComplex*)arg->A.data, arg->lda,
      (const cuDoubleComplex*)arg->B.data, arg->ldb,
      (const cuDoubleComplex*)&arg->beta,
      (cuDoubleComplex*)arg->C.data, arg->ldc
  );
}
#endif //USE CUDA


#include "task_format.h"

