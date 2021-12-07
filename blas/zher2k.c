/**
 *
 * @file zher2k.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zher2k wrappers
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @date 2010-11-15
 * @precisions normal z -> c
 *
 */
#include "common.h"
#include "ztask.h"
#include "ztask_internal.h"
#include <string.h>

/**
 ********************************************************************************
 *
 * @ingroup Complex64_t
 *
 *  CHAMELEON_zher2k - Performs one of the hermitian rank 2k operations
 *
 *    \f[ C = \alpha [ op( A ) \times conjg( op( B )' )] + conjg( \alpha ) [ op( B ) \times conjg( op( A )' )] + \beta C \f],
 *    or
 *    \f[ C = \alpha [ conjg( op( A )' ) \times op( B ) ] + conjg( \alpha ) [ conjg( op( B )' ) \times op( A ) ] + \beta C \f],
 *
 *  where op( X ) is one of
 *
 *    op( X ) = X  or op( X ) = conjg( X' )
 *
 *  where alpha and beta are real scalars, C is an n-by-n symmetric
 *  matrix and A and B are an n-by-k matrices the first case and k-by-n
 *  matrices in the second case.
 *
 *******************************************************************************
 *
 * @param[in] uplo
 *          = CblasUpper: Upper triangle of C is stored;
 *          = CblasLower: Lower triangle of C is stored.
 *
 * @param[in] trans
 *          Specifies whether the matrix A is transposed or conjugate transposed:
 *          = CblasNoTrans:   \f[ C = \alpha [ op( A ) \times conjg( op( B )' )] + conjg( \alpha ) [ op( B ) \times conjg( op( A )' )] + \beta C \f]
 *          = CblasConjTrans: \f[ C = \alpha [ conjg( op( A )' ) \times op( B ) ] + conjg( \alpha ) [ conjg( op( B )' ) \times op( A ) ] + \beta C \f]
 *
 * @param[in] N
 *          N specifies the order of the matrix C. N must be at least zero.
 *
 * @param[in] K
 *          K specifies the number of columns of the A and B matrices with trans = CblasNoTrans.
 *          K specifies the number of rows of the A and B matrices with trans = CblasTrans.
 *
 * @param[in] alpha
 *          alpha specifies the scalar alpha.
 *
 * @param[in] A
 *          A is a LDA-by-ka matrix, where ka is K when trans = CblasNoTrans,
 *          and is N otherwise.
 *
 * @param[in] LDA
 *          The leading dimension of the array A. LDA must be at least
 *          max( 1, N ), otherwise LDA must be at least max( 1, K ).
 *
 * @param[in] B
 *          B is a LDB-by-kb matrix, where kb is K when trans = CblasNoTrans,
 *          and is N otherwise.
 *
 * @param[in] LDB
 *          The leading dimension of the array B. LDB must be at least
 *          max( 1, N ), otherwise LDB must be at least max( 1, K ).
 *
 * @param[in] beta
 *          beta specifies the scalar beta.
 *
 * @param[in,out] C
 *          C is a LDC-by-N matrix.
 *          On exit, the array uplo part of the matrix is overwritten
 *          by the uplo part of the updated matrix.
 *
 * @param[in] LDC
 *          The leading dimension of the array C. LDC >= max( 1, N ).
 *
 *******************************************************************************
 *
 * @retval 0 successful exit
 *
 *******************************************************************************
 *
 */
#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n
#define C(m, n) C##h,  m,  n

int xkblas_zher2k_async( int uplo, int trans, int N, int K,
                 const Complex64_t* alpha, const Complex64_t *A, int LDA,
                                           const Complex64_t *B, int LDB,
                 const CFloat64_t* beta,   Complex64_t *C, int LDC
)
{
    int Am, An;

    /* Check input arguments */
    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of uplo");
        return -1;
    }
    if ((trans != CblasNoTrans) && (trans != CblasConjTrans)) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of trans");
        return -2;
    }
    if ( trans == CblasNoTrans ) {
        Am = N; An = K;
    } else {
        Am = K; An = N;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of N");
        return -3;
    }
    if (K < 0) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of K");
        return -4;
    }
    if (LDA < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of LDA");
        return -7;
    }
    if (LDB < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of LDB");
        return -9;
    }
    if (LDC < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_zher2k", "illegal value of LDC");
        return -12;
    }

    /* Quick return */
    if (N == 0 ||
        ((*alpha == (Complex64_t)0.0 || K == 0.0) && *beta == (CFloat64_t)1.0))
        return 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_HER2K,N,K,Am);

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
      xkblas_init_matrix_handle(Bh, (void*)B, Am, An, LDB, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Bh->ld == LDB) && (Bh->M == Am) && (Bh->N == An) );
    }
    if (!xkblas_matrix_descr_isinit(Ch))
    {
      xkblas_init_matrix_handle(Ch, (void*)C, N, N, LDC, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ch->ld == LDC) && (Ch->M == N) && (Ch->N == N) );
    }

    /* */
    Am = Ah->M;
    An = Ah->N;
    size_t Cm = Ch->M;
    size_t Cn = Ch->N;

    size_t Amb = Ah->mb;
    size_t Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

    size_t Cmb = Ch->mb;
    size_t Cnb = Ch->nb;
    size_t Cmt = Ch->mt;
    size_t Cnt = Ch->nt;

    size_t m, n, k;
    size_t ldak, ldam, ldan, ldcm, ldcn;
    size_t ldbk, ldbm, ldbn;
    size_t tempnn, tempmm, tempkn, tempkm;

    Complex64_t zone   = (Complex64_t)1.0;
    Complex64_t zbeta;
    double dbeta;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "B", Bh ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "C", Ch ) );
  }
#endif

    xkblas_auto_map( KERN_HER2K, Ch );

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt =kaapi_self_context();
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"zher2k",8);
      evt->u.s.d1.u = N;
      evt->u.s.d2.u = K;
      evt->u.s.d3.u = uplo;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
    evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 2 /*info*/ );
    if (evt)
    {
      evt->u.s.d0.u = trans;
      evt->u.s.d1.u = 0;
      evt->u.s.d2.u = 0;
      evt->u.s.d3.u = 0;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
#endif

    for (n = 0; n < Cnt; n++) {
        tempnn = n == Cnt-1 ? Cn-n*Cnb : Cnb;
        ldan = LDA;//BLKLDD(A, n);
        ldbn = LDB;//BLKLDD(B, n);
        ldcn = LDC;//BLKLDD(C, n);
        /*
         *  CblasNoTrans
         */
        if (trans == CblasNoTrans) {
            for (k = 0; k < Ant; k++) {
                tempkn = k == Ant-1 ? An-k*Anb : Anb;
                dbeta = k == 0 ? *beta : 1.0;
                INSERT_TASK_zher2k(
                    uplo, trans,
                    tempnn, tempkn, 
                    *alpha, A(n, k), ldan, /* ldan * K */
                           B(n, k), ldbn,
                    dbeta, C(n, n), ldcn); /* ldc  * N */
            }
            /*
             *  CblasNoTrans / CblasLower
             */
            if (uplo == CblasLower) {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    ldam = LDA;//BLKLDD(A, m);
                    ldbm = LDB;//BLKLDD(B, m);
                    ldcm = LDC;//BLKLDD(C, m);
                    for (k = 0; k < Ant; k++) {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        zbeta = k == 0 ? (Complex64_t)*beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasConjTrans,
                            tempmm, tempnn, tempkn, 
                            *alpha, A(m, k), ldam,  /* ldam * K */
                                         B(n, k), ldbn,  /* ldan * K */
                            zbeta,       C(m, n), ldcm); /* ldc  * N */

                        INSERT_TASK_zgemm(
                            trans, CblasConjTrans,
                            tempmm, tempnn, tempkn, 
                            conj(*alpha), B(m, k), ldbm,  /* ldam * K */
                                   A(n, k), ldan,  /* ldan * K */
                            zone,  C(m, n), ldcm); /* ldc  * N */
                    }
                }
            }
            /*
             *  CblasNoTrans / CblasUpper
             */
            else {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    ldam = LDA;//BLKLDD(A, m);
                    ldbm = LDB;//BLKLDD(B, m);
                    for (k = 0; k < Ant; k++) {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        zbeta = k == 0 ? (Complex64_t)*beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasConjTrans,
                            tempnn, tempmm, tempkn, 
                            *alpha, A(n, k), ldan,  /* ldan * K */
                                   B(m, k), ldbm,  /* ldam * M */
                            zbeta, C(n, m), ldcn); /* ldc  * M */

                        INSERT_TASK_zgemm(
                            trans, CblasConjTrans,
                            tempnn, tempmm, tempkn, 
                            conj(*alpha), B(n, k), ldan,  /* ldan * K */
                                         A(m, k), ldam,  /* ldam * M */
                            zone,        C(n, m), ldcn); /* ldc  * M */
                    }
                }
            }
        }
        /*
         *  Cham[Conj]Trans
         */
        else {
            for (k = 0; k < Amt; k++) {
                tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                ldak = LDA;//BLKLDD(A, k);
                ldbk = LDB;//BLKLDD(B, k);
                dbeta = k == 0 ? *beta : 1.0;
                INSERT_TASK_zher2k(
                    uplo, trans,
                    tempnn, tempkm, 
                    *alpha, A(k, n), ldak,  /* lda * N */
                           B(k, n), ldbk,
                    dbeta, C(n, n), ldcn); /* ldc * N */
            }
            /*
             *  Cham[Conj]Trans / CblasLower
             */
            if (uplo == CblasLower) {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    ldcm = LDC;//BLKLDD(C, m);
                    for (k = 0; k < Amt; k++) {
                        tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                        ldak = LDA;//BLKLDD(A, k);
                        ldbk = LDB;//BLKLDD(B, k);
                        zbeta = k == 0 ? (Complex64_t)*beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempmm, tempnn, tempkm, 
                            *alpha, A(k, m), ldak,  /* lda * M */
                                   B(k, n), ldbk,  /* lda * N */
                            zbeta, C(m, n), ldcm); /* ldc * N */

                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempmm, tempnn, tempkm, 
                            conj(*alpha), B(k, m), ldbk,  /* lda * M */
                                         A(k, n), ldak,  /* lda * N */
                            zone,        C(m, n), ldcm); /* ldc * N */
                    }
                }
            }
            /*
             *  Cham[Conj]Trans / CblasUpper
             */
            else {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    for (k = 0; k < Amt; k++) {
                        tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                        ldak = LDA;//BLKLDD(A, k);
                        ldbk = LDB;//BLKLDD(B, k);
                        zbeta = k == 0 ? (Complex64_t)*beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempnn, tempmm, tempkm, 
                            *alpha, A(k, n), ldak,  /* lda * K */
                                   B(k, m), ldbk,  /* lda * M */
                            zbeta, C(n, m), ldcn); /* ldc * M */

                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempnn, tempmm, tempkm, 
                            conj(*alpha), B(k, n), ldbk,  /* lda * K */
                                         A(k, m), ldak,  /* lda * M */
                            zone,        C(n, m), ldcn); /* ldc * M */
                    }
                }
            }
        }
    }
}


/* her2k */
static void (*dl_zher2k)(
  const char* uplo, const char* transa,
  const KBLAS_INT* n, const KBLAS_INT* k,
  const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT* lda,
                            const Complex64_t* B, const KBLAS_INT* ldb,
  const CFloat64_t* beta,   Complex64_t* C, const KBLAS_INT* ldc) = 0;

/* CPU driver */
extern void xkblas_zher2k_native_(
  const char * uplo, const char * transa,
  const KBLAS_INT *n, const KBLAS_INT *k,
  const Complex64_t *alpha, const Complex64_t *A, const KBLAS_INT* lda,
                            const Complex64_t *B, const KBLAS_INT* ldb,
  const CFloat64_t *beta,   Complex64_t *C, const KBLAS_INT* ldc)
{
  if (dl_zher2k ==0) xkblas_load_sym((void**)&dl_zher2k,SYMBLAS_NAME(zher2k));
  dl_zher2k( uplo, transa,
             n, k,
             alpha, A, lda,
                    B, ldb,
             beta,  C, ldc
  );
}

extern int xkblas_zher2k_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const CFloat64_t* beta,   Complex64_t *C, int LDC)
{
  char u = cblas2blas_fill(uplo);
  char trA = cblas2blas_op(trans);
  const KBLAS_INT iN = N;
  const KBLAS_INT iK = K;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDB = LDB;
  const KBLAS_INT iLDC = LDC;
  xkblas_zher2k_native_( &u, &trA, &iN, &iK, alpha, A, &iLDA, B, &iLDB, beta, C, &iLDC );
  return 0;
}
