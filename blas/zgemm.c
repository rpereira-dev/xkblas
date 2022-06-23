/**
 *
 * @file zgemm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zgemm wrappers
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Thierry Gautier
 * @date 2018-11-20
 * @precisions normal z -> s d c
 * This file was merged from pzgemm and zgemm from Chameleon by Thierry Gautier
 * for Kaapi that support natively 2D memory view.
 */
#include "common.h"
#include "ztask.h"
#include "ztask_internal.h"
#include <math.h>
#include <string.h>

#ifdef KAAPI_DEBUG
#undef KAAPI_DEBUG
#endif
//#define KAAPI_DEBUG 1

/**
 ********************************************************************************
 *
 * @ingroup Complex64_t
 *
 *  CHAMELEON_zgemm - Performs one of the matrix-matrix operations
 *
 *    \f[ C = \alpha [op( A )\times op( B )] + \beta C \f],
 *
 *  where op( X ) is one of
 *
 *    op( X ) = X  or op( X ) = X' or op( X ) = conjg( X' )
 *
 *  alpha and beta are scalars, and A, B and C  are matrices, with op( A )
 *  an m by k matrix, op( B ) a k by n matrix and C an m by n matrix.
 *
 *******************************************************************************
 *
 * @param[in] transA
 *          Specifies whether the matrix A is transposed, not transposed or conjugate transposed:
 *          = CblasNoTrans:   A is not transposed;
 *          = CblasTrans:     A is transposed;
 *          = CblasConjTrans: A is conjugate transposed.
 *
 * @param[in] transB
 *          Specifies whether the matrix B is transposed, not transposed or conjugate transposed:
 *          = CblasNoTrans:   B is not transposed;
 *          = CblasTrans:     B is transposed;
 *          = CblasConjTrans: B is conjugate transposed.
 *
 * @param[in] M
 *          M specifies the number of rows of the matrix op( A ) and of the matrix C. M >= 0.
 *
 * @param[in] N
 *          N specifies the number of columns of the matrix op( B ) and of the matrix C. N >= 0.
 *
 * @param[in] K
 *          K specifies the number of columns of the matrix op( A ) and the number of rows of
 *          the matrix op( B ). K >= 0.
 *
 * @param[in] alpha
 *          alpha specifies the scalar alpha
 *
 * @param[in] A
 *          A is a LDA-by-ka matrix, where ka is K when  transA = CblasNoTrans,
 *          and is  M  otherwise.
 *
 * @param[in] LDA
 *          The leading dimension of the array A. LDA >= max(1,M).
 *
 * @param[in] B
 *          B is a LDB-by-kb matrix, where kb is N when  transB = CblasNoTrans,
 *          and is  K  otherwise.
 *
 * @param[in] LDB
 *          The leading dimension of the array B. LDB >= max(1,N).
 *
 * @param[in] beta
 *          beta specifies the scalar beta
 *
 * @param[in,out] C
 *          C is a LDC-by-N matrix.
 *          On exit, the array is overwritten by the M by N matrix ( alpha*op( A )*op( B ) + beta*C )
 *
 * @param[in] LDC
 *          The leading dimension of the array C. LDC >= max(1,M).
 *
 *******************************************************************************
 *
 * @return
 *          \retval 0 successful exit
 *
 *******************************************************************************
 *
 * @sa CHAMELEON_zgemm_Tile
 * @sa CHAMELEON_cgemm
 * @sa CHAMELEON_dgemm
 * @sa CHAMELEON_sgemm
 *
 */

#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n
#define C(m, n) C##h,  m,  n

int xkblas_zgemm_async(
    int transA, int transB, int M, int N, int K,
    const Complex64_t* alpha, const Complex64_t *A, int LDA,
                              const Complex64_t *B, int LDB,
    const Complex64_t* beta,        Complex64_t *C, int LDC )
{
    size_t Am, An, Bm, Bn;

    /* Check input arguments */
    if ((transA < CblasNoTrans) || (transA > CblasConjTrans)) {
        kaapi_error("zgemm", "illegal value of transA");
        return -1;
    }
    if ((transB < CblasNoTrans) || (transB > CblasConjTrans)) {
        kaapi_error("zgemm", "illegal value of transB");
        return -2;
    }
    if ( transA == CblasNoTrans ) {
        Am = M; An = K;
    } else {
        Am = K; An = M;
    }
    if ( transB == CblasNoTrans ) {
        Bm = K; Bn = N;
    } else {
        Bm = N; Bn = K;
    }
    if (M < 0) {
        kaapi_error("zgemm",  "illegal value of M");
        return -3;
    }
    if (N < 0) {
        kaapi_error("zgemm", "illegal value of N");
        return -4;
    }
    if (K < 0) {
        kaapi_error("zgemm", "illegal value of N");
        return -5;
    }
    if (LDA < kaapi_max(1, Am)) {
        kaapi_error("zgemm", "illegal value of LDA");
        return -8;
    }
    if (LDB < kaapi_max(1, Bm)) {
        kaapi_error("zgemm", "illegal value of LDB");
        return -10;
    }
    if (LDC < kaapi_max(1, M)) {
        kaapi_error("zgemm", "illegal value of LDC");
        return -13;
    }

    /* Quick return */
    if (M == 0 || N == 0 ||
      ((*alpha == 0.0 || K == 0) && *beta == 1.0))
        return 0;

    size_t Cmb = 0;
    size_t Cnb = 0;
    size_t Amb = 0;
    size_t Anb = 0;
    size_t Bmb = 0;
    size_t Bnb = 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_GEMM,M,N,K);

#if 00// TO force synchronous call: may be this should be provided as runtime option
printf("Gemm: M=%i, N=%i, K=%i, NB=%i\n", M,N,K,NB);
return xkblas_zgemm_native(
    transA, transB, M, N, K,
    alpha, A, LDA,
           B, LDB,
    beta,  C, LDC 
);
#endif
#if BUG_2022_03_18 && defined(PRECISION_d)
printf("%p:: %30.30s: READ(%p), READ(%p), %s(%p)\n", 
   pthread_self(), __FUNCTION__, 
   A, B, (*beta ==0.0? "WRITE":"READWRITE"), C
); 
#endif

    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    xkblas_matrix_descr_t* Bh = xkblas_find(B);
    xkblas_matrix_descr_t* Ch = xkblas_find(C);

    if (!xkblas_matrix_descr_isinit(Ah))
    {
      xkblas_init_matrix_handle(Ah, (void*)A, Am, An, LDA, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == Am) && (Ah->N == An) );
    }
    if (!xkblas_matrix_descr_isinit(Bh))
    {
      xkblas_init_matrix_handle(Bh, (void*)B, Bm, Bn, LDB, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Bh->ld == LDB) && (Bh->M == Bm) && (Bh->N == Bn) );
    }
    if (!xkblas_matrix_descr_isinit(Ch))
    {
      xkblas_init_matrix_handle(Ch, C, M, N, LDC, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ch->ld == LDC) && (Ch->M == M) && (Ch->N == N) );
    }

    Cmb = Ch->mb;
    Cnb = Ch->nb;
    size_t Cmt = Ch->mt;
    size_t Cnt = Ch->nt;

    Amb = Ah->mb;
    Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

    Bmb = Bh->mb;
    Bnb = Bh->nb;
    size_t Bmt = Bh->mt;
    size_t Bnt = Bh->nt;

    size_t m, n, k;
    size_t ldam, ldak, ldbn, ldbk, ldcm;
    size_t tempmm, tempnn, tempkn, tempkm;

    Complex64_t zbeta;

    xkblas_context_t* xkctxt = xkblas_context_get();
    xkblas_auto_map( xkctxt, KERN_GEMM, Ch );
    
#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt = xkctxt->kctxt;
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"zgemm",8);
      evt->u.s.d1.u = M;
      evt->u.s.d2.u = N;
      evt->u.s.d3.u = K;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
    evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 2 /*info*/ );
    if (evt)
    {
      evt->u.s.d0.u = NB;
      evt->u.s.d1.u = transA;
      evt->u.s.d2.u = transB;
      evt->u.s.d3.u = 0;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
#endif

    for (m = 0; m < Cmt; m++)
    {
        tempmm = m == Cmt-1 ? M-m*Cmb : Cmb;
        ldcm = LDC; //BLKLDD(C, m);
        for (n = 0; n < Cnt; n++)
        {
            tempnn = n == Cnt-1 ? N-n*Cnb : Cnb;
            /*
             *  A: CblasNoTrans / B: CblasNoTrans
             */
            if (transA == CblasNoTrans)
            {
                ldam = LDA; //BLKLDD(A, m);
                if (transB == CblasNoTrans)
                {
                    for (k = 0; k < Ant; k++)
                    {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        ldbk = LDB; //BLKLDD(B, k);
                        zbeta = k == 0 ? *beta : 1.0;
                        INSERT_TASK_zgemm(
                            transA, transB,
                            tempmm, tempnn, tempkn, 
                            *alpha, A(m, k), ldam,  /* lda * Z */
                                   B(k, n), ldbk,   /* ldb * Y */
                            zbeta, C(m, n), ldcm);  /* ldc * Y */
                    }
                }
                /*
                 *  A: CblasNoTrans / B: Cham[Conj]Trans
                 */
                else {
                    ldbn = LDB; //BLKLDD(B, n);
                    for (k = 0; k < Ant; k++)
                    {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        zbeta = k == 0 ? *beta : 1.0;
                        INSERT_TASK_zgemm(
                            transA, transB,
                            tempmm, tempnn, tempkn, 
                            *alpha, A(m, k), ldam,  /* lda * Z */
                                    B(n, k), ldbn,  /* ldb * Z */
                            zbeta,  C(m, n), ldcm); /* ldc * Y */
                    }
                }
            }
            /*
             *  A: Cham[Conj]Trans / B: CblasNoTrans
             */
            else
            {
                if (transB == CblasNoTrans)
                {
                    for (k = 0; k < Amt; k++)
                    {
                        tempkm = k == Amt-1 ? Am-k* Amb : Amb;
                        ldak = LDA; //BLKLDD(A, k);
                        ldbk = LDB; //BLKLDD(B, k);
                        zbeta = k == 0 ? *beta : 1.0;
                        INSERT_TASK_zgemm(
                            transA, transB,
                            tempmm, tempnn, tempkm, 
                            *alpha, A(k, m), ldak,  /* lda * X */
                                    B(k, n), ldbk,  /* ldb * Y */
                            zbeta,  C(m, n), ldcm); /* ldc * Y */
                    }
                }
                /*
                 *  A: Cham[Conj]Trans / B: Cham[Conj]Trans
                 */
                else
                {
                    ldbn = LDB; //BLKLDD(B, n);
                    for (k = 0; k < Amt; k++)
                    {
                        tempkm = k == Amt-1 ? Am-k* Amb : Amb;
                        ldak = LDA; //BLKLDD(A, k);
                        zbeta = k == 0 ? *beta : 1.0;
                        INSERT_TASK_zgemm(
                            transA, transB,
                            tempmm, tempnn, tempkm, 
                            *alpha, A(k, m), ldak,  /* lda * X */
                                    B(n, k), ldbn,  /* ldb * Z */
                            zbeta,  C(m, n), ldcm); /* ldc * Y */
                    }
                }
            }
        }
    }
    return 0;
}


/* gemm native pointer */
static void (*dl_zgemm)(
    const char * transa, const char * transb,
    const KBLAS_INT * m, const KBLAS_INT * n, const KBLAS_INT * k,
    const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT * lda,
                              const Complex64_t * B, const KBLAS_INT * ldb,
    const Complex64_t* beta,  Complex64_t * C, const KBLAS_INT * ldc) = 0;


/* CPU driver */
extern void xkblas_zgemm_native_(
    const char * transa, const char * transb,
    const KBLAS_INT * m, const KBLAS_INT * n, const KBLAS_INT * k,
    const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT * lda,
                              const Complex64_t * B, const KBLAS_INT * ldb,
    const Complex64_t* beta,  Complex64_t * C, const KBLAS_INT * ldc)
{
  if (dl_zgemm ==0) xkblas_load_sym((void**)&dl_zgemm,SYMBLAS_NAME(zgemm));
  dl_zgemm( transa, transb,
            m, n, k,
            alpha, A, lda,
                   B, ldb,
            beta,  C, ldc);
}

extern int xkblas_zgemm_native(
  int transA, int transB, int M, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC )
{
  char trA = cblas2blas_op(transA);
  char trB = cblas2blas_op(transB);
  const KBLAS_INT iM = M;
  const KBLAS_INT iN = N;
  const KBLAS_INT iK = K;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDB = LDB;
  const KBLAS_INT iLDC = LDC;
  xkblas_zgemm_native_( &trA, &trB, &iM, &iN, &iK, alpha, A, &iLDA, B, &iLDB, beta, C, &iLDC );
  return 0;
}
