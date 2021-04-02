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

extern void xkblas_zgemm_native_(
    const char * transa, const char * transb,
    const int * m, const int * n, const int * k,
    const Complex64_t* alpha, const Complex64_t* A, const int * lda,
                              const Complex64_t * B, const int * ldb,
    const Complex64_t* beta,  Complex64_t * C, const int * ldc);

extern void xkblas_zgemmt_native_(
    const char* uplo, const char * transa, const char * transb,
    const int * n, const int * k,
    const Complex64_t* alpha, const Complex64_t* A, const int * lda,
                              const Complex64_t * B, const int * ldb,
    const Complex64_t* beta,  Complex64_t * C, const int * ldc);

extern void xkblas_ztrsm_native_(
    const char * side, const char *uplo, const char* transa, const char* diag,
    const int* m, const int* n,
    const Complex64_t* alpha, const Complex64_t* A, const int * lda,
                              Complex64_t* B, const int * ldb );

extern void xkblas_ztrmm_native_(
  const char * side, const char *uplo, const char *transa, const char * diag,
  const int *m, const int * n,
  const Complex64_t* alpha, const Complex64_t *A, const int *lda,
                            Complex64_t *B, const int *ldb);

extern void xkblas_zsymm_native_(
  const char * side, const char * uplo,
  const int * m, const int * n,
  const Complex64_t* alpha, const Complex64_t* A, const int *lda,
                            const Complex64_t* B, const int *ldb,
  const Complex64_t* beta,  Complex64_t* C, const int *ldc);

extern void xkblas_zsyrk_native_(
  const char * uplo, const char * transa,
  const int *n, const int *k,
  const Complex64_t *alpha, const Complex64_t *A, const int* lda,
  const Complex64_t *beta,  Complex64_t *C, const int* ldc);

extern void xkblas_zsyr2k_native_(
  const char * uplo, const char * transa,
  const int *n, const int *k,
  const Complex64_t *alpha, const Complex64_t *A, const int* lda,
                            const Complex64_t *B, const int* ldb,
  const Complex64_t *beta,  Complex64_t *C, const int* ldc);

extern void xkblas_zhemm_native_(
  const char * side, const char * uplo,
  const int * m, const int * n,
  const Complex64_t* alpha, const Complex64_t* A, const int *lda,
                            const Complex64_t* B, const int *ldb,
  const Complex64_t* beta,  Complex64_t* C, const int *ldc);

extern void xkblas_zherk_native_(
  const char * uplo, const char * transa,
  const int *n, const int *k,
  const CFloat64_t *alpha, const Complex64_t *A, const int* lda,
  const CFloat64_t *beta,  Complex64_t *C, const int* ldc);

extern void xkblas_zher2k_native_(
  const char * uplo, const char * transa,
  const int *n, const int *k,
  const Complex64_t *alpha, const Complex64_t *A, const int* lda,
                            const Complex64_t *B, const int* ldb,
  const CFloat64_t *beta,   Complex64_t *C, const int* ldc);

extern int xkblas_zswap_native_(
  int* M, int* Ni, int* Nj, int* i, int* j,
  const Complex64_t *A, int* LDA,
  const Complex64_t *B, int* LDB);

