/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
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

#ifndef _KAAPI_ERROR_H
#define _KAAPI_ERROR_H 1


#if defined(__cplusplus)
extern "C" {
#endif
#include <errno.h>

extern void kaapi_abort(unsigned long int line, const char* file, const char* msg);
extern void kaapi_warning(unsigned long int line, const char* file, const char* msg);

#define _KE_STR_EXP2(tok) #tok
#define _KE_STR_EXP(tok) ""_KE_STR_EXP2(tok)""

#if defined(NDEBUG)
#  define kaapi_assert( cond ) if (!(cond)) kaapi_abort(__LINE__, __FILE__, 0)
#  define kaapi_assert_m( cond, msg ) if (!(cond)) kaapi_abort(__LINE__, __FILE__, msg)
#  define kaapi_assert_warning(cond)  (!(cond) ? kaapi_warning( __LINE__, __FILE__, _KE_STR_EXP(cond)), 1: 0)
#  define kaapi_assert_debug( cond )
#  define kaapi_assert_debug_m( cond, msg )
#  define KAAPI_DEBUG_INST( inst ) 
#  define KAAPI_NODEBUG_INST( inst ) inst
#  define KAAPI_NDEBUG 1
#else
#  define kaapi_assert( cond ) \
     if (!(cond)) kaapi_abort(__LINE__, __FILE__, "Bad assertion")
#  define kaapi_assert_m( cond, msg ) \
     if (!(cond)) kaapi_abort(__LINE__, __FILE__, msg)
///#  define kaapi_assert_warning(cond) if (!(cond)) kaapi_warning( __LINE__, __FILE__, _KE_STR_EXP(cond))
#  define kaapi_assert_warning(cond)  (!(cond) ? kaapi_warning( __LINE__, __FILE__, _KE_STR_EXP(cond)), 1: 0)

/* force value for KAAPI_DEBUG */
#if KAAPI_DEBUG==0
#elif KAAPI_DEBUG ==1
#elif defined(KAAPI_DEBUG)
#undef KAAPI_DEBUG
#define KAAPI_DEBUG 1
#else
#define KAAPI_DEBUG 1
#endif

#if defined(NDEBUG)
#undef KAAPI_DEBUG
#endif

#if KAAPI_DEBUG
#  define kaapi_assert_debug( cond ) if (!(cond)) kaapi_abort(__LINE__, __FILE__, "Bad assertion")
#  define kaapi_assert_debug_m( cond, msg ) if (!(cond)) kaapi_abort(__LINE__, __FILE__, msg)
#  define KAAPI_DEBUG_INST( inst ) inst
#  define KAAPI_NODEBUG_INST( inst )
#else
#  define kaapi_assert_debug( cond )
#  define kaapi_assert_debug_m( cond, msg )
#  define KAAPI_DEBUG_INST( inst )
#  define KAAPI_NODEBUG_INST( inst ) inst
#endif
#endif

#if defined(__cplusplus)
}
#endif

#endif
