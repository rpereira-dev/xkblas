
if (KAAPI_USE_CUDA_DRIVER OR KAAPI_USE_CUDA_RT)
  add_custom_command (
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/common.h
    COMMAND ${SHELL_EXECUTABLE} cp ${CMAKE_SOURCE_DIR}/blas/common.gen ${CMAKE_CURRENT_BINARY_DIR}/common.h
    DEPENDS ${CMAKE_SOURCE_DIR}/blas/common.gen
  )
else()
  add_custom_command (
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/common.h
    COMMAND ${SHELL_EXECUTABLE} hipify-perl ${CMAKE_SOURCE_DIR}/blas/common.gen > ${CMAKE_CURRENT_BINARY_DIR}/common.h
    DEPENDS ${CMAKE_SOURCE_DIR}/blas/common.gen
  )
endif()

# Bootstrap 
add_custom_command(
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/ztask_internal.h
  COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh z ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
)
add_custom_command(
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/ctask_internal.h
  COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh c ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
)
add_custom_command(
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/dtask_internal.h
  COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh d ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
)
add_custom_command(
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/stask_internal.h
  COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh s ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ztask_internal.h ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
)


list(APPEND SOURCES_S
  sgemm.c sgemmt.c strsm.c strmm.c ssymm.c ssyrk.c ssyr2k.c sxkblas.h sxkblas_f77.h
  stask_gemm.c stask_symm.c stask_gemmt.c stask_trsm.c stask_trmm.c stask_syrk.c stask_syr2k.c stask.h 
)
#set_source_files_properties(${SOURCE_S} PROPERTIES COMPILE_OPTIONS "-UPRECISION_d;-DPRECISION_s;-UPRECISION_z;-UPRECISION_c")

list(APPEND SOURCES_D
  dgemm.c dgemmt.c dtrsm.c dtrmm.c dsymm.c dsyrk.c dsyr2k.c dxkblas.h dxkblas_f77.h 
  dtask_gemm.c dtask_symm.c dtask_gemmt.c dtask_trsm.c dtask_trmm.c dtask_syrk.c dtask_syr2k.c dtask.h 
)
#set_source_files_properties(${SOURCE_D} PROPERTIES COMPILE_OPTIONS "-DPRECISION_d;-UPRECISION_s;-UPRECISION_z;-UPRECISION_c")

list(APPEND SOURCES_C
  cgemm.c cgemmt.c ctrsm.c ctrmm.c csymm.c csyrk.c csyr2k.c chemm.c cherk.c cher2k.c cxkblas.h cxkblas_f77.h 
  ctask_gemm.c ctask_symm.c ctask_gemmt.c ctask_trsm.c ctask_trmm.c ctask_syrk.c ctask_syr2k.c ctask_hemm.c ctask_herk.c ctask_her2k.c ctask.h 
)
#set_source_files_properties(${SOURCE_C} PROPERTIES COMPILE_OPTIONS "-UPRECISION_d;-UPRECISION_s;-UPRECISION_z;-DPRECISION_c")

list(APPEND SOURCES_Z
  zgemm.c zgemmt.c ztrsm.c ztrmm.c zsymm.c zsyrk.c zsyr2k.c zhemm.c zherk.c zher2k.c zswap.c zxkblas.h 
  ztask_gemm.c ztask_symm.c ztask_gemmt.c ztask_trsm.c ztask_trmm.c ztask_syrk.c ztask_syr2k.c ztask_hemm.c ztask_herk.c ztask_her2k.c ztask_swap.c ztask.h zxkblas_f77.h 
)
#set_source_files_properties(${SOURCE_Z} PROPERTIES COMPILE_OPTIONS "-UPRECISION_d;-UPRECISION_s;-DPRECISION_z;-UPRECISION_c")

list(APPEND GENERATED_SOURCES ${SOURCES_S} ${SOURCES_D} ${SOURCES_C})

# Append special file Xtask_internal.h
list(APPEND SOURCES_Z ${CMAKE_CURRENT_BINARY_DIR}/ztask_internal.h)
list(APPEND SOURCES_C ${CMAKE_CURRENT_BINARY_DIR}/ctask_internal.h)
list(APPEND SOURCES_D ${CMAKE_CURRENT_BINARY_DIR}/dtask_internal.h)
list(APPEND SOURCES_S ${CMAKE_CURRENT_BINARY_DIR}/stask_internal.h)

# internal_register.h
# Current implementation generates explicit call to register function for each known task.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/internal_register.h
    COMMAND ${SHELL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/gen_internal_register.sh gemm gemmt trsm trmm syrk syr2k symm herk her2k hemm
    DEPENDS ${CMAKE_SOURCE_DIR}/gen_internal_register.sh ${CMAKE_CURRENT_SOURCE_DIR}/task_format.h
)

#foreach(f IN LISTS GENERATED_SOURCES)
#string(STRIP "${GENERATED_SOURCES}" stripppedinput)

foreach(file IN LISTS GENERATED_SOURCES)
  string(REGEX REPLACE "^[sdc](.*)$" "z\\1" source "${file}")
  string(REGEX REPLACE "^([sdc]).*$" "\\1" precision "${file}")
  add_custom_command(
      OUTPUT  ${file}
      COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh ${precision} ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${CMAKE_CURRENT_BINARY_DIR}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
  )
  #message(STATUS "Gen precision ${precision} from ${source}")
endforeach()

#
if (${BLAS_FOUND})
  message(STATUS "Blas library, found: libraies: ${BLAS_LIBRARIES}")
endif()

#--- XKBLAS
add_library(xkblas SHARED internal_register.h common.h xkblas.h xkblas.c dbg_blas.c 
  ${SOURCES_Z}
  ${SOURCES_C}
  ${SOURCES_D}
  ${SOURCES_S}
) 

target_link_libraries(xkblas kaapi ${BLAS_LIBRARIES} ${CUBLAS_LIBRARIES})
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CUBLAS_INCLUDE_DIRS})

install(TARGETS xkblas DESTINATION lib)
install(FILES xkblas.h ${CMAKE_CURRENT_BINARY_DIR}/sxkblas.h ${CMAKE_CURRENT_BINARY_DIR}/dxkblas.h ${CMAKE_CURRENT_BINARY_DIR}/cxkblas.h zxkblas.h DESTINATION include)

