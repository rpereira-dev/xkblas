add_custom_command (
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/git_hash.h
  COMMAND ${SHELL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/config/git_hash.sh ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/git_hash.h
  )

add_custom_command (
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hw_count.h
  COMMAND ${SHELL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/config/hw_count.sh ${CMAKE_CURRENT_BINARY_DIR}/hw_count.h
  )

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (${HWLOC_FOUND})
  add_compile_options("-DKAAPI_USE_HWLOC=1")
endif()

if (${PAPI_FOUND})
  add_compile_options("-DKAAPI_USE_PAPI=1")
else()
  add_compile_options("-DKAAPI_USE_PAPI=0")
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})



if (KAAPI_USE_CUDA_DRIVER)
  list(APPEND ADDITIONAL_SOURCES kaapi_plugin_cuda.c)
endif()
if (KAAPI_USE_CUDA_RT)
  list(APPEND ADDITIONAL_SOURCES kaapi_plugin_cuda.c)
endif()
if (KAAPI_USE_HIP)
  list(APPEND ADDITIONAL_SOURCES kaapi_plugin_hip.c)
endif()

if (KAAPI_USE_TRACE)
  list(APPEND ADDITIONAL_SOURCES kaapi_trace.h kaapi_perfctr.h kaapi_trace_lib.c kaapi_trace_parser.c kaapi_trace_recorder.c)
else()
endif()

#if (KAAPI_USE_PERFCTR)
#else()
#endif()
#
#if (KAAPI_USE_TRACE AND KAAPI_USE_PERFCTR)
#  if (${PAPI_FOUND})
#  else()
#  endif()
#endif()
#
#if (KAAPI_USE_SLEEP)
#else()
#endif()
#

# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)
 
# when building, use the install RPATH already
# (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_library(kaapi SHARED  ${CMAKE_CURRENT_BINARY_DIR}/git_hash.h kaapi.h kaapi_error.h kaapi_atomic.h kaapi_format.c kaapi_impl.c kaapi_task.c kaapi_rt.c kaapi_hashmap.c kaapi_barrier.c kaapi_memory.c kaapi_offload_stream.c kaapi_offload.c kaapi_offload_device.c kaapi_ld.c kaapi_dbg.c kaapi_plugin_host.c ${ADDITIONAL_SOURCES})
target_link_libraries(kaapi PRIVATE Threads::Threads)

if (${HWLOC_FOUND})
  include_directories(${HWLOC_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${HWLOC_LIBRARIES})
endif()

if (${HWLOC_FOUND} AND ${HIP_FOUND}) 
  message(STATUS "Found ROCSMI_INCLUDE: ${ROCSMI_INCLUDE_DIRS}")
  include_directories(${ROCSMI_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${ROCSMI_LIBRARIES})
endif()

if (${CUDA_DRIVER_FOUND})
  include_directories(${CUDA_DRIVER_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${CUDA_DRIVER_LIBRARIES} ${CUBLAS_LIBRARIES})
endif()

if (${CUDA_RT_FOUND})
  include_directories(${CUDA_RT_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${CUDA_RT_LIBRARIES} ${CUBLAS_LIBRARIES})
endif()

if (${HIP_FOUND})
  include_directories(${HIP_INCLUDE_DIRS} ${HIPBLAS_INCLUDE_DIRS} ${ROCBLAS_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${HIP_LIBRARIES} ${HIPBLAS_LIBRARIES})
endif()

if (${PAPI_FOUND})
  include_directories(${PAPI_INCLUDE_DIRS})
  target_link_libraries(kaapi PRIVATE ${PAPI_LIBRARIES})
endif()

install(TARGETS kaapi DESTINATION lib)
install(FILES kaapi.h kaapi_atomic.h kaapi_error.h DESTINATION include)

