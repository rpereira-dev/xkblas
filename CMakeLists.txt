cmake_minimum_required (VERSION 3.7)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

set(CMAKE_CXX_FLAGS_DEBUG_INIT "-DKAAPI_DEBUG=1")
set(CMAKE_CC_FLAGS_DEBUG_INIT "-DKAAPI_DEBUG=1")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DKAAPI_DEBUG=1")


project (XKblas)


# Global options
option(KAAPI_USE_CUDA_RT "To set if Kaapi should be compiled with support for NVIDIA GPU using Runtime API." OFF)
option(KAAPI_USE_HIP "To set if Kaapi should be compiled with support for AMD GPU using HIP API." OFF)

option(KAAPI_USE_TRACE "Use tracing facility for post mortem analysis (gantt, dot graph, ...)" OFF)
option(KAAPI_USE_PERFCTR "Add support for performance counter in the trace of execution. If PAPI is found then hardware performance counters are also available." OFF)

option(KAAPI_USE_MKL "Use MKL" OFF)
option(KAAPI_USE_OPENBLAS "Use OpenBLAS" OFF)
option(KAAPI_USE_CRAYBLAS "Use Cray LIBSCI" OFF)

option(KAAPI_USE_SLEEP "Sleep thread is nothing to do" ON)
option(KAAPI_BUILD_TESTING "Compile /testing files" OFF)

#--
if (KAAPI_USE_HIP AND KAAPI_USE_CUDA_RT)
  message(FATAL_ERROR "Current implementation of Kaapi does not support HIP pluging and CUDA plugin compiled together.")
endif()

#--- CUDA
find_package(CUDA_RT)
if (KAAPI_USE_CUDA_RT)
  if (NOT ${CUDA_RT_FOUND})
    message(FATAL_ERROR "Kaapi is configured with CUDA Runtime API but library could not be found.")
  endif()
endif()
if (KAAPI_USE_CUDA_RT)
  find_package(CUBLAS)
  if (NOT ${CUBLAS_FOUND})
    message(FATAL_ERROR "Kaapi is configured with CUDA API but CUBLAS is not found for XKblas !")
  endif()
#  find_path(BLAS_INCLUDE_DIRS cblas.h
#    /usr/include
#    /usr/local/include
#    $ENV{BLAS_HOME}/include)
endif()

#Hip
if (KAAPI_USE_HIP)
  # Cannot use find_package HIP that imply unknown package on neowise (or I do not read all the documentation)
  #
  find_package(HIP)
  if (NOT ${HIP_FOUND})
    message(FATAL_ERROR "Kaapi is configured with HIP, but hip installation could not be found!")
  endif()

  find_package(ROCBLAS)
  if (NOT ${ROCBLAS_FOUND})
    message(FATAL_ERROR "Kaapi is configured with HIP, but rocblas could not be found!")
  endif()
  find_package(HIPBLAS)
  if (NOT ${HIPBLAS_FOUND})
    message(FATAL_ERROR "Kaapi is configured with HIP, but hipblas could not be found!")
  endif()

  message(STATUS "Found HIP_INCLUDE_DIRS: ${HIP_INCLUDE_DIRS}")
  message(STATUS "Found HIP_LIBRARIES: ${HIP_LIBRARIES}")
  message(STATUS "Found HIPBLAS_INCLUDE_DIRS: ${HIPBLAS_INCLUDE_DIRS}")
  message(STATUS "Found HIPBLAS_LIBRARIES: ${HIPBLAS_LIBRARIES}")
  message(STATUS "Found ROCBLAS_INCLUDE_DIRS: ${ROCBLAS_INCLUDE_DIRS}")
  message(STATUS "Found ROCBLAS_LIBRARIES: ${ROCBLAS_LIBRARIES}")
else()
  set(HIP_FOUND FALSE)
endif()

#--- PAPI
if (KAAPI_USE_TRACE AND KAAPI_USE_PERFCTR)
  find_package(PAPI)
endif()

#--- HWLOC
find_package(HWLOC)
if (${HIP_FOUND} AND ${HWLOC_FOUND})
  find_path(ROCSMI_INCLUDE_DIRS
    NAMES rocm_smi/rocm_smi.h
    HINTS ${ROCSMI_PREFIX}/include 
  )
  find_library(ROCSMI_LIBRARIES
    # Pick the static library first for easier run-time linking.
    NAMES librocm_smi64.so 
    HINTS ${ROCSMI_PREFIX}/lib 
  )

endif()

#--- BLAS
if (KAAPI_USE_MKL)
  set(BLA_VENDOR Intel10_64lp_seq)
endif()
if (KAAPI_USE_CRAYBLAS)
  set(BLA_VENDOR Generic)
endif()
if (KAAPI_USE_OPENBLAS)
  set(BLA_VENDOR OpenBLAS)
endif()
if (NOT KAAPI_USE_MKL AND NOT KAAPI_USE_OPENBLAS AND NOT KAAPI_USE_CRAYBLAS)
  message(FATAL_ERROR "XKblas requires you specify the BLAS Vendor library.")
endif()

find_package(BLAS REQUIRED)
if (NOT ${BLAS_FOUND})
  message(FATAL_ERROR "XKblas requires a BLAS library.")
endif()
if (KAAPI_USE_MKL)
  find_path(BLAS_INCLUDE_DIRS mkl_lapacke.h
    $ENV{MKLROOT}/include
  )
endif()
if (KAAPI_USE_CRAYBLAS)
  find_path(BLAS_INCLUDE_DIRS lapacke.h
    ${CRAY_LIBSCI_PREFIX_DIR}/include 
  )
endif()

add_compile_options(${BLAS_LINKER_FLAGS})

#--- Add common command line options for all subdirectories
if (KAAPI_USE_CUDA_RT)
  add_compile_options("-DKAAPI_USE_CUDA=1")
  add_compile_options("-DKAAPI_USE_CUDA_RUNTIME_API=1")
  message(STATUS "Configuring Kaapi with CUDA runtime plugin")
endif()
if (KAAPI_USE_HIP)
  add_compile_options("-DKAAPI_USE_HIP=1")
  add_compile_options("-D__HIP_PLATFORM_AMD__=1")
  message(STATUS "Configuring Kaapi with HIP plugin")
endif()

if (KAAPI_USE_TRACE)
  add_compile_options("-DKAAPI_USE_TRACELIB=1")
  message(STATUS "Configuring Kaapi with tracing support")
else()
  add_compile_options("-DKAAPI_USE_TRACELIB=0")
endif()

if (KAAPI_USE_PERFCTR)
  add_compile_options("-DKAAPI_USE_PERFCOUNTER=1")
  message(STATUS "Configuring Kaapi with performance counter support")
else()
   add_compile_options("-DKAAPI_USE_PERFCOUNTER=0")
endif()

if (KAAPI_USE_TRACE AND KAAPI_USE_PERFCTR)
  if (${PAPI_FOUND})
    add_compile_options("-DKAAPI_USE_PAPI=1")
  else()
    add_compile_options("-DKAAPI_USE_PAPI=0")
  endif()
endif()

if (KAAPI_USE_SLEEP)
  add_compile_options("-DDKAAPI_USE_SLEEP=1")
else()
  add_compile_options("-DDKAAPI_USE_SLEEP=0")
endif()

if (${HWLOC_FOUND})
  add_compile_options("-DKAAPI_USE_HWLOC=1")
else()
  add_compile_options("-DKAAPI_USE_HWLOC=0")
endif()

if (KAAPI_USE_MKL)
  add_compile_options("-DKAAPI_BLAS_USE_MKL=1")
  add_compile_options("-DMKL_ILP64")
  add_compile_options("-m64")
  add_compile_options("-DXKBLAS_BLASLIB=\"libmkl_intel_ilp64.so\"")
endif()
if (KAAPI_USE_CRAYBLAS)
  add_compile_options("-DKAAPI_BLAS_USE_CRAYBLAS=1")
  add_compile_options("-DXKBLAS_BLASLIB=\"libsci_cray.so\"")
endif()
if (KAAPI_USE_OPENBLAS)
  add_compile_options("-DKAAPI_BLAS_USE_OPENBLAS=1")
  add_compile_options("-DXKBLAS_BLASLIB=\"libopenblas.so\"")
endif()
find_package(BLAS REQUIRED)
if (NOT ${BLAS_FOUND})
  message(FATAL_ERROR "XKblas requires a BLAS library.")
endif()

#--- Subdirectories
include_directories ("kaapi")

add_subdirectory(kaapi)
add_subdirectory(bin)
add_subdirectory(blas)
if (KAAPI_BUILD_TESTING)
  add_subdirectory(testing)
endif()

