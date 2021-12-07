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

#ifndef KAAPI_OFFLOAD_DATATYPE_H_INCLUDED
#define KAAPI_OFFLOAD_DATATYPE_H_INCLUDED

/* fwd
*/
struct kaapi_io_stream;
struct kaapi_memory_device;

/*
*/
typedef struct {
  int   error;
  float cpu_delay; /* time on CPU between launch and completion */
  float gpu_delay; /* time of CPU between launch and completion */
} kaapi_io_status_t;

typedef void (*kaapi_io_cbk_fnc_t)(
    kaapi_io_status_t,
    struct kaapi_io_stream*,
    void*, void*, void*
);

/* io instruction bck: all differents instruction should have this fields first
*/
struct kaapi_io_cbk {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
};

/*
*/
typedef enum kaapi_io_copy_priority {
  KAAPI_IO_COPY_PRIORITY_LOW    = 0,
  KAAPI_IO_COPY_PRIORITY_NORMAL = 1,
  KAAPI_IO_COPY_PRIORITY_HIGH   = 2
} kaapi_io_copy_priority_t;

/* io instruction to write/read data from the corresponding device
   src == host emitting the request
*/
struct kaapi_io_copy {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
  kaapi_io_copy_priority_t     prio;
  const void*                  src;
  const kaapi_memory_view_t*   view_src;
  struct kaapi_memory_device*  dev_src;
  void*                        dest;
  const kaapi_memory_view_t*   view_dest;
  struct kaapi_memory_device*  dev_dest;
#if KAAPI_USE_TRACELIB==1
  uint64_t                     reserved;
#endif
};


/* marker begin...end for group of request
*/
struct kaapi_io_begin {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
  struct kaapi_io_instruction* first;
};

struct kaapi_io_end {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
  struct kaapi_io_instruction* last;
};


/* marker call back, acts as a full memory barrier : any write, read or kernel instructon
   before the sync are never re-ordered after the sync.
*/
struct kaapi_io_barrier {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
};

/* io instruction kernel : to launch kernel on the device
  The delay field of the status arguments of the callback, if defined, is the delay in millisecond
  to execute the kernel.
*/
struct kaapi_io_kernel {
  kaapi_io_cbk_fnc_t           fnc;
  void*                        arg[3];
  kaapi_task_body_t            body;
  kaapi_task_t*                task;
#if KAAPI_USE_TRACELIB==1
  uint64_t                     reserved;
#endif
};


typedef enum kaapi_io_type {
  KAAPI_IO_NOP      = 0,
  KAAPI_IO_BEGIN    = 1,
  KAAPI_IO_END      = 2,
  KAAPI_IO_COPY_H2H = 3,
  KAAPI_IO_COPY_H2D = 4,
  KAAPI_IO_COPY_D2H = 5,
  KAAPI_IO_COPY_D2D = 6,
  KAAPI_IO_BARRIER  = 7,
  KAAPI_IO_KERN     = 8
} kaapi_io_type_t;


/* one instruction in the stream:
   - each different case correspond to particular operation between the host (that emit
   the instruction) and the device implied in the operation.
   - once the instruction is locally terminated, a corresponding callback, if defined, 
   is called to signal the application of the completion of the operation.
*/
typedef struct kaapi_io_instruction {
  kaapi_io_type_t          type;
  union {
    struct kaapi_io_cbk     cbk;   /* cbk info always first fields of structure */
    struct kaapi_io_begin   f_io;
    struct kaapi_io_end     l_io;
    struct kaapi_io_copy    c_io;
    struct kaapi_io_kernel  k_io;
    struct kaapi_io_barrier b_io;
  } inst;
#if KAAPI_USE_PERFCOUNTER==1
  double                    t0; /* insert time in the stream */
  double                    t1; /* start time of execution */
  double                    t2; /* time where detected completed */
  double                    t3; /* time where callback returns or == t2 */
#endif
} kaapi_io_instruction_t;

#endif

