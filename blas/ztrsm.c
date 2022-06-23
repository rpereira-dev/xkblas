/**
 *
 * @file ztrsm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Cblaseleon ztrsm wrappers
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Jakub Kurzak
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Thierry Gautier
 * @date 2018-11-20
 * @precisions normal z -> s d c
 * This file was merged from pzgemm and zgemm from Chameleon by Thierry Gautier
 * for Kaapi that support natively 2D memory view.
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
 *  CHAMELEON_ztrsm - Computes triangular solve A*X = B or X*A = B.
 *
 *******************************************************************************
 *
 * @param[in] side
 *          Specifies whether A appears on the left or on the right of X:
 *          = CblasLeft:  A*X = B
 *          = CblasRight: X*A = B
 *
 * @param[in] uplo
 *          Specifies whether the matrix A is upper triangular or lower triangular:
 *          = CblasUpper: Upper triangle of A is stored;
 *          = CblasLower: Lower triangle of A is stored.
 *
 * @param[in] transA
 *          Specifies whether the matrix A is transposed, not transposed or conjugate transposed:
 *          = CblasNoTrans:   A is transposed;
 *          = CblasTrans:     A is not transposed;
 *          = CblasConjTrans: A is conjugate transposed.
 *
 * @param[in] diag
 *          Specifies whether or not A is unit triangular:
 *          = CblasNonUnit: A is non unit;
 *          = CblasUnit:    A us unit.
 *
 * @param[in] N
 *          The order of the matrix A. N >= 0.
 *
 * @param[in] NRHS
 *          The number of right hand sides, i.e., the number of columns of the matrix B. NRHS >= 0.
 *
 * @param[in] alpha
 *          alpha specifies the scalar alpha.
 *
 * @param[in] A
 *          The triangular matrix A. If uplo = CblasUpper, the leading N-by-N upper triangular
 *          part of the array A contains the upper triangular matrix, and the strictly lower
 *          triangular part of A is not referenced. If uplo = CblasLower, the leading N-by-N
 *          lower triangular part of the array A contains the lower triangular matrix, and the
 *          strictly upper triangular part of A is not referenced. If diag = CblasUnit, the
 *          diagonal elements of A are also not referenced and are assumed to be 1.
 *
 * @param[in] LDA
 *          The leading dimension of the array A. LDA >= max(1,N).
 *
 * @param[in,out] B
 *          On entry, the N-by-NRHS right hand side matrix B.
 *          On exit, if return value = 0, the N-by-NRHS solution matrix X.
 *
 * @param[in] LDB
 *          The leading dimension of the array B. LDB >= max(1,N).
 *
 *******************************************************************************
 *
 * @return
 *          \retval CHAMELEON_SUCCESS successful exit
 *          \retval <0 if -i, the i-th argument had an illegal value
 *
 *******************************************************************************
 *
 */
#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n

int xkblas_ztrsm_async(
    int side, int uplo,
    int transA, int diag,
    int N, int NRHS,
    const Complex64_t* alpha, const Complex64_t* A, int LDA,
                              Complex64_t* B, int LDB )
{
    int NA;
    int status;

    if (side == CblasLeft) {
        NA = N;
    } else {
        NA = NRHS;
    }

    /* Check input arguments */
    if (side != CblasLeft && side != CblasRight) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of side");
        return -1;
    }
    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of uplo");
        return -2;
    }
    if (((transA < CblasNoTrans) || (transA > CblasConjTrans)) ) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of transA");
        return -3;
    }
    if ((diag != CblasUnit) && (diag != CblasNonUnit)) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of diag");
        return -4;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of N");
        return -5;
    }
    if (NRHS < 0) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of NRHS");
        return -6;
    }
    if (LDA < kaapi_max(1, NA)) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of LDA");
        return -8;
    }
    if (LDB < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_ztrsm", "illegal value of LDB");
        return -10;
    }
    /* Quick return */
    if (kaapi_min(N, NRHS) == 0)
        return 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_TRSM,N,NRHS,NA);

    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    xkblas_matrix_descr_t* Bh = xkblas_find(B);
    if (!xkblas_matrix_descr_isinit(Ah))
    {
      xkblas_init_matrix_handle(Ah, (void*)A, NA, NA, LDA, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == NA) && (Ah->N == NA) );
    }
    if (!xkblas_matrix_descr_isinit(Bh))
    {
      xkblas_init_matrix_handle(Bh, (void*)B, N, NRHS, LDB, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Bh->ld == LDB) && (Bh->M == N) && (Bh->N == NRHS) );
    }

    size_t Am = Ah->M;
    size_t An = Ah->N;
    size_t Bm = Bh->M;
    size_t Bn = Bh->N;

    size_t Amb = Ah->mb;
    size_t Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

    size_t Bmb = Bh->mb;
    size_t Bnb = Bh->nb;
    size_t Bmt = Bh->mt;
    size_t Bnt = Bh->nt;

    size_t k, m, n;
    size_t ldak, ldam, ldan, ldbk, ldbm;
    size_t tempkm, tempkn, tempmm, tempnn;

    Complex64_t zone       = (Complex64_t) 1.0;
    Complex64_t mzone      = (Complex64_t)-1.0;
    Complex64_t minvalpha  = (Complex64_t)-1.0 / *alpha;
    Complex64_t lalpha;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "B", Bh ) );
  }
#endif

    xkblas_context_t* xkctxt =xkblas_context_get();
    xkblas_auto_map( xkctxt, KERN_TRSM, Bh );

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt = xkctxt->kctxt;
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"ztrsm",8);
      evt->u.s.d1.u = N;
      evt->u.s.d2.u = NRHS;
      evt->u.s.d3.u = side;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
    evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 2 /*info*/ );
    if (evt)
    {
      evt->u.s.d0.u = uplo;
      evt->u.s.d1.u = transA;
      evt->u.s.d2.u = diag;
      evt->u.s.d3.u = 0;
      KAAPI_EVENT_PUSH(&ctxt->kproc, KAAPI_EVT_CALL);
    }
#endif

    /*
     *  CblasLeft / CblasUpper / CblasNoTrans
     */
    if (side == CblasLeft) {
        if (uplo == CblasUpper) {
            if (transA == CblasNoTrans) {
                for (k = 0; k < Bmt; k++) {
                    tempkm = k == 0 ? Bm-(Bmt-1)*Bmb : Bmb;
                    ldak = LDA;
                    ldbk = LDB;
                    lalpha = k == 0 ? *alpha : zone;
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempkm, tempnn, 
                            lalpha, A(Bmt-1-k, Bmt-1-k), ldak,  /* lda * tempkm */
                                    B(Bmt-1-k,        n), ldbk); /* ldb * tempnn */
                    }
                    for (m = k+1; m < Bmt; m++) {
                        ldam = LDA;
                        ldbm = LDB;
                        for (n = 0; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                Bmb, tempnn, tempkm, 
                                mzone,  A(Bmt-1-m, Bmt-1-k), ldam,
                                        B(Bmt-1-k, n       ), ldbk,
                                lalpha, B(Bmt-1-m, n       ), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasLeft / CblasUpper / Cblas[Conj]Trans
             */
            else {
                for (k = 0; k < Bmt; k++) {
                    tempkm = k == Bmt-1 ? Bm-k*Bmb : Bmb;
                    ldak = LDA;
                    ldbk = LDB;
                    lalpha = k == 0 ? *alpha : zone;
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempkm, tempnn, 
                            lalpha, A(k, k), ldak,
                                    B(k, n), ldbk);
                    }
                    for (m = k+1; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        for (n = 0; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                tempmm, tempnn, Bmb, 
                                mzone,  A(k, m), ldak,
                                        B(k, n), ldbk,
                                lalpha, B(m, n), ldbm);
                        }
                    }
                }
            }
        }
        /*
         *  CblasLeft / CblasLower / CblasNoTrans
         */
        else {
            if (transA == CblasNoTrans) {
                for (k = 0; k < Bmt; k++) {
                    tempkm = k == Bmt-1 ? Bm-k*Bmb : Bmb;
                    ldak = LDA;
                    ldbk = LDB;
                    lalpha = k == 0 ? *alpha : zone;
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempkm, tempnn, 
                            lalpha, A(k, k), ldak,
                                    B(k, n), ldbk);
                    }
                    for (m = k+1; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldam = LDA;
                        ldbm = LDB;
                        for (n = 0; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                tempmm, tempnn, Bmb, 
                                mzone,  A(m, k), ldam,
                                        B(k, n), ldbk,
                                lalpha, B(m, n), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasLeft / CblasLower / Cblas[Conj]Trans
             */
            else {
                for (k = 0; k < Bmt; k++) {
                    tempkm = k == 0 ? Bm-(Bmt-1)*Bmb : Bmb;
                    ldak = LDA;
                    ldbk = LDB;
                    lalpha = k == 0 ? *alpha : zone;
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempkm, tempnn, 
                            lalpha, A(Bmt-1-k, Bmt-1-k), ldak,
                                    B(Bmt-1-k,        n), ldbk);
                    }
                    for (m = k+1; m < Bmt; m++) {
                        ldbm = LDB;
                        for (n = 0; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                Bmb, tempnn, tempkm, 
                                mzone,  A(Bmt-1-k, Bmt-1-m), ldak,
                                        B(Bmt-1-k, n       ), ldbk,
                                lalpha, B(Bmt-1-m, n       ), ldbm);
                        }
                    }
                }
            }
        }
    }
    /*
     *  CblasRight / CblasUpper / CblasNoTrans
     */
    else {
        if (uplo == CblasUpper) {
            if (transA == CblasNoTrans) {
                for (k = 0; k < Bnt; k++) {
                    tempkn = k == Bnt-1 ? Bn-k*Bnb : Bnb;
                    ldak = LDA;
                    lalpha = k == 0 ? *alpha : zone;
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempmm, tempkn, 
                            lalpha, A(k, k), ldak,  /* lda * tempkn */
                                    B(m, k), ldbm); /* ldb * tempkn */
                    }
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        for (n = k+1; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                tempmm, tempnn, Bmb, 
                                mzone,  B(m, k), ldbm,  /* ldb * Bmb   */
                                        A(k, n), ldak,  /* lda * tempnn */
                                lalpha, B(m, n), ldbm); /* ldb * tempnn */
                        }
                    }
                }
            }
            /*
             *  CblasRight / CblasUpper / Cblas[Conj]Trans
             */
            else {
                for (k = 0; k < Bnt; k++) {
                    tempkn = k == 0 ? Bn-(Bnt-1)*Bnb : Bnb;
                    ldak = LDA;
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempmm, tempkn, 
                            *alpha, A(Bnt-1-k, Bnt-1-k), ldak,  /* lda * tempkn */
                                   B(       m, Bnt-1-k), ldbm); /* ldb * tempkn */

                        for (n = k+1; n < Bnt; n++) {
                            ldan = LDA;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, Bnb, tempkn, 
                                minvalpha, B(m,       Bnt-1-k), ldbm,  /* ldb  * tempkn */
                                           A(Bnt-1-n, Bnt-1-k), ldan, /* Amb * tempkn (Never last row) */
                                zone,      B(m,       Bnt-1-n), ldbm); /* ldb  * Bnb   */
                        }
                    }
                }
            }
        }
        /*
         *  CblasRight / CblasLower / CblasNoTrans
         */
        else {
            if (transA == CblasNoTrans) {
                for (k = 0; k < Bnt; k++) {
                    tempkn = k == 0 ? Bn-(Bnt-1)*Bnb : Bnb;
                    ldak = LDA;
                    lalpha = k == 0 ? *alpha : zone;
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempmm, tempkn, 
                            lalpha, A(Bnt-1-k, Bnt-1-k), ldak,  /* lda * tempkn */
                                    B(      m, Bnt-1-k), ldbm); /* ldb * tempkn */

                        for (n = k+1; n < Bnt; n++) {
                            INSERT_TASK_zgemm(
                                CblasNoTrans, CblasNoTrans,
                                tempmm, Bnb, tempkn, 
                                mzone,  B(m,       Bnt-1-k), ldbm,  /* ldb * tempkn */
                                        A(Bnt-1-k, Bnt-1-n), ldak,  /* lda * Bnb   */
                                lalpha, B(m,       Bnt-1-n), ldbm); /* ldb * Bnb   */
                        }
                    }
                }
            }
            /*
             *  CblasRight / CblasLower / Cblas[Conj]Trans
             */
            else {
                for (k = 0; k < Bnt; k++) {
                    tempkn = k == Bnt-1 ? Bn-k*Bnb : Bnb;
                    ldak = LDA;
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;
                        INSERT_TASK_ztrsm(
                            side, uplo, transA, diag,
                            tempmm, tempkn, 
                            *alpha, A(k, k), ldak,  /* lda * tempkn */
                                    B(m, k), ldbm); /* ldb * tempkn */

                        for (n = k+1; n < Bnt; n++) {
                            tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                            ldan = LDA;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, tempnn, Bmb, 
                                minvalpha, B(m, k), ldbm,  /* ldb  * tempkn */
                                           A(n, k), ldan, /* ldan * tempkn */
                                zone,      B(m, n), ldbm); /* ldb  * tempnn */
                        }
                    }
                }
            }
        }
    }
    return 0;
}


/* trsm */
static void (*dl_ztrsm)(
    const char * side, const char *uplo, const char* transa, const char* diag,
    const KBLAS_INT* m, const KBLAS_INT* n,
    const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT * lda,
                              Complex64_t* B, const KBLAS_INT * ldb ) = 0;

/* CPU driver */
extern void xkblas_ztrsm_native_(
    const char * side, const char *uplo, const char* transa, const char* diag,
    const KBLAS_INT* m, const KBLAS_INT* n,
    const Complex64_t* alpha, const Complex64_t* A, const KBLAS_INT * lda,
                              Complex64_t* B, const KBLAS_INT * ldb )
{
  if (dl_ztrsm ==0) xkblas_load_sym((void**)&dl_ztrsm,SYMBLAS_NAME(ztrsm));
  dl_ztrsm( side, uplo, transa, diag,
            m, n,
            alpha, A, lda,
                   B, ldb
  );
}

extern int xkblas_ztrsm_native(
  int side, int uplo, int transA, int diag, int N, int NRHS,
  const Complex64_t* alpha, const Complex64_t* A, int LDA, Complex64_t* B, int LDB )
{
  char s = cblas2blas_side(side);
  char u = cblas2blas_fill(uplo);
  char trA = cblas2blas_op(transA);
  char d = cblas2blas_diag(diag);
  const KBLAS_INT iN = N;
  const KBLAS_INT iNRHS = NRHS;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDB = LDB;
  xkblas_ztrsm_native_( &s, &u, &trA, &d, &iN, &iNRHS, alpha, A, &iLDA, B, &iLDB );
  return 0;
}
