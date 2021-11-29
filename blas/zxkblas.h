/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
**
** This software is a computer program whose purpose is to execute
** blas subroutines on multi-GPUs system.
**
** This software is governed by the CeCILL-C license under French law and
** abiding by the rules of distribution of free software.  You can  use,
** modify and/ or redistribute the software under the terms of the CeCILL-C
** license as circulated by CEA, CNRS and INRIA at the following URL
** "http://www.cecill.info".

** As a counterpart to the access to the source code and  rights to copy,
** modify and redistribute granted by the license, users are provided only
** with a limited warranty  and the software's author,  the holder of the
** economic rights,  and the successive licensors  have only  limited
** liability.

** In this respect, the user's attention is drawn to the risks associated
** with loading,  using,  modifying and/or developing or reproducing the
** software by the user in light of its specific status of free software,
** that may mean  that it is complicated to manipulate,  and  that  also
** therefore means  that it is reserved for developers  and  experienced
** professionals having in-depth computer knowledge. Users are therefore
** encouraged to load and test the software's suitability as regards their
** requirements in conditions enabling the security of their systems and/or
** data to be ensured and,  more generally, to use and operate it in the
** same conditions as regards security.

** The fact that you are presently reading this means that you have had
** knowledge of the CeCILL-C license and that you accept its terms.
**/

/* Naming convention:
   * XX_async -> multiGPU asynchronous version
   * XX_native -> call to the native blas CPU variant
*/

extern int xkblas_zgemm_async(
  int transA, int transB, int M, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zgemm_native(
  int transA, int transB, int M, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zgemmt_async(
  int uplo, int transA, int transB, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zgemmt_native(
  int uplo, int transA, int transB, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_ztrsm_async( 
  int side, int uplo, int transA, int diag, int N, int NRHS,
  const Complex64_t* alpha, const Complex64_t* A, int LDA, Complex64_t* B, int LDB );

extern int xkblas_ztrsm_native(
  int side, int uplo, int transA, int diag, int N, int NRHS,
  const Complex64_t* alpha, const Complex64_t* A, int LDA, Complex64_t* B, int LDB );

extern int xkblas_ztrmm_async( 
  int side, int uplo, int transA, int diag, int N, int NRHS, 
  const Complex64_t* alpha, const Complex64_t *A, int LDA, Complex64_t *B, int LDB );

extern int xkblas_ztrmm_native(
  int side, int uplo, int transA, int diag, int N, int NRHS,
  const Complex64_t* alpha, const Complex64_t *A, int LDA, Complex64_t *B, int LDB );

extern int xkblas_zsymm_async(
  int side, int uplo, int M, int N,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zsymm_native(
  int side, int uplo, int M, int N,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zsyrk_async(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zsyrk_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zsyr2k_async( 
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zsyr2k_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zhemm_async(
  int side, int uplo, int M, int N,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zhemm_native(
  int side, int uplo, int M, int N,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const Complex64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zherk_async( 
  int uplo, int trans, int N, int K,
  const CFloat64_t* alpha, const Complex64_t *A, int LDA,
  const CFloat64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zherk_native(
  int uplo, int trans, int N, int K,
  const CFloat64_t* alpha, const Complex64_t *A, int LDA,
  const CFloat64_t* beta,  Complex64_t *C, int LDC );

extern int xkblas_zher2k_async( 
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const CFloat64_t* beta,   Complex64_t *C, int LDC);

extern int xkblas_zher2k_native(
  int uplo, int trans, int N, int K,
  const Complex64_t* alpha, const Complex64_t *A, int LDA,
                            const Complex64_t *B, int LDB,
  const CFloat64_t* beta,   Complex64_t *C, int LDC);

extern int xkblas_zswap_native(
  int M, int Ni, int Nj, int i, int j,
  Complex64_t *A, int LDA,
  Complex64_t *B, int LDB);
