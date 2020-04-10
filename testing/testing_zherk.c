/**
 *
 * @file testing_zherk.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zherk testing
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
#include "testing_zauxiliary.h"
#include "task_z_internal.h"

#include "flops.h"

static int check_solution(int uplo, int trans, int N, int K,
                          double alpha, Complex64_t *A, int LDA,
                          double beta,  Complex64_t *Cref, Complex64_t *Ccham, int LDC);


int testing_zherk(int argc, char **argv)
{
    int hres = 0;
    /* Check for number of arguments*/
    if ( argc != 6 ){
        USAGE("HERK", "alpha beta M N LDA LDC",
              "   - alpha : alpha coefficient\n"
              "   - beta : beta coefficient\n"
              "   - N : number of columns and rows of matrix C and number of row of matrix A\n"
              "   - K : number of columns of matrix A\n"
              "   - LDA : leading dimension of matrix A\n"
              "   - LDC : leading dimension of matrix C\n");
        return -1;
    }

    CFloat64_t alpha = (double) atol(argv[0]);
    CFloat64_t beta  = (double) atol(argv[1]);
    int N     = atoi(argv[2]);
    int K     = atoi(argv[3]);
    int LDA   = atoi(argv[4]);
    int LDC   = atoi(argv[5]);
    int NKmax = max(N, K);
    double flops, fmuls, fadds, fp_per_mul, fp_per_add;

    double eps;
    int info_solution;
    int u, t, i, k;
    size_t LDAxK = LDA*NKmax;
    size_t LDCxN = LDC*N;

    Complex64_t *A      = (Complex64_t *)xkblas_malloc(LDAxK*sizeof(Complex64_t));
    Complex64_t *C      = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cinit  = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cfinal = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));

    /* Check if unable to allocate memory */
    if ( (!A) || (!C) || (!Cinit) || (!Cfinal) ){
        xkblas_free(A,LDAxK*sizeof(Complex64_t));
        xkblas_free(C,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));
        printf("Out of Memory \n ");
        return -2;
    }

    eps = LAPACKE_dlamch_work('e');

    printf("\n");
#if defined(TESTING_API_XKBLAS_WRAPPER)
    printf("------ TESTS FOR XKBLAS WRAPPER API ZHERK ROUTINE -------  \n");
#else
    printf("------ TESTS FOR XKBLAS ZHERK ROUTINE -------  \n");
#endif
    printf("            Size of the Matrix A %d by %d\n", N, K);
    printf("\n");
    printf(" The matrix A is randomly generated for each test.\n");
    printf("============\n");
    printf(" The relative machine precision (eps) is to be %e \n",eps);
    printf(" Computational tests pass if scaled residuals are less than 10.\n");

    /*----------------------------------------------------------
    *  TESTING ZHERK
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
              /* Initialize A */
              LAPACKE_zlarnv_work(IONE, ISEED, LDAxK, A);

              /* Initialize C */
              testing_zplgsy( (double)0., N, C, LDC, 51 );

              /* Initialize  Cinit / Cfinal */
              memcpy(Cinit,  C, LDCxN*sizeof(Complex64_t));
              memcpy(Cfinal, C, LDCxN*sizeof(Complex64_t));

#if defined(PRECISION_z)
              LAPACKE_dlarnv_work(1, ISEED, 1, &alpha);
              LAPACKE_dlarnv_work(1, ISEED, 1, &beta);
#elif defined(PRECISION_c)
              LAPACKE_slarnv_work(1, ISEED, 1, &alpha);
              LAPACKE_slarnv_work(1, ISEED, 1, &beta);
#else
  #error "here"
#endif

#if !defined(TESTING_API_XKBLAS_WRAPPER)
              double t0 = xkblas_elapsedtime();
              /* XKBLAS ZHERK */
              xkblas_zherk_async(uplo[u], trans[t], N, K, &alpha, A, LDA, &beta, Cfinal, LDC);
              xkblas_memory_coherent_async(uplo[u], 0, N, N, Cfinal, LDC, sizeof(Complex64_t));
              xkblas_sync();
              double t1 = xkblas_elapsedtime();
              xkblas_memory_invalidate_caches();
#else
              /* test for F77 native call */
              extern void zherk_(
                const char * uplo, const char * transa,
                const int *n, const int *k,
                const CFloat64_t *alpha, const Complex64_t *A, const int* lda,
                const CFloat64_t *beta,  Complex64_t *C, const int* ldc);
              double t0 = time_get_elapsedtime();
              char up = cblas2blas_fill( uplo[u] );
              char tr = cblas2blas_op( trans[t] );
              zherk_(&up, &tr, &N, &K, &alpha, A, &LDA, &beta, Cfinal, &LDC);
              double t1 = time_get_elapsedtime();
#endif

              fadds = (double)(FADDS_HERK(N,K));
              fmuls = (double)(FMULS_HERK(N,K));
              flops[k] = 1e-9 * (fmuls * fp_per_mul + fadds * fp_per_add);
              time[k] = t1-t0;

              /* Check the solution */
              if (getenv("NO_CHECK") ==0)
              {
                info_solution = check_solution(uplo[u], trans[t], N, K,
                                           alpha, A, LDA, beta, Cinit, Cfinal, LDC);
                if (info_solution) suspicious = 1;
              } else
                info_solution = 0;
            }

            printf("***************************************************\n");
            if (suspicious == 0) {
                printf(" ---- TESTING ZHERK (%5s, %s) ........... PASSED !\n", uplostr[u], transstr[t]);
                for (i=0; i<ITER; ++i)
                  printf("GFlosp=%f\n", flops[i]/time[i]);
            }
            else {
                printf(" - TESTING ZHERK (%5s, %s) ... FAILED !\n", uplostr[u], transstr[t]);    hres++;
            }
            printf("***************************************************\n");
        }
    }

    xkblas_free(A,LDAxK*sizeof(Complex64_t));
    xkblas_free(C,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));

    return hres;
}

/*--------------------------------------------------------------
 * Check the solution
 */

static int check_solution(int uplo, int trans, int N, int K,
                          double alpha, Complex64_t *A, int LDA,
                          double beta,  Complex64_t *Cref, Complex64_t *Ccham, int LDC)
{
    int info_solution;
    double Anorm, Cinitnorm, Cchamnorm, Clapacknorm, Rnorm;
    double eps;
    Complex64_t beta_const;
    double result;
    CFloat64_t *work = (CFloat64_t *)malloc(max(N, K)* sizeof(CFloat64_t));

    beta_const  = -1.0;
    Anorm       = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I',
                                (trans == CblasNoTrans) ? N : K,
                                (trans == CblasNoTrans) ? K : N, A, LDA, work);
    Cinitnorm   = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref,    LDC, work);
    Cchamnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Ccham, LDC, work);

    CFloat64_t a = alpha;
    CFloat64_t b = beta;
    xkblas_zherk_native(uplo, trans, N, K, &a, A, LDA, &b, Cref, LDC);

    Clapacknorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref, LDC, work);

    cblas_zaxpy(LDC*N, CBLAS_SADDR(beta_const), Ccham, 1, Cref, 1);

    Rnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', N, N, Cref, LDC, work);

    eps = LAPACKE_dlamch_work('e');

    printf("Rnorm %e, Anorm %e, Cinitnorm %e, Cchamnorm %e, Clapacknorm %e\n",
           Rnorm, Anorm, Cinitnorm, Cchamnorm, Clapacknorm);

    result = Rnorm / ((Anorm + Cinitnorm) * N * eps);

    printf("============\n");
    printf("Checking the norm of the difference against reference ZHERK \n");
    printf("-- ||Ccham - Clapack||_oo/((||A||_oo+||C||_oo).N.eps) = %e \n", result);

    if ( isinf(Clapacknorm) || isinf(Cchamnorm) || isnan(result) || isinf(result) || (result > 10.0) ) {
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
