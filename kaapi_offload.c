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

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#if defined(__linux__)
#  include <linux/limits.h>
#endif

#define KAAPI_PREFIX "."

#define _OFFLOAD_DEBUG 0
#include "kaapi_impl.h"
#include "kaapi_offload.h"

/* number of devices from plugins */
static unsigned int kaapi_offload_num_devices = 0;

/* kaapi device pointers indexed by Id */
static kaapi_device_t** kaapi_offload_devices = NULL;

/* current device Id (internal) */
static __thread kaapi_device_t* kaapi_offload_current_device = 0;

/* need a host device to execute task following constraints on the data flow graph */
static int _kaapi_host_device = -1;

/* head of the list of all the configured drivers */
static kaapi_driver_t* kaapi_list_drivers = 0;

/* driver by type table */
static kaapi_driver_t* kaapi_drivers_bytype[KAAPI_PROC_TYPE_MAX];

/*
*/
kaapi_device_t* kaapi_offload_get_host_device(void)
{
  return (_kaapi_host_device == -1 ? 0 : kaapi_offload_devices[_kaapi_host_device]);
}


/*
*/
kaapi_device_t* kaapi_offload_device(int devid)
{
  if ((devid<0) && (devid >=kaapi_offload_num_devices))
     return 0;
  kaapi_device_t* device = kaapi_offload_devices[devid];
  if (!device->is_initialized)
    kaapi_offload_device_init(device);
  return device;
}


/*
*/
void kaapi_offload_set_current_device(kaapi_device_t* device)
{
  kaapi_offload_current_device = device;
}

/*
*/
kaapi_device_t* kaapi_offload_get_current_device(void)
{
  kaapi_device_t* device = kaapi_offload_current_device;
  return device;
}

/*
*/
kaapi_device_t* kaapi_offload_self_device(void)
{
  kaapi_device_t* device = kaapi_offload_current_device;
  return device;
}


#if KAAPI_USE_DYNLOADER
static bool kaapi_offload_check_plugin_name(char* fname)
{
  const char *prefix = "libkaapi_plugin_";
#if defined(__linux__)
  const char *suffix = ".so.1";
#elif defined(__APPLE__)
  const char *suffix = ".1.dylib";
#endif
  if (!fname)
    return false;
  if (strncmp(fname, prefix, strlen(prefix)) != 0)
    return false;
  if (strncmp(fname + strlen(fname) - strlen(suffix), suffix,
             strlen(suffix)) != 0)
    return false;
  return true;
}

/* based on GCC GOMP implementation (target.c) 
*/
static bool kaapi_offload_load_plugin(
    kaapi_driver_t* const driver,
    const char* plugin_name
)
{
  KAAPI_OFFLOAD_TRACE_IN
  char* err = NULL;

  dlerror();
  driver->handle = dlopen(plugin_name, RTLD_LAZY);
  if (driver->handle == NULL){
    err = dlerror();
    goto out;
  }
  /* */
#define DLSYM(func)                                                     \
  do                                                                    \
  {                                                                     \
    *(void**)(&driver->f_##func) = dlsym (driver->handle, KAAPI_PLUGIN_ENTRYPOINT_NAME(func));	                        \
    if (driver->f_##func ==0) {                                         \
      err = dlerror ();                                                 \
      goto out;                                                         \
    }                                                                   \
  }                                                                     \
  while (0)

#define DLSYM_OPT(func)                                                 \
    *(void**)(&driver->f_##func) = dlsym (driver->handle, KAAPI_PLUGIN_ENTRYPOINT_NAME(func))

  /* */
  DLSYM (get_name);
  DLSYM (get_flags);
  DLSYM (get_type);
  DLSYM (get_number);
  DLSYM (init);
  DLSYM (finalize);
  DLSYM (host_register);
  DLSYM (host_register_testwait);
  DLSYM (host_unregister);
  DLSYM (device_create);
  DLSYM (device_destroy);
  DLSYM (device_info);
  DLSYM (device_init);
  DLSYM (device_commit);
  DLSYM (device_start);
  DLSYM (device_stop);
  DLSYM (device_finalize);
  DLSYM (device_attach);
  DLSYM (device_detach);
  DLSYM (get_cublas_handle);

out:
  if(err != NULL){
    fprintf(stderr, "**** Error loading offload plugin '%s', "
            "file: '%s', line: %d, error='%s'\n", plugin_name,
            __FILE__, __LINE__, err);
    if(driver->handle != NULL)
      dlclose(driver->handle);
  }
#if _OFFLOAD_DEBUG
  else {
    fprintf(stdout, "%s: plugin %s successfully loaded\n", __FUNCTION__, plugin_name );
    fflush(stdout);
  }
#endif
KAAPI_OFFLOAD_TRACE_OUT
  return err == NULL;
}
#undef DLSYM
#undef DLSYM_OPT
#endif


/* Configure Kaapi based on the loaded driver plugin.
   All devices from its plugin are not initialized here.
*/
static void
kaapi_offload_config_devices(kaapi_driver_t* driver)
{
  KAAPI_OFFLOAD_TRACE_IN
  int n_devices;
  int i;

  if (driver->f_init() != 0)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: driver %s not initialized !\n", __FUNCTION__, driver->f_get_name() == 0 ? "<no name>" : driver->f_get_name() );
    fflush(stdout);
#endif
    goto out;
  }

  n_devices = driver->f_get_number();
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: driver %s export #device(s)=%i\n", __FUNCTION__, driver->f_get_name() == 0 ? "<no name>" : driver->f_get_name(), n_devices );
  fflush(stdout);
#endif
  if( n_devices < 1 )
    goto out;

  /* ToDo: share data between library & plugin ? 
     It seems to be good that plugin extends the driver in order to add internal,
     plugin specific fields.
     Currently only identifier allows to make correspondance between offload part and
     plugin part (device_id is the identifier in the plugin part, specific for each
     type of plugin).
     Advantage: avoid to recopy data to initialize device.
  */
  kaapi_offload_devices =
      (kaapi_device_t**)realloc( kaapi_offload_devices,
                                (kaapi_offload_num_devices+n_devices)*sizeof(kaapi_device_t*));
  if (kaapi_offload_devices == 0 )
    return;
  memset(kaapi_offload_devices+kaapi_offload_num_devices, 0, n_devices*sizeof(kaapi_device_t*));

  for (i= 0; i < n_devices; i++)
  {
    /* assume that device_create set at least the internal device_id */
    kaapi_device_t* device = driver->f_device_create(i);
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: driver %p/%s create device=%i/%p\n", 
     __FUNCTION__, (void*)driver, driver->f_get_name() == 0 ? "<no name>" : driver->f_get_name(), 
     i, (void*)device );
  fflush(stdout);
#endif
    kaapi_assert(device->device_id == i);
    device->driver = driver;
    device->tid = 0;
    device->spawn_count = 0;
    device->exec_count = 0;
    device->finalize = false;
    device->is_initialized = false;
    kaapi_assert( 0 == pthread_mutex_init(&device->lock, 0));
    kaapi_assert( 0 == pthread_cond_init(&device->cond, 0));
    kaapi_assert( 0 == pthread_cond_init(&device->cond_sleep, 0));
    device->issleeping = 0;
    device->request.op = KAAPI_DEVICEOP_NOP;
    device->request.arg = 0;
    device->request.counter = 0;

    KAAPI_ATOMIC_WRITE(&device->cnt_push, 0);
    device->name = driver->f_get_name();
    device->handle  = 0;

    device->memdev.device = device;
    device->stream.device = device;

#if KAAPI_DEBUG
    device->memdev.size_alloc = 0;
    device->memdev.size_free = 0;
    device->memdev.size_dev_alloc = 0;
    device->memdev.size_dev_free = 0;
#endif

    /* not yet fully initialize */
    device->is_initialized = false;

    /* register the host device (should always be loaded) and initialize it */
    if ((_kaapi_host_device == -1) &&
        (strcasecmp("host", device->name)==0)
      )
      _kaapi_host_device = kaapi_offload_num_devices;

    kaapi_offload_devices[kaapi_offload_num_devices] = device;
    kaapi_offload_num_devices++;
  }
out:
  KAAPI_OFFLOAD_TRACE_OUT
  return;
}


#if KAAPI_USE_DYNLOADER
/*
*/
static bool
kaapi_offload_plugin_filter(kaapi_driver_t* driver, char *const plugin_filter)
{
  char* filter;
  char* _filter; /* to free */
  char* token;
  const char* sep = ",";
  const char* plugin_name = driver->f_get_name();

  /* No filter = load all plugins */
  if ((plugin_filter == NULL)|| (strcmp(plugin_filter,"") ==0))
    return true;

  /* Plugin with no name = load it */
  if (plugin_name == NULL)
    return true;

  filter = _filter = strdup(plugin_filter);
  while ((token = strsep(&filter, sep)) != NULL ){
    if (strcmp(token, plugin_name) == 0 ){
      /* load this plugin */
      free(_filter);
      return true;
    }
  }
  free(_filter);

  /* not NULL, does not load plugins by default */
  return false;
}
#endif



#if KAAPI_USE_DYNLOADER
/* Find available plugins and dynamically load them.
*/
static void
kaapi_offload_find_plugins(void)
{
  KAAPI_OFFLOAD_TRACE_IN
  char* plugin_path = getenv("KAAPI_PLUGIN_PATH"); // Path to compiled plugins
  char* env_plugin_filter = getenv("KAAPI_DEVICE_TYPE"); // device type: cuda, host, etc
  char plugin_filter[128];
  if (env_plugin_filter !=0)
    snprintf(plugin_filter, 128, "%s", env_plugin_filter);
  else
    plugin_filter[0] = 0;

  DIR* dir = NULL;
  struct dirent *ent;
  char plugin_name[PATH_MAX];

  if (plugin_path == 0)
    plugin_path = KAAPI_PREFIX "/lib";

  if (plugin_path == NULL)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: empty KAAPI_PLUGIN_PATH\n", __FUNCTION__ );
    fflush(stdout);
#endif
    goto return_label;
  }

  /* */
  dir = opendir(plugin_path);
  if(dir == NULL)
  {
#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: directory %s does not exist\n", __FUNCTION__, plugin_path);
    fflush(stdout);
#endif
    goto return_label;
  }

#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: reading directory %s\n", __FUNCTION__, plugin_path);
  fflush(stdout);
#endif

  while ((ent = readdir(dir)) != NULL)
  {
    if(!kaapi_offload_check_plugin_name(ent->d_name))
      continue;
    if(strlen(plugin_path) + 1 + strlen(ent->d_name) >= PATH_MAX)
      continue;

    strcpy(plugin_name, plugin_path);
    strcat(plugin_name, "/");
    strcat(plugin_name, ent->d_name);

#if _OFFLOAD_DEBUG
    fprintf(stdout, "%s: found %s\n", __FUNCTION__, plugin_name);
    fflush(stdout);
#endif
    kaapi_driver_t* current = malloc(sizeof(kaapi_driver_t));
    if (!kaapi_offload_load_plugin(current, plugin_name))
    {
      free(current);
      continue;
    }  

    /* Can I load this device ? */
    if (kaapi_offload_plugin_filter(current, plugin_filter))
    {
      kaapi_offload_config_devices(current);
#if _OFFLOAD_DEBUG
      fprintf(stdout, "%s: plugin %s configured\n", __FUNCTION__, plugin_name);
      fflush(stdout);
#endif
      current->next = kaapi_list_drivers;
      kaapi_list_drivers = current;
      unsigned int type = current->f_get_type();
      kaapi_assert( type < KAAPI_PROC_TYPE_MAX );
      kaapi_drivers_bytype[type] = current;
    }
    else {
      free(current);
    }
  }

  closedir(dir);
return_label:
  KAAPI_OFFLOAD_TRACE_OUT
  return;
}
#else // no KAAPI_USE_DYNLOADER

static void
kaapi_offload_find_plugins(void)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_driver_t* current;

#if KAAPI_USE_HOST_PLUGIN
{
  current = malloc(sizeof(kaapi_driver_t));
  extern void KAAPI_PLUGIN_ENTRYPOINT(get_host_driver)(kaapi_driver_t* driver);
  KAAPI_PLUGIN_ENTRYPOINT(get_host_driver)(current);
  current->handle = 0;
  kaapi_offload_config_devices(current);
  current->next = kaapi_list_drivers;
  kaapi_list_drivers = current;
  unsigned int type = current->f_get_type();
  kaapi_assert( type < KAAPI_PROC_TYPE_MAX );
  kaapi_drivers_bytype[type] = current;
}
#endif

#if KAAPI_USE_CUDA_PLUGIN
{
  current = malloc(sizeof(kaapi_driver_t));
  extern void KAAPI_PLUGIN_ENTRYPOINT(get_cuda_driver)(kaapi_driver_t* driver);
  KAAPI_PLUGIN_ENTRYPOINT(get_cuda_driver)(current);
  current->handle = 0;
  kaapi_offload_config_devices(current);
  current->next = kaapi_list_drivers;
  kaapi_list_drivers = current;
  unsigned int type = current->f_get_type();
  kaapi_assert( type < KAAPI_PROC_TYPE_MAX );
  kaapi_drivers_bytype[type] = current;
}
#endif

  KAAPI_OFFLOAD_TRACE_OUT
  return;
}

#endif

/*
*/
unsigned int kaapi_offload_get_num_devices(void)
{
  return kaapi_offload_num_devices;
}

/* This function is  make all communications progress through all the stream.
   A a thread or a set of threads management communication progress for the device, this
   function becomes not necessary.
   This function must be called by the agregation protocol through call
   to kaapi_place_internalop for instance.
*/
 int kaapi_offload_poll_device( kaapi_device_t* device)
{
  int err =0;
  kaapi_assert_debug( device->is_initialized );
  kaapi_assert_debug( kaapi_offload_self_device() == device );

#if KAAPI_HAVE_IO_THREADS
#if KAAPI_USE_STREAM_D2D
  err = kaapi_offload_stream_process_instruction(&device->stream, KAAPI_IO_STREAM_D2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_D2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
#endif

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_H2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_KERN );
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_KERN);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream( &device->stream, KAAPI_IO_STREAM_D2H );
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
#else
#if KAAPI_USE_STREAM_D2D
  err = kaapi_offload_stream_process_instruction(&device->stream, KAAPI_IO_STREAM_D2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_D2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
#endif

  err = kaapi_offload_stream_process_instruction(&device->stream, KAAPI_IO_STREAM_H2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_H2D);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_KERN );
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  err = kaapi_offload_test_stream(&device->stream, KAAPI_IO_STREAM_KERN);
  kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

  if (kaapi_offload_stream_isempty(&device->stream, KAAPI_IO_STREAM_H2D))
  {
    err = kaapi_offload_stream_process_instruction( &device->stream, KAAPI_IO_STREAM_D2H );
    kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));

    err = kaapi_offload_test_stream( &device->stream, KAAPI_IO_STREAM_D2H );
    kaapi_assert_debug( (err == 0) || (err == EINPROGRESS));
  }
#endif
  return err;
}



/* Pool for all devices.
*/
int kaapi_offload_poll_devices(void)
{
  int err, i;
  for (i=0; i<kaapi_offload_num_devices; ++i)
  {
    kaapi_device_t* device = kaapi_offload_device(i);
    err = kaapi_offload_poll_device( device );
    if ((err !=0) && (err !=EINPROGRESS)) return err;
  }
  return 0;
}


/* Thread to poll streams and generate communications with device and calls callback functions.
 * Any update to the stream/queue/.. should be signaled to the thread.
 * The thread plays role to ensure completion of asynchronous operations.
 * Worker thread executes tasks (cpu) and initiate asynchronous commmunication with the device:
 * memory copies, kernel launches. The internal offload thread only test/wait for 
 * completion of asynchronous operation.
 */

/*
*/
int kaapi_offload_init(int flag)
{
  KAAPI_OFFLOAD_TRACE_IN

  /* global vars. init */
  memset(kaapi_drivers_bytype, 0, sizeof(kaapi_drivers_bytype));

  /* load device plugins and functions */
  kaapi_offload_find_plugins();

  /* initialize the devices
     - phase1: initialization
     - phase2: commit once all initializations have been done
  */
  if (kaapi_offload_num_devices >0)
  {
    for (int i=0; i<kaapi_offload_num_devices; ++i)
      kaapi_offload_device_init(kaapi_offload_devices[i]);

    for (int i=0; i<kaapi_offload_num_devices; ++i)
      kaapi_offload_device_commit(kaapi_offload_devices[i]);
  }

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}

/*
*/
int kaapi_offload_start(void)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _OFFLOAD_DEBUG
  fprintf(stdout, "%s: #devices = %i\n", __FUNCTION__, kaapi_offload_num_devices );
  fflush(stdout);
#endif
  
  /* initialize the host device */
  if (kaapi_offload_num_devices >0)
  {
    for (int i=0; i<kaapi_offload_num_devices; ++i)
      kaapi_offload_device_start(kaapi_offload_devices[i]);
  }
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
int kaapi_offload_free_memory(void)
{
  KAAPI_OFFLOAD_TRACE_IN

  if (kaapi_offload_num_devices > 0)
  {
    int i;
    for(i = 0; i < kaapi_offload_num_devices; i++)
      kaapi_offload_device_free_memory(kaapi_offload_devices[i]);
  }
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
int kaapi_offload_finalize(void)
{
  KAAPI_OFFLOAD_TRACE_IN

  if (kaapi_offload_num_devices > 0)
  {
    int i;
    for(i = 0; i < kaapi_offload_num_devices; i++)
      kaapi_offload_device_stop(kaapi_offload_devices[i]);
    for(i = 0; i < kaapi_offload_num_devices; i++)
      kaapi_offload_device_finalize(kaapi_offload_devices[i]);
    free(kaapi_offload_devices);
    kaapi_offload_devices = 0;
    kaapi_offload_num_devices = 0;
  }


  /* */
  while (kaapi_list_drivers !=0)
  {
    kaapi_driver_t* next_driver = kaapi_list_drivers->next;
    kaapi_list_drivers->f_finalize();
#if KAAPI_USE_DYNLOADER
    if (kaapi_list_drivers->handle != NULL)
      dlclose(kaapi_list_drivers->handle);
#endif
    free(kaapi_list_drivers);
    kaapi_list_drivers = next_driver;
  }
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}

/*
*/
kaapi_driver_t* kaapi_offload_driver_bytype( unsigned int type )
{
  if (type >=KAAPI_PROC_TYPE_MAX) return 0;
  return kaapi_drivers_bytype[type];
}
