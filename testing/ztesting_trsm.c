/**
 *
 * @file testing_ztrsm.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon ztrsm testing
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
#include "ztask_internal.h"
#include "ztesting_auxiliary.h"

#include "flops.h"

static int check_solution(int side, int uplo, int trans, int diag,
                          int M, int N, Complex64_t alpha,
                          Complex64_t *A, int LDA,
                          Complex64_t *Bref, Complex64_t *Bcham, int LDB);

int testing_ztrsm(int argc, char **argv)
{
    int hres = 0;
    /* Check for number of arguments*/
    if ( argc != 5 ) {
        USAGE("TRSM", "alpha M N LDA LDB",
              "   - alpha  : alpha coefficient\n"
              "   - M      : number of rows of matrices B\n"
              "   - N      : number of columns of matrices B\n"
              "   - LDA    : leading dimension of matrix A\n"
              "   - LDB    : leading dimension of matrix B\n");
        return -1;
    }

    Complex64_t alpha = (Complex64_t) atof(argv[0]);
    int M     = atoi(argv[1]);
    int N     = atoi(argv[2]);
    int LDA   = atoi(argv[3]);
    int LDB   = atoi(argv[4]);

    double eps;
    int info_solution;
    int s, u, t, d, i, k;
    double flops, fmuls, fadds, fp_per_mul, fp_per_add;

    int LDAxM = LDA*max(M,N);
    int LDBxN = LDB*max(M,N);

    Complex64_t *A      = (Complex64_t *)xkblas_malloc(LDAxM*sizeof(Complex64_t));
    Complex64_t *B      = (Complex64_t *)xkblas_malloc(LDBxN*sizeof(Complex64_t));
    Complex64_t *Binit  = (Complex64_t *)xkblas_malloc(LDBxN*sizeof(Complex64_t));
    Complex64_t *Bfinal = (Complex64_t *)xkblas_malloc(LDBxN*sizeof(Complex64_t));

    /* Check if unable to allocate memory */
    if ( (!A) || (!B) || (!Binit) || (!Bfinal) )
    {
        xkblas_free(A,LDAxM*sizeof(Complex64_t)); xkblas_free(B,LDBxN*sizeof(Complex64_t));
        xkblas_free(Binit,LDBxN*sizeof(Complex64_t)); xkblas_free(Bfinal,LDBxN*sizeof(Complex64_t));
        printf("Out of Memory \n ");
        return -2;
    }

    eps = LAPACKE_dlamch_work('e');

    printf("\n");
#if defined(TESTING_API_XKBLAS_WRAPPER)
    printf("------ TESTS FOR XKBLAS WRAPPER API ZTRSM ROUTINE -------  \n");
#else
    printf("------ TESTS FOR XKBLAS ZTRSM ROUTINE -------  \n");
#endif

    printf("            Size of the Matrix B : %d by %d\n", M, N);
    printf("\n");
    printf(" The matrix A is randomly generated for each test.\n");
    printf("============\n");
    printf(" The relative machine precision (eps) is to be %e \n",eps);
    printf(" Computational tests pass if scaled residuals are less than 10.\n");

    /*----------------------------------------------------------
     *  TESTING ZTRSM
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
#if (PRECISION_z==1) || (PRECISION_c==1)
            for (t=0; t<3; t++) {
#else
            for (t=0; t<2; t++) {
#endif
                for (d=0; d<2; d++) {

#define ITER 5
                   double time[ITER];
                   double flops[ITER];

                   int suspicious = 0;
                   for (k=0; k<ITER; ++k)
                   {
                      /* Initialize A, B, C */
                      LAPACKE_zlarnv_work(IONE, ISEED, LDAxM, A);
                      CFloat64_t invmax = 1.0/(CFloat64_t)max(M,N);
                      for(i=0;i<LDAxM;i++)
                       A[i] *= invmax;
                      for(i=0;i<max(M,N);i++)
                        A[LDA*i+i] = 1.0;
                      LAPACKE_zlarnv_work(IONE, ISEED, LDBxN, B);

                      memcpy(Binit,  B, LDBxN*sizeof(Complex64_t));
                      memcpy(Bfinal, B, LDBxN*sizeof(Complex64_t));

                      /* XKBLAS ZTRSM */
                      LAPACKE_zlarnv_work(1, ISEED, 1, &alpha);

#if !defined(TESTING_API_XKBLAS_WRAPPER)
                      double t0 = xkblas_elapsedtime();
                      xkblas_ztrsm_async(side[s], uplo[u], trans[t], diag[d], M, N, &alpha, A, LDA, Bfinal, LDB);
                      xkblas_memory_coherent_async(0, 0, M, N, Bfinal, LDB, sizeof(Complex64_t));
                      xkblas_sync();
                      double t1 = xkblas_elapsedtime();
                      xkblas_memory_invalidate_caches();
#else
                      /* test for F77 native call */
                      extern void ztrsm_(
                          const char * side, const char *uplo, const char* transa, const char* diag,
                          const int* m, const int* n,
                          const Complex64_t* alpha, const Complex64_t* A, const int * lda,
                                                          Complex64_t* B, const int * ldb );

                      double t0 = time_get_elapsedtime();
                      char sd = cblas2blas_side( side[s] );
                      char up = cblas2blas_fill( uplo[u] );
                      char tr = cblas2blas_op( trans[t] );
                      char dg = cblas2blas_diag( diag[d] );
                      ztrsm_(&sd, &up, &tr, &dg, &M, &N, &alpha, A, &LDA, Bfinal, &LDB);
                      double t1 = time_get_elapsedtime();
#endif

                      fadds = (double)(FADDS_TRSM( side[s], M, N ));
                      fmuls = (double)(FMULS_TRSM( side[s], M, N ));
                      time[k] = t1-t0;
                      flops[k] = 1e-9 * (fmuls * fp_per_mul + fadds * fp_per_add);

                      /* Check the solution */
                      if (getenv("NO_CHECK") ==0)
                      {
                        info_solution = check_solution(side[s], uplo[u], trans[t], diag[d],
                                                 M, N, alpha, A, LDA, Binit, Bfinal, LDB);
                        if (info_solution) suspicious = 1;
                      } else
                        info_solution = 0;
                    }

                    printf("***************************************************\n");
                    if (suspicious == 0) {
                      printf(" ---- TESTING ZTRSM (%s, %s, %s, %s) ...... PASSED !\n",
                             sidestr[s], uplostr[u], transstr[t], diagstr[d]);
                      for (i=0; i<ITER; ++i)
                        printf("GFlosp=%f\n", flops[i]/time[i]);
                    }
                    else {
                      printf(" ---- TESTING ZTRSM (%s, %s, %s, %s) ... FAILED !\n",
                             sidestr[s], uplostr[u], transstr[t], diagstr[d]);    hres++;
                    }
                    printf("***************************************************\n");
                  }
            }
        }
    }

    xkblas_free(A,LDAxM*sizeof(Complex64_t)); 
    xkblas_free(B,LDBxN*sizeof(Complex64_t));
    xkblas_free(Binit,LDBxN*sizeof(Complex64_t)); 
    xkblas_free(Bfinal,LDBxN*sizeof(Complex64_t));

    return hres;
}

/*--------------------------------------------------------------
 * Check the solution
 */
static int check_solution(int side, int uplo, int trans, int diag,
                          int M, int N, Complex64_t alpha,
                          Complex64_t *A, int LDA,
                          Complex64_t *Bref, Complex64_t *Bcham, int LDB)
{
    int info_solution;
    double Anorm, Binitnorm, Bchamnorm, Blapacknorm, Rnorm, result;
    double eps;
    Complex64_t mzone = (Complex64_t)-1.0;

    CFloat64_t *work = (CFloat64_t *)malloc(max(M, N)* sizeof(CFloat64_t));
    int Am, An;

    if (side == CblasLeft) {
        Am = M; An = M;
    } else {
        Am = N; An = N;
    }

    Anorm       = LAPACKE_zlantr_work(LAPACK_COL_MAJOR, 'I', uplo, diag, Am, An, A, LDA, work);
    Binitnorm   = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Bref,  LDB, work);
    Bchamnorm   = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Bcham, LDB, work);

    xkblas_ztrsm_native(side, uplo, trans, diag, M, N, &alpha, A, LDA, Bref, LDB);

    Blapacknorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Bref, LDB, work);

    cblas_zaxpy(LDB * N, CBLAS_SADDR(mzone), Bcham, 1, Bref, 1);

    Rnorm = LAPACKE_zlange_work(LAPACK_COL_MAJOR, 'I', M, N, Bref, LDB, work);

    eps = LAPACKE_dlamch_work('e');

    printf("Rnorm %e, Anorm %e, Binitnorm %e, Bchamnorm %e, Blapacknorm %e\n",
           Rnorm, Anorm, Binitnorm, Bchamnorm, Blapacknorm);

    result = Rnorm / ((Anorm + Blapacknorm) * max(M,N) * eps);

    printf("============\n");
    printf("Checking the norm of the difference against reference ZTRSM \n");
    printf("-- ||Ccham - Clapack||_oo/((||A||_oo+||B||_oo).N.eps) = %e \n", result);

    if ( isinf(Blapacknorm) || isinf(Bchamnorm) || isnan(result) || isinf(result) || (result > 10.0) ) {
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
