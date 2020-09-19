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

#ifndef _KAAPI_OFFLOAD_H_
#define _KAAPI_OFFLOAD_H_


#define KAAPI_PLUGIN_PREFIX_NAME "KAAPI_PLUGIN_"
#define KAAPI_PLUGIN_ENTRYPOINT_NAME( func_name ) KAAPI_PLUGIN_PREFIX_NAME #func_name
#define KAAPI_PLUGIN_ENTRYPOINT( func_name ) KAAPI_PLUGIN_ ## func_name

#include "kaapi_offload_stream.h"


#if !defined(KAAPI_USE_OFFLOAD)
/* exported function even if OFFLOAD is disable */
static inline unsigned int kaapi_offload_get_num_devices(void)
{ return 0; }

#else

struct kaapi_driver;

typedef enum {
  KAAPI_DEVICEOP_NOP =0,
  KAAPI_DEVICEOP_REPLY,
  KAAPI_DEVICEOP_WRITEBACK,
  KAAPI_DEVICEOP_WRITEBACK_WAIT,
  KAAPI_DEVICEOP_MEMSYNC,
  KAAPI_DEVICEOP_INVALIDATE_CACHES
} kaapi_device_op_t;


/* perfcounter of task performed by device
*/
typedef struct  {
  uint64_t        spawn;          /*  */
  double          time;           /*  */
  double          flops;          /*  */
  double          ai;             /*  */
} kaapi_offloadtask_perfcounter_t;

typedef struct  {
  kaapi_offloadtask_perfcounter_t task[KAAPI_FORMAT_MAX];
} kaapi_offload_perfcounter_t;


/* A device virtualize a ressource with its one address space and
   a communication stream between host and the ressource
*/
struct kaapi_device {
    kaapi_memory_device_t    memdev;     /* casted to kaapi_device */
    kaapi_offload_stream_t   stream;     /* communication streams host<->device */
    kaapi_localitydomain_t*  ld;         /* the device locality domain */
    kaapi_context_t*         ctxt;       /* running thread */
    kaapi_atomic_t           cnt_push;   /* number of times the ressource is pushed */
    unsigned int             device_id;  /* Internal id for a specific device type (ordering) */
    pthread_t                tid;
    struct kaapi_driver*     driver;
    uint64_t                 spawn_count;   /* number of tasks */
    uint64_t                 exec_count;    /* number of tasks completed */
    int volatile             finalize;      /* true iff driver stop device */
    int                      is_initialized;/* True if driver is initialized */
    kaapi_offload_perfcounter_t perfcnt; /* */
    const char*              name;          /* Device name */
    void*                    handle;        /* device handle, e.g. cublas handle for GPU*/

    pthread_mutex_t          lock;          /* used to synchronize device thread */
    pthread_cond_t           cond;          /* and cpu threads if any */
    struct {
      kaapi_device_op_t      op;            /* op request for the device */
      uintptr_t              arg;
      kaapi_atomic64_t*      counter;       /* for MEMSYNC or WRITEBACK request */
      int                    err;           /* error returned by the request */
    } request;

    size_t  cnt_pending;
    size_t  cnt_ready;
    size_t  cnt_exec;
};


/* Kaapi driver.
   Each remote computing ressource is currently view as a device on the local host
   Kaapi_device here is both the device pluging and serves as instance of device...
*/
typedef struct kaapi_driver {
    unsigned int             flags;	        /* Device flags */

    void*                    handle;        /* plugin handle */

    /* Function handlers: accessor to meta data */
    const char   *(*f_get_name)(void); /* name of the plugin */
    unsigned int (*f_get_flags)(void); /* flags: not really used */
    unsigned int (*f_get_type)(void);  /* type of ressource CPU,GPU etc */
    unsigned int (*f_get_number)(void);/* number of devices managed by the driver */

    /* life cycle functions for the driver of devices (1 device == 1 ressource) */
    int (*f_init)(void);
    void (*f_finalize)(void);

    /* driver specific functions for all devices managed by the driver */
    /* Memory registration of host memory */
    uint64_t (*f_host_register)(
        void* ptr, size_t,
        kaapi_io_cbk_fnc_t cbk,
        void* arg0, void* arg1, void* arg2
    );

    /* test completion of asynchronous host_register operation.
       Argument is the handle returned by host_register (if != -1).
       If flag == 0, then the operation is a non blocking test that allows
       to test progression of pinning.
       If flag == 1, then the operation blocks the caller until the request
       has been completed.
       If flag == 2, then the operation blocks the caller until all previous
       requests have been completed.
       Return values are:
        - 0 non error or the test operation complets
        - EINVAL: invalid handle
        - EINPROGESS: request not yet completed
     */
    int  (*f_host_register_testwait)(
        uint64_t handle,
        int flag
    );

    /* Memory unregistration of host memory: asynchronous version */
    uint64_t  (*f_host_unregister)(
        void* ptr, size_t sz,
        kaapi_io_cbk_fnc_t cbk,
        void* arg0, void* arg1, void* arg2
    );

    /* create device object and initialize device_id field with argument */
    kaapi_device_t* (*f_device_create)(int);
    int (*f_device_destroy)(kaapi_device_t*);
    /* initialize device fields, especially with virtual functions */
    const char* (*f_device_info)(kaapi_device_t*);
    int (*f_device_init)(kaapi_device_t*);
    int (*f_device_commit)(kaapi_device_t*);
    int (*f_device_start)(kaapi_device_t*);
    int (*f_device_stop)(kaapi_device_t*);
    void (*f_device_finalize)(kaapi_device_t*);
    /* consider device as the current device */
    int (*f_device_attach)(kaapi_device_t*);
    /* consider device as the current device */
    int (*f_device_detach)(kaapi_device_t*);

    /* cublas support */
    void* (*f_get_cublas_handle)(kaapi_device_t*);

    /* linked list of all drivers */
    struct kaapi_driver* next;
} kaapi_driver_t;


/* API methods */
extern kaapi_device_t* kaapi_offload_get_host_device(void);


/*
*/
extern int kaapi_offload_device_execute_task(
     kaapi_device_t* const device,
     kaapi_frame_t* frame, /* to signal */
     kaapi_task_t* task,   /* task was pushed in the context of 'frame' */
     void* handle
);

/** \ingroup Offload
 Search for xkaapi plugins in KAAPI_PLUGIN_PATH and initilize then. */
extern int kaapi_offload_init(int flag);

/** \ingroup Offload
  Start offload module. Start thread (if any) on each device.
*/
extern int kaapi_offload_start(void);

/** \ingroup Offload
 Free allocated memory on each device
 */
extern int kaapi_offload_free_memory(void);

/** \ingroup Offload
 Finalize all devices from plugins
 */
extern int kaapi_offload_finalize(void);

/** \ingroup Offload
  Return a descriptor of the current offload device.
 \retval 0 if the current thread is not a device.
 \retval a pointer to the current offload device.
 */
extern kaapi_device_t* kaapi_offload_self_device(void);

/*
*/
static inline kaapi_address_space_id_t kaapi_offload_self_device_get_asid(void)
{
    kaapi_assert_debug(kaapi_offload_self_device() != NULL);
    kaapi_assert_debug(kaapi_offload_self_device()->is_initialized == true);
    return kaapi_offload_self_device()->memdev.asid;
}


/** \ingroup Offload
  It inits a device and attaches to the current thread.
 */
extern int kaapi_offload_device_init(kaapi_device_t* const device);

/** \ingroup Offload
  It commits a device.
  Called after all inits have been call on each devices
 */
extern int kaapi_offload_device_commit(kaapi_device_t* const device);


/** \ingroup Offload
  Gets info for the device
 */
extern const char* kaapi_offload_device_info(kaapi_device_t* const device);

/** \ingroup Offload
  It inits a device and attaches to the current thread.
 */
extern int kaapi_offload_device_start(kaapi_device_t* const device);

/** \ingroup Offload
  It stop the device thread (if any) and allows caller to push/pop
  device in its own context.
 */
extern void kaapi_offload_device_stop(kaapi_device_t* const device);

/** \ingroup Offload
 Force device attached thread to wakeup
*/
extern void kaapi_offload_device_wakeup(kaapi_device_t* const device);

/** \ingroup Offload
  Free allocated memory on the device
 */
extern void kaapi_offload_device_free_memory(kaapi_device_t* const device);


/** \ingroup Offload
 */
extern void kaapi_offload_device_finalize(kaapi_device_t* const device);


/** \ingroup Offload
 */
extern void* kaapi_offload_device_thread( void* arg );


/** \ingroup Offload
 */
extern kaapi_device_t* kaapi_offload_device_push(kaapi_device_t* const device);

/** \ingroup Offload
     device is the device returned by push
 */
extern void kaapi_offload_device_pop(kaapi_device_t* const device);


/** \ingroup Offload
 */
extern unsigned int kaapi_offload_get_num_devices(void);

/** \ingroup Offload
 */
extern kaapi_device_t* kaapi_offload_device(int devid);

/** \ingroup Offload
 */
extern kaapi_driver_t* kaapi_offload_driver_bytype( unsigned int type );


/** \ingroup Offload
 */
extern void kaapi_offload_register_node(kaapi_device_t* device);

static inline bool kaapi_offload_is_active(void){
  if(kaapi_offload_get_num_devices() > 0)
    return true;
  else
    return false;
}


/** \ingroup Offload
 * invalidate all caches
 */
extern int kaapi_offload_invalidate_caches(void);

/** \ingroup Offload
 * Synchronize all devices: wait until all spawned and pending operations have been completed
 */
extern int kaapi_offload_synchronize(void);

/** \ingroup Offload
 * Synchronize one specific device
 */
extern int kaapi_offload_synchronize_device(kaapi_device_t* device);

/** \ingroup Offload
 * Return total memory size (physical memory).
 */
extern size_t  kaapi_offload_get_mem_info( kaapi_device_t* device, size_t* mem_total, size_t* mem_limit);


/** \ingroup Offload
 * Poll IO for the specific device.
 * Returns 0 in case of success
 */
extern int kaapi_offload_poll_device(kaapi_device_t*);


/** \ingroup Offload
 * Poll IO for the all devices.
 * Returns 0 in case of success
 */
extern int kaapi_offload_poll_devices(void);


/** \ingroup Offload
 * Set the device dev to be the current device
 */
extern void kaapi_offload_set_current_device( kaapi_device_t* device);

/** \ingroup Offload
 * Return the current device
 */
extern kaapi_device_t* kaapi_offload_get_current_device(void);


#endif // #if defined(KAAPI_USE_OFFLOAD)


#endif /* _KAAPI_OFFLOAD_H_ */
