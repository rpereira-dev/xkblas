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
#include <string.h>

#include "kaapi_impl.h"
#include "kaapi_offload.h"

#define _PLUGIN_NAME   "host"
#define _PLUGIN_DEBUG   0

#if KAAPI_USE_DYNLOADER
#  define KAAPI_CLASS_ENTRYPOINT extern 
#else
#  define KAAPI_CLASS_ENTRYPOINT static
#endif
#include "kaapi_plugin.h"

typedef struct kaapi_device_host {
  kaapi_device_t inherited;
  size_t         total_mem;
  size_t         free_mem;
  size_t         used_mem;
} kaapi_device_host_t;


/* memdev functions */
static uintptr_t host_alloc(kaapi_memory_device_t* dev, size_t size, int* flag)
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
  void* p = malloc(size);
  device->used_mem += size;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: alloc ptr=%p size=%ld\n", __FUNCTION__, p, size);
#endif
  if (flag) *flag = KAAPI_MEMORY_DEVICE_FLAG_NONE;
  return (uintptr_t)p;
}

static void host_free(kaapi_memory_device_t* dev, uintptr_t p, size_t size)
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: free ptr=%p\n", __FUNCTION__, (void*)p);
#endif
  kaapi_assert_debug( size < device->used_mem );
  device->used_mem -= size;
  free((void*)p);
}



/*
*/
static int host_copy(
    kaapi_memory_device_t* dev,
    kaapi_pointer_t dest, const kaapi_memory_view_t* view_dest,
    kaapi_pointer_t src, const kaapi_memory_view_t* view_src,
    int flags, 
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  int err =0;

  /* can only support host memory to host memory copy */
  if ((dest.asid != src.asid) || (dest.asid != dev->asid))
  {
    err = EINVAL;
    goto ret_val;
  }

  switch (view_src->type)
  {
    case KAAPI_MEMORY_VIEW_1D:
    {
      kaapi_assert(view_dest->type == KAAPI_MEMORY_VIEW_1D);
      if (view_dest->size[0] != view_src->size[0])
      {
        err = EINVAL;
        break;
      }
      memcpy( kaapi_pointer2void(dest),
              kaapi_pointer2void(src),
              view_src->size[0]*view_src->wordsize
      );
      break;
    }

    case KAAPI_MEMORY_VIEW_2D:
    {
      const char* laddr;
      char* raddr;
      size_t size;

      size = view_src->size[0] * view_src->size[1];
      kaapi_assert(view_dest->type == KAAPI_MEMORY_VIEW_2D);

      if ((view_dest->size[0] != view_src->size[0]) || (view_src->size[1] != view_dest->size[1]))
      {
        err = EINVAL;
        break;
      }

      laddr = (char*)kaapi_pointer2void(src);
      raddr = (char*)kaapi_pointer2void(dest);

      if (kaapi_memory_view_iscontiguous(view_src) &&
          kaapi_memory_view_iscontiguous(view_dest))
        memcpy( raddr, laddr, size * view_src->wordsize);
      else
      {
        size_t i;
        size_t size_row = view_src->size[1]*view_src->wordsize;
        size_t llda = view_src->ld * view_src->wordsize;
        size_t rlda = view_dest->ld * view_src->wordsize;

        kaapi_assert_debug( view_dest->size[1] == view_src->size[1] );

        for (i=0; i<view_src->size[0]; ++i, laddr += llda, raddr += rlda)
          memcpy(raddr, laddr, size_row);
      }
      break;
    }

    default:
      err = EINVAL;
  }

ret_val:
  if (cbk)
  {
    kaapi_io_status_t ios = {0, err };
    cbk(ios, 0, arg0, arg1, arg2 );
  }
  return err;
}


/*
*/
static int host_memsync(kaapi_memory_device_t* dev, int begend)
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  kaapi_mem_barrier();
  return 0;
}


/*
*/
static size_t host_get_mem_info(kaapi_memory_device_t* dev, size_t* mem_total, size_t* mem_limit)
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  if (mem_total) *mem_total = device->total_mem;
  if (mem_limit) *mem_limit = device->total_mem;
  return device->total_mem;
}


/*
*/
static size_t host_get_free_mem(kaapi_memory_device_t* dev)
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  return device->free_mem;
}


/*
*/
__attribute__((unused))
static size_t host_get_used_mem(kaapi_memory_device_t* dev) 
{
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev->device;
#if defined(_PLUGIN_DEBUG)
  fprintf(stdout, "host:%s: device %d init\n", __FUNCTION__, device->inherited.device_id);
#endif
  return device->used_mem;
}

/*
 */
static kaapi_io_stream_t* host_stream_alloc(
    kaapi_device_t* dev,
    int type,
    unsigned int capacity
)
{
  kaapi_io_stream_t* cios = (kaapi_io_stream_t*)malloc(sizeof(kaapi_io_stream_t));
  return cios;
}

/*
 */
static void host_stream_free(
    kaapi_device_t* dev,
    kaapi_io_stream_t* ios
)
{
  free(ios);
}


/*
 */
static int host_stream_decode_ioinstruction(
    kaapi_device_t* device,
    kaapi_io_stream_t* ios,
    kaapi_io_instruction_t* instr
)
{
#if _PLUGIN_DEBUG
static char* name_io[] = {
  "IO_NOP",
  "IO_BEGIN",
  "IO_END",
  "IO_COPY_H2H",
  "IO_COPY_H2D",
  "IO_COPY_D2H",
  "IO_COPY_D2D",
  "IO_BARRIER",
  "IO_KERN"
};
#endif

  KAAPI_PLUGIN_TRACE_IN

  int err = 0;
  kaapi_address_space_id_t asid = device->memdev.asid;
  kaapi_assert_debug(device == ios->stream->device);

  KAAPI_PLUGIN_TRACE_MSG("%s: instr '%s'\n", __FUNCTION__, name_io[instr->type]);

  switch (instr->type)
  {
    case KAAPI_IO_NOP:
    case KAAPI_IO_BEGIN:
    case KAAPI_IO_END:
      break;

    case KAAPI_IO_COPY_H2H:
    case KAAPI_IO_COPY_H2D:
    case KAAPI_IO_COPY_D2H:
    case KAAPI_IO_COPY_D2D:
    {
      struct kaapi_io_copy* op = &instr->inst.c_io;
      err = host_copy(&device->memdev,
        kaapi_make_pointer((void*)op->dest, 0, asid), op->view_dest,
        kaapi_make_pointer((void*)op->src, 0, asid), op->view_src,
        0,
        0, 0, 0, 0
      );
    } break;

    case KAAPI_IO_BARRIER:
      break;

    case KAAPI_IO_KERN:
    {
      struct kaapi_io_kernel* op = &instr->inst.k_io;
      err = kaapi_offload_device_execute_task(
        device,
        op->task,
        0
      );
    }
  }
  /* call the callback : host is always (in this version) synchronous */
  struct kaapi_io_cbk* op = &instr->inst.cbk;
  if (op->fnc)
  {
    kaapi_io_status_t status = {0, err };
    op->fnc(status, ios, op->arg[0],op->arg[1],op->arg[2]);
  }

  KAAPI_PLUGIN_TRACE_OUT

  /* return 0: no in progress operation */
  return 0;
}



/* */
static uint16_t host_get_source(
  kaapi_memory_device_t* dev,
  uint16_t lid0,
  KAAPI_MEMORY_VALUE_TYPE valid_bit, KAAPI_MEMORY_VALUE_TYPE xfer_bit
)
{
  uint16_t lid_src;
  kaapi_assert_debug((valid_bit !=0) || (xfer_bit !=0));

  if (valid_bit !=0)
    lid_src = KAAPI_MEMORY_FFS( valid_bit );
  else
    lid_src = KAAPI_MEMORY_FFS( xfer_bit );
  --lid_src;
  kaapi_assert_debug(lid_src < KAAPI_MEMORY_MAX_NODES);
  return lid_src;
}


/*
 */
static int host_stream_process_pending(
    kaapi_device_t* device,
    kaapi_io_stream_t* ios,
    int blocking
)
{
  KAAPI_PLUGIN_TRACE_IN

  kaapi_assert(kaapi_io_stream_emptypending(ios));

  KAAPI_PLUGIN_TRACE_OUT
  return 0;
}



/*
*/
KAAPI_CLASS_ENTRYPOINT const char *
KAAPI_PLUGIN_ENTRYPOINT(get_name)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return "host";
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_flags)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_type)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return KAAPI_PROC_TYPE_HOST; /* KAAPI_PROC_TYPE_HOST */
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_number)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return 1;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT unsigned int
KAAPI_PLUGIN_ENTRYPOINT(get_ndevices)(void)
{
  KAAPI_PLUGIN_TRACE_IN
  KAAPI_PLUGIN_TRACE_OUT
  return 1;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int
KAAPI_PLUGIN_ENTRYPOINT(init)(void)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: host init\n", __FUNCTION__);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT void
KAAPI_PLUGIN_ENTRYPOINT(finalize)(void)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: host finalize\n", __FUNCTION__);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
}


/*
*/
KAAPI_CLASS_ENTRYPOINT
uint64_t KAAPI_PLUGIN_ENTRYPOINT(host_register)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  if (cbk)
  {
    kaapi_io_status_t ios = {0, 0 };
    cbk(ios, 0, arg0, arg1, arg2 );
  }
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT
int KAAPI_PLUGIN_ENTRYPOINT(host_register_testwait)(
    uint64_t index,
    int flag
)
{
  if (index !=0) return EINVAL;
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT
uint64_t KAAPI_PLUGIN_ENTRYPOINT(host_unregister)(
    void* ptr, size_t size,
    kaapi_io_cbk_fnc_t cbk,
    void* arg0, void* arg1, void* arg2
)
{
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT kaapi_device_t* KAAPI_PLUGIN_ENTRYPOINT(device_create)(kaapi_driver_t* driver, int dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_host_t* hostdevice = (kaapi_device_host_t*)malloc( sizeof(kaapi_device_host_t));
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: deriver create device: %d/%p\n", __FUNCTION__, dev, hostdevice);
#endif
  memset(hostdevice, 0, sizeof(kaapi_device_host_t) );
  hostdevice->inherited.device_id = dev;
  _kaapi_offload_config_data_field_device(driver, &hostdevice->inherited);
  kaapi_offload_device_init( &hostdevice->inherited );
  kaapi_offload_device_commit( &hostdevice->inherited );
  KAAPI_OFFLOAD_TRACE_OUT

  return &hostdevice->inherited;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_destroy)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %lu init\n", __FUNCTION__, (uintptr_t)device);
#endif
  kaapi_localitydomain_destroy(device->inherited.ld);
  free(device->inherited.ld);
  free(device);
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_init)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d init\n", __FUNCTION__, dev->device_id);
#endif
  /* memory device */
  device->total_mem = (size_t)-1ULL;
  device->free_mem = (size_t)-1ULL;
  device->used_mem = 0;

  dev->name = "host";
  dev->memdev.f_alloc = host_alloc;
  dev->memdev.f_free = host_free;
  dev->memdev.f_copy = host_copy;
  dev->memdev.f_memsync = host_memsync;
  dev->memdev.f_get_mem_info = host_get_mem_info;
  dev->memdev.f_get_free_mem = host_get_free_mem;
  dev->memdev.f_get_source = host_get_source;

  /* stream device */
  dev->stream.f_stream_free = host_stream_free;
  dev->stream.f_stream_alloc = host_stream_alloc;
  dev->stream.f_stream_process_pending = host_stream_process_pending;
  dev->stream.f_stream_decode_ioinstruction = host_stream_decode_ioinstruction;

  kaapi_localitydomain_t* ld = malloc(sizeof(kaapi_localitydomain_t));
  kaapi_localitydomain_init(ld, &device->inherited);
  device->inherited.ld = ld;
  kaapi_localitydomain_attach( KAAPI_LD_NUMA, 0, ld );
  kaapi_dsm_register_device(&kaapi_the_dsm, &dev->memdev, dev->driver->f_get_type(), ld->ldid );
  dev->state == KAAPI_DEVICE_STATE_INIT;

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}



/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_commit)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
  dev->state == KAAPI_DEVICE_STATE_COMMIT;
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}



/*
*/
KAAPI_CLASS_ENTRYPOINT const char* KAAPI_PLUGIN_ENTRYPOINT(device_info)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
  static char buffer[256];
  snprintf(buffer, 256, "host device");
  return buffer;
}

/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_start)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  //kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
#if _PLUGIN_DEBUG
  //fprintf(stdout, "host:%s: device %d start\n", __FUNCTION__, dev->device_id);
#endif
  dev->state = KAAPI_DEVICE_STATE_START;
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_stop)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
  //kaapi_device_host_t* device = (kaapi_device_host_t*)dev;
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d stop\n", __FUNCTION__, dev->device_id);
#endif
  dev->state = KAAPI_DEVICE_STATE_STOPPED;
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT void KAAPI_PLUGIN_ENTRYPOINT(device_finalize)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d finalize\n", __FUNCTION__, dev->device_id);
#endif
  kaapi_dsm_unregister_device(&kaapi_the_dsm, &dev->memdev);
  kaapi_localitydomain_deattach( KAAPI_LD_NUMA, dev->ld );
  dev->state = KAAPI_DEVICE_STATE_FINALIZED;
  KAAPI_OFFLOAD_TRACE_OUT
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_attach)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d finalize\n", __FUNCTION__, dev->device_id);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT int KAAPI_PLUGIN_ENTRYPOINT(device_detach)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d finalize\n", __FUNCTION__, dev->device_id);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


/*
*/
KAAPI_CLASS_ENTRYPOINT void* KAAPI_PLUGIN_ENTRYPOINT(get_cublas_handle)(kaapi_device_t* dev)
{
  KAAPI_OFFLOAD_TRACE_IN
#if _PLUGIN_DEBUG
  fprintf(stdout, "host:%s: device %d finalize\n", __FUNCTION__, dev->device_id);
#endif
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


#if KAAPI_USE_DYNLOADER==0
  /* */
#define EP(func)  driver->f_##func = KAAPI_PLUGIN_ENTRYPOINT(func)

void KAAPI_PLUGIN_ENTRYPOINT(get_host_driver)(kaapi_driver_t* driver)
{

  /* */
  EP (get_name);
  EP (get_flags);
  EP (get_type);
  EP (get_number);
  EP (get_ndevices);
  EP (init);
  EP (finalize);
  EP (host_register);
  EP (host_register_testwait);
  EP (host_unregister);

  EP (device_create);
  EP (device_destroy);
  EP (device_info);
  EP (device_init);
  EP (device_commit);
  EP (device_start);
  EP (device_stop);
  EP (device_finalize);
  EP (device_attach);
  EP (device_detach);
  EP (get_cublas_handle);
}
#endif
