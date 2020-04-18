  double                    t3; /* time where callback returns or == t2 */
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

#ifndef KAAPI_OFFLOAD_STREAM_H_INCLUDED
#define KAAPI_OFFLOAD_STREAM_H_INCLUDED

#include "kaapi_offload_dbg.h"


/* fwd decl
*/
struct kaapi_io_instruction;
struct kaapi_io_stream;
struct kaapi_offload_stream;
struct kaapi_device;
struct kaapi_memory_device;
typedef struct kaapi_memory_device kaapi_memory_device_t;

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
  kaapi_memory_device_t*       dev_src;
  void*                        dest;
  const kaapi_memory_view_t*   view_dest;
  kaapi_memory_device_t*       dev_dest;
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
  kaapi_frame_t*               frame;
};


typedef enum kaapi_io_type {
  KAAPI_IO_NOP = 0,
  KAAPI_IO_BEGIN,
  KAAPI_IO_END,
  KAAPI_IO_COPY_H2H,
  KAAPI_IO_COPY_H2D,
  KAAPI_IO_COPY_D2H,
  KAAPI_IO_COPY_D2D,
  KAAPI_IO_BARRIER,
  KAAPI_IO_KERN
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
#if defined(KAAPI_USE_PERFCOUNTER)
  double                    t0; /* insert time in the stream */
  double                    t1; /* start time of execution */
  double                    t2; /* time where detected completed */
  double                    t3; /* time where callback returns or == t2 */
#endif
} kaapi_io_instruction_t;


/*
*/
typedef enum kaapi_io_stream_type {
  KAAPI_IO_STREAM_H2D  = 0, /* from CPU to GPU */
  KAAPI_IO_STREAM_KERN = 1,
  KAAPI_IO_STREAM_D2H  = 2, /* from GPU to CPU */
#if KAAPI_USE_STREAM_D2D
  KAAPI_IO_STREAM_D2D  = 3, /* from GPU to GPU */
#endif
  KAAPI_IO_STREAM_ALL       /* internal purpose */
} kaapi_io_stream_type_t;


/* A Kaapi stream of IO requests
   - bounded io instructions
   - any read/write instructions may be reordered
   - group of instructions (between marker io_begin/io_end) cannot re-ordered outside the
   group
   - io_barrier acts as a full memory barrier
   - instructions may be aggregated
   pos_r, pos_w, pos_rp and pos_wp are non decreasing integer that correspond to entries %count
   in the table.

   If IO threads are activated, then one thread manage the IO while the device thread manages
   the kernel stream. In any case the device thread manages the progression of the whole computation
   and calls the callback at when events are posted.

*/
typedef struct kaapi_io_stream {
  kaapi_io_stream_type_t       type;
  kaapi_lock_t                 mutex;     /*  lock */
  uint64_t                     count;     /* the size of array instr and pending */
  uint64_t                     pos_r;	  /* first instruction to process */
  uint64_t                     pos_w;	  /* next position for writing instructions */
  volatile uint64_t            pos_rp;	  /* first pending instruction into the bloc */
  volatile uint64_t            pos_wp;	  /* next position for writing into the pending bloc */
  kaapi_io_instruction_t*      instr;	  /* first instruction */
  kaapi_io_instruction_t*      pending;   /* pending instructions, not yet completed */
  struct kaapi_offload_stream* stream;
  volatile uint64_t            ok_p __attribute__((aligned(KAAPI_CACHE_LINE)));
                                          /* past the last position of pending notified instr in [pos_rp,pos_wp] */
} kaapi_io_stream_t;


/* Kaapi offload stream is virtual interface to be implemented by a device.
   Streams are decoupled from H2D/D2H/D2D/kernel executions.
   The number of stream per type is subject to change at start time
   by reading environement variables. See kaapi_usage.

   ios[0] is the pointer of all the iostream_t*.
   ios[1] point to the first output stream, and ios[2] to the first kernel thread.
*/
typedef struct kaapi_offload_stream {
  struct kaapi_device*   device;
  int                    count[KAAPI_IO_STREAM_ALL];    /* number of iostream per type */
  kaapi_atomic_t         next[KAAPI_IO_STREAM_ALL];     /* next  stream fifo */
  kaapi_io_stream_t**    ios[KAAPI_IO_STREAM_ALL];      /* relatively to the host that emits request */

  /* virtualisation */
  struct kaapi_io_stream* (*f_stream_alloc)(
      struct kaapi_device*,
      int,
      unsigned int
  );
  void (*f_stream_free)(
      struct kaapi_device*,
      struct kaapi_io_stream*
  );
  int (*f_stream_process_pending )(
      struct kaapi_device*,
      struct kaapi_io_stream*,
      int
  );
  int (*f_stream_decode_ioinstruction)(
      struct kaapi_device*,
      struct kaapi_io_stream*,
      struct kaapi_io_instruction*
  );
} kaapi_offload_stream_t;

/* Create a kaapi_offload_stream_t object.
   The routine allocates and initializes
   a kaapi stream for attached to the device.
   The capacity value is the capacity of the stream
   to handle pending asynchronous operation.
   If for some usage, the number of pending asynchronous
   operation is greather than this capacity, then
   the stream will do not accept new asynchronous operation
   until a previously pushed operation completes.

   Return 0 in case of success else, the error code
*/
extern int kaapi_offload_stream_init(
    struct kaapi_device* device,
    kaapi_offload_stream_t* stream,
    unsigned int capacity
);

extern int kaapi_offload_stream_destroy(
    kaapi_offload_stream_t * stream
);

/* */
static inline int kaapi_io_stream_fullinstr( const kaapi_io_stream_t* ios )
{
  return (ios->pos_w - ios->pos_r >= ios->count);
}
static inline int kaapi_io_stream_fullpending( const kaapi_io_stream_t* ios )
{
  return (ios->pos_wp - ios->pos_rp >= ios->count);
}

/* */
static inline int kaapi_io_stream_emptyinstr( const kaapi_io_stream_t* ios )
{
  return (ios->pos_r == ios->pos_w);
}
static inline int kaapi_io_stream_emptypending( const kaapi_io_stream_t* ios )
{
  return (ios->pos_rp == ios->pos_wp);
}

/* */
static inline int kaapi_io_stream_sizeinstr( const kaapi_io_stream_t* ios )
{
  return (ios->pos_w - ios->pos_r);
}

/* */
static inline int kaapi_io_stream_sizepending( const kaapi_io_stream_t* ios )
{
  return (ios->pos_wp - ios->pos_rp);
}

/* Push a new asynchronous event into the kaapi_offload_stream.
   Depending of the kind of operation, the event is recorded
   into one of the different underlaying ios_stream.
   The ios_stream is returned through formal parameter ios.
   
   On the completion of the event, the runtime calls the call back
   function cbk(ios_stream, arg_action) and stores the return value
   into the status of the request.
   The return value of the callback function is avaible in the
   request status, once the user has tested or wait for the handle.
   
   The call back function may be null.
   
   All pushed requests with the same type of operation are enqueued 
   in a fifo maner and they complet in order: the runtime invokes 
   the callback in the same order as the requests were pushed.
*/
extern kaapi_io_instruction_t* kaapi_offload_stream_push(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype,
    kaapi_io_stream_t** ios
);

/* Make last instruction returned by push visible
   The caller must pass stream, stype and ios returned by previous call to
   kaapi_offload_stream_push.
*/
extern kaapi_io_instruction_t* kaapi_offload_stream_commit(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype,
    kaapi_io_stream_t* ios
);

/**
*/
extern int kaapi_offload_stream_isempty(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
);

/*
*/
extern int kaapi_offload_stream_size(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
);

/*
*/
extern int kaapi_offload_stream_sizepending(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
);


/** Blocking operation
*/
extern int kaapi_offload_wait_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
);

/** Non blocking operation
    Return: 0 in case of success
    else return an error code
*/
extern int kaapi_offload_test_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
);


/* Process each instruction of the stream
   f_operator is called on each instruction and should return 0 in case of success
*/
extern int kaapi_offload_stream_process_instruction(
  kaapi_offload_stream_t* const stream,
  kaapi_io_stream_type_t stype
);


/*
*/
static inline void kaapi_stream_insert_io_begin_inst(
    kaapi_offload_stream_t* stream,
    kaapi_io_stream_type_t stype
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_io_stream_t* ios;
  kaapi_io_instruction_t* inst
    = kaapi_offload_stream_push( stream, stype, &ios );
  inst->type = KAAPI_IO_BEGIN;
  inst->inst.f_io.fnc   = 0;
  inst->inst.f_io.first = inst;
  kaapi_offload_stream_commit( stream, stype, ios );
  KAAPI_OFFLOAD_TRACE_OUT
}

/*
*/
static inline void kaapi_stream_insert_io_end_inst(
    kaapi_offload_stream_t* stream,
    kaapi_io_stream_type_t  stype,
    kaapi_io_cbk_fnc_t      fnc,
    void*                   arg0,
    void*                   arg1,
    void*                   arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_io_stream_t* ios;
  kaapi_io_instruction_t* inst
    = kaapi_offload_stream_push( stream, stype, &ios );
  inst->type = KAAPI_IO_END;
  inst->inst.l_io.last  = inst;
  inst->inst.l_io.fnc   = fnc;
  inst->inst.l_io.arg[0]= arg0;
  inst->inst.l_io.arg[1]= arg1;
  inst->inst.l_io.arg[2]= arg2;
  kaapi_offload_stream_commit( stream, stype, ios );
  KAAPI_OFFLOAD_TRACE_OUT
}

/*
*/
static inline void kaapi_stream_insert_io_task_inst(
    kaapi_offload_stream_t* stream,
    kaapi_io_stream_type_t  stype,
    kaapi_task_t*           task,
    kaapi_frame_t*          frame,
    kaapi_io_cbk_fnc_t      fnc,
    void*                   arg0,
    void*                   arg1,
    void*                   arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN

#if defined(KAAPI_USE_PERFCOUNTER)
  double t0 = kaapi_get_elapsedtime();
#endif
  kaapi_io_stream_t* ios;
  kaapi_io_instruction_t* inst
    = kaapi_offload_stream_push( stream, stype, &ios );

#if KAAPI_DEBUG
  kaapi_assert_debug( ios != 0 );
  kaapi_assert_debug( inst ==  &ios->instr[ios->pos_w % ios->count] );
  kaapi_assert_debug( ios->mutex._owner == pthread_self());
#endif

  inst->type = KAAPI_IO_KERN;
  inst->inst.k_io.fnc   = fnc;
  inst->inst.l_io.arg[0]= arg0;
  inst->inst.l_io.arg[1]= arg1;
  inst->inst.l_io.arg[2]= arg2;
  inst->inst.k_io.task  = task;
  inst->inst.k_io.frame = frame;
#if defined(KAAPI_USE_PERFCOUNTER)
  inst->t0 = t0;
  inst->t1 =0;
  inst->t2 =0;
  inst->t3 =0;
#endif
  kaapi_offload_stream_commit( stream, stype, ios );
#if KAAPI_DEBUG
  kaapi_assert_debug( ios->mutex._owner != pthread_self() );
#endif
  KAAPI_OFFLOAD_TRACE_OUT
}

/*
*/
static inline void kaapi_stream_insert_io_copy_inst(
    kaapi_offload_stream_t*    stream,
    kaapi_io_stream_type_t     stype,
    kaapi_io_type_t            io_type,
    kaapi_io_copy_priority_t   prio,
    const void*                src,
    const kaapi_memory_view_t* view_src,
    kaapi_memory_device_t*     dev_src,
    void*                      dest,
    const kaapi_memory_view_t* view_dest,
    kaapi_memory_device_t*     dev_dest,
    kaapi_io_cbk_fnc_t         fnc,
    void*                      arg0,
    void*                      arg1,
    void*                      arg2
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_assert_debug( (io_type >=KAAPI_IO_COPY_H2H) && (io_type <= KAAPI_IO_COPY_D2D));
  kaapi_assert( kaapi_memory_view_size(view_src) == kaapi_memory_view_size(view_dest));
  kaapi_assert_debug( (io_type !=KAAPI_IO_COPY_D2D)|| (kaapi_memory_view_iscontiguous(view_src) &&  kaapi_memory_view_iscontiguous(view_src)) );
  kaapi_assert_debug( (io_type !=KAAPI_IO_COPY_H2D)|| kaapi_memory_view_iscontiguous(view_dest) );
  kaapi_assert_debug( (io_type !=KAAPI_IO_COPY_D2H)|| kaapi_memory_view_iscontiguous(view_src) );
  kaapi_assert_debug( (io_type !=KAAPI_IO_COPY_D2H)|| kaapi_memory_view_iscontiguous(view_src) );

  kaapi_io_stream_t* ios;
  kaapi_io_instruction_t* inst
    = kaapi_offload_stream_push( stream, stype, &ios );

#if KAAPI_DEBUG
  kaapi_assert_debug( ios != 0 );
  kaapi_assert_debug( ios->mutex._owner == pthread_self());
#endif

  inst->type = io_type;
  inst->inst.c_io.fnc   = fnc;
  inst->inst.l_io.arg[0]= arg0;
  inst->inst.l_io.arg[1]= arg1;
  inst->inst.l_io.arg[2]= arg2;
  inst->inst.c_io.prio  = prio;
  inst->inst.c_io.src   = src;
  inst->inst.c_io.view_src  = view_src;
  inst->inst.c_io.dev_src  = dev_src;
  inst->inst.c_io.dest  = dest;
  inst->inst.c_io.view_dest = view_dest;
  inst->inst.c_io.dev_dest  = dev_dest;
  kaapi_offload_stream_commit( stream, stype, ios );
#if KAAPI_DEBUG
  kaapi_assert_debug( ios->mutex._owner != pthread_self() );
#endif
  KAAPI_OFFLOAD_TRACE_OUT
}


/*
*/
void kaapi_offload_stream_poll( kaapi_offload_stream_t* const stream);

#endif
