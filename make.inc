
# Very is the location of the source of KAAPI
# If you compile into the source directory, the setting is correct
# Else you should indicate the path to the source of KAAPI
KAAPI_HOME=$(PWD)

#Select the target GPU: CUDA/Nvidia card XOR HIP/Amd card
#If to use the CUDA Driver API:
#KAAPI_USE_GPU_CUDA=1

##If to use the CUDA Runtime API:
KAAPI_USE_GPU_CUDA=2

##If to use AMD GPU through HIP (exclusive with previous options)
#KAAPI_USE_GPU_HIP=1

#Enable(1)/Disable(0) support tracing execution
KAAPI_USE_TRACELIB=0

#Enable(1)/Disable(0) support for performance counter
KAAPI_USE_PERFCOUNTER=1

# To set: the location of CUDA. Default is to use environment variable.
#CUDA_HOME=/usr/local/cuda-9.0/

# To set: the location of HIP, HIPBLAS
HIP_HOME=${HIP_PATH}
HIPBLAS_HOME=/grid5000/spack/opt/spack/linux-debian10-x86_64/gcc-8.3.0/hipblas-4.2.0-bosa57rktklrdotbcw3xdjpzymxvnhgz
ROCBLAS_HOME=/grid5000/spack/opt/spack/linux-debian10-x86_64/gcc-8.3.0/rocblas-4.2.0-pa2rkgfzxrfdegrofv4p7pnt2ebwbzgq/

# To use hwloc: uncomment following line with correct location
HWLOC_HOME=/usr

# For MKL
#BLAS_USE=-DKAAPI_BLAS_USE_MKL
#BLAS_LIBDIR=${MKLROOT}/lib
#BLAS_LIB_SO=${MKLROOT}/lib/intel64/libmkl_intel_ilp64.so
#BLAS_CPPFLAGS=-DMKL_ILP64 -m64 -I${MKLROOT}/include
#BLAS_LDFLAGS=-L${MKLROOT}/lib/intel64 -lmkl_intel_ilp64 -lmkl_sequential -lmkl_core -lpthread -lm -ldl

# For OpenBLAS:
BLAS_USE=-DKAAPI_BLAS_USE_OPENBLAS
BLAS_LIBDIR=$(shell pkg-config --libs-only-L openblas|cut -c3-)
BLAS_LIB_SO=${BLAS_LIBDIR}/libopenblas.so
BLAS_CPPFLAGS=$(shell pkg-config --cflags openblas)
BLAS_LDFLAGS=$(shell pkg-config --libs openblas)

# C Compiler:
CC=gcc

# Archive + ranlib for static libraries:
AR=ar
RANLIB=ranlib


# Extra flags for XKBlas:
# - KAAPI_USE_SLEEP=1 to activate the policy to make active threads sleeping more
# Set to KAAPI_USE_SLEEP=0 if you want to unactivate the policy
KAAPI_OPTIONFLAGS=-DKAAPI_USE_SLEEP=1

# FLAGS use for compiling the library
#OPT=-O3 -std=gnu99 -DKAAPI_DEBUG=0 ${EXTRA} -DNDEBUG ${BLAS_USE} ${KAAPI_OPTIONFLAGS}
OPT=-O3 -std=gnu99 -DKAAPI_DEBUG=0 ${EXTRA} -DNDEBUG ${BLAS_USE} ${KAAPI_OPTIONFLAGS}
#OPT=-O0 -g -std=gnu99 -DKAAPI_DEBUG=1 ${EXTRA} ${BLAS_USE} ${KAAPI_OPTIONFLAGS}

