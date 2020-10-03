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

#include "kaapi_impl.h"
#include "kaapi_offload.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>



static char* name_io[] __attribute__((unused)) = {
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


static char* name_io_type[] __attribute__((unused)) = {
  "IO_STREAM_H2D",
  "IO_STREAM_KER",
  "IO_STREAM_D2H",
  "IO_STREAM_D2D"
};


/*
*/
static int _kaapi_offload_iostream_init(
    kaapi_io_stream_t* io,
    kaapi_io_stream_type_t type,
    unsigned int capacity
)
{
  KAAPI_OFFLOAD_TRACE_IN
  kaapi_assert( capacity >= 2);
  ///if (type == KAAPI_IO_STREAM_KERN) capacity = 2;

  io->type    = type;
  kaapi_assert(0==kaapi_atomic_initlock(&io->mutex));

  io->smax    = 0;
  io->smax_p  = 0;
  io->max_p   = (uint64_t)-1; /* means not used */
  io->pos_r   = 0;
  io->pos_rp  = 0;
  io->pos_w   = 0;
  io->pos_wp  = 0;
  io->ok_p   = 0;
  io->instr   = (kaapi_io_instruction_t*) malloc( capacity * sizeof(kaapi_io_instruction_t) );
  if (io->instr ==0) return ENOMEM;
  io->pending = (kaapi_io_instruction_t*) malloc( capacity * sizeof(kaapi_io_instruction_t) );
  if (io->pending ==0) 
  {
    free(io->instr); io->instr = 0;
    return ENOMEM;
  }
  io->count  = capacity;
  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}

/*
*/
static int _kaapi_offload_iostream_destroy(
    kaapi_io_stream_t* io
)
{
  if (io ==0) return 0;
  int err = 0;
  KAAPI_OFFLOAD_TRACE_IN
#if 0
  printf("* stream[%s]:%p smax:%i, smax_p: %i\n", name_io_type[io->type], io, io->smax, io->smax_p );
#endif

  if (io->pos_r != io->pos_w)
    err = EBUSY;
  else
    free(io->instr);
  if (io->pos_rp != io->pos_wp)
    err = EBUSY;
  else
    free(io->pending);
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
*/
int kaapi_offload_stream_init(
    kaapi_device_t* device,
    kaapi_offload_stream_t* s,
    unsigned int capacity
)
{
  KAAPI_OFFLOAD_TRACE_IN

  s->device = device;
  unsigned int i = 0;
  unsigned int cnt = 0;
  kaapi_io_stream_t** ios;
  unsigned int prefix[KAAPI_IO_STREAM_ALL+1];

  prefix[KAAPI_IO_STREAM_H2D] = 0;
  cnt += (s->count[KAAPI_IO_STREAM_H2D]  = kaapi_default_param.cuda_conc_h2d);
  prefix[KAAPI_IO_STREAM_KERN] = cnt;
  cnt += (s->count[KAAPI_IO_STREAM_KERN] = kaapi_default_param.cuda_conc_stream_kernel);
  prefix[KAAPI_IO_STREAM_D2H] = cnt;
  cnt += (s->count[KAAPI_IO_STREAM_D2H]  = kaapi_default_param.cuda_conc_d2h);
#if KAAPI_USE_STREAM_D2D
  prefix[KAAPI_IO_STREAM_D2D] = cnt;
  cnt += (s->count[KAAPI_IO_STREAM_D2D]  = kaapi_default_param.cuda_conc_d2d);
  prefix[KAAPI_IO_STREAM_D2D+1] = cnt;
#else
  prefix[KAAPI_IO_STREAM_D2H+1] = cnt;
#endif

  KAAPI_ATOMIC_WRITE(&s->next[KAAPI_IO_STREAM_D2H], 0);
  KAAPI_ATOMIC_WRITE(&s->next[KAAPI_IO_STREAM_KERN], 0);
  KAAPI_ATOMIC_WRITE(&s->next[KAAPI_IO_STREAM_H2D], 0);
#if KAAPI_USE_STREAM_D2D
  KAAPI_ATOMIC_WRITE(&s->next[KAAPI_IO_STREAM_D2D], 0);
#endif
  s->ios[0] = ios = (kaapi_io_stream_t**)malloc(sizeof(kaapi_io_stream_t*) * cnt );
  kaapi_assert( s->ios[0]!= 0 );
  s->ios[KAAPI_IO_STREAM_H2D] = s->ios[0]+prefix[KAAPI_IO_STREAM_H2D];
  s->ios[KAAPI_IO_STREAM_KERN] = s->ios[0]+prefix[KAAPI_IO_STREAM_KERN];
  s->ios[KAAPI_IO_STREAM_D2H]  = s->ios[0]+prefix[KAAPI_IO_STREAM_D2H];
#if KAAPI_USE_STREAM_D2D
  s->ios[KAAPI_IO_STREAM_D2D]  = s->ios[0]+prefix[KAAPI_IO_STREAM_D2D];
#endif

  for (i = 0; i<cnt; ++i)
  {
    kaapi_io_stream_type_t type =
        i < prefix[KAAPI_IO_STREAM_KERN] ? KAAPI_IO_STREAM_H2D :
        i < prefix[KAAPI_IO_STREAM_D2H] ? KAAPI_IO_STREAM_KERN :
#if KAAPI_USE_STREAM_D2D
        i < prefix[KAAPI_IO_STREAM_D2D] ? KAAPI_IO_STREAM_D2H : KAAPI_IO_STREAM_D2D
#else
          KAAPI_IO_STREAM_D2H
#endif
    ;
    ios[i]  = s->f_stream_alloc( device, type, capacity );
    kaapi_assert( ios[i] != 0 );
    ios[i]->stream = s;
//printf("%i:: init stream %i type: %s\n", device->ld->ldid, i, 
//    type == KAAPI_IO_STREAM_H2D ? "H2D" : type == KAAPI_IO_STREAM_KERN ? "kern": type == KAAPI_IO_STREAM_D2H ? "D2H" : type == KAAPI_IO_STREAM_D2D ? "D2D" : "<NOTYPE>" );
    kaapi_assert( 0 == _kaapi_offload_iostream_init( ios[i], type, capacity ));
  }

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}

/*
*/
int kaapi_offload_stream_destroy(
    kaapi_offload_stream_t * stream
)
{
  if (stream ==0) return 0;
  KAAPI_OFFLOAD_TRACE_IN
  unsigned int cnt = stream->count[KAAPI_IO_STREAM_D2H]+
                     stream->count[KAAPI_IO_STREAM_H2D]+
                     stream->count[KAAPI_IO_STREAM_KERN]
#if KAAPI_USE_STREAM_D2D
                     +stream->count[KAAPI_IO_STREAM_D2D]
#endif
;
  unsigned int i;

  for (i = 0; i<cnt; ++i)
    kaapi_assert(0 == _kaapi_offload_iostream_destroy( stream->ios[0][i] ));
  for (i = 0; i<cnt; ++i)
    stream->f_stream_free( stream->device, stream->ios[0][i] );

  KAAPI_OFFLOAD_TRACE_OUT
  return 0;
}


#if KAAPI_DEBUG
/*
*/
void kaapi_offload_print_stream_info(kaapi_offload_stream_t* stream)
{
  static const char* name_type[] = {
    "h2d", "ker", "d2h", "d2d"
  };
  unsigned int i;
  unsigned int cnt = stream->count[KAAPI_IO_STREAM_D2H]+
                     stream->count[KAAPI_IO_STREAM_H2D]+
                     stream->count[KAAPI_IO_STREAM_KERN]
#if KAAPI_USE_STREAM_D2D
                     +stream->count[KAAPI_IO_STREAM_D2D]
#endif
;

  for (i = 0; i<cnt; ++i)
  {
    kaapi_io_stream_t* ios =stream->ios[0][i];
    printf("  ios:@%p, %s, type:%s, pos_r:%i, pos_rp: %i, pos_w:%i, pos_wp:%i, ok_p:%i, capacity:%i\n",
      ios, 
      (kaapi_io_stream_emptyinstr(ios) ? "EMPTY" : "READY"),
      name_type[ios->type],
      ios->pos_r,
      ios->pos_rp,
      ios->pos_w,
      ios->pos_wp,
      ios->ok_p,
      ios->count
    );
  }
}
#endif

/*
*/
static kaapi_io_stream_t* kaapi_offload_select_io_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  kaapi_io_stream_t* retval;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_KERN));
#if ROUND_ROBIN
  /* increment the next stream to used */
  int snext0 = KAAPI_ATOMIC_READ(&stream->next[stype]);
  int snext = snext0;
  if (stream->count[stype] >1)
  {
    snext0 = KAAPI_ATOMIC_INCR_ORIG(&stream->next[stype]);
    snext = snext0 % stream->count[stype];
  }
#elif 1
  int count = stream->count[stype];
  int snext0 = KAAPI_ATOMIC_READ(&stream->next[stype]);
  int snext = snext0;
  if (count >1)
  {
    snext0 = KAAPI_ATOMIC_INCR_ORIG(&stream->next[stype]);
    snext = snext0 % count;
    if (stype == KAAPI_IO_STREAM_KERN)
    {
      int snextorig= snext;
      //int smin = kaapi_io_stream_sizeinstr(stream->ios[stype][snext]) + kaapi_io_stream_sizepending(stream->ios[stype][snext]);
      int smin = kaapi_io_stream_sizepending(stream->ios[stype][snext]);
      int smax = smin;
      for (int i=1; i<count; ++i)
      {
        //int load = kaapi_io_stream_sizeinstr(stream->ios[stype][snext]) + kaapi_io_stream_sizepending(stream->ios[stype][snext]);
        int load = kaapi_io_stream_sizepending(stream->ios[stype][snext]);
        if (load < smin)
        {  
          smin = load;
          snext = i;
        }
        else if (load > smax)
          smax = load;
      }
      if (snextorig != snext)
        printf("Stream load #%i[min:%i, max:%i] -> %i\n", count, smin, smax, snext);
    }
  }
#elif 0
#warning
  int snext = 0;
#endif
  retval = stream->ios[stype][snext];
  kaapi_assert_debug( retval != 0 );
  return retval;
}


/*
*/
int kaapi_offload_stream_size(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  int s = 0;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
            || (stype == KAAPI_IO_STREAM_KERN)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_ALL));

  kaapi_io_stream_type_t deb;
  kaapi_io_stream_type_t end;
  if (stype == KAAPI_IO_STREAM_ALL)
  {
    deb = 0; end = KAAPI_IO_STREAM_ALL;
  }
  else
  {
    deb = stype; end = deb+1;
  }

  for (stype=deb; stype < end; ++stype)
    for (unsigned int i=0; i< stream->count[stype]; ++i)
      s += kaapi_io_stream_sizeinstr(stream->ios[stype][i])
         + kaapi_io_stream_sizepending(stream->ios[stype][i]);
  return s;
}


/*
*/
int kaapi_offload_stream_sizepending(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  int s = 0;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
            || (stype == KAAPI_IO_STREAM_KERN)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_ALL));

  kaapi_io_stream_type_t deb;
  kaapi_io_stream_type_t end;
  if (stype == KAAPI_IO_STREAM_ALL)
  {
    deb = 0; end = KAAPI_IO_STREAM_ALL;
  }
  else
  {
    deb = stype; end = deb+1;
  }

  for (stype=deb; stype < end; ++stype)
    for (unsigned int i=0; i< stream->count[stype]; ++i)
      s += kaapi_io_stream_sizepending(stream->ios[stype][i]);
  return s;
}


/**
*/
int kaapi_offload_stream_isempty(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  unsigned int deb;
  unsigned int end;
  if (stype == KAAPI_IO_STREAM_ALL)
  {
    deb = 0; end = KAAPI_IO_STREAM_ALL;
  }
  else
  {
    deb = stype; end = deb+1;
  }

  for (unsigned int s=deb; s<end; ++s)
    for (unsigned int i=0; i< stream->count[s]; ++i)
      if (!kaapi_io_stream_emptyinstr(stream->ios[s][i])
       || !kaapi_io_stream_emptypending(stream->ios[s][i]))
        return 0;
  return 1;
}



/*
*/
kaapi_io_instruction_t* kaapi_offload_stream_push(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype,
    kaapi_io_stream_t** sios
)
{
  if (stream ==0) return 0;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_KERN));

  KAAPI_OFFLOAD_TRACE_IN

  /* call it only once because it increment the next for stream */
  kaapi_io_stream_t* ios = kaapi_offload_select_io_stream( stream, stype );
  kaapi_assert_debug( ios != 0 );

  *sios = ios;

  /* lock the stream to add a new entry
     mutex is released in commit operation.
     between them, caller should initialize the io_instruction
   */
  do {
    //kaapi_offload_test_stream( stream, stype );
    if (!kaapi_io_stream_fullinstr(ios))
    {
      kaapi_atomic_lock(&ios->mutex);
      if (!kaapi_io_stream_fullinstr(ios)) break;
      kaapi_atomic_unlock(&ios->mutex);
    }
  } while (kaapi_io_stream_fullinstr(ios));

  kaapi_io_instruction_t* inst = &ios->instr[ios->pos_w % ios->count];
  kaapi_assert_debug( ios->mutex._owner == pthread_self());

  KAAPI_OFFLOAD_TRACE_OUT
  return inst;
}


/*
*/
kaapi_io_instruction_t* kaapi_offload_stream_commit(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype,
    kaapi_io_stream_t* ios
)
{
  kaapi_assert_debug(stream !=0);
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_KERN));

  KAAPI_OFFLOAD_TRACE_IN

  kaapi_assert_debug( ios != 0 );
  kaapi_assert_debug( ios->mutex._owner == pthread_self());

  kaapi_io_instruction_t* inst = &ios->instr[ios->pos_w % ios->count];
  switch (inst->type)
  {
    case KAAPI_IO_BEGIN:
    case KAAPI_IO_END:
      break;
    case KAAPI_IO_COPY_H2H:
    case KAAPI_IO_COPY_H2D:
    case KAAPI_IO_COPY_D2H:
    case KAAPI_IO_COPY_D2D: {
    } break;
    case KAAPI_IO_BARRIER:
      break;
    case KAAPI_IO_KERN:
      break;
    default:
//#if KAAPI_DEBUG
      printf("Bad instruction type: %i\n", (int)inst->type);
      kaapi_assert( 0 );
//#endif
      KAAPI_OFFLOAD_TRACE_OUT
      return 0;
  }

  /* assuming TSO, so writemen barrier is enough */
  kaapi_writemem_barrier();
  ++ios->pos_w;

  /* unlock mutex locked in stream_push */
  kaapi_assert_debug( ios->mutex._owner == pthread_self());
  kaapi_atomic_unlock(&ios->mutex);

  KAAPI_OFFLOAD_TRACE_OUT

  return inst;
}


/** blocking=1 -> wait; blocking=0 -> test
*/
static int _kaapi_offload_onestream_process_pending(kaapi_offload_stream_t* const stream, kaapi_io_stream_t* ios, int mode)
{
  return stream->f_stream_process_pending(stream->device, ios, mode);
}


/*
 */
static int _kaapi_offload_onestream_process_instruction(
  kaapi_offload_stream_t* const stream,
  kaapi_io_stream_t* ios
)
{
  KAAPI_OFFLOAD_TRACE_IN

  kaapi_assert_debug( kaapi_offload_self_device() == stream->device );

  int err = 0;
  kaapi_assert_debug( ios->pos_r <= ios->pos_wp );
  uint64_t s = ios->pos_w - ios->pos_r;
  if (s > ios->smax) ios->smax = s;
uint64_t s0 = s;
  while (!kaapi_io_stream_emptyinstr(ios))
  {
    kaapi_atomic_lock(&ios->mutex);
    if (!kaapi_io_stream_emptyinstr(ios))
    {
      int p  = ios->pos_r  % ios->count;
      err = stream->f_stream_decode_ioinstruction(stream->device, ios, &ios->instr[p]);
      kaapi_assert_debug(err ==0 || err == EINPROGRESS);
      ++ios->pos_r;
      if (err == EINPROGRESS)
      {
        /* recopy op in pending op */
        int wp = ios->pos_wp  % ios->count;
        ios->pending[wp] = ios->instr[p];
        ++ios->pos_wp;
#if 0//FORCE
        if ((ios->type == KAAPI_IO_STREAM_KERN) && (ios->pos_wp - ios->pos_rp > kaapi_default_param.cuda_conc_kernel))
        {
          kaapi_atomic_unlock(&ios->mutex);
          while (ios->pos_wp - ios->pos_rp > kaapi_default_param.cuda_conc_kernel)
            _kaapi_offload_onestream_process_pending(stream, ios,  0);
          kaapi_atomic_lock(&ios->mutex);
        }
#endif
      }
      else kaapi_assert(err==0);
    }
    kaapi_atomic_unlock(&ios->mutex);
  }

  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}

/*
*/
int kaapi_offload_stream_process_instruction(
  kaapi_offload_stream_t* const stream,
  kaapi_io_stream_type_t stype
)
{
  int err = 0;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_KERN)
            || (stype == KAAPI_IO_STREAM_ALL));
  KAAPI_OFFLOAD_TRACE_IN

  kaapi_assert_debug( kaapi_offload_self_device() == stream->device );

  unsigned int deb;
  unsigned int end;
  if (stype == KAAPI_IO_STREAM_ALL)
  {
    deb = 0; end = KAAPI_IO_STREAM_ALL;
  }
  else
  {
    deb = stype; end = deb+1;
  }

  for (unsigned int s=deb; s<end; ++s)
    for (unsigned int i=0; i< stream->count[s]; ++i)
    {
      err = _kaapi_offload_onestream_process_instruction(stream, stream->ios[s][i]);
      kaapi_assert_debug(err ==0 || err == EINPROGRESS);
    }

  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}



/** blocking=1 -> wait; blocking=0 -> test
*/
static int _kaapi_offload_waittest_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype,
    int blocking
)
{

  int err;
  kaapi_assert((stype == KAAPI_IO_STREAM_D2H)
            || (stype == KAAPI_IO_STREAM_H2D)
#if KAAPI_USE_STREAM_D2D
            || (stype == KAAPI_IO_STREAM_D2D)
#endif
            || (stype == KAAPI_IO_STREAM_KERN)
            || (stype == KAAPI_IO_STREAM_ALL));
  KAAPI_OFFLOAD_TRACE_IN

  unsigned int deb;
  unsigned int end;
  if (stype == KAAPI_IO_STREAM_ALL)
  {
    deb = 0; end = KAAPI_IO_STREAM_ALL;
  }
  else
  {
    deb = stype; end = deb+1;
  }

  for (unsigned int s=deb; s<end; ++s)
    for (unsigned int i=0; i< stream->count[s]; ++i)
    {
      kaapi_io_stream_t* ios = stream->ios[s][i];
      if (!kaapi_io_stream_emptypending(ios));
      {
        uint64_t s = ios->pos_wp - ios->pos_rp;
        if (s > ios->smax_p) ios->smax_p = s;
        do {
          s = ios->pos_wp - ios->pos_rp;
          err = _kaapi_offload_onestream_process_pending( stream, ios, blocking ); 
        } while ((ios->type == KAAPI_IO_STREAM_KERN) && (s > kaapi_default_param.cuda_conc_kernel));
        kaapi_assert(err ==0 || err == EINPROGRESS);
      }
    }

  return 0;
}


/*
*/
int kaapi_offload_wait_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  if (stream ==0) return EINVAL;
  KAAPI_OFFLOAD_TRACE_IN
  int err = _kaapi_offload_waittest_stream(stream, stype, 1);
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}


/*
*/
int kaapi_offload_test_stream(
    kaapi_offload_stream_t* const stream,
    kaapi_io_stream_type_t stype
)
{
  if (stream ==0) return EINVAL;
  KAAPI_OFFLOAD_TRACE_IN
  int err = _kaapi_offload_waittest_stream(stream, stype, 0);
  KAAPI_OFFLOAD_TRACE_OUT
  return err;
}
