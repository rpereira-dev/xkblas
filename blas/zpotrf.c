/**
 *
 * @file pzpotrf.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zpotrf parallel algorithm
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Jakub Kurzak
 * @author Hatem Ltaief
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Florent Pruvost
 * @date 2010-11-15
 * @author Thierry Gautier
 * @date 2018-11-20
 * @precisions normal z -> s d c
 * This file was merged from pzpotrf and zpotrf from Chameleon by Thierry Gautier
 * for Kaapi that support natively 2D memory view.
 *
 */
#include "common.h"
#include "ztask.h"
#include "ztask_internal.h"

#ifdef KAAPI_DEBUG
#undef KAAPI_DEBUG
#endif

#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n
#define C(m, n) C##h,  m,  n

/**
 *  Parallel tile Cholesky factorization - dynamic scheduling
 */
int xkblas_zpotrf_async(
    int uplo, int N, const Complex64_t *A, int LDA
)
{
    int k, m, n;
    int ldak, ldam, ldan;
    int tempkm, tempmm, tempnn;
    size_t ws_host   = 0;

    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_zpotrf", "illegal value of uplo");
        return -2;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_zpotrf", "illegal value of N");
        return -5;
    }
    if (LDA < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_zpotrf", "illegal value of LDA");
        return -8;
    }

    Complex64_t zone  = (Complex64_t) 1.0;
    Complex64_t mzone = (Complex64_t)-1.0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_get_param();
    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    if (Ah == 0) xkblas_init_matrix_handle(Ah, (void*)A, N, N, LDA, sizeof(Complex64_t), NB, NB);
    kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == N) && (Ah->N == N) );

    size_t Am = N;
    size_t An = N;
    size_t Amb = Ah->mb;
    size_t Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

#if defined(KAAPI_DEBUG)
  {
    assert( 0 == xkblas_dbg_setname( "A", Ah ) );
  }
#endif

    /*
     *  CblasLower
     */
    if (uplo == CblasLower) {
        for (k = 0; k < Amt; k++) {
            tempkm = k == Amt-1 ? Am-k*Amb : Amb;
            ldak = LDA; //BLKLDD(A, k);

            //options.priority = 2*A->mt - 2*k;
            INSERT_TASK_zpotrf(
                CblasLower, tempkm,
                A(k, k), ldak );

            for (m = k+1; m < Amt; m++) {
                tempmm = m == Amt-1 ? Am-m*Amb : Amb;
                ldam = LDA; //BLKLDD(A, m);

                //options.priority = 2*A->mt - 2*k - m;
                INSERT_TASK_ztrsm(
                    CblasRight, CblasLower, CblasConjTrans, CblasNonUnit,
                    tempmm, Amb,
                    zone, A(k, k), ldak,
                          A(m, k), ldam);
            }

            for (n = k+1; n < Ant; n++) {
                tempnn = n == Ant-1 ? An-n*Anb : Anb;
                ldan = LDA; // BLKLDD(A, n);

                //options.priority = 2*A->mt - 2*k - n;
                INSERT_TASK_zherk(
                    CblasLower, CblasNoTrans,
                    tempnn, Anb,
                    mzone, A(n, k), ldan,
                     zone, A(n, n), ldan);

                for (m = n+1; m < Amt; m++) {
                    tempmm = m == Amt-1 ? Am - m*Amb : Amb;
                    ldam = LDA; //BLKLDD(A, m);

                    //options.priority = 2*A->mt - 2*k - n - m;
                    INSERT_TASK_zgemm(
                        CblasNoTrans, CblasConjTrans,
                        tempmm, tempnn, Amb,
                        mzone, A(m, k), ldam,
                               A(n, k), ldan,
                        zone,  A(m, n), ldam);
                }
            }
        }
    }
    /*
     *  CblasUpper
     */
    else {
        for (k = 0; k < Ant; k++) {

            tempkm = k == Ant-1 ? An-k*Anb : Anb;
            ldak = LDA; //BLKLDD(A, k);

            //options.priority = 2*A->nt - 2*k;
            INSERT_TASK_zpotrf(
                CblasUpper,
                tempkm,
                A(k, k), ldak );

            for (n = k+1; n < Ant; n++) {
                tempnn = n == Ant-1 ? An - n*Anb : Anb;

                //options.priority = 2*A->nt - 2*k - n;
                INSERT_TASK_ztrsm(
                    CblasLeft, CblasUpper, CblasConjTrans, CblasNonUnit,
                    Amb, tempnn,
                    zone, A(k, k), ldak,
                          A(k, n), ldak);
            }

            for (m = k+1; m < Amt; m++) {
                tempmm = m == Amt-1 ? Am - m*Amb : Amb;
                ldam = LDA; //BLKLDD(A, m);

                //options.priority = 2*A->nt - 2*k  - m;
                INSERT_TASK_zherk(
                    CblasUpper, CblasConjTrans,
                    tempmm, Amb,
                    mzone, A(k, m), ldak,
                     zone, A(m, m), ldam);

                for (n = m+1; n < Ant; n++) {
                    tempnn = n == Ant-1 ? An-n*Anb : Anb;

                    //options.priority = 2*A->nt - 2*k - n - m;
                    INSERT_TASK_zgemm(
                        CblasConjTrans, CblasNoTrans,
                        tempmm, tempnn, Amb,
                        mzone, A(k, m), ldak,
                               A(k, n), ldak,
                        zone,  A(m, n), ldam);
                }
            }
        }
    }
}
