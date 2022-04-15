XKBlas v0.4
===

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
You can clone the projet or get the tarball [here](https://gitlab.inria.fr/xkblas/versions/-/blob/master/xkblas-v0.4-XXX.tgz).

# Installation
XKBlas needs: a CPU blas library (*e.g.* MKL or OpenBLAS); a CUDA toolkit 
(we have test it with CUDA from version 8 to version 11) if NVidia GPUs are targeted (tested on P100,V100,A100); or the HIP environment 
if AMD GPUs are targeted.

The configuration is through the use of CMAKE.
On a build directory enter:
```bash
> cmake <path to the XKBlas root directory> [options]
```
where options are:
* Selection of the target GPUs with its toolkit (only one choice is simultaneously possible):
  * ```-DKAAPI_USE_CUDA_RT=ON```: 
  * ```-DKAAPI_USE_HIP=ON```:
* Selection of the Vendor BLAS library (only one choice is simultaneously possible): 
* ```-DKAAPI_USE_MKL=ON```: 
* ```-DKAAPI_USE_OPENBLAS=ON```: 

* Other options:
  * ```-DKAAPI_USE_TRACE=ON```: [disable by default] to enable the tracing capability of Kaapi to generation execution trace. 
  * ```-DKAAPI_USE_PERFCTR=ON```: [disable by default]  to enable support for performance counter (software and hardware with PAPI).
  * ```-DKAAPI_USE_SLEEP=ON```: [enable by default] to relinghish the processor on idle period rather than actively poll. 

* Common cmake options:
  * ```-DCMAKE_INSTALL_PREFIX=<installation path>```
  * ```-DCMAKE_BUILD_TYPE=Debug|Release```

For instance to configure with openblas and CUDA Runtime API:
```
> cmake -DKAAPI_USE_CUDA_RT=ON -DKAAPI_USE_OPENBLAS=ON -DCMAKE_INSTALL_PREFIX=$HOME/xkblas-install 
```

Then one could enter:
```
> make -j && make install
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

# Running with libXKBlas_blaswrapper.so

**Note: libXKBlas_blaswrapper.so is broken in this release**
If you want to run with XKBlas as a drop in replacement BLAS library:
```
> LD_PRELOAD=<prefix>/lib/libXKBlas_blaswrapper.so XKBLAS_NGPUS=2 ./prog arg
```

# Configuring with support for tracing program execution
To get access to tracing and visualization tools, you need to build XKBlas and XKaapi with support for tracing facilities.
Access to performance counter (software or hardware) requires explicit support for performance counter.

Here typical command line to configure both the tracing and performance counter supports in XKBlas/XKaapi.
Note that hardware performance counter requires [PAPI software](https://icl.utk.edu/papi/) to be installed. 
```
cmake -DKAAPI_USE_CUDA_RT=ON -DKAAPI_USE_OPENBLAS=ON -DCMAKE_INSTALL_PREFIX=/tmp/xkblas -DKAAPI_USE_TRACE=ON -DKAAPI_USE_PERFCTR=ON
``` 
Typicall output should contains followng lines:
```
-- Configuring Kaapi with tracing support
-- Configuring Kaapi with performance counter support
```
Moreover peformance counter may be enable even if PAPI package/software is not found.

Then enter make and make install.
With support for tracing facilities, XKaapi compiles ukilli that will be installed in ```<prefix>/bin/ukilli``. This is the tool to convert trace files to external format.

## Runing to capture trace of execution
The workflow is simple: 1/ run our program with specific environment variable needs to capure trace of execution. Trace files are stored by default in /tmp. 2/ convert the trace files to human readable version using ukilli tool compiled with XKBlas. There are different output: textual dump of the event of the trace; conversion to different csv files that can be processed on afterwards (XKBlas provides R script to dump Gantt); conversion to dot file to dump the graph. **dump to dot file is broken in this version**

Running with collection of events related to task execution and call to XKBlas routines: 
```
KAAPI_RECORD_TRACE=1 KAAPI_RECORD_MASK=COMPUTE,CALL LD_LIBRARY_PATH=/tmp/xkblas/lib:$LD_LIBRARY_PATH ./myprog myargs
``` 
By default the filenames of the trace are ```/tmp/events.$USER.0.X.evt``` where X is the internal thread identifier in XKaapi.
Environment variable ``KAAPI_RECORD_PREFIX``` could be used to replace the prefix ```/tmp/events.$USER.0```, for instance to store file locally in the homedir with prefix name ```exp_big_data8```:
```
KAAPI_RECORD_PREFIX=$HOME/exp_big_data8 KAAPI_RECORD_TRACE=1 KAAPI_RECORD_MASK=COMPUTE,CALL LD_LIBRARY_PATH=/tmp/xkblas/lib:$LD_LIBRARY_PATH ./myprog myargs
``` 

Because XKBlas is dedicated for multi-GPUs add the record mask of events ```OFFLOAD``` is important!

## Generating a Gantt
From your newly generated trace files in /tmp/event..., simply enter:
```
> ukilli -c /tmp/events.$USER.0.*.evt
...
*** File 'parallels.csv' generated
*** File 'task.csv' generated
*** File 'cpy.csv' generated
*** File 'kern.csv' generated
*** File 'call.csv' generated
```
The generated .csv files could be used by the R script in ```<prefix>/scripts/gantt.R``` to display the Gantt.
The contents of each files is:
* parallels.csv: the list of thread identifier, thread type (CPU,CUDA,INT) with the start/stop date of execution.
* task.csv: the start/stop of each task, with indication of the ressource where it was running.
* cpy.csv: all start/stop of events related to memory data transfer between CPU and GPU (size, direction, ...).
* kern.csv: all start/stop of events related to kernel execution on the GPU as viewed by the CPU.
* call.csv: all start/stop to trace call to BLAS routines. For each call, the parameter of the call is stored: first the matrix dimensions, then the bloc size, then all other usefull parameters (*e.g.* transA and transB for GEMM call).



# Any support ?
Please contact us or fill an [issue here](https://gitlab.inria.fr/xkblas/versions/-/issues/new).


# References
* [1] Thierry Gautier, João V. F. Lima: XKBlas: a High Performance Implementation of BLAS-3 Kernels on Multi-GPU Server. PDP 2020, Västerås, Sweden, March 11-13, 2020. IEEE 2020, 
* [2] João V. F. Lima, Thierry Gautier, Vincent Danjean, Bruno Raffin, Nicolas Maillard. Design and analysis of scheduling strategies for multi-CPU and multi-GPU architectures. Parallel Computing 44: 37-52 (2015)
* [3] Thierry Gautier, Joao Vicente Ferreira Lima, Nicolas Maillard, Bruno Raffin. XKaapi: A Runtime System for Data-Flow Task Programming on Heterogeneous Architectures. In Proc. of the 27-th IEEE International Parallel and Distributed Processing Symposium (IPDPS), Boston, USA, jun 2013.
* [4] João V. F. Lima, Thierry Gautier, Nicolas Maillard, Vincent Danjean. Exploiting Concurrent GPU Operations for Efficient Work Stealing on Multi-GPUs. 24rd International Symposium on Computer Architecture and High Performance Computing (SBAC-PAD), Columbia University, New York, USA, oct 2012.
* [5] http://icl.cs.utk.edu/plasma/software/
* [6] https://solverstack.gitlabpages.inria.fr/chameleon/doxygen/index.html

