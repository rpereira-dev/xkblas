/**
 *
 * @file zsyrk.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zsyrk wrappers
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Thierry Gautier
 * @date 2010-11-15
 * @precisions normal z -> s d c
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
 *  CHAMELEON_zsyrk - Performs one of the hermitian rank k operations
 *
 *    \f[ C = \alpha [ op( A ) \times conjg( op( A )' )] + \beta C \f],
 *
 *  where op( X ) is one of
 *
 *    op( X ) = X  or op( X ) = conjg( X' )
 *
 *  where alpha and beta are real scalars, C is an n-by-n hermitian
 *  matrix and A is an n-by-k matrix in the first case and a k-by-n
 *  matrix in the second case.
 *
 *******************************************************************************
 *
 * @param[in] uplo
 *          = CblasUpper: Upper triangle of C is stored;
 *          = CblasLower: Lower triangle of C is stored.
 *
 * @param[in] trans
 *          Specifies whether the matrix A is transposed or conjugate transposed:
 *          = CblasNoTrans:   A is not transposed;
 *          = CblasTrans  :   A is transposed.
 *
 * @param[in] N
 *          N specifies the order of the matrix C. N must be at least zero.
 *
 * @param[in] K
 *          K specifies the number of columns of the matrix op( A ).
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
 *          max( 1, N ) if trans == CblasNoTrans, otherwise LDA must
 *          be at least max( 1, K ).
 *
 * @param[in] beta
 *          beta specifies the scalar beta
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
#define C(m, n) C##h,  m,  n

int xkblas_zsyrk_async( int uplo, int trans, int N, int K,
                 const Complex64_t* alpha, const Complex64_t *A, int LDA,
                 const Complex64_t* beta,  Complex64_t *C, int LDC )
{
    size_t Am, An;

    /* Check input arguments */
    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of uplo");
        return -1;
    }
    if ((trans != CblasNoTrans) && (trans != CblasTrans)) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of trans");
        return -2;
    }
    if ( trans == CblasNoTrans ) {
        Am = N; An = K;
    } else {
        Am = K; An = N;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of N");
        return -3;
    }
    if (K < 0) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of K");
        return -4;
    }
    if (LDA < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of LDA");
        return -7;
    }
    if (LDC < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_zsyrk", "illegal value of LDC");
        return -10;
    }

    /* Quick return */
    if (N == 0 ||
        ((*alpha == (Complex64_t)0.0 || K == 0.0) && *beta == (Complex64_t)1.0))
        return 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_SYRK,N,K,Am);

    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    xkblas_matrix_descr_t* Ch = xkblas_find(C);
    if (!xkblas_matrix_descr_isinit(Ah))
    {
      xkblas_init_matrix_handle(Ah, (void*)A, Am, An, LDA, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == Am) && (Ah->N == An) );
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
    size_t tempnn, tempmm, tempkn, tempkm;

    Complex64_t zbeta;
    Complex64_t zone = (Complex64_t)1.0;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "C", Ch ) );
  }
#endif

    xkblas_context_t* xkctxt =xkblas_context_get();
    xkblas_auto_map( xkctxt, KERN_SYRK, Ch );

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt = xkctxt->kctxt;
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"zsyrk",8);
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
        ldcn = LDC;//BLKLDD(C, n);
        /*
         *  CblasNoTrans
         */
        if (trans == CblasNoTrans) {
            for (k = 0; k < Ant; k++) {
                tempkn = k == Ant-1 ? An-k*Anb : Anb;
                zbeta = k == 0 ? *beta : zone;
                INSERT_TASK_zsyrk(
                    uplo, trans,
                    tempnn, tempkn, 
                    *alpha, A(n, k), ldan, /* ldan * K */
                    zbeta, C(n, n), ldcn); /* ldc  * N */
            }
            /*
             *  CblasNoTrans / CblasLower
             */
            if (uplo == CblasLower) {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    ldam = LDA;//BLKLDD(A, m);
                    ldcm = LDC;//BLKLDD(C, m);
                    for (k = 0; k < Ant; k++) {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        zbeta = k == 0 ? *beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasTrans,
                            tempmm, tempnn, tempkn, 
                            *alpha, A(m, k), ldam,  /* ldam * K */
                                   A(n, k), ldan,  /* ldan * K */
                            zbeta, C(m, n), ldcm); /* ldc  * N */
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
                    for (k = 0; k < Ant; k++) {
                        tempkn = k == Ant-1 ? An-k*Anb : Anb;
                        zbeta = k == 0 ? *beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasTrans,
                            tempnn, tempmm, tempkn, 
                            *alpha, A(n, k), ldan,  /* ldan * K */
                                   A(m, k), ldam,  /* ldam * M */
                            zbeta, C(n, m), ldcn); /* ldc  * M */
                    }
                }
            }
        }
        /*
         *  CblasTrans
         */
        else {
            for (k = 0; k < Amt; k++) {
                tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                ldak = LDA;//BLKLDD(A, k);
                zbeta = k == 0 ? *beta : zone;
                INSERT_TASK_zsyrk(
                    uplo, trans,
                    tempnn, tempkm, 
                    *alpha, A(k, n), ldak,  /* lda * N */
                    zbeta, C(n, n), ldcn); /* ldc * N */
            }
            /*
             *  CblasTrans / CblasLower
             */
            if (uplo == CblasLower) {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    ldcm = LDC;//BLKLDD(C, m);
                    for (k = 0; k < Amt; k++) {
                        tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                        ldak = LDA;//BLKLDD(A, k);
                        zbeta = k == 0 ? *beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempmm, tempnn, tempkm,
                            *alpha, A(k, m), ldak,  /* lda * M */
                                   A(k, n), ldak,  /* lda * N */
                            zbeta, C(m, n), ldcm); /* ldc * N */
                    }
                }
            }
            /*
             *  CblasTrans / CblasUpper
             */
            else {
                for (m = n+1; m < Cmt; m++) {
                    tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                    for (k = 0; k < Amt; k++) {
                        tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                        ldak = LDA;//BLKLDD(A, k);
                        zbeta = k == 0 ? *beta : zone;
                        INSERT_TASK_zgemm(
                            trans, CblasNoTrans,
                            tempnn, tempmm, tempkm, 
                            *alpha, A(k, n), ldak,  /* lda * K */
                                   A(k, m), ldak,  /* lda * M */
                            zbeta, C(n, m), ldcn); /* ldc * M */
                    }
                }
            }
        }
    }
    return 0;
}


/* syrk */
static void (*dl_zsyrk)(
  const char * uplo, const char * transa,
  const KBLAS_INT *n, const KBLAS_INT *k,
  const Complex64_t *alpha, const Complex64_t *A, const KBLAS_INT* lda,
  const Complex64_t *beta,  Complex64_t *C, const KBLAS_INT* ldc) = 0;

/* CPU driver */
extern void xkblas_zsyrk_native_(
  const char * uplo, const char * transa,
  const KBLAS_INT *n, const KBLAS_INT *k,
  const Complex64_t *alpha, const Complex64_t *A, const KBLAS_INT* lda,
  const Complex64_t *beta,  Complex64_t *C, const KBLAS_INT* ldc)
{
  if (dl_zsyrk ==0) xkblas_load_sym((void**)&dl_zsyrk,SYMBLAS_NAME(zsyrk));
  dl_zsyrk( uplo, transa,
            n, k,
            alpha, A, lda,
            beta,  C, ldc
  );
}

extern int xkblas_zsyrk_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t* beta,  Complex64_t *C, int LDC )
{
  char u = cblas2blas_fill(uplo);
  char trA = cblas2blas_op(trans);
  const KBLAS_INT iN = N;
  const KBLAS_INT iK = K;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDC = LDC;
  xkblas_zsyrk_native_( &u, &trA, &iN, &iK, alpha, A, &iLDA, beta, C, &iLDC );
  return 0;
}
