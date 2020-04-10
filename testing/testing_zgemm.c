/**
 *
 * @file testing_zgemm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zgemm testing
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @author Thierry Gautier, xkblas port
 * @date 2010-11-15
 * @precisions normal z -> c d s
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
/* See Lawn 41 page 120 */
#define _FMULS FMULS_GEMM(M, N, K)
#define _FADDS FADDS_GEMM(M, N, K)

static int check_solution(int transA, int transB, int M, int N, int K,
                          Complex64_t alpha, Complex64_t *A, int LDA,
                          Complex64_t *B, int LDB,
                          Complex64_t beta, Complex64_t *Cref, Complex64_t *Ccham, int LDC);

int testing_zgemm(int argc, char **argv)
{
    int hres = 0;
    /* Check for number of arguments*/
    if ( argc < 8) {
        USAGE("GEMM", "alpha beta M N K LDA LDB LDC",
              "   - alpha  : alpha coefficient\n"
              "   - beta   : beta coefficient\n"
              "   - M      : number of rows of matrices A and C\n"
              "   - N      : number of columns of matrices B and C\n"
              "   - K      : number of columns of matrix A / number of rows of matrix B\n"
              "   - LDA    : leading dimension of matrix A\n"
              "   - LDB    : leading dimension of matrix B\n"
              "   - LDC    : leading dimension of matrix C\n");
        return -1;
    }

    Complex64_t alpha = (Complex64_t) atof(argv[0]);
    Complex64_t beta = (Complex64_t) atof(argv[1]);
    int M     = atoi(argv[2]);
    int N     = atoi(argv[3]);
    int K     = atoi(argv[4]);
    int LDA   = atoi(argv[5]);
    int LDB   = atoi(argv[6]);
    int LDC   = atoi(argv[7]);

    double eps;
    int info_solution;
    int i, j, k, ta, tb;
    double flops, fmuls, fadds, fp_per_mul, fp_per_add;

    int LDAxK = LDA*max(M,K);
    int LDBxN = LDB*max(K,N);
    int LDCxN = LDC*N;

    eps = LAPACKE_dlamch_work('e');

    printf("\n");
#if defined(TESTING_API_XKBLAS_WRAPPER)
    printf("------ TESTS FOR XKBLAS WRAPPER API ZGEMM ROUTINE -------  \n");
#else
    printf("------ TESTS FOR XKBLAS ZGEMM ROUTINE -------  \n");
#endif
    printf("            Size of the Matrix %d by %d\n", M, N);
    printf("\n");
    printf(" The matrix A is randomly generated for each test.\n");
    printf("============\n");
    printf(" The relative machine precision (eps) is to be %e \n",eps);
    printf(" Computational tests pass if scaled residuals are less than 10.\n");

    /*----------------------------------------------------------
     *  TESTING ZGEMM
     */

    /* Initialize A, B, C */
    if (sizeof(Complex64_t) == sizeof(CFloat64_t)) {
        fp_per_mul = 1;
        fp_per_add = 1;
    } else {
        fp_per_mul = 6;
        fp_per_add = 2;
    }

#if defined(PRECISION_z) || defined(PRECISION_c)
    for (ta=0; ta<3; ta++) {
        for (tb=0; tb<3; tb++) {
#elif 1
    for (ta=0; ta<2; ta++) {
        for (tb=0; tb<2; tb++) {
#else
    for (ta=0; ta<1; ta++) {
        for (tb=1; tb<2; tb++) {
#endif

#define ITER 5
	    double time[ITER];
	    double flops[ITER];

	    int suspicious = 0;
	    for (k=0; k<ITER; ++k)
	    {
              Complex64_t *A      = (Complex64_t *)xkblas_malloc(LDAxK*sizeof(Complex64_t));
              Complex64_t *B      = (Complex64_t *)xkblas_malloc(LDBxN*sizeof(Complex64_t));
              Complex64_t *C      = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
              Complex64_t *Cinit  = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));
              Complex64_t *Cfinal = (Complex64_t *)xkblas_malloc(LDCxN*sizeof(Complex64_t));

              /* Check if unable to allocate memory */
              if ( (!A) || (!B) || (!C) || (!Cinit) || (!Cfinal) )
              {
                  xkblas_free(A,LDAxK*sizeof(Complex64_t));
                  xkblas_free(B,LDBxN*sizeof(Complex64_t));
                  xkblas_free(C,LDCxN*sizeof(Complex64_t));
                  xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
                  xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));
                  printf("Out of Memory \n ");
                  return -2;
              }

              LAPACKE_zlarnv_work(IONE, ISEED, LDAxK, A);
              LAPACKE_zlarnv_work(IONE, ISEED, LDBxN, B);
              LAPACKE_zlarnv_work(IONE, ISEED, LDCxN, C);

              #pragma omp parallel for
              for ( i = 0; i < LDCxN; ++i)
                Cinit[i] = Cfinal[i] = C[i];

              /* XKBLAS ZGEMM */
              LAPACKE_zlarnv_work(1, ISEED, 1, &alpha);
              LAPACKE_zlarnv_work(1, ISEED, 1, &beta );

#if !defined(TESTING_API_XKBLAS_WRAPPER)
              double t0 = xkblas_elapsedtime();
              xkblas_zgemm_async(trans[ta], trans[tb], M, N, K, &alpha, A, LDA, B, LDB, &beta, Cfinal, LDC);
              xkblas_memory_coherent_async(0, 0, M, N, Cfinal, LDC, sizeof(Complex64_t));
              xkblas_sync();
              double t1 = xkblas_elapsedtime();
              xkblas_memory_invalidate_caches();
#else
              /* test for F77 native call */
              extern void zgemm_(
                  const char * transa, const char * transb,
                  const int * m, const int * n, const int * k,
                  const Complex64_t* alpha, const Complex64_t* A, const int * lda,
                                  const Complex64_t * B, const int * ldb,
                  const Complex64_t* beta,  Complex64_t * C, const int * ldc);

              double t0 = time_get_elapsedtime();
              char transa = cblas2blas_op(trans[ta]);
              char transb = cblas2blas_op(trans[tb]);

              zgemm_(&transa, &transb, &M, &N, &K,
                     &alpha, A, &LDA,
                             B, &LDB,
                     &beta,  Cfinal, &LDC);
              double t1 = time_get_elapsedtime();
#endif

              fadds = (double)(FADDS_GEMM(M,N,K));
              fmuls = (double)(FMULS_GEMM(M,N,K));
              time[k] = t1-t0;
              flops[k] = 1e-9 * (fmuls * fp_per_mul + fadds * fp_per_add);

              /* Check the solution */
              if (getenv("NO_CHECK") ==0)
              {
                info_solution = check_solution(trans[ta], trans[tb], M, N, K,
                                           alpha, A, LDA, B, LDB, beta, Cinit, Cfinal, LDC);
                if (info_solution) suspicious = 1;
              } else 
                info_solution = 0;

               xkblas_free(A,LDAxK*sizeof(Complex64_t));
               xkblas_free(B,LDBxN*sizeof(Complex64_t));
               xkblas_free(C,LDCxN*sizeof(Complex64_t));
               xkblas_free(Cinit,LDCxN*sizeof(Complex64_t));
               xkblas_free(Cfinal,LDCxN*sizeof(Complex64_t));
            }

            printf("***************************************************\n");
            if (suspicious == 0) 
            {
                printf(" ---- TESTING ZGEMM (%s, %s) ............... PASSED !\n", transstr[ta], transstr[tb]);
                for (i=0; i<ITER; ++i)
                  printf("GFlosp=%f\n", flops[i]/time[i]);
            }
            else {
                printf(" - TESTING ZGEMM (%s, %s) ... FAILED !\n", transstr[ta], transstr[tb]);    hres++;
            }
            printf("***************************************************\n");
        }
    }
#ifdef _UNUSED_
    }}
#endif

    return hres;
}


/*--------------------------------------------------------------
 * Check the solution
 */
static int check_solution(
    int transA, int transB, int M, int N, int K,
    Complex64_t alpha, Complex64_t *A, int LDA,
    Complex64_t *B, int LDB,
    Complex64_t beta, Complex64_t *Cref, Complex64_t *Ccham, int LDC
)
{
    int info_solution;
    double Anorm, Bnorm, Cinitnorm, Cchamnorm, Clapacknorm, Rnorm, result;
    double eps;
    Complex64_t beta_const;

    CFloat64_t *work = (CFloat64_t *)malloc(max(K,max(M, N))* sizeof(CFloat64_t));
    int Am, An, Bm, Bn;

    beta_const  = -1.0;

    if (transA == CblasNoTrans) {
        Am = M; An = K;
    } else {
        Am = K; An = M;
    }
    if (transB == CblasNoTrans) {
        Bm = K; Bn = N;
    } else {
        Bm = N; Bn = K;
    }

    Anorm      = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', Am, An, A,     LDA, work);
    Bnorm      = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', Bm, Bn, B,     LDB, work);
    Cinitnorm  = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M,  N,  Cref,  LDC, work);
    Cchamnorm  = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M,  N,  Ccham, LDC, work);

    /* call to original BLAS version */
    xkblas_zgemm_native(transA, transB, M, N, K,
                &alpha, A, LDA,
                B, LDB, 
                &beta, Cref, LDC);

    Clapacknorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Cref, LDC, work);

    cblas_zaxpy(LDC * N, CBLAS_SADDR(beta_const), Ccham, 1, Cref, 1);

    Rnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Cref, LDC, work);

    eps = LAPACKE_dlamch_work('e');

    printf("Rnorm %e, Anorm %e, Bnorm %e, Cinitnorm %e, Cchamnorm %e, Clapacknorm %e\n",
            Rnorm, Anorm, Bnorm, Cinitnorm, Cchamnorm, Clapacknorm);

//TG Takes plasma-openmp test:
        // |R - R_ref|_p < gamma_{k+2} * |alpha| * |A|_p * |B|_p +
        //                 gamma_2 * |beta| * |C|_p
        // holds component-wise or with |.|_p as 1, inf, or Frobenius norm.
        // gamma_k = k*eps / (1 - k*eps), but we use
        // gamma_k = sqrt(k)*eps as a statistical average case.
        // Using 3*eps covers complex arithmetic.
        // See Higham, Accuracy and Stability of Numerical Algorithms, ch 2-3.

//    result = Rnorm / ((Anorm + Bnorm + Cinitnorm) * N * eps);
#if defined(PRECISION_z)
    double normalize = sqrt((double)K+2) * cabs(alpha) * Anorm * Bnorm + sqrt(2.0)*cabs(beta)*Cinitnorm;
#elif defined(PRECISION_c)
    double normalize = sqrt((double)K+2) * cabsf(alpha) * Anorm * Bnorm + sqrt(2.0)*cabsf(beta)*Cinitnorm;
#else
    double normalize = sqrt((double)K+2) * fabs(alpha) * Anorm * Bnorm + sqrt(2.0)*fabs(beta)*Cinitnorm;
#endif
    if (!isnan(normalize) && (normalize !=0))
       result = Rnorm / normalize;

    printf("============\n");
    printf("Checking the norm of the difference against reference ZGEMM \n");
    printf("-- gamma_(k+2) * |alpha| * |A|_p * |B|_p + gamma_2 * |beta| * |C|_p = %e < 3 * %e \n",
           result,eps);

    if (  isnan(Rnorm) || isinf(Rnorm) || isnan(result) || isinf(result) || (result > 3*eps) ) {
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
