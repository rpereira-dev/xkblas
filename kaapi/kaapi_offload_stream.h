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
#include "kaapi_offload_datatype.h"

/*
*/
typedef enum kaapi_io_stream_type {
  KAAPI_IO_STREAM_H2D  = 0, /* from CPU to GPU */
  KAAPI_IO_STREAM_D2H  = 1, /* from GPU to CPU */
  KAAPI_IO_STREAM_D2D  = 2, /* from GPU to GPU */
  KAAPI_IO_STREAM_KERN = 3,
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
  int                          sid;       /* with respect to all io_stream in the device offload_stream */
  kaapi_lock_t                 mutex;     /*  lock */
  uint64_t                     count;     /* the size of array instr and pending */
  uint64_t                     smax;      /* maximal occupency of the stream */
  uint64_t                     smax_p;    /* maximal occupency of pending requests in the stream */
  uint64_t                     max_p;     /* ok_p..max_p should have been directly notified */
  uint64_t                     pos_r;	    /* first instruction to process */
  uint64_t                     pos_w;	    /* next position for writing instructions */
  volatile uint64_t            pos_rp;	  /* first pending instruction into the bloc */
  volatile uint64_t            pos_wp;	  /* next position for writing into the pending bloc */
  kaapi_io_instruction_t*      instr;	    /* first instruction */
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
  kaapi_io_stream_t**    ios[KAAPI_IO_STREAM_ALL];      /* basic stream */

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
extern void kaapi_stream_insert_io_task_inst(
    kaapi_offload_stream_t* stream,
    kaapi_io_stream_type_t  stype,
    kaapi_task_t*           task,
    kaapi_io_cbk_fnc_t      fnc,
    void*                   arg0,
    void*                   arg1,
    void*                   arg2
);

/*
*/
extern void kaapi_stream_insert_io_copy_inst(
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
);


/*
*/
void kaapi_offload_stream_poll( kaapi_offload_stream_t* const stream);

#endif
