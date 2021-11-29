/**
 *
 * @file ztesting_auxiliary.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon CHAMELEON_Complex64_t auxiliary testings routines
 *
 * @version 1.0.0
 * @author Mathieu Faverge
 * @author Cédric Castagnède
 * @author Thierry Gautier: adaptation to xkblas
 * @date 2010-11-15
 * @precisions normal z -> c d s
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined( _WIN32 ) || defined( _WIN64 )
#include <windows.h>
#else  /* Non-Windows */
#include <unistd.h>
#include <sys/resource.h>
#endif

#if defined(__STDC_NO_COMPLEX__)
# error "Compiler support for complex number is required."
#else
# include <complex.h>
typedef float complex ct32_helper;
typedef double complex ct64_helper;
#endif

#if defined(KAAPI_BLAS_USE_OPENBLAS)
#  include <cblas.h>
#  include <lapacke.h>
#elif defined(KAAPI_BLAS_USE_MKL)
#  define lapack_complex_float ct32_helper
#  define lapack_complex_double ct64_helper
#  include <mkl.h>
#  include <mkl_types.h>
#  include <mkl_cblas.h>
#  include <mkl_lapacke.h>
#else
#  error "Blas library undefined"
#endif


#if defined(HAVE_CLOCK_GETTIME) || defined(KMP_OS_LINUX)
# include <time.h>
#else
# include <sys/time.h>
#endif
#if defined(HAVE_CLOCK_GETTIME) || defined(KMP_OS_LINUX)
typedef struct timespec struct_time;
#  define gettime(t) clock_gettime( CLOCK_REALTIME, t)
#  define get_sub_second(t) (1e-9*(double)t.tv_nsec)
#  define get_sub_second_ns(t) ((uint64_t)t.tv_nsec)
#else
typedef struct timeval struct_time;
#  define gettime(t) gettimeofday( t, 0)
#  define get_sub_second(t) (1e-6*(double)t.tv_usec)
#  define get_sub_second_ns(t) (1000*(uint64_t)t.tv_usec)
#endif

#include "ztesting_auxiliary.h"

#if !defined(TESTING_API_XKBLAS_WRAPPER)
#include "xkblas.h"
#endif

int   IONE  = 1;
int   PAD[2048] = {0,0,0,0,0,0,0}; /* pad */
lapack_int ISEED[5] = {0,0,0,1,1};   /* initial seed for zlarnv() */
int   PAD2[2048] = {0,0,0,0,0,0,0}; /* pad */

int     side[2]   = { CblasLeft,    CblasRight };
int     uplo[2]   = { CblasUpper,   CblasLower };
int     diag[2]   = { CblasNonUnit, CblasUnit  };
int    trans[3]  = { CblasNoTrans, CblasTrans, CblasConjTrans };

char *sidestr[2]  = { "Left ", "Right" };
char *uplostr[2]  = { "Upper", "Lower" };
char *diagstr[2]  = { "NonUnit", "Unit   " };
char *transstr[3] = { "N", "T", "H" };


double time_get_elapsedtime(void)
{
  struct_time st;
  int err = gettime(&st);
  if (err !=0) return 0;
  return (double)st.tv_sec + get_sub_second(st);
}


int main (int argc, char **argv)
{
    int ncores, ngpus, nb, ib;
    int info = 0;
    char func[32];

    /* Check for number of arguments*/
    if ( argc < 6 ) {
        printf(" Proper Usage is : ./ztesting ncores ngpus nb ib FUNC ...\n"
               "   - ncores : number of cores\n"
               "   - ngpus  : number of GPUs\n"
               "   - nb     : define the tile size\n"
               "   - ib     : define the inner tile size\n"
               "   - FUNC   : name of function to test\n"
               "              name could be GEMM TRMM TRSM SYMM SYRK SR2K"
#if (PRECISION_z==1) || (PRECISION_c==1)
               " HEMM HERK HER2K\n"
#else
               "\n"
#endif

               "   - ... plus arguments depending on the testing function \n");
        exit(1);
    }

    sscanf( argv[1], "%d",   &ncores );
    sscanf( argv[2], "%d",   &ngpus  );
    sscanf( argv[3], "%d",   &nb     );
    sscanf( argv[4], "%d",   &ib     );
    sscanf( argv[5], "%31s",  func   );

    argc -= 6;
    argv += 6;
    info  = 0;

#if !defined(TESTING_API_XKBLAS_WRAPPER)
    xkblas_set_param( nb, sizeof(Complex64_t) );

    /* Initialize Kaapi */
    xkblas_init();
#endif

    /*
     * Blas Level 3
     */
    if ( strcmp(func, "GEMM") == 0 ) {
        info += testing_zgemm( argc, argv );
    }
    else if ( strcmp(func, "TRMM") == 0 ) {
        info += testing_ztrmm( argc, argv );
    }
    else if ( strcmp(func, "TRSM") == 0 ) {
        info += testing_ztrsm( argc, argv );
    }
    else if ( strcmp(func, "SYMM") == 0 ) {
        info += testing_zsymm( argc, argv );
    }
    else if ( strcmp(func, "SYRK") == 0 ) {
        info += testing_zsyrk( argc, argv );
    }
    else if ( strcmp(func, "SYR2K") == 0 ) {
        info += testing_zsyr2k( argc, argv );
    }
#if (PRECISION_z==1) || (PRECISION_c==1)
    else if ( strcmp(func, "HEMM") == 0 ) {
        info += testing_zhemm( argc, argv );
    }
    else if ( strcmp(func, "HERK") == 0 ) {
        info += testing_zherk( argc, argv );
    }
    else if ( strcmp(func, "HER2K") == 0 ) {
        info += testing_zher2k( argc, argv );
    }
#endif
    else
      abort();

    if ( info == -1 ) {
        printf( "TESTING %s FAILED : incorrect number of arguments\n", func);
    } else if ( info == -2 ) {
        printf( "TESTING %s FAILED : not enough memory\n", func);
    }

#if !defined(TESTING_API_XKBLAS_WRAPPER)
    xkblas_finalize();
#endif

    return info;
}
