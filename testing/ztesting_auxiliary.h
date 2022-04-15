/**
 *
 * @file testing_zauxiliary.h
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon CHAMELEON_Complex64_t auxiliary testings header
 *
 * @version 1.0.0
 * @author Mathieu Faverge
 * @author Cédric Castagnède
 * @date 2010-11-15
 * @precisions normal z -> c d s
 *
 */
#ifndef _testing_zauxiliary_h_
#define _testing_zauxiliary_h_

#include "common.h"
#if defined(KAAPI_BLAS_USE_OPENBLAS)||defined(KAAPI_BLAS_USE_CRAYBLAS)
#  include <cblas.h>
#  include <lapacke.h>
#elif defined(KAAPI_BLAS_USE_MKL)
#  undef lapack_complex_float
#  undef lapack_complex_double
#  define lapack_complex_float Complex32_t
#  define lapack_complex_double Complex64_t
#  include <mkl.h>
#  include <mkl_types.h>
#  include <mkl_cblas.h>
#  include <mkl_lapacke.h>
#else
#  error "Blas library undefined"
#endif


#define USAGE(name, args, details)                                      \
    printf(" Proper Usage is : ./testing_z ncores ngpus nb ib " name " " args " with\n" \
           "   - ncores : number of cores\n"                            \
           "   - ngpus  : number of GPUs\n"                             \
           "   - nb     : define the tile size\n"                       \
           "   - ib     : define the inner tile size\n"                 \
           "   - FUNC   : name of function to test\n"                   \
           details);

#ifdef WIN32
#include <float.h>
#define isnan _isnan
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

extern int IONE;
extern lapack_int ISEED[5];

extern int    trans[3];
extern int    uplo[2];
extern int    side[2];
extern int    diag[2];

extern char *transstr[3];
extern char *uplostr[2];
extern char *sidestr[2];
extern char *diagstr[2];

extern double time_get_elapsedtime(void);

int testing_zgemm(int argc, char **argv);
int testing_ztrsm(int argc, char **argv);
int testing_ztrmm(int argc, char **argv);
int testing_zsymm(int argc, char **argv);
int testing_zsyrk(int argc, char **argv);
int testing_zsyr2k(int argc, char **argv);
#if defined(PRECISION_z) || defined(PRECISION_c)
int testing_zhemm(int argc, char **argv);
int testing_zherk(int argc, char **argv);
int testing_zher2k(int argc, char **argv);
#endif

extern void testing_zplgsy(
  Complex64_t bump, size_t N, Complex64_t *A, int lda,
                  unsigned long long int seed );

#if (PRECISION_z==1) || (PRECISION_c==1)
extern void testing_zplghe(
  Complex64_t bump, size_t N, Complex64_t *A, int lda,
                  unsigned long long int seed );
#endif

#endif /* _testing_zauxiliary_h_ */
