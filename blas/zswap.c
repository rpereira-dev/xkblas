/**
 *
 * @file zswap.c
 *
 * @copyright 2021 - INRIA
 *
 * @brief Column entry point for swapping 2 columns
 *
 * @author Thierry Gautier
 * @precisions normal z -> s d c
 */
#include "common.h"
#include "task_z.h"
#include "task_z_internal.h"
#include <math.h>


/**
 ********************************************************************************
 *
 * @ingroup Complex64_t
 *
 *  zswap - Performs columns swap
 *
 *******************************************************************************
 *
 * @param[in] M
 *          M specifies the number of rows of the matrix A.
 *
 * @param[in] N
 *          N specifies the number of columns of the matrix A.
 *
 * @param[in] i,j
 *          i specifies the index of columns of the matrix  A to swap.
 * *
 * @param[in,out] A
 *          C is a LDC-by-N matrix.
 *          On exit, the array is overwritten by the M by N matrix ( alpha*op( A )*op( B ) + beta*C )
 *
 * @param[in] LDC
 *          The leading dimension of the array C. LDC >= max(1,M).
 *
 *******************************************************************************
 *
 * @return
 *          \retval 0 successful exit
 *
 *******************************************************************************
 *
 */

#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n
#define C(m, n) C##h,  m,  n

int xkblas_zswap_async(
    int M, int N, int i, int j,
    Complex64_t *C, int LDC )
{
    if (M < 0) {
        kaapi_error("zswap",  "illegal value of M");
        return -3;
    }
    if (N < 0) {
        kaapi_error("zswap", "illegal value of N");
        return -4;
    }
    if ((i < 0) || (i>N)) {
        kaapi_error("zswap", "illegal value of i");
        return -5;
    }
    if ((j < 0) || (j>N)) {
        kaapi_error("zswap", "illegal value of j");
        return -8;
    }
    if (LDC < kaapi_max(1, M)) {
        kaapi_error("zswap", "illegal value of LDC");
        return -13;
    }

    /* Quick return */
    if (M == 0 || N == 0 || (i==j))
        return 0;

    size_t Cmb = 0;
    size_t Cnb = 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = 1024; /* xkblas_auto_tilesize(KERN_GEMM,M,N,K); */

    /* get the internal matrix descriptor for C */
    xkblas_matrix_descr_t* Ch = xkblas_find(C);

    /* may be not yet registered to xkblas, do it */
    if (!xkblas_matrix_descr_isinit(Ch))
    {
      xkblas_init_matrix_handle(Ch, C, M, N, LDC, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ch->ld == LDC) && (Ch->M == M) && (Ch->N == N) );
    }

    Cmb = Ch->mb;
    Cnb = Ch->nb;
    size_t Cmt = Ch->mt;
    size_t Cnt = Ch->nt;

    size_t m, n, k;
    size_t ldam, ldak, ldbn, ldbk, ldcm;
    size_t tempmm, tempi, tempj;

    Complex64_t zs;

    /* map matrix blocs onto the ressource : do nothing if matrix blocs are already mapped */
    xkblas_auto_map( KERN_GEMM, Ch );

    /* get the bloc's indexes holding i and j */
    int ib = i / Cnb;
    int jb = j / Cnb;
    /* N-dim bloc size at column position ib and jb */
    tempi = ib == Cnt-1 ? N-ib*Cnb : Cnb;
    tempj = jb == Cnt-1 ? N-jb*Cnb : Cnb;
    ldcm = LDC; //BLKLDD(C, m);

    for (m = 0; m < Cmt; m++)
    {
      /* M-dim bloc size at row position m */
      tempmm = m == Cmt-1 ? M-m*Cmb : Cmb;
      INSERT_TASK_zswap(
          tempmm, tempi, tempj, i % Cnb, j %Cnb,
          C(m, ib), ldcm, 
          C(m,jb), ldcm
      );
    }
    return 0;
}


/* gemm native pointer */
static void (*dl_zswap)(
    KBLAS_INT* N, Complex64_t *X, KBLAS_INT* incX, Complex64_t *Y, KBLAS_INT* incY 
) = 0;


/* CPU driver */
extern void xkblas_zswap_native_(
    KBLAS_INT *M, KBLAS_INT *Ni, KBLAS_INT *Nj, KBLAS_INT *i, KBLAS_INT *j,
    Complex64_t *A, KBLAS_INT *LDA,
    Complex64_t *B, KBLAS_INT *LDB 
)
{
  if (dl_zswap ==0) xkblas_load_sym((void**)&dl_zswap,SYMBLAS_NAME(zswap));
  KBLAS_INT incOne=1;
  dl_zswap( M, A + *i * *LDA, &incOne, B + *j * *LDB, &incOne);
}

extern int xkblas_zswap_native(
    int M, int Ni, int Nj, 
    int i, int j,
    Complex64_t *A, int LDA,
    Complex64_t *B, int LDB 
)
{
  KBLAS_INT iM = M;
  KBLAS_INT iNi = Ni;
  KBLAS_INT iNj = Nj;
  KBLAS_INT iI = i;
  KBLAS_INT iJ = j;
  KBLAS_INT iLDA = LDA;
  KBLAS_INT iLDB = LDB;
  xkblas_zswap_native_( &iM, &iNi, &iNj, &iI, &iJ, A, &iLDA, B, &iLDB );
  return 0;
}
