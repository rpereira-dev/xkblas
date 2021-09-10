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

#ifndef __KAAPI_PLUGIN_H_
#define __KAAPI_PLUGIN_H_

#include <stdint.h>

#define KAAPI_PLUGIN_PREFIX_NAME "KAAPI_PLUGIN_"
#define KAAPI_PLUGIN_ENTRYPOINT_NAME( func_name ) KAAPI_PLUGIN_PREFIX_NAME #func_name
#define KAAPI_PLUGIN_ENTRYPOINT( func_name ) KAAPI_PLUGIN_ ## func_name

struct kaapi_io_stream;
struct kaapi_io_instruction;
struct kaapi_device;

//#define _PLUGIN_DEBUG   1

#if _PLUGIN_DEBUG
#if 1
#  define KAAPI_PLUGIN_TRACE_IN
#  define KAAPI_PLUGIN_TRACE_OUT
#else
#  define KAAPI_PLUGIN_TRACE_IN \
    fprintf(stdout, "IN %s/%s\n", _PLUGIN_NAME, __FUNCTION__);\
    fflush(stdout);
#  define KAAPI_PLUGIN_TRACE_OUT \
    fprintf(stdout, "OUT %s/%s\n", _PLUGIN_NAME, __FUNCTION__);\
    fflush(stdout);
#endif
#  define KAAPI_PLUGIN_TRACE_MSG(...) \
    fprintf(stdout, __VA_ARGS__);\
    fflush(stdout);
#else
#  define KAAPI_PLUGIN_TRACE_IN
#  define KAAPI_PLUGIN_TRACE_OUT
#  define KAAPI_PLUGIN_TRACE_MSG(...)
#endif

/*
 */
KAAPI_CLASS_ENTRYPOINT const char *
KAAPI_PLUGIN_ENTRYPOINT(get_name)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_flags)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_type)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_number)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(init)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT void
KAAPI_PLUGIN_ENTRYPOINT(finalize)(void);

/*
 */
KAAPI_CLASS_ENTRYPOINT uint64_t
KAAPI_PLUGIN_ENTRYPOINT(host_register)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
);


/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(host_register_testwait)( 
  uint64_t handle, int flag 
);


/*
 */
KAAPI_CLASS_ENTRYPOINT uint64_t
KAAPI_PLUGIN_ENTRYPOINT(host_unregister)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
);

/*
 */
KAAPI_CLASS_ENTRYPOINT struct kaapi_device*
KAAPI_PLUGIN_ENTRYPOINT(device_create)(int dev);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_destroy)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT const char*
KAAPI_PLUGIN_ENTRYPOINT(device_info)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_init)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_start)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_stop)(struct kaapi_device*);


/*
 */
KAAPI_CLASS_ENTRYPOINT void
KAAPI_PLUGIN_ENTRYPOINT(device_finalize)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_attach)(struct kaapi_device*);

/*
 */
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(device_detach)(struct kaapi_device*);

#endif
