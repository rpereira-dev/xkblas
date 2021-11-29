/**
 *
 * @file testing_zhemm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zhemm testing
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

static int check_solution(int side, int uplo, int M, int N,
                          Complex64_t alpha, Complex64_t *A, int LDA,
                          Complex64_t *B, int LDB,
                          Complex64_t beta, Complex64_t *Cref, Complex64_t *Ccham, int LDC);

int testing_zhemm(int argc, char **argv)
{
    int hres = 0;
    /* Check for number of arguments*/
    if ( argc != 7 ){
        USAGE("HEMM", "alpha beta M N K LDA LDB LDC",
              "   - alpha : alpha coefficient \n"
              "   - beta : beta coefficient \n"
              "   - M : number of rows of matrices A and C \n"
              "   - N : number of columns of matrices B and C \n"
              "   - LDA : leading dimension of matrix A \n"
              "   - LDB : leading dimension of matrix B \n"
              "   - LDC : leading dimension of matrix C\n");
        return -1;
    }

    Complex64_t alpha = (Complex64_t) atol(argv[0]);
    Complex64_t beta  = (Complex64_t) atol(argv[1]);
    int M     = atoi(argv[2]);
    int N     = atoi(argv[3]);
    int LDA   = atoi(argv[4]);
    int LDB   = atoi(argv[5]);
    int LDC   = atoi(argv[6]);
    int MNmax = max(M, N);
    double flops, fmuls, fadds, fp_per_mul, fp_per_add;

    double eps;
    int info_solution;
    int i, j, k, s, u;
    int LDAxM = LDA*MNmax;
    int LDBxN = LDB*N;
    int LDCxN = LDC*N;

    Complex64_t *A      = (Complex64_t *)xkblas_malloc(LDAxM*sizeof(Complex64_t));
    Complex64_t *B      = (Complex64_t *)xkblas_malloc(LDBxN*sizeof(Complex64_t));
    Complex64_t *C      = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cinit  = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
    Complex64_t *Cfinal = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));

    /* Check if unable to allocate memory */
    if ( (!A) || (!B) || (!C) || (!Cinit) || (!Cfinal) )
    {
        xkblas_free(A,LDAxM*sizeof(Complex64_t));
        xkblas_free(B,LDBxN*sizeof(Complex64_t));
        xkblas_free(C,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
        xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));
        printf("Out of Memory \n ");
        return -2;
    }

    eps = LAPACKE_dlamch_work('e');

    printf("\n");
#if defined(TESTING_API_XKBLAS_WRAPPER)
    printf("------ TESTS FOR XKBLAS WRAPPER API ZHEMM ROUTINE -------  \n");
#else
    printf("------ TESTS FOR XKBLAS ZHEMM ROUTINE -------  \n");
#endif
    printf("            Size of the Matrix %d by %d\n", M, N);
    printf("\n");
    printf(" The matrix A is randomly generated for each test.\n");
    printf("============\n");
    printf(" The relative machine precision (eps) is to be %e \n",eps);
    printf(" Computational tests pass if scaled residuals are less than 10.\n");

    /*----------------------------------------------------------
    *  TESTING ZHEMM
    */
    if (sizeof(Complex64_t) == sizeof(CFloat64_t)) {
        fp_per_mul = 1;
        fp_per_add = 1;
    } else {
        fp_per_mul = 6;
        fp_per_add = 2;
    }

    for (s=0; s<2; s++) {
        for (u=0; u<2; u++) {

#define ITER 5
            double time[ITER];
            double flops[ITER];

            int suspicious = 0;
            for (k=0; k<ITER; ++k)
            {
              /* Initialize A */
              testing_zplghe( (double)0., MNmax, A, LDA, 51 );

              /* Initialize B */
              LAPACKE_zlarnv_work(IONE, ISEED, LDBxN, B);

              /* Initialize C */
              LAPACKE_zlarnv_work(IONE, ISEED, LDCxN, C);

              /* Initialize  Cinit / Cfinal */
              for ( i = 0; i < M; i++)
                  for (  j = 0; j < N; j++)
                      Cinit[LDC*j+i] = C[LDC*j+i];
              for ( i = 0; i < M; i++)
                  for (  j = 0; j < N; j++)
                      Cfinal[LDC*j+i] = C[LDC*j+i];

              LAPACKE_zlarnv_work(1, ISEED, 1, &alpha);
              LAPACKE_zlarnv_work(1, ISEED, 1, &beta);

#if !defined(TESTING_API_XKBLAS_WRAPPER)
              double t0 = xkblas_elapsedtime();
              /* XKBLAS ZHEMM */
              xkblas_zhemm_async(side[s], uplo[u], M, N, &alpha, A, LDA, B, LDB, &beta, Cfinal, LDC);
              xkblas_memory_coherent_async(0, 0, M, N, Cfinal, LDC, sizeof(Complex64_t));
              xkblas_sync();
              double t1 = xkblas_elapsedtime();
              xkblas_memory_invalidate_caches();
#else
              /* test for F77 native call */
              extern void zhemm_(
                const char * side, const char * uplo,
                const int * m, const int * n,
                const Complex64_t* alpha, const Complex64_t* A, const int *lda,
                                          const Complex64_t* B, const int *ldb,
                const Complex64_t* beta,  Complex64_t* C, const int *ldc);
              double t0 = time_get_elapsedtime();
              char sd = cblas2blas_side( side[s] );
              char up = cblas2blas_fill( uplo[u] );
              zhemm_(&sd, &up, &M, &N, &alpha, A, &LDA, B, &LDB, &beta, Cfinal, &LDC);
              double t1 = time_get_elapsedtime();
#endif

              fadds = (double)(FADDS_HEMM(side[s],M,N));
              fmuls = (double)(FMULS_HEMM(side[s],M,N));
              flops[k] = 1e-9 * (fmuls * fp_per_mul + fadds * fp_per_add);
              time[k] = t1-t0;

              /* Check the solution */
              if (getenv("NO_CHECK") ==0)
              {
                info_solution = check_solution(side[s], uplo[u], M, N, alpha, A, LDA, B, LDB, beta, Cinit, Cfinal, LDC);
                if (info_solution) suspicious = 1;
              } else
                info_solution = 0;
            }

            printf("***************************************************\n");
            if (suspicious == 0) {
                printf(" ---- TESTING ZHEMM (%5s, %5s) ....... PASSED !\n", sidestr[s], uplostr[u]);
                for (i=0; i<ITER; ++i)
                  printf("GFlosp=%f\n", flops[i]/time[i]);
            }
            else {
                printf(" - TESTING ZHEMM (%s, %s) ... FAILED !\n", sidestr[s], uplostr[u]);    hres++;
            }
            printf("***************************************************\n");
        }
    }

    xkblas_free(A,LDAxM*sizeof(Complex64_t));
    xkblas_free(B,LDBxN*sizeof(Complex64_t));
    xkblas_free(C,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
    xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));

    return hres;
}

/*--------------------------------------------------------------
 * Check the solution
 */

static int check_solution(int side, int uplo, int M, int N,
                   Complex64_t alpha, Complex64_t *A, int LDA,
                   Complex64_t *B, int LDB,
                   Complex64_t beta, Complex64_t *Cref, Complex64_t *Ccham, int LDC)
{
    int info_solution, NrowA;
    double Anorm, Bnorm, Cinitnorm, Cchamnorm, Clapacknorm, Rnorm;
    double eps;
    Complex64_t beta_const;
    double result;
    CFloat64_t *work = (CFloat64_t *)malloc(max(M, N)* sizeof(CFloat64_t));

    beta_const  = (Complex64_t)-1.0;

    NrowA = (side == CblasLeft) ? M : N;
    Anorm       = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', NrowA, NrowA, A,       LDA, work);
    Bnorm       = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M,     N,     B,       LDB, work);
    Cinitnorm   = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M,     N,     Cref,    LDC, work);
    Cchamnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M,     N,     Ccham, LDC, work);

    xkblas_zhemm_native(side, uplo, M, N, &alpha, A, LDA, B, LDB, &beta, Cref, LDC);

    Clapacknorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Cref, LDC, work);

    cblas_zaxpy(LDC * N, CBLAS_SADDR(beta_const), Ccham, 1, Cref, 1);

    Rnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Cref, LDC, work);

    eps = LAPACKE_dlamch_work('e');

    printf("Rnorm %e, Anorm %e, Bnorm %e, Cinitnorm %e, Cchamnorm %e, Clapacknorm %e\n",Rnorm,Anorm,Bnorm,Cinitnorm,Cchamnorm,Clapacknorm);

    result = Rnorm / ((Anorm + Bnorm + Cinitnorm) * N * eps);

    printf("============\n");
    printf("Checking the norm of the difference against reference ZHEMM \n");
    printf("-- ||Ccham - Clapack||_oo/((||A||_oo+||B||_oo+||C||_oo).N.eps) = %e \n", result );

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
