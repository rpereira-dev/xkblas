/**
 *
 * @file task_zswap.c
 *
 * @copyright 2021 - INRIA
 *
 * @author Thierry Gautier
 * @precisions normal z -> c d s
 *
 */
#include "common.h"
#include "xkblas.h"
#include "task_z.h"
#include "task_z_internal.h"

/* In the following macro arg is of type of the task argument structure */
#define STR_EXPAND(tok) #tok
#define TASK_NAME zswap
#define STRNAME  STR_EXPAND(TASK_NAME)
#define NAME(x) x##_##zswap
#define PNAME(x) zswap##_##x
#define SIZE_NPARAM 2
#define NPARAM (arg->issame ? 1: 2)
#define MODE_PARAM {KAAPI_ACCESS_MODE_RW,KAAPI_ACCESS_MODE_RW}
#define ADDR_PARAM {&arg->A, &arg->B}
#define VIEW_PARAM {\
  { &arg->M, &arg->Ni, &arg->LDA}, \
  { &arg->M, &arg->Nj, &arg->LDB}\
}
#define FORMAT_TYPE kaapi_dcplx_format
#define SIZEOF_TYPE sizeof(Complex64_t)
#define DOT_COLOR "chartreuse1"
#define TASK_FLOPS arg->M
#define TASK_DATA  arg->M

/**
 * Task data structure for its arguments
 *
 */
 typedef struct {
  size_t M;
  size_t Ni;
  size_t Nj;
  size_t i;
  size_t j;
  int issame;
  kaapi_access_t A;
  kaapi_access_t B;
  size_t LDA;
  size_t LDB;
} NAME(Arg);

/* Internal Kaapi task format for the structure:
   - reuse macro definition juste above to instanciate the right fields
*/
static kaapi_format_id_t NAME(task_fmtid) = 0;

/* Create a new task
*/
void INSERT_TASK_zswap(
    size_t M, size_t Ni, size_t Nj, size_t i, size_t j,
    xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t ldA,
    xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldB
)
{
    kaapi_task_t* task;
    xkblas_context_t* ctxt = xkblas_context_get();
    kaapi_thread_t* thread = ctxt->kthread;
    size_t tasksize = sizeof(NAME(Arg)) + sizeof(kaapi_task_withperfcnt_t);
    task = kaapi_task_alloc( thread, NAME(task_fmtid), tasksize );
    NAME(Arg)* taskarg = kaapi_task_getargst((kaapi_task_withperfcnt_t*)task,NAME(Arg));

    taskarg->M = M;
    taskarg->Ni = Ni;
    taskarg->Nj = Nj;
    taskarg->i = i;
    taskarg->j = j;
    /* do not declare 2 dependencies on the same data */
    taskarg->issame = (Ah == Bh ? 1: 0);
    kaapi_update_dependencies(thread, &taskarg->A, task,
        KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Ah, Am, An));
    taskarg->LDA = ldA;
 
    if (taskarg->issame !=0)
    {
      /* */
      kaapi_update_dependencies(thread, &taskarg->B, task,
        KAAPI_ACCESS_MODE_RW, xkblas_get_handle(Bh, Bm, Bn));
      taskarg->LDB = ldB;
    }

#if KAAPI_USE_OCR
    /* OCR on the third parameter */
    kaapi_task_set_ld(task, KAAPI_TASK_OCR_PARAM, 1);
#else
    uint16_t ldid = xkblas_get_ld(Ah, Am, An);
    kaapi_task_set_ld(task, KAAPI_TASK_LD_BOUND, ldid);
#endif
    kaapi_taskflag_set(task, KAAPI_TASK_PERFCNT);
    kaapi_task_commit( thread, task );
}


static void NAME(task_body_cpu)( kaapi_task_t* task, kaapi_thread_t* thread )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  xkblas_zswap_native(
      arg->M, arg->Ni, arg->Nj,
      arg->j, arg->i,
      (Complex64_t*)arg->A.data, arg->LDA,
      (Complex64_t*)arg->B.data, arg->LDB
  );
}

#if KAAPI_USE_CUDA
static void NAME(task_body_gpu)( kaapi_task_t* task, kaapi_thread_t* thread, void* handle )
{
  NAME(Arg)* arg = (NAME(Arg)*)kaapi_task_getargs(task);
  if (arg->issame)
  {
    cublasZswap((cublasHandle_t)handle,
      arg->M,
      arg->i*arg->LDA+(cuDoubleComplex*)arg->A.data, 1,
      arg->j*arg->LDA+(cuDoubleComplex*)arg->A.data, 1
    );
  }
  else 
  {
    cublasZswap((cublasHandle_t)handle,
      arg->M,
      arg->i*arg->LDA+(cuDoubleComplex*)arg->A.data, 1,
      arg->j*arg->LDB+(cuDoubleComplex*)arg->B.data, 1
    );
  }
}
#endif

#include "task_format.h"

