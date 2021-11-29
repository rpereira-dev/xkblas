set(SOURCES
  ukilli.cpp
  kaapi_trace_reader.cpp
  )

include_directories( ${CMAKE_CURRENT_BINARY_DIR} ../kaapi )

if (KAAPI_USE_TRACE)
  list(APPEND ADDITIONAL_SOURCES kaapi_trace.h kaapi_perfctr.h kaapi_trace_lib.c kaapi_trace_parser.c kaapi_trace_recorder.c)
  add_compile_options("-DKAAPI_USE_TRACELIB=1")
else()
  add_compile_options("-DKAAPI_USE_TRACELIB=0")
endif()

if (KAAPI_USE_PERFCTR)
   add_compile_options("-DKAAPI_USE_PERFCOUNTER=1")
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

if (KAAPI_USE_TRACE)
  add_executable(ukilli ${SOURCES})
  target_link_libraries (ukilli kaapi)
  install(TARGETS ukilli DESTINATION bin)
endif()
