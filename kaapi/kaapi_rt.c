/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** fabien.lementec@imag.fr
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

#include "git_hash.h"
#include "kaapi_impl.h"
#include "kaapi_trace.h"
//#include "kaapi_error.h"
#include <stdio.h>
#include <stdlib.h>


#if defined(__cplusplus)
extern "C" {
#endif

#define STR_EXP2(tok) #tok
#define STR_EXP(tok) STR_EXP2(tok)

/**
*/
void kaapi_abort(unsigned long int line, const char* file, const char* msg)
{
  if (msg)
    printf("**** %s. Line: %li, File: '%s'\n", msg, line, file);
  else
    printf("**** Aborting Line: %li, File: '%s'\n", line, file);
  abort();
}

void kaapi_warning(unsigned long int line, const char* file, const char* msg)
{
  if (msg)
    printf("**** warning: Line: %li, File: '%s', msg: '%s'\n", line, file, msg);
  else
    printf("**** warning: Line: %li, File: '%s'\n", line, file);
}

/* How it was compiled */
const char* get_kaapi_info(void)
{ 
  static char buffer[8192];
  static int isinit = 0;
  if (isinit ==0)
  {
    int err;
#if KAAPI_USE_PERFCOUNTER==1
    static char buffer_perfctr[1024];
    err = kaapi_perfctr_get_name_mask( kaapi_tracelib_param.perfctr_idset, 1024, buffer_perfctr);
#endif
#if KAAPI_USE_TRACELIB
    static char buffer_eventmask[1024];
    err = kaapi_event_get_name_mask( kaapi_tracelib_param.eventmask, 1024, buffer_eventmask);
#endif
 
    snprintf( buffer, 8192, 
            "  Git last commit: %s\n"
            "  BlasLib  : %s\n"
            "  Compiled : %s\n"
            "  GPUSET   : %u\n"
            "  NGPUS    : %i\n"
            "  NSTREAMS : %i\n"
            "  NKERNELS : %i\n" 
            "  PREFETCH : %i\n" 
#if KAAPI_USE_OCR
            "  MAPPING  : OCR\n" 
#else
            "  MAPPING  : direct\n" 
#endif
#if KAAPI_WS_GPUTASK
            "  LOAD.IMBL: WS\n" 
#else
            "  LOAD.IMBL: NO WS\n" 
#endif
#if KAAPI_PIPELINE_GPUTASK 
# if KAAPI_REORDER_TASK_EXEC
            "  REORDER  : yes\n" 
# else
            "  REORDER  : no\n" 
# endif
#else
            "  REORDER  : unlimited\n" 
#endif
            "  STREAMD2D: %s\n" 
            "  D2D OPT1 : %s\n" 
#if KAAPI_SLEEP_DEVICETHREAD
            "  SLEEPTHR : yes\n" 
#else
            "  SLEEPTHR : no\n" 
#endif
#if KAAPI_USE_OWN_HEAP_ALLOCATOR
            "  ALLOCATOR: %s\n" 
#endif
            "  CACHE_LIM: %i\n" 
#if KAAPI_USE_PERFCOUNTER
            "  PERFCTR  : %s\n" 
#else
            "  PERFCTR  : disable\n" 
#endif
#if KAAPI_USE_TRACELIB
            "  TRACE    : %s\n" 
#else
            "  TRACE    : disable\n" 
#endif
            "  IO/THR   : %i\n"
            "  API      : %s\n", 
         STR_EXP(GIT_HASH),
         STR_EXP(XKBLAS_BLASLIB),
         STR_EXP(XKBLAS_CFLAGS),
         kaapi_default_param.gpu_set,
         (int)kaapi_default_param.ngpus,
         kaapi_default_param.cuda_conc_stream_kernel,
         kaapi_default_param.cuda_conc_kernel,
#if KAAPI_USE_PREFETCH
         KAAPI_MAX_PREFETCH_WINDOW,
#else
         0,
#endif 
#if KAAPI_USE_STREAM_D2D
         "yes",
#else
         "no",
#endif 
#if KAAPI_USE_FAVOR_D2D_1
         "yes",
#else
         "no",
#endif 
#if KAAPI_USE_OWN_HEAP_ALLOCATOR
         "heap", /* heap allocator */
#else
         "default", /* default allocator */
#endif 
         (int)(kaapi_default_param.cuda_cache_limit*100),
#if KAAPI_USE_PERFCOUNTER
         (kaapi_perf_idset_empty(&kaapi_tracelib_param.perfctr_idset) ? "no_recorded" : buffer_perfctr ),
#endif
#if KAAPI_USE_TRACELIB
         (kaapi_tracelib_param.eventmask==0 ? "no_recorded" : buffer_eventmask ),
#endif
         (int)KAAPI_HAVE_IO_THREADS,
#if KAAPI_USE_CUDA_RUNTIME_API
         "cuda" 
#elif KAAPI_USE_HIP
         "hip" 
#endif 
    );
  }
  return buffer; 
}

/* autotools can prompt for this ... */
static const char* _get_kaapi_vesion = "Git last commit:" STR_EXP(GIT_HASH);
const char* get_kaapi_version(void)
{ return _get_kaapi_vesion; }


/* autotools can prompt for this ... */
static const char* _get_kaapi_git_hash = ""STR_EXP(GIT_HASH);
const char* get_kaapi_git_hash(void)
{ return _get_kaapi_git_hash; }


#if defined(__cplusplus)
}
#endif

