/**
 *
 * @file zhemm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zhemm wrappers
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
 *  CHAMELEON_zhemm - Performs one of the matrix-matrix operations
 *
 *     \f[ C = \alpha \times A \times B + \beta \times C \f]
 *
 *  or
 *
 *     \f[ C = \alpha \times B \times A + \beta \times C \f]
 *
 *  where alpha and beta are scalars, A is an hermitian matrix and  B and
 *  C are m by n matrices.
 *
 *******************************************************************************
 *
 * @param[in] side
 *          Specifies whether the hermitian matrix A appears on the
 *          left or right in the operation as follows:
 *          = CblasLeft:      \f[ C = \alpha \times A \times B + \beta \times C \f]
 *          = CblasRight:     \f[ C = \alpha \times B \times A + \beta \times C \f]
 *
 * @param[in] uplo
 *          Specifies whether the upper or lower triangular part of
 *          the hermitian matrix A is to be referenced as follows:
 *          = CblasLower:     Only the lower triangular part of the
 *                             hermitian matrix A is to be referenced.
 *          = CblasUpper:     Only the upper triangular part of the
 *                             hermitian matrix A is to be referenced.
 *
 * @param[in] M
 *          Specifies the number of rows of the matrix C. M >= 0.
 *
 * @param[in] N
 *          Specifies the number of columns of the matrix C. N >= 0.
 *
 * @param[in] alpha
 *          Specifies the scalar alpha.
 *
 * @param[in] A
 *          A is a LDA-by-ka matrix, where ka is M when side = CblasLeft,
 *          and is N otherwise. Only the uplo triangular part is referenced.
 *
 * @param[in] LDA
 *          The leading dimension of the array A. LDA >= max(1,ka).
 *
 * @param[in] B
 *          B is a LDB-by-N matrix, where the leading M-by-N part of
 *          the array B must contain the matrix B.
 *
 * @param[in] LDB
 *          The leading dimension of the array B. LDB >= max(1,M).
 *
 * @param[in] beta
 *          Specifies the scalar beta.
 *
 * @param[in,out] C
 *          C is a LDC-by-N matrix.
 *          On exit, the array is overwritten by the M by N updated matrix.
 *
 * @param[in] LDC
 *          The leading dimension of the array C. LDC >= max(1,M).
 *
 *******************************************************************************
 *
 * @retval 0 successful exit
 *
 *******************************************************************************
 *
 */

#define A(m,n) Ah,  m,  n
#define B(m,n) Bh,  m,  n
#define C(m,n) Ch,  m,  n

int xkblas_zhemm_async( int side, int uplo, int M, int N,
                 const Complex64_t* alpha, const Complex64_t *A, int LDA,
                                           const Complex64_t *B, int LDB,
                 const Complex64_t* beta,  Complex64_t *C, int LDC )
{
    int Am;

    /* Check input arguments */
    if ( (side != CblasLeft) && (side != CblasRight) ){
        kaapi_error("CHAMELEON_zhemm", "illegal value of side");
        return -1;
    }
    if ((uplo != CblasLower) && (uplo != CblasUpper)) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of uplo");
        return -2;
    }
    Am = ( side == CblasLeft ) ? M : N;
    if (M < 0) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of M");
        return -3;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of N");
        return -4;
    }
    if (LDA < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of LDA");
        return -7;
    }
    if (LDB < kaapi_max(1, M)) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of LDB");
        return -9;
    }
    if (LDC < kaapi_max(1, M)) {
        kaapi_error("CHAMELEON_zhemm", "illegal value of LDC");
        return -12;
    }

    /* Quick return */
    if (M == 0 || N == 0 ||
        ((*alpha == (Complex64_t)0.0) && *beta == (Complex64_t)1.0))
        return 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_HEMM,M,N,Am);

    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    xkblas_matrix_descr_t* Bh = xkblas_find(B);
    xkblas_matrix_descr_t* Ch = xkblas_find(C);
    if (!xkblas_matrix_descr_isinit(Ah))
    {
      xkblas_init_matrix_handle(Ah, (void*)A, Am, Am, LDA, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == Am) && (Ah->N == Am) );
    }
    if (!xkblas_matrix_descr_isinit(Bh))
    {
      xkblas_init_matrix_handle(Bh, (void*)B, M, N, LDB, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Bh->ld == LDB) && (Bh->M == M) && (Bh->N == N) );
    }
    if (!xkblas_matrix_descr_isinit(Ch))
    {
      xkblas_init_matrix_handle(Ch, C, M, N, LDC, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ch->ld == LDC) && (Ch->M == M) && (Ch->N == N) );
    }

    size_t Cm = Ch->M;
    size_t Cn = Ch->N;

    size_t Cmb = Ch->mb;
    size_t Cnb = Ch->nb;
    size_t Cmt = Ch->mt;
    size_t Cnt = Ch->nt;

    size_t Amb = Ah->mb;
    size_t Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

    size_t Bmb = Bh->mb;
    size_t Bnb = Bh->nb;
    size_t Bmt = Bh->mt;
    size_t Bnt = Bh->nt;

    size_t k, m, n;
    size_t ldam, ldan, ldak, ldbk, ldbm, ldcm;
    size_t tempmm, tempnn, tempkn, tempkm;

    Complex64_t zbeta;
    Complex64_t zone = (Complex64_t)1.0;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "B", Bh ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "C", Ch ) );
  }
#endif

    /* map output of C on ressources */
    xkblas_context_t* xkctxt = xkblas_context_get();
    xkblas_auto_map( xkctxt, KERN_HEMM, Ch );

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt = xkctxt->kctxt;
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"zhemm",8);
      evt->u.s.d1.u = M;
      evt->u.s.d2.u = N;
      evt->u.s.d3.u = side;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
    evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 2 /*info*/ );
    if (evt)
    {
      evt->u.s.d0.u = uplo;
      evt->u.s.d1.u = 0;
      evt->u.s.d2.u = 0;
      evt->u.s.d3.u = 0;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
#endif

    for(m = 0; m < Cmt; m++) {
        tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
        ldcm = LDC;//BLKLDD(C, m);
        for(n = 0; n < Cnt; n++) {
            tempnn = n == Cnt-1 ? Cn-n*Cnb : Cnb;
            /*
             *  CblasLeft / CblasLower
             */
            if (side == CblasLeft) {
                ldam = LDA;//BLKLDD(A, m);
                if (uplo == CblasLower) {
                    for (k = 0; k < Cmt; k++) {
                        tempkm = k == Cmt-1 ? Cm-k*Cmb : Cmb;
                        ldak = LDA;//BLKLDD(A, k);
                        ldbk = LDB;//BLKLDD(B, k);
                        zbeta = k == 0 ? *beta : zone;
                        if (k < m) {
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                tempmm, tempnn, tempkm, 
                                *alpha, A(m, k), ldam,  /* lda * K */
                                       B(k, n), ldbk,  /* ldb * Y */
                                zbeta, C(m, n), ldcm); /* ldc * Y */
                        }
                        else {
                            if (k == m) {
                                INSERT_TASK_zhemm(
                                    side, uplo,
                                    tempmm, tempnn, 
                                    *alpha, A(k, k), ldak,  /* ldak * X */
                                           B(k, n), ldbk,  /* ldb  * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                            else {
                                INSERT_TASK_zgemm(
                                    CblasConjTrans, CblasNoTrans,
                                    tempmm, tempnn, tempkm, 
                                    *alpha, A(k, m), ldak,  /* ldak * X */
                                           B(k, n), ldbk,  /* ldb  * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                        }
                    }
                }
                /*
                 *  CblasLeft / CblasUpper
                 */
                else {
                    for (k = 0; k < Cmt; k++) {
                        tempkm = k == Cmt-1 ? Cm-k*Cmb : Cmb;
                        ldak = LDA;//BLKLDD(A, k);
                        ldbk = LDB;//BLKLDD(B, k);
                        zbeta = k == 0 ? *beta : zone;
                        if (k < m) {
                            INSERT_TASK_zgemm(
                                CblasConjTrans, CblasNoTrans,
                                tempmm, tempnn, tempkm, 
                                *alpha, A(k, m), ldak,  /* ldak * X */
                                       B(k, n), ldbk,  /* ldb  * Y */
                                zbeta, C(m, n), ldcm); /* ldc  * Y */
                        }
                        else {
                            if (k == m) {
                                INSERT_TASK_zhemm(
                                    side, uplo,
                                    tempmm, tempnn, 
                                    *alpha, A(k, k), ldak,  /* ldak * K */
                                           B(k, n), ldbk,  /* ldb  * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                            else {
                                INSERT_TASK_zgemm(
                                    CblasNoTrans, CblasNoTrans,
                                    tempmm, tempnn, tempkm, 
                                    *alpha, A(m, k), ldam,  /* lda * K */
                                           B(k, n), ldbk,  /* ldb * Y */
                                    zbeta, C(m, n), ldcm); /* ldc * Y */
                            }
                        }
                    }
                }
            }
            /*
             *  CblasRight / CblasLower
             */
            else {
                ldan = LDA;//BLKLDD(A, n);
                ldbm = LDB;//BLKLDD(B, m);
                if (uplo == CblasLower) {
                    for (k = 0; k < Cnt; k++) {
                        tempkn = k == Cnt-1 ? Cn-k*Cnb : Cnb;
                        ldak = LDA;//BLKLDD(A, k);
                        zbeta = k == 0 ? *beta : zone;
                        if (k < n) {
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasConjTrans,
                                tempmm, tempnn, tempkn, 
                                *alpha, B(m, k), ldbm,  /* ldb * K */
                                       A(n, k), ldan,  /* lda * K */
                                zbeta, C(m, n), ldcm); /* ldc * Y */
                        }
                        else {
                            if (k == n) {
                                INSERT_TASK_zhemm(
                                    side, uplo,
                                    tempmm, tempnn, 
                                    *alpha, A(k, k), ldak,  /* ldak * Y */
                                           B(m, k), ldbm,  /* ldb  * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                            else {
                                INSERT_TASK_zgemm(
                                    CblasNoTrans, CblasNoTrans,
                                    tempmm, tempnn, tempkn, 
                                    *alpha, B(m, k), ldbm,  /* ldb  * K */
                                           A(k, n), ldak,  /* ldak * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                        }
                    }
                }
                /*
                 *  CblasRight / CblasUpper
                 */
                else {
                    for (k = 0; k < Cnt; k++) {
                        tempkn = k == Cnt-1 ? Cn-k*Cnb : Cnb;
                        ldak = LDA;//BLKLDD(A, k);
                        zbeta = k == 0 ? *beta : zone;
                        if (k < n) {
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                tempmm, tempnn, tempkn, 
                                *alpha, B(m, k), ldbm,  /* ldb  * K */
                                       A(k, n), ldak,  /* ldak * Y */
                                zbeta, C(m, n), ldcm); /* ldc  * Y */
                        }
                        else {
                            if (k == n) {
                                INSERT_TASK_zhemm(
                                    side, uplo,
                                    tempmm, tempnn, 
                                    *alpha, A(k, k), ldak,  /* ldak * Y */
                                           B(m, k), ldbm,  /* ldb  * Y */
                                    zbeta, C(m, n), ldcm); /* ldc  * Y */
                            }
                            else {
                                INSERT_TASK_zgemm(
                                    CblasNoTrans, CblasConjTrans,
                                    tempmm, tempnn, tempkn, 
                                    *alpha, B(m, k), ldbm,  /* ldb * K */
                                           A(n, k), ldan,  /* lda * K */
                                    zbeta, C(m, n), ldcm); /* ldc * Y */
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* hemm */
static void (*dl_zhemm)(
  const char* side, const char* uplo,
  const KBLAS_INT* m, const KBLAS_INT* n,
  const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT *lda,
                            const Complex64_t* B, const KBLAS_INT *ldb,
  const Complex64_t* beta,  Complex64_t* C, const KBLAS_INT *ldc ) = 0;

/* CPU driver */
extern void xkblas_zhemm_native_(
  const char * side, const char * uplo,
  const KBLAS_INT * m, const KBLAS_INT * n,
  const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT *lda,
                            const Complex64_t* B, const KBLAS_INT *ldb,
  const Complex64_t* beta,  Complex64_t* C, const KBLAS_INT *ldc
)
{
  if (dl_zhemm ==0) xkblas_load_sym((void**)&dl_zhemm,SYMBLAS_NAME(zhemm));
  dl_zhemm( side, uplo,
            m, n,
            alpha, A, lda,
                   B, ldb,
            beta,  C, ldc
  );
}

extern int xkblas_zhemm_native(
  int side, int uplo, int M, int N,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC )
{
  char s = cblas2blas_side(side);
  char u = cblas2blas_fill(uplo);
  const KBLAS_INT iM = M;
  const KBLAS_INT iN = N;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDB = LDB;
  const KBLAS_INT iLDC = LDC;
  xkblas_zhemm_native_( &s, &u, &iM, &iN, alpha, A, &iLDA, B, &iLDB, beta, C, &iLDC);
  return 0;
}

