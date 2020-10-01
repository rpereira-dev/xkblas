/**
 *
 * @file zsyr2k.c
 *
 * @copyright 2009-2014 The University of Tennessee and The University of
 *                      Tennessee Research Foundation. All rights reserved.
 * @copyright 2012-2018 Bordeaux INP, CNRS (LaBRI UMR 5800), Inria,
 *                      Univ. Bordeaux. All rights reserved.
 *
 ***
 *
 * @brief Chameleon zsyr2k wrappers
 *
 * @version 1.0.0
 * @comment This file has been automatically generated
 *          from Plasma 2.5.0 for CHAMELEON 1.0.0
 * @author Mathieu Faverge
 * @author Emmanuel Agullo
 * @author Cedric Castagnede
 * @date 2010-11-15
 * @precisions normal z -> c d s
 *
 */
#include "common.h"
#include "task_z.h"
#include "task_z_internal.h"

/**
 ********************************************************************************
 *
 * @ingroup Complex64_t
 *
 *  CHAMELEON_zsyr2k - Performs one of the symmetric rank 2k operations
 *
 *    \f[ C = \alpha [ op( A ) \times conjg( op( B )' )] + \alpha [ op( B ) \times conjg( op( A )' )] + \beta C \f],
 *    or
 *    \f[ C = \alpha [ conjg( op( A )' ) \times op( B ) ] + \alpha [ conjg( op( B )' ) \times op( A ) ] + \beta C \f],
 *
 *  where op( X ) is one of
 *
 *    op( X ) = X  or op( X ) = conjg( X' )
 *
 *  where alpha and beta are real scalars, C is an n-by-n symmetric
 *  matrix and A and B are an n-by-k matrices the first case and k-by-n
 *  matrices in the second case.
 *
 *******************************************************************************
 *
 * @param[in] uplo
 *          = CblasUpper: Upper triangle of C is stored;
 *          = CblasLower: Lower triangle of C is stored.
 *
 * @param[in] trans
 *          Specifies whether the matrix A is transposed or conjugate transposed:
 *          = CblasNoTrans: \f[ C = \alpha [ op( A ) \times conjg( op( B )' )] + \alpha [ op( B ) \times conjg( op( A )' )] + \beta C \f]
 *          = CblasTrans: \f[ C = \alpha [ conjg( op( A )' ) \times op( B ) ] + \alpha [ conjg( op( B )' ) \times op( A ) ] + \beta C \f]
 *
 * @param[in] N
 *          N specifies the order of the matrix C. N must be at least zero.
 *
 * @param[in] K
 *          K specifies the number of columns of the A and B matrices with trans = CblasNoTrans.
 *          K specifies the number of rows of the A and B matrices with trans = CblasTrans.
 *
 * @param[in] alpha
 *          alpha specifies the scalar alpha.
 *
 * @param[in] A
 *          A is a LDA-by-ka matrix, where ka is K when trans = CblasNoTrans,
 *          and is N otherwise.
 *
 * @param[in] LDA
 *          The leading dimension of the array A. LDA must be at least
 *          max( 1, N ), otherwise LDA must be at least max( 1, K ).
 *
 * @param[in] B
 *          B is a LDB-by-kb matrix, where kb is K when trans = CblasNoTrans,
 *          and is N otherwise.
 *
 * @param[in] LDB
 *          The leading dimension of the array B. LDB must be at least
 *          max( 1, N ), otherwise LDB must be at least max( 1, K ).
 *
 * @param[in] beta
 *          beta specifies the scalar beta.
 *
 * @param[in,out] C
 *          C is a LDC-by-N matrix.
 *          On exit, the array uplo part of the matrix is overwritten
 *          by the uplo part of the updated matrix.
 *
 * @param[in] LDC
 *          The leading dimension of the array C. LDC >= max( 1, N ).
 *
 *******************************************************************************
 *
 * @retval 0 successful exit
 *
 *******************************************************************************
 *
 */
#define A(m, n) A##h,  m,  n
#define B(m, n) B##h,  m,  n
#define C(m, n) C##h,  m,  n

int xkblas_zsyr2k_async( int uplo, int trans, int N, int K,
                 const Complex64_t* alpha, const Complex64_t *A, int LDA,
                                           const Complex64_t *B, int LDB,
                 const Complex64_t* beta,  Complex64_t *C, int LDC 
)
{
    int Am, An;
    xkblas_matrix_descr_t descAl, descAt;
    xkblas_matrix_descr_t descBl, descBt;
    xkblas_matrix_descr_t descCl, descCt;

    /* Check input arguments */
    if ((uplo != CblasUpper) && (uplo != CblasLower)) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of uplo");
        return -1;
    }
    if ((trans != CblasNoTrans) && (trans != CblasTrans)) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of trans");
        return -2;
    }
    if ( trans == CblasNoTrans ) {
        Am = N; An = K;
    } else {
        Am = K; An = N;
    }
    if (N < 0) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of N");
        return -3;
    }
    if (K < 0) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of K");
        return -4;
    }
    if (LDA < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of LDA");
        return -7;
    }
    if (LDB < kaapi_max(1, Am)) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of LDB");
        return -9;
    }
    if (LDC < kaapi_max(1, N)) {
        kaapi_error("CHAMELEON_zsyr2k", "illegal value of LDC");
        return -12;
    }

    /* Quick return */
    if (N == 0 ||
        ((*alpha == (Complex64_t)0.0 || K == 0.0) && *beta == (Complex64_t)1.0))
        return 0;

    /* get default tile size and initialize internal descriptor if not yet */
    size_t NB = xkblas_auto_tilesize(KERN_SYR2K,N,K,Am);

    xkblas_matrix_descr_t* Ah = xkblas_find(A);
    xkblas_matrix_descr_t* Bh = xkblas_find(B);
    xkblas_matrix_descr_t* Ch = xkblas_find(C);
    if (!xkblas_matrix_descr_isinit(Ah))
    {
      xkblas_init_matrix_handle(Ah, (void*)A, Am, An, LDA, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ah->ld == LDA) && (Ah->M == Am) && (Ah->N == An) );
    }
    if (!xkblas_matrix_descr_isinit(Bh))
    {
      xkblas_init_matrix_handle(Bh, (void*)B, Am, An, LDB, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Bh->ld == LDB) && (Bh->M == Am) && (Bh->N == An) );
    }
    if (!xkblas_matrix_descr_isinit(Ch))
    {
      xkblas_init_matrix_handle(Ch, (void*)C, N, N, LDC, sizeof(Complex64_t), NB, NB);
      kaapi_assert_debug( (Ch->ld == LDC) && (Ch->M == N) && (Ch->N == N) );
    }

    /* */
    Am = Ah->M;
    An = Ah->N;
    size_t Cm = Ch->M;
    size_t Cn = Ch->N;

    size_t Amb = Ah->mb;
    size_t Anb = Ah->nb;
    size_t Amt = Ah->mt;
    size_t Ant = Ah->nt;

    size_t Cmb = Ch->mb;
    size_t Cnb = Ch->nb;
    size_t Cmt = Ch->mt;
    size_t Cnt = Ch->nt;

    size_t m, n, k;
    size_t mmin,mmax;
    size_t ldak, ldam, ldan, ldcm, ldcn;
    size_t ldbk, ldbm, ldbn;
    size_t tempnn, tempmm, tempkn, tempkm;

    Complex64_t zone   = (Complex64_t)1.0;
    Complex64_t zbeta;

#if defined(KAAPI_DEBUG)
  {
    kaapi_assert( 0 == xkblas_dbg_setname( "A", Ah ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "B", Bh ) );
    kaapi_assert( 0 == xkblas_dbg_setname( "C", Ch ) );
  }
#endif

    xkblas_auto_map( KERN_SYR2K, Ch );

#if 0
int loadgpu[20];
for (int i=0; i<20; ++i)
  loadgpu[i] = 0;

int load[Cmt][Cnt];
for (int i=0; i<Cmt; ++i)
  for (int j=0; j<Cnt; ++j)
    load[i][j] = 0;
#endif

    for (n = 0; n < Cnt; n++) {
        tempnn = n == Cnt-1 ? Cn-n*Cnb : Cnb;

        if (uplo == CblasLower) {
            mmin = n+1;
            mmax = Cmt;
        }
        else {
            mmin = 0;
            mmax = n;
        }

        ldan = LDA;//BLKLDD(A, n);
        ldbn = LDB;//BLKLDD(B, n);
        ldcn = LDC;//BLKLDD(C, n);
        /*
         *  CblasNoTrans
         */
        if (trans == CblasNoTrans) {
            for (k = 0; k < Ant; k++) {
                tempkn = k == Ant-1 ? An-k*Anb : Anb;
                zbeta = k == 0 ? *beta : zone;
                INSERT_TASK_zsyr2k(
                    uplo, trans,
                    tempnn, tempkn, 
                    *alpha, A(n, k), ldan, /* ldan * K */
                           B(n, k), ldbn,
                    zbeta, C(n, n), ldcn); /* ldc  * N */
#if 0
    loadgpu[xkblas_get_ld(C(n,n))] += 1;
    load[n][n] +=1;
#endif
            }
            for (m = mmin; m < mmax; m++) {
                tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                ldam = LDA;//BLKLDD(A, m);
                ldbm = LDB;//BLKLDD(B, m);
                ldcm = LDC;//BLKLDD(C, m);
                for (k = 0; k < Ant; k++) {
                    tempkn = k == Ant-1 ? An-k*Anb : Anb;
                    zbeta = k == 0 ? *beta : zone;
                    INSERT_TASK_zgemm(
                            CblasNoTrans, CblasTrans,
                            tempmm, tempnn, tempkn, 
                            *alpha, A(m, k), ldam,  /* ldam * K */
                                    B(n, k), ldbn,  /* ldan * K */
                            zbeta,  C(m, n), ldcm); /* ldc  * N */
#if 0
    loadgpu[xkblas_get_ld(C(m,n))] += 1;
    load[m][n] +=1;
#endif

                    INSERT_TASK_zgemm(
                            CblasNoTrans, CblasTrans,
                            tempmm, tempnn, tempkn, 
                            *alpha, B(m, k), ldbm,  /* ldam * K */
                                   A(n, k), ldan,  /* ldan * K */
                            zone,  C(m, n), ldcm); /* ldc  * N */
#if 0
    loadgpu[xkblas_get_ld(C(m,n))] += 1;
    load[m][n] +=1;
#endif
                }
            }
        }
        /*
         *  Cham[Conj]Trans
         */
        else {
            for (k = 0; k < Amt; k++) {
                tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                ldak = LDA;//BLKLDD(A, k);
                ldbk = LDB;//BLKLDD(B, k);
                zbeta = k == 0 ? *beta : zone;
                INSERT_TASK_zsyr2k(
                    uplo, trans,
                    tempnn, tempkm, 
                    *alpha, A(k, n), ldak,  /* lda * N */
                           B(k, n), ldbk,
                    zbeta, C(n, n), ldcn); /* ldc * N */
            }
            for (m = mmin; m < mmax; m++) {
                tempmm = m == Cmt-1 ? Cm-m*Cmb : Cmb;
                ldcm = LDC;//BLKLDD(C, m);
                for (k = 0; k < Amt; k++) {
                    tempkm = k == Amt-1 ? Am-k*Amb : Amb;
                    ldak = LDA;//BLKLDD(A, k);
                    ldbk = LDB;//BLKLDD(B, k);
                    zbeta = k == 0 ? *beta : zone;
                    INSERT_TASK_zgemm(
                        CblasTrans, CblasNoTrans,
                        tempmm, tempnn, tempkm, 
                        *alpha, A(k, m), ldak,  /* lda * M */
                               B(k, n), ldbk,  /* lda * N */
                        zbeta, C(m, n), ldcm); /* ldc * N */

                    INSERT_TASK_zgemm(
                        CblasTrans, CblasNoTrans,
                        tempmm, tempnn, tempkm, 
                        *alpha, B(k, m), ldbk,  /* lda * M */
                               A(k, n), ldak,  /* lda * N */
                        zone,  C(m, n), ldcm); /* ldc * N */
                }
            }
        }
    }

#if 0
for (int i=0; i<20; ++i)
  printf("Load[%o] = %i\n",i,loadgpu[i]);
printf("load:\n");
for (int i=0; i<Cmt; ++i)
{
  for (int j=0; j<Cnt; ++j)
    printf(" %02i",load[i][j]);
  printf("\n");
}
printf("Mapping:\n");
for (int i=0; i<Cmt; ++i)
{
  for (int j=0; j<Cnt; ++j)
    if (load[i][j] >0)
      printf(" %4i",xkblas_get_ld(C(i,j))-1);
    else printf("     ");
  printf("\n");
}
#endif
}

/* syr2k */
static void (*dl_zsyr2k)(
  const char* uplo, const char* transa,
  const KBLAS_INT* n, const KBLAS_INT* k,
  const Complex64_t *alpha, const Complex64_t *A, const KBLAS_INT* lda,
                            const Complex64_t *B, const KBLAS_INT* ldb,
  const Complex64_t *beta,  Complex64_t *C, const KBLAS_INT* ldc) = 0;

/* CPU driver */
extern void xkblas_zsyr2k_native_(
  const char * uplo, const char * transa,
  const KBLAS_INT *n, const KBLAS_INT *k,
  const Complex64_t *alpha, const Complex64_t *A, const KBLAS_INT* lda,
                            const Complex64_t *B, const KBLAS_INT* ldb,
  const Complex64_t *beta,  Complex64_t *C, const KBLAS_INT* ldc)
{
  if (dl_zsyr2k ==0) xkblas_load_sym((void**)&dl_zsyr2k,SYMBLAS_NAME(zsyr2k));
  dl_zsyr2k( uplo, transa,
             n, k,
             alpha, A, lda,
                    B, ldb,
             beta,  C, ldc
  );
}

extern int xkblas_zsyr2k_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC )
{
  char u = cblas2blas_fill(uplo);
  char trA = cblas2blas_op(trans);
  const KBLAS_INT iN = N;
  const KBLAS_INT iK = K;
  const KBLAS_INT iLDA = LDA;
  const KBLAS_INT iLDB = LDB;
  const KBLAS_INT iLDC = LDC;
  xkblas_zsyr2k_native_( &u, &trA, &iN, &iK, alpha, A, &iLDA, B, &iLDB, beta, C, &iLDC );
  return 0;
}
