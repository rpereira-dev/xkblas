
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

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${BLAS_INCLUDE_DIRS} ${CUBLAS_INCLUDE_DIRS} ${HIP_INCLUDE_DIRS} ${HIPBLAS_INCLUDE_DIRS})

# Bootstrap 

# internal_register.h
# Current implementation generates explicit call to register function for each known task.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/internal_register.h
    COMMAND ${SHELL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/gen_internal_register.sh gemm gemmt trsm trmm syrk syr2k symm herk her2k hemm
    DEPENDS ${CMAKE_SOURCE_DIR}/gen_internal_register.sh ${CMAKE_CURRENT_SOURCE_DIR}/task_format.h
)

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

list(APPEND SOURCES_D
  dgemm.c dgemmt.c dtrsm.c dtrmm.c dsymm.c dsyrk.c dsyr2k.c dxkblas.h dxkblas_f77.h 
  dtask_gemm.c dtask_symm.c dtask_gemmt.c dtask_trsm.c dtask_trmm.c dtask_syrk.c dtask_syr2k.c dtask.h 
)

list(APPEND SOURCES_C
  cgemm.c cgemmt.c ctrsm.c ctrmm.c csymm.c csyrk.c csyr2k.c chemm.c cherk.c cher2k.c cxkblas.h cxkblas_f77.h 
  ctask_gemm.c ctask_symm.c ctask_gemmt.c ctask_trsm.c ctask_trmm.c ctask_syrk.c ctask_syr2k.c ctask_hemm.c ctask_herk.c ctask_her2k.c ctask.h 
)

list(APPEND SOURCES_Z
  zgemm.c zgemmt.c ztrsm.c ztrmm.c zsymm.c zsyrk.c zsyr2k.c zhemm.c zherk.c zher2k.c zswap.c zxkblas.h 
  ztask_gemm.c ztask_symm.c ztask_gemmt.c ztask_trsm.c ztask_trmm.c ztask_syrk.c ztask_syr2k.c ztask_hemm.c ztask_herk.c ztask_her2k.c ztask_swap.c ztask.h zxkblas_f77.h 
)

list(APPEND GENERATED_SOURCES ${SOURCES_S} ${SOURCES_D} ${SOURCES_C})


# Generation of precision code
foreach(file IN LISTS GENERATED_SOURCES)
  string(REGEX REPLACE "^[sdc](.*)$" "z\\1" source "${file}")
  string(REGEX REPLACE "^([sdc]).*$" "\\1" precision "${file}")
  add_custom_command(
      OUTPUT  ${file}
      COMMAND ${CMAKE_SOURCE_DIR}/gen_precision_one.sh ${precision} ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${CMAKE_CURRENT_BINARY_DIR}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${CMAKE_SOURCE_DIR}/gen_precision_one.sh 
  )
  #message(STATUS "Gen file ${file} == precision ${precision} from ${source}")
endforeach()


# Append special file Xtask_internal.h
list(APPEND GENERATED_SOURCES 
    ztask_internal.h 
    ctask_internal.h
    dtask_internal.h
    stask_internal.h
)

#Append source directory to source file
foreach(file IN LISTS SOURCES_Z)
  list(APPEND ASOURCES_Z "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
endforeach()
set(SOURCES_Z ${ASOURCES_Z})
#message(STATUS "Generated Source Z:" ${SOURCES_Z})

#Append directory to generated file
foreach(file IN LISTS GENERATED_SOURCES)
  #message(STATUS "Change paths to ${file}")
  list(APPEND AGENERATED_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${file}")
endforeach()

set(GENERATED_SOURCES ${AGENERATED_SOURCES})
#message(STATUS "Generated Source:" ${GENERATED_SOURCES})

#--- XKBLAS
if (${HIP_FOUND})
  message(STATUS "Compilation with HIBLAS in : ${HIPBLAS_INCLUDE_DIR}")
  list(APPEND HIPIFY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/xkblas.c ${CMAKE_CURRENT_SOURCE_DIR}/dbg_blas.c 
    ${SOURCES_Z}
    ${GENERATED_SOURCES}
  )
  set(HEADER_FILE ${HIPIFY_FILE})
  list(FILTER HIPIFY_FILE INCLUDE REGEX "(.)*.c$") 
  list(FILTER HEADER_FILE EXCLUDE REGEX "(.)*.c$") 
#  message(FATAL_ERROR "HEADER_FILE: ${HEADER_FILE}")
  foreach(file IN LISTS HIPIFY_FILE)
     string(REPLACE ".c" ".hip.c" file_hip ${file})
     get_filename_component(file_hip ${file_hip} NAME )
     string(REGEX REPLACE "^([sdc]).*$" "\\1" precision "${file}")
     add_custom_command (
       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file_hip}
       COMMAND ${SHELL_EXECUTABLE} hipify-perl ${file}|sed -e 's+hipComplex+hipblasComplex+g' -e 's+hipDoubleComplex+hipblasDoubleComplex+g' -e 's+cublasChemm+hipblasChemm+g' -e 's+cublasCher2k+hipblasCher2k+g' -e 's+cublasCherk+hipblasCherk+g' -e 's+cublasCsymm+hipblasCsymm+g' -e 's+cublasCsyr2k+hipblasCsyr2k+g' -e 's+cublasCsyrk+hipblasCsyrk+g' -e 's+cublasCtrmm+hipblasCtrmm+g' -e 's+cublasCtrsm+hipblasCtrsm+g' -e 's+cublasDsymm+hipblasDsymm+g' -e 's+cublasDsyr2k+hipblasDsyr2k+g' -e 's+cublasDsyrk+hipblasDsyrk+g' -e 's+cublasDtrmm+hipblasDtrmm+g' -e 's+cublasSsymm+hipblasSsymm+g' -e 's+cublasSsyr2k+hipblasSsyr2k+g' -e 's+cublasSsyrk+hipblasSsyrk+g' -e 's+cublasStrmm+hipblasStrmm+g' -e 's+cublasZhemm+hipblasZhemm+g' -e 's+cublasZher2k+hipblasZher2k+g' -e 's+cublasZherk+hipblasZherk+g' -e 's+cublasZsymm+hipblasZsymm+g' -e 's+cublasZsyr2k+hipblasZsyr2k+g' -e 's+cublasZsyrk+hipblasZsyrk+g' -e 's+cublasZtrmm+hipblasZtrmm+g' -e 's+cublasZtrsm+hipblasZtrsm+g' > ${file_hip}
       DEPENDS ${file} 
       DEPENDS zxkblas.h cxkblas.h dxkblas.h sxkblas.h ctask.h dtask.h stask.h
     )
     #message(STATUS "Hipyfied: ${file} -> ${CMAKE_CURRENT_BINARY_DIR}/${file_hip}")
     list(APPEND HIPYFIED_FILE ${CMAKE_CURRENT_BINARY_DIR}/${file_hip})
  endforeach()
  add_library(xkblas SHARED internal_register.h common.h xkblas.h ztask_internal.h ctask_internal.h dtask_internal.h stask_internal.h ${HEADER_FILE} ${HIPYFIED_FILE}) 
else()
  add_library(xkblas SHARED internal_register.h common.h xkblas.h xkblas.c dbg_blas.c 
    ${SOURCES_Z}
    ${GENERATED_SOURCES}
  ) 
endif()


# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH already
# (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set_target_properties(xkblas PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE) 

target_link_libraries(xkblas PUBLIC kaapi)
target_link_libraries(xkblas PRIVATE ${CUBLAS_LIBRARIES} ${HIPBLAS_LIBRARIES} ${ROCBLAS_LIBRARIES})
#set_target_properties(xkblas PROPERTIES LINKER_LANGUAGE C)

install(TARGETS xkblas DESTINATION lib)
install(FILES xkblas.h ${CMAKE_CURRENT_BINARY_DIR}/sxkblas.h ${CMAKE_CURRENT_BINARY_DIR}/dxkblas.h ${CMAKE_CURRENT_BINARY_DIR}/cxkblas.h zxkblas.h DESTINATION include)

