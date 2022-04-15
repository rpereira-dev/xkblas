# Try to find ROCBLAS headers and libraries.
#
# Usage of this module as follows:
#
#     find_package(ROCBLAS)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  ROCBLAS_PREFIX          Set this variable to the root installation of
#                      libamdhip64.so if the module has problems finding the
#                      proper installation path.
#
# Variables defined by this module:
#
#  ROCBLAS_FOUND              System has ROCBLAS libraries and headers
#  ROCBLAS_LIBRARIES          The ROCBLAS library
#  ROCBLAS_INCLUDE_DIRS       The location of ROCBLAS headers

find_path(ROCBLAS_PREFIX_DIRS
    NAMES include/internal/rocblas-functions.h include/internal/rocblas-auxiliary.h
    HINTS $ENV{ROCBLAS_PATH} $ENV{ROCM_PATH}/rocblas
)

find_library(ROCBLAS_LIBRARIES
    # Pick the static library first for easier run-time linking.
    NAMES librocblas.so
    HINTS ${ROCBLAS_PREFIX_DIRS}/lib ${HILTIDEPS}/lib 
)
find_path(ROCBLAS_INCLUDE_DIRS
    NAMES internal/rocblas-functions.h internal/rocblas-auxiliary.h
    HINTS ${ROCBLAS_PREFIX_DIRS}/include ${HILTIDEPS}/include 
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ROCBLAS DEFAULT_MSG
    ROCBLAS_LIBRARIES
    ROCBLAS_INCLUDE_DIRS
)
mark_as_advanced(
    ROCBLAS_PREFIX_DIRS
    ROCBLAS_LIBRARIES
    ROCBLAS_INCLUDE_DIRS
)
