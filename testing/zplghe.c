/**
 *
 * @file core_zplgsy.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon core_zplgsy CPU kernel
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Piotr Luszczek
 * @author Pierre Lemarinier
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @date 2010-11-15
 * @precisions normal z -> c d s
 *
 */

/*
 Rnd64seed is a global variable but it doesn't spoil thread safety. All matrix
 generating threads only read Rnd64seed. It is safe to set Rnd64seed before
 and after any calls to create_tile(). The only problem can be caused if
 Rnd64seed is changed during the matrix generation time.
 */
#include "xkblas.h"

//static unsigned long long int Rnd64seed = 100;
#define Rnd64_A 6364136223846793005ULL
#define Rnd64_C 1ULL
#define RndF_Mul 5.4210108624275222e-20f
#define RndD_Mul 5.4210108624275222e-20

#if (PRECISION_z==1) || (PRECISION_c==1)
#define NBELEM   2
#else
#define NBELEM   1
#endif

static unsigned long long int
Rnd64_jump(unsigned long long int n, unsigned long long int seed ) {
  unsigned long long int a_k, c_k, ran;
  int i;

  a_k = Rnd64_A;
  c_k = Rnd64_C;

  ran = seed;
  for (i = 0; n; n >>= 1, i++) {
    if (n & 1)
      ran = a_k * ran + c_k;
    c_k *= (a_k + 1);
    a_k *= a_k;
  }

  return ran;
}

//  testing_zplghe - Generate a tile for random hermitian (positive definite if bump is large enough) matrix.

void testing_zplghe(
  Complex64_t bump, size_t N, Complex64_t *A, int lda,
                  unsigned long long int seed )
{
    int64_t i, j;
    unsigned long long int ran;

    /*
     * LowerUpper part
     */
    for (j = 0; j < N; j++) {
        ran = Rnd64_jump( NBELEM * N, seed );

        for (i = j; i < N; i++) {
            A[j*lda+i] = 0.5f - ran * RndF_Mul;
            ran  = Rnd64_A * ran + Rnd64_C;
#if (PRECISION_z==1) || (PRECISION_c==1)
            A[j*lda+i] += I*(0.5f - ran * RndF_Mul);
            ran   = Rnd64_A * ran + Rnd64_C;
#endif
        }
    }

    for (j = 0; j < N; j++) {
#if (PRECISION_z==1) || (PRECISION_c==1)
        A[j+j*lda] += bump - I*cimag( A[j+j*lda] );
#else
        A[j+j*lda] += bump;
#endif

        for (i=0; i<j; i++) {
            A[lda*j+i] = conj( A[lda*i+j] );
        }
    }
}
