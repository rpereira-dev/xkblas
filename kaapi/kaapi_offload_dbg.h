/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** joao.lima@inf.ufsm.br
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

/*
 * XKaapi interface for Offload plugins. Each plugin can load several devices.
 *
 * Based on the GCC libgomp plugins (2014) and clang offloading for Intel.
 * Extended to allow highly asynchronous communication between devices & host
 */

#ifndef _KAAPI_OFFLOAD_DBG_H_
#define _KAAPI_OFFLOAD_DBG_H_


/* Debug flag */
//#define _OFFLOAD_DEBUG  1
#ifndef _OFFLOAD_DEBUG  
#define _OFFLOAD_DEBUG  0
#endif

#if _OFFLOAD_DEBUG
#include <stdio.h>
#  define KAAPI_OFFLOAD_TRACE_IN \
    fprintf(stdout, "%p::IN %s\n", (void*)pthread_self(), __FUNCTION__);\
    fflush(stdout);
#  define KAAPI_OFFLOAD_TRACE_OUT \
    fprintf(stdout, "%p::OUT %s\n", (void*)pthread_self(), __FUNCTION__);\
    fflush(stdout);
#  define KAAPI_OFFLOAD_TRACE_MSG(...) \
    fprintf(stdout, __VA_ARGS__);\
    fflush(stdout);
#else
#  define KAAPI_OFFLOAD_TRACE_IN
#  define KAAPI_OFFLOAD_TRACE_OUT
#  define KAAPI_OFFLOAD_TRACE_MSG(...)
#endif

#endif /* _KAAPI_OFFLOAD_DBG_H_ */
