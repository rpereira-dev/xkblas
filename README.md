# XKBlas v0.2

XKBlas is a drop in replacement of blas library for multi-GPUs servers similar
to CUBLASXt but with higher performances especially when matrix dimensions 
becomes smaller.

XKBlas was only developped and tested on Linux plateform with CUDA >= 8.0.

XKBlas is built from 2 components:
*  the multi-GPU module of the XKaapi[1,2] runtime that has low overhead in task management.
*  tile algorithms from PLASMA [3] or CHAMELEON [4] libraries.

This current version of XKBlas is the first public version and contains only BLAS level 3
algorithms, including XGEMMT:
- XGEMM
- XGEMMT: see [MKL GEMMT](https://software.intel.com/en-us/mkl-developer-reference-fortran-gemmt) interface
- XTRSM
- XTRMM
- XSYMM
- XSYRK
- XSYR2K
- XHEMM
- XHERK
- XHER2K

For classical precision Z, C, D, S.

# Source code
You can clone the projet or get the tarball [here](https://gitlab.inria.fr/xkblas/versions/-/blob/master/xkblas-v0.2-rc2-26-gee6c0576b02635c6.tgz).

# Installation
XKBlas needs: a CPU blas library (*e.g.* MKL or OpenBLAS); an a CUDA toolkit 
(we have test it with CUDA from version 8 to version 10).
Then edit the make.inc to specify, by profiding their installation paths the:
* the location and libraries for the CPU BLAS library
* the location and libraries for CUDA (>= 8.0)
* the installation path for XKBlas

The configuration file is ```make.inc``` in the root directory. You should 
have to update the following definition:
* Root for XKBlas:
  KAAPI_HOME= should point to the installation repository you get
* For CUDA:
  CUDA_HOME: points to the CUDA installation directory where ```include``` and ```lib``` subdirectories could be find.
* For MKL BLAS library.
  If MKL was selected, then we need to ensure that MKLROOT is correctly defined (see Intel documentation) and 
  comment part of ```make.inc``` that deals with OpenBLAS library.
  A typical entry for MKL is:
```
  EXTRA=-DKAAPI_BLAS_USE_MKL
  BLAS_CPPFLAGS=-I${MKLROOT}/include
  BLAS_LDFLAGS=-L${MKLROOT}/lib/intel64 -lmkl_intel_ilp64 -lmkl_sequential -lmkl_core -lpthread -lm -ldl
  BLAS_LIBDIR=${MKLROOT}/lib
  BLAS_LIB_SO=${MKLROOT}/lib/intel64/libmkl_intel_ilp64.so
```
* For OpenBLAS library.
  If OpenBlas is selected do not forget to comment lines concerning MKL.
  Final entries in the ```make.inc``` file should be similar to the following, if you relies on pkg-config tool 
  to get compilation flags for OpenBLAS:
```
  EXTRA=-DKAAPI_BLAS_USE_OPENBLAS
  BLAS_LIBDIR=$(shell pkg-config --libs-only-L openblas|cut -c3-)
  BLAS_CPPFLAGS=$(shell pkg-config --cflags openblas)
  BLAS_LDFLAGS=$(shell pkg-config --libs openblas)
  BLAS_LIB_SO=${BLAS_LIBDIR}/libopenblas.so
```

The `BLAS_LIB_SO` contains the BLAS library on which XKBlas (drop-in library, see below) transfers calls to CPU BLAS kernel.
Note that this library is loaded dynamically when a thread call such CPU subroutine.


Then enter
```
> make -j 
> make install PREFIX=<your installation repository>
```

# Testing the installation
XKBlas has testing programs ported from PLASMA/Chameleon.
To compile them and run all tests for all precisions, enter
```
> make -j testing
> ./testing/run_test
```
All BLAS routines, for each precision and all possible values of trans/side/uplo/diag parameters,
are tested against 3 matrix sizes (1024, 2048, 8192) using all the numbers of GPUs
the host have.
The Failure/Passed status are reported to stdout while all output traces are
added to a file called `log`.


# Compilation of XKBlas program
This step is optional if you want to use the drop-in replacement of the BLAS library,
in the same way NVBLAS traps redirect BLAS calls to equivalent CUBLASXT, then you only need to
run your program with preloading `libXKBlas_blaswrapper.so` (see below).

The installation generates 3 libraries in  `<prefix>/lib`:
* `libkaapi.so`: the low level XKaapi runtime
* `libXKBlas.so`: the XKBlas library which is linked against libkaapi.so
* `libXKBlas_blaswrapper.so`: the BLAS drop-in replacement library

The XKBlas API is defined in `<prefix>/include` repository and the user that want to 
compile XKBlas program with its API should includes `XKBlas.h`.

# Running with XKBlas
Before running any programs, you need to export `<prefix>/lib` in your LD_LIBRARY_PATH variable.

XKaapi manages the computation on GPUs. There is 2 main environment variables you can use to control execution of your program:
* `XKBLAS_NGPUS`, an integer: the number of GPUs to use for execution. By default it is the number of plugged GPU on the local server.
* `XKBLAS_GPUSET`, an integer representing the bit set of GPUs to use. The i-th bit is equal to 1 in the number iff the GPU `i` would be used.
for instance XKBLAS_GPUSET=5 (=0b101) selects GPU id 0 and GPU id 2.
* `XKBLAS_CACHE_LIMIT`, an integer (default 95) >0 and less or equal to 100 representing the percent of GPU memory that XKBlas may use for caching tiles.
* `XKBLAS_NSTREAMS`, an integer (default 4) that gives the number of CUDA streams for launching kernel to GPU
* `XKBLAS_NKERNELS`, an integer (default 2) that gives the maximum of CUDA kernels pending into each CUDA stream for launching kernel.


For instance, if you want to run with XKBlas as a drop in replacement BLAS library:
```
> LD_PRELOAD=<prefix>/lib/libXKBlas_blaswrapper.so XKBLAS_NGPUS=2 ./prog arg
```

# Any support ?
Please contact us or fill an [issue here](https://gitlab.inria.fr/xkblas/versions/-/issues/new).



# References
* [1] Thierry Gautier, João V. F. Lima: XKBlas: a High Performance Implementation of BLAS-3 Kernels on Multi-GPU Server. PDP 2020, Västerås, Sweden, March 11-13, 2020. IEEE 2020, 
* [2] João V. F. Lima, Thierry Gautier, Vincent Danjean, Bruno Raffin, Nicolas Maillard. Design and analysis of scheduling strategies for multi-CPU and multi-GPU architectures. Parallel Computing 44: 37-52 (2015)
* [3] Thierry Gautier, Joao Vicente Ferreira Lima, Nicolas Maillard, Bruno Raffin. XKaapi: A Runtime System for Data-Flow Task Programming on Heterogeneous Architectures. In Proc. of the 27-th IEEE International Parallel and Distributed Processing Symposium (IPDPS), Boston, USA, jun 2013.
* [4] João V. F. Lima, Thierry Gautier, Nicolas Maillard, Vincent Danjean. Exploiting Concurrent GPU Operations for Efficient Work Stealing on Multi-GPUs. 24rd International Symposium on Computer Architecture and High Performance Computing (SBAC-PAD), Columbia University, New York, USA, oct 2012.
* [5] http://icl.cs.utk.edu/plasma/software/
* [6] https://solverstack.gitlabpages.inria.fr/chameleon/doxygen/index.html

