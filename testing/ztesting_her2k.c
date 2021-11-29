/**
 *
 * @file testing_zher2k.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zher2k testing
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Thierry Gautier, xkblas port
 * @date 2010-11-15
 * @precisions normal z -> c
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "xkblas.h"
#include "ztask_internal.h"
#include "ztesting_auxiliary.h"

#include "flops.h"

static int check_solution(int uplo, int trans, int N, int K,
                          Complex64_t alpha, Complex64_t *A, int LDA,
                          Complex64_t *B, int LDB,
                          CFloat64_t beta,  Complex64_t *Cref, Complex64_t *Ccham, int LDC);


int testing_zher2k(int argc, char **argv)
{
    int hres = 0;
    /* Check for number of arguments*/
    if ( argc != 7 ){
        USAGE("HER2K", "alpha beta M N LDA LDB LDC",
              "   - alpha : alpha coefficient\n"
              "   - beta : beta coefficient\n"
              "   - N : number of columns and rows of matrix C and number of row of matrix A and B\n"
              "   - K : number of columns of matrix A and B\n"
              "   - LDA : leading dimension of matrix A\n"
              "   - LDB : leading dimension of matrix B\n"
              "   - LDC : leading dimension of matrix C\n");
        return -1;
    }

    Complex64_t alpha = (Complex64_t) atof(argv[0]);
    CFloat64_t beta  = (CFloat64_t) atof(argv[1]);
    int N     = atoi(argv[2]);
    int K     = atoi(argv[3]);
    int LDA   = atoi(argv[4]);
    int LDB   = atoi(argv[5]);
    int LDC   = atoi(argv[6]);
    int NKmax = max(N, K);
    double flops, fmuls, fadds, fp_per_mul, fp_per_add;

    double eps;
    int info_solution;
    int u, t, i, k;
    size_t LDAxK = LDA*NKmax;
    size_t LDBxK = LDB*NKmax;
    size_t LDCxN = LDC*N;

    Complex64_t *A      = (Complex64_t *)xkblas_malloc(LDAxK*sizeof(Complex64_t));
    Complex64_t *B      = (Complex64_t *)xkblas_malloc(LDBxK*sizeof(Complex64_t));
    Complex64_t *C      = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cinit  = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cfinal = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));

    /* Check if unable to allocate memory */
    if ( (!A) || (!B) || (!C) || (!Cinit) || (!Cfinal) ){
        xkblas_free(A,LDAxK*sizeof(Complex64_t));
        xkblas_free(B,LDBxK*sizeof(Complex64_t));
        xkblas_free(C,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));
        printf("Out of Memory \n ");
        return -2;
    }

    eps = LAPACKE_dlamch_work('e');

    printf("\n");
#if defined(TESTING_API_XKBLAS_WRAPPER)
    printf("------ TESTS FOR XKBLAS WRAPPER API ZHER2K ROUTINE -------  \n");
#else
    printf("------ TESTS FOR XKBLAS ZHER2K ROUTINE -------  \n");
#endif
    printf("            Size of the Matrix C %d by %d\n", N, K);
    printf("\n");
    printf(" The matrix A is randomly generated for each test.\n");
    printf("============\n");
    printf(" The relative machine precision (eps) is to be %e \n",eps);
    printf(" Computational tests pass if scaled residuals are less than 10.\n");

    /*----------------------------------------------------------
    *  TESTING ZHER2K
    */
    if (sizeof(Complex64_t) == sizeof(CFloat64_t)) {
        fp_per_mul = 1;
        fp_per_add = 1;
    } else {
        fp_per_mul = 6;
        fp_per_add = 2;
    }

    for (u=0; u<2; u++) {
        for (t=0; t<3; t++) {
            if (trans[t] == CblasTrans) continue;

#define ITER 5
            double time[ITER];
            double flops[ITER];

            int suspicious = 0;
            for (k=0; k<ITER; ++k)
            {
              /* Initialize A,B */
              LAPACKE_zlarnv_work(IONE, ISEED, LDAxK, A);
              LAPACKE_zlarnv_work(IONE, ISEED, LDBxK, B);

              /* Initialize C */
              testing_zplghe( (double)N, N, C, LDC, 51 );

              memcpy(Cinit,  C, LDCxN*sizeof(Complex64_t));
              memcpy(Cfinal, C, LDCxN*sizeof(Complex64_t));

              LAPACKE_zlarnv_work(1, ISEED, 1, &alpha);
#if (PRECISION_z==1)
              LAPACKE_dlarnv_work(1, ISEED, 1, &beta);
#elif (PRECISION_c==1)
              LAPACKE_slarnv_work(1, ISEED, 1, &beta);
#else
  #error "here"
#endif

#if !defined(TESTING_API_XKBLAS_WRAPPER)
              double t0 = xkblas_elapsedtime();
              /* XKBLAS ZHER2K */
              xkblas_zher2k_async(uplo[u], trans[t], N, K, &alpha, A, LDA, B, LDB, &beta, Cfinal, LDC);
              xkblas_memory_coherent_async(uplo[u], 0, N, N, Cfinal, LDC, sizeof(Complex64_t));
              xkblas_sync();
              double t1 = xkblas_elapsedtime();
              xkblas_memory_invalidate_caches();
#else
              /* test for F77 native call */
              extern void zher2k_(
                const char * uplo, const char * transa,
                const int *n, const int *k,
                const Complex64_t *alpha, const Complex64_t *A, const int* lda,
                                          const Complex64_t *B, const int* ldb,
                const CFloat64_t *beta,   Complex64_t *C, const int* ldc);
              double t0 = time_get_elapsedtime();
              char up = cblas2blas_fill( uplo[u] );
              char tr = cblas2blas_op( trans[t] );
              zher2k_(&up, &tr, &N, &K, &alpha, A, &LDA, B, &LDB, &beta, Cfinal, &LDC);
              double t1 = time_get_elapsedtime();
#endif

              fadds = (double)(FADDS_HER2K(N,K));
              fmuls = (double)(FMULS_HER2K(N,K));
              flops[k] = 1e-9 * (fmuls * fp_per_mul + fadds * fp_per_add);
              time[k] = t1-t0;

              /* Check the solution */
              if (getenv("NO_CHECK") ==0)
              {
                info_solution = check_solution(uplo[u], trans[t], N, K,
                                           alpha, A, LDA, B, LDB, beta, Cinit, Cfinal, LDC);
                if (info_solution) suspicious = 1;
              } else
                info_solution = 0;
            }

            printf("***************************************************\n");
            if (suspicious == 0) {
                printf(" ---- TESTING ZHER2K (%5s, %s) ........... PASSED !\n", uplostr[u], transstr[t]);
                for (i=0; i<ITER; ++i)
                  printf("GFlosp=%f\n", flops[i]/time[i]);
            }
            else {
                printf(" - TESTING ZHER2K (%5s, %s) ... FAILED !\n", uplostr[u], transstr[t]);    hres++;
            }
            printf("***************************************************\n");
        }
    }

    xkblas_free(A,LDAxK*sizeof(Complex64_t));
    xkblas_free(B,LDBxK*sizeof(Complex64_t));
    xkblas_free(C,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));

    return hres;
}

/*--------------------------------------------------------------
 * Check the solution
 */

static int check_solution(int uplo, int trans, int N, int K,
                          Complex64_t alpha, Complex64_t *A, int LDA,
                          Complex64_t *B, int LDB,
                          CFloat64_t beta, Complex64_t *Cref, Complex64_t *Ccham, int LDC)
{
    int info_solution;
    double Anorm, Bnorm, Cinitnorm, Cchamnorm, Clapacknorm, Rnorm, result;
    double eps;
    Complex64_t beta_const;

    CFloat64_t *work = (CFloat64_t *)malloc(max(N, K)* sizeof(CFloat64_t));

    beta_const  = -1.0;
    Anorm       = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I',
                                      (trans == CblasNoTrans) ? N : K,
                                      (trans == CblasNoTrans) ? K : N, A, LDA, work);
    Bnorm       = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I',
                                      (trans == CblasNoTrans) ? N : K,
                                      (trans == CblasNoTrans) ? K : N, B, LDB, work);
    Cinitnorm   = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref,    LDC, work);
    Cchamnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Ccham, LDC, work);

    xkblas_zher2k_native(uplo, trans, N, K, &alpha, A, LDA, B, LDB, &beta, Cref, LDC);

    Clapacknorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref, LDC, work);

    cblas_zaxpy(LDC*N, CBLAS_SADDR(beta_const), Ccham, 1, Cref, 1);

    Rnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref, LDC, work);

    eps = LAPACKE_dlamch_work('e');

    printf("Rnorm %e, Anorm %e, Cinitnorm %e, Cchamnorm %e, Clapacknorm %e\n",
           Rnorm, Anorm, Cinitnorm, Cchamnorm, Clapacknorm);

    result = Rnorm / ((Anorm + Bnorm + Cinitnorm) * N * eps);
    printf("============\n");
    printf("Checking the norm of the difference against reference ZHER2K \n");
    printf("-- ||Ccham - Clapack||_oo/((||A||_oo+||C||_oo).N.eps) = %e \n", result);

    if (  isnan(Rnorm) || isinf(Rnorm) || isnan(result) || isinf(result) || (result > 10.0) ) {
         printf("-- The solution is suspicious ! \n");
         info_solution = 1;
    }
    else {
         printf("-- The solution is CORRECT ! \n");
         info_solution= 0 ;
    }

    free(work);

    return info_solution;
}
