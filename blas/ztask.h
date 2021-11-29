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

#ifndef _kblas_task_z
#define _kblas_task_z

void INSERT_TASK_zgemm(
    int transA, int transB,
    size_t m, size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *C, size_t Cm, size_t Cn, size_t ldc
);
void register_format_zgemm(void);

void INSERT_TASK_zgemmt(
    int uplo, int transA, int transB,
    size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *C, size_t Cm, size_t Cn, size_t ldc
);
void register_format_zgemmt(void);

void INSERT_TASK_ztrsm(
    int side, int uplo, int transA, int diag,
    size_t m, size_t n, 
    Complex64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
    xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb
);
void register_format_ztrsm(void);

void INSERT_TASK_ztrmm(
    int side, int uplo, int transA, int diag,
    size_t m, size_t n,
    Complex64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
    xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb
);
void register_format_ztrmm(void);


void INSERT_TASK_zsyrk(
    int uplo, int trans,
    size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
    Complex64_t beta,  xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb
);
void register_format_zsyrk(void);

void INSERT_TASK_zsyr2k(
    int uplo, int trans,
    size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *Ch, size_t Cm, size_t Cn, size_t ldc
);
void register_format_zsyr2k(void);

void INSERT_TASK_zsymm(
    int side, int uplo,
    size_t m, size_t n,
    Complex64_t alpha, xkblas_matrix_descr_t *A, int Am, int An, int lda,
                       xkblas_matrix_descr_t *B, int Bm, int Bn, int ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *C, int Cm, int Cn, int ldc
);
void register_format_zsymm(void);

void INSERT_TASK_zherk(
    int uplo, int trans,
    size_t n, size_t k,
    CFloat64_t alpha, xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t lda,
    CFloat64_t beta,  xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldb
);
void register_format_zherk(void);

void INSERT_TASK_zher2k(
    int uplo, int trans,
    size_t n, size_t k,
    Complex64_t alpha, xkblas_matrix_descr_t *Ah, size_t Am, size_t An, size_t lda,
                       xkblas_matrix_descr_t *Bh, size_t Bm, size_t Bn, size_t ldb,
    CFloat64_t beta,   xkblas_matrix_descr_t *Ch, size_t Cm, size_t Cn, size_t ldc
);
void register_format_zher2k(void);

void INSERT_TASK_zhemm(
    int side, int uplo,
    int m, int n,
    Complex64_t alpha, xkblas_matrix_descr_t *A, int Am, int An, int lda,
                       xkblas_matrix_descr_t *B, int Bm, int Bn, int ldb,
    Complex64_t beta,  xkblas_matrix_descr_t *C, int Cm, int Cn, int ldc
);
void register_format_zhemm(void);

void INSERT_TASK_zswap(
    size_t m, size_t ni, size_t nj, size_t i, size_t j,
    xkblas_matrix_descr_t *A, size_t Am, size_t An, size_t ldA,
    xkblas_matrix_descr_t *B, size_t Bm, size_t Bn, size_t ldB
);
void register_format_zswap(void);

#endif

