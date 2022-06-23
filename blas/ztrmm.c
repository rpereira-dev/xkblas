/**
 *
 * @file ztrmm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief  Chameleon ztrmm wrappers
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
 *  CHAMELEON_ztrmm - Computes B = alpha*op( A )*B or B = alpha*B*op( A ).
 *
 *******************************************************************************
 *
 * @param[in] side
 *          Specifies whether A appears on the left or on the right of X:
 *          = ChamLeft:  A*X = B
 *          = ChamRight: X*A = B
 *
 * @param[in] uplo
 *          Specifies whether the matrix A is upper triangular or lower triangular:
 *          = CblasUpper: Upper triangle of A is stored;
 *          = CblasLower: Lower triangle of A is stored.
 *
 * @param[in] transA
 *          Specifies whether the matrix A is transposed, not transposed or conjugate transposed:
 *          = CblasNoTrans:   A is transposed;
 *          = ChamTrans:     A is not transposed;
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
 * @retval CHAMELEON_SUCCESS successful exit
 * @retval <0 if -i, the i-th argument had an illegal value
 *
 *******************************************************************************
 *
 *
 */

#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n

int xkblas_ztrmm_async( int side, int uplo,
                 int transA, int diag,
                 int N, int NRHS, 
                 const Complex64_t* alpha, const Complex64_t *A, int LDA,
                 Complex64_t *B, int LDB )
{
    int NA;

    if (side == CblasLeft) {
        NA = N;
    } else {
        NA = NRHS;
    }

    /* Check input arguments */
    if (side != CblasLeft && side != CblasRight) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of side");
        return -1;
    }
    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of uplo");
        return -2;
    }
    if (transA != CblasConjTrans &&
        transA != CblasNoTrans   &&
        transA != CblasTrans )
    {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of transA");
        return -3;
    }
    if ((diag != CblasUnit) && (diag != CblasNonUnit)) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of diag");
        return -4;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of N");
        return -5;
    }
    if (NRHS < 0) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of NRHS");
        return -6;
    }
    if (LDA < kaapi_max(1, NA)) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of LDA");
        return -8;
    }
    if (LDB < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_ztrmm", "illegal value of LDB");
        return -10;
    }
    /* Quick return */
    if (kaapi_min(N, NRHS) == 0)
        return 0;

    /* */
    size_t NB = xkblas_auto_tilesize(KERN_TRMM,N,NRHS,NA);

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

    /* */
    int Am = Ah->M;
    int An = Ah->N;
    int Bm = Bh->M;
    int Bn = Bh->N;

    int Amb = Ah->mb;
    int Anb = Ah->nb;
    int Amt = Ah->mt;
    int Ant = Ah->nt;

    int Bmb = Bh->mb;
    int Bnb = Bh->nb;
    int Bmt = Bh->mt;
    int Bnt = Bh->nt;

    int k, m, n;
    int ldak, ldam, ldan, ldbk, ldbm;
    int tempkm, tempkn, tempmm, tempnn;

    Complex64_t zone       = (Complex64_t) 1.0;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "B", Bh ) );
  }
#endif

    xkblas_context_t* xkctxt =xkblas_context_get();
    xkblas_auto_map( xkctxt, KERN_TRMM, Bh );

#if KAAPI_USE_TRACELIB==1
    kaapi_context_t* ctxt = xkctxt->kctxt;
    kaapi_event_t* evt = KAAPI_EVENT_GET(&ctxt->kproc, KAAPI_EVT_CALL, 0 /*begin*/ );
    if (evt)
    {
      strncpy(evt->u.s.d0.c8,"ztrmm",8);
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
                for (m = 0; m < Bmt; m++) {
                    tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                    ldbm = LDB; //BLKLDD(B, m);
                    ldam = LDA; //BLKLDD(A, m);
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(m, m), ldam,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = m+1; k < Amt; k++) {
                            tempkn = k == Ant-1 ? An-k*Anb : Anb;
                            ldbk = LDB;//BLKLDD(B, k);
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                tempmm, tempnn, tempkn, 
                                *alpha, A(m, k), ldam,
                                        B(k, n), ldbk,
                                zone,   B(m, n), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasLeft / CblasUpper / Cblas[Conj]Trans
             */
            else {
                for (m = Bmt-1; m > -1; m--) {
                    tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                    ldbm = LDB;//BLKLDD(B, m);
                    ldam = LDA;//BLKLDD(A, m);
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(m, m), ldam,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = 0; k < m; k++) {
                            ldbk = LDB;//BLKLDD(B, k);
                            ldak = LDA;//BLKLDD(A, k);
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                tempmm, tempnn, Bmb, 
                                *alpha, A(k, m), ldak,
                                       B(k, n), ldbk,
                                zone,  B(m, n), ldbm);
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
                for (m = Bmt-1; m > -1; m--) {
                    tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                    ldbm = LDB;//BLKLDD(B, m);
                    ldam = LDA;//BLKLDD(A, m);
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(m, m), ldam,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = 0; k < m; k++) {
                            ldbk = LDB;//BLKLDD(B, k);
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                tempmm, tempnn, Bmb,
                                *alpha, A(m, k), ldam,
                                       B(k, n), ldbk,
                                zone,  B(m, n), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasLeft / CblasLower / Cblas[Conj]Trans
             */
            else {
                for (m = 0; m < Bmt; m++) {
                    tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                    ldbm = LDB;//BLKLDD(B, m);
                    ldam = LDA;//BLKLDD(A, m);
                    for (n = 0; n < Bnt; n++) {
                        tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(m, m), ldam,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = m+1; k < Amt; k++) {
                            tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                            ldak = LDA;//BLKLDD(A, k);
                            ldbk = LDB;//BLKLDD(B, k);
                            INSERT_TASK_zgemm(
                                transA, CblasNoTrans,
                                tempmm, tempnn, tempkm, 
                                *alpha, A(k, m), ldak,
                                       B(k, n), ldbk,
                                zone,  B(m, n), ldbm);
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
                for (n = Bnt-1; n > -1; n--) {
                    tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                    ldan = LDA;//BLKLDD(A, n);
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;//BLKLDD(B, m);
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(n, n), ldan,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = 0; k < n; k++) {
                            ldak = LDA;//BLKLDD(A, k);
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, tempnn, Bmb, 
                                *alpha, B(m, k), ldbm,
                                       A(k, n), ldak,
                                zone,  B(m, n), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasRight / CblasUpper / Cblas[Conj]Trans
             */
            else {
                for (n = 0; n < Bnt; n++) {
                    tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                    ldan = LDA;//BLKLDD(A, n);
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;//BLKLDD(B, m);
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(n, n), ldan,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = n+1; k < Amt; k++) {
                            tempkn = k == Ant-1 ? An-k*Anb : Anb;
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, tempnn, tempkn, 
                                *alpha, B(m, k), ldbm,
                                       A(n, k), ldan,
                                zone,  B(m, n), ldbm);
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
                for (n = 0; n < Bnt; n++) {
                    tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                    ldan = LDA;//BLKLDD(A, n);
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;//BLKLDD(B, m);
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(n, n), ldan,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = n+1; k < Amt; k++) {
                            tempkn = k == Ant-1 ? An-k*Anb : Anb;
                            ldak = LDA;//BLKLDD(A, k);
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, tempnn, tempkn, 
                                *alpha, B(m, k), ldbm,
                                       A(k, n), ldak,
                                zone,  B(m, n), ldbm);
                        }
                    }
                }
            }
            /*
             *  CblasRight / CblasLower / Cblas[Conj]Trans
             */
            else {
                for (n = Bnt-1; n > -1; n--) {
                    tempnn = n == Bnt-1 ? Bn-n*Bnb : Bnb;
                    ldan = LDA;//BLKLDD(A, n);
                    for (m = 0; m < Bmt; m++) {
                        tempmm = m == Bmt-1 ? Bm-m*Bmb : Bmb;
                        ldbm = LDB;//BLKLDD(B, m);
                        INSERT_TASK_ztrmm(
                            side, uplo, transA, diag,
                            tempmm, tempnn, 
                            *alpha, A(n, n), ldan,  /* lda * tempkm */
                                   B(m, n), ldbm); /* ldb * tempnn */

                        for (k = 0; k < n; k++) {
                            INSERT_TASK_zgemm(
                                CblasNoTrans, transA,
                                tempmm, tempnn, Bmb, 
                                *alpha, B(m, k), ldbm,
                                       A(n, k), ldan,
                                zone,  B(m, n), ldbm);
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/* trmm */
static void (*dl_ztrmm)(
  const char* side, const char* uplo, const char* transa, const char* diag,
  const KBLAS_INT* m, const KBLAS_INT* n,
  const Complex64_t*alpha, const Complex64_t *A, const KBLAS_INT *lda,
                           Complex64_t *B, const KBLAS_INT* ldb ) = 0;

/* CPU driver */
extern void xkblas_ztrmm_native_(
  const char * side, const char *uplo, const char *transa, const char * diag,
  const KBLAS_INT *m, const KBLAS_INT * n,
  const Complex64_t* alpha,  const Complex64_t *A, const KBLAS_INT *lda,
                            Complex64_t *B, const KBLAS_INT *ldb
)
{
  if (dl_ztrmm ==0) xkblas_load_sym((void**)&dl_ztrmm,SYMBLAS_NAME(ztrmm));
  dl_ztrmm( side, uplo, transa, diag,
            m, n,
            alpha, A, lda,
                   B, ldb
  );
}

extern int xkblas_ztrmm_native(
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
  xkblas_ztrmm_native_( &s, &u, &trA, &d, &iN, &iNRHS, alpha, A, &iLDA, B, &iLDB );
  return 0;
}
