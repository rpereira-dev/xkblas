/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012, 2021 INRIA.
**
** Contributors :
**
** fabien.lementec@gmail.com
** Thierry Gautier thierry.gautier@inrialpes.fr
** 
** This software is a computer program whose purpose is to execute
** multithreaded computation with data flow synchronization between
** threads.
** 
** This software is governed by the CeCILL-C license under French law
** and abiding by the rules of distribution of free software.  You can
** use, modify and/ or redistribute the software under the terms of
** the CeCILL-C license as circulated by CEA, CNRS and INRIA at the
** following URL "http://www.cecill.info".
** 
** As a counterpart to the access to the source code and rights to
** copy, modify and redistribute granted by the license, users are
** provided only with a limited warranty and the software's author,
** the holder of the economic rights, and the successive licensors
** have only limited liability.
** 
** In this respect, the user's attention is drawn to the risks
** associated with loading, using, modifying and/or developing or
** reproducing the software by the user in light of its specific
** status of free software, that may mean that it is complicated to
** manipulate, and that also therefore means that it is reserved for
** developers and experienced professionals having in-depth computer
** knowledge. Users are therefore encouraged to load and test the
** software's suitability as regards their requirements in conditions
** enabling the security of their systems and/or data to be ensured
** and, more generally, to use and operate it in the same conditions
** as regards security.
** 
** The fact that you are presently reading this means that you have
** had knowledge of the CeCILL-C license and that you accept its
** terms.
** 
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#include "kaapi.h"
#include "kaapi_impl.h" 
#include "kaapi_trace.h"
#include "kaapi_trace_recorder.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Global information 
*/

/*
*/
uint64_t kaapi_event_startuptime = 0;

/** Fifo List of buffers to record on file
    - push in tail
    - pop in head
*/
static kaapi_event_buffer_t* listevt_head =0;
static kaapi_event_buffer_t* listevt_tail =0;
static pthread_mutex_t mutex_listevt;

static pthread_cond_t signal_thread;

/** List of free buffers
*/
static kaapi_event_buffer_t*  listevtfree_head =0;
static pthread_mutex_t mutex_listevtfree_head;


/** List of fd, one for each core:
    avoid to reorder buffer...
    if kaapi_event_reader is redesigned, then we can imagine to write all event buffers into one file.
*/

#define KAAPI_MAX_PROCESSOR 256
#define KAAPI_SHIFT_PROCESSOR 16
static int listfd_setcontainer[KAAPI_MAX_PROCESSOR];
static int* listfd_set = listfd_setcontainer+KAAPI_SHIFT_PROCESSOR; /* shift to keep few negative */


/** The thread to join in termination
*/
static pthread_t collector_threadid;
static int volatile finalize_flushimator = 0;

#if KAAPI_USE_PERFCOUNTER==1
/* Push a set of performance counter to the stream of events
*/
kaapi_event_buffer_t* kaapi_event_push_perfctr(
    kaapi_tracelib_thread_t*    kproc,
    uint64_t                    tclock,
    uint8_t                     eventno,
    uint8_t                     kind,
    uint64_t                    d0,
    const kaapi_perf_idset_t*   idset,
    const kaapi_perf_counter_t* perfctr
)
{
  kaapi_event_buffer_t* evb = kproc->eventbuffer;
  kaapi_event_t* evt = 0;

  unsigned int cnt = 0;
  unsigned int i = 0;
  unsigned int idx;
  kaapi_perf_idset_t set = *idset;
  while (set !=0)
  {
    idx = __builtin_ffsl( set )-1;
    set &= ~(1UL << idx);
    if (evt ==0)
    {
      if (evb->pos+1 >= KAAPI_EVENT_BUFFER_SIZE)
        evb = kproc->eventbuffer = kaapi_event_flushbuffer(evb);
      evt = &evb->buffer[evb->pos++];
      evt->evtno = eventno;
      evt->kind  = kind;
      evt->kid   = kproc->kid;
      evt->date  = tclock;
      KAAPI_EVENT_DATA(evt,0,u)  = d0;
    }
    KAAPI_EVENT_DATA(evt,1,i8)[cnt]= (uint8_t)idx;
    KAAPI_EVENT_DATA(evt,1,i8)[cnt+1]= (uint8_t)-1;  /* mark for next counter: unused */
    evt->u.data[cnt+2].u = perfctr[i]; /* shift 2 because data[0] = d0 */
    ++i;
    ++cnt;
    if (cnt == 2)
    {
      evt = 0;
      cnt = 0;
    }
  }

  return evb;
}
#endif

/*
*/
static int _kaapi_write_header( int kid )
{
  if (listfd_set[kid] == -1)
    return EINVAL;

  kaapi_eventfile_header_t header;
  memset(&header, 0, sizeof(header));
  header.version         = __KAAPI__;
  header.minor_version   = __KAAPI_MINOR__;
  header.trace_version   = __KAAPI_TRACE_VERSION__;
  if (ENAMETOOLONG==gethostname( header.hostname, 32 ))
    header.hostname[31]=0;
  header.kid             = kid;
  header.numaid          = 0;/* now a specific event map it to a numa node */
  header.ptype           = 0;/* unused, should be to group threads */
  header.cpucount        = -1;
  header.gpucount        = kaapi_default_param.ngpus;
  header.gpuset          = kaapi_default_param.gpu_set;
  header.s_kern          = kaapi_default_param.cuda_conc_stream_kernel;
  header.s_d2h           = kaapi_default_param.cuda_conc_d2h;
  header.s_h2d           = kaapi_default_param.cuda_conc_h2d;
  header.s_d2d           = kaapi_default_param.cuda_conc_d2d;
  header.event_mask      = kaapi_tracelib_param.eventmask;

  size_t cnt;
  sprintf(header.event_date_unit, "%s", kaapi_event_date_unit());
  
#if KAAPI_USE_PERFCOUNTER==1
  /* register : max perf counter in the low 8 bits, base for papi counter in bit 8-15 */
  header.perfcounter_count = (KAAPI_PERF_ID_MAX & 0xFF) | (KAAPI_PERF_ID_PAPI_BASE << 8);
  for (cnt=0; cnt<kaapi_tracelib_count_perfctr(); ++cnt)
  {
    const char* pname = kaapi_tracelib_perfid_to_name( (kaapi_perf_id_t)cnt );
    if (pname !=0)
    {
      int c = snprintf(header.perfcounter_name[cnt], KAAPI_SIZE_PERFCTR_NAME-1, "%s",pname);
      header.perfcounter_name[cnt][c] = 0;
    }
  }
  header.perf_mask = kaapi_tracelib_param.perfctr_idset;
  header.task_perf_mask = kaapi_tracelib_param.taskperfctr_idset;
  header.uncore_perf_mask = kaapi_tracelib_param.uncoreperfctr_idset;
  header.uncore_perf_period = kaapi_tracelib_param.uncore_period;
#else
  header.perfcounter_count = 0;
  header.perf_mask = 0;
  header.task_perf_mask = 0;
  header.uncore_perf_mask = 0;
  header.uncore_perf_period = 0;
#endif
  int i;
  header.taskfmt_count = 0;
  for (i=0; i<kaapi_tracelib_param.fmt_listsize; ++i)
  {
    const kaapi_descrformat_t* fmt = kaapi_tracelib_param.fmt_list[i];
    // TODO ?
    if (fmt ==0) continue;
    if (header.taskfmt_count >= KAAPI_FORMAT_MAX){ 
        fprintf(stderr, "Warning: too many fmtdefs\n");
        break;
    }
    kaapi_fmttrace_def* fmtdef = &header.fmtdefs[header.taskfmt_count];
    fmtdef->fmtid = fmt->fmtid;
    if (fmt->name !=0){
      strncpy( fmtdef->name, fmt->name, 63);
      fmtdef->name[63] = 0;
    }
    else
      strncpy( fmtdef->name, "no name", 64);
    if (fmt->color !=0){
      strncpy( fmtdef->color, fmt->color, 31);
      fmtdef->color[31] = 0;
    }
  
 else
      strncpy( fmtdef->color, "0.0 0.0 1.0", 32);
    ++header.taskfmt_count;
  }
  //printf("%i: #fmtdef: %i, numaid: %i\n", header.kid, header.taskfmt_count, header.numaid);

  const char* gitversion = get_kaapi_git_hash();
  strncpy(header.package, gitversion, sizeof(header.package)-1);

  /* rewind the file: */
  off_t o = lseek( listfd_set[kid], 0, SEEK_SET);
  if (o == (off_t) -1)
    return errno;

  /* write the header */
  ssize_t sz_write = write(listfd_set[kid], &header, sizeof(header));
  kaapi_assert(sz_write == sizeof(header));
  return 0;
}

/* write one bloc. Should not be concurrent */
static int _kaapi_write_evb( kaapi_event_buffer_t* evb )
{
  int32_t kid   = evb->kid;
  uint32_t ptype = evb->ptype;
  kaapi_assert( kid < KAAPI_MAX_PROCESSOR-KAAPI_SHIFT_PROCESSOR);
  kaapi_assert( kid >= -KAAPI_SHIFT_PROCESSOR);

  //printf("write evb:@=%p\n", evb);

  if (listfd_set[kid] == -1)
  { /* open the file and write a dummy header */
    char filename[128];
    sprintf(filename,"%s.%i.evt", kaapi_tracelib_param.recordfilename, kid );

    /* open it */
    listfd_set[kid] = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    kaapi_assert( listfd_set[kid] != -1 );
    fchmod( listfd_set[kid], S_IRUSR|S_IWUSR);

    //printf("Write header to trace file\n"); fflush(stdout);
    
    /* write the header */
    kaapi_eventfile_header_t header;
    memset(&header, 0, sizeof(header));
    ssize_t sz_write = write(listfd_set[kid], &header, sizeof(header));
    kaapi_assert(sz_write == sizeof(header));
  }

  ssize_t sz_towrite = sizeof(kaapi_event_t)*evb->pos;
  char* buffer = (char*)&evb->buffer[0];
  while (sz_towrite !=0)
  {
    ssize_t sz_write = write(listfd_set[kid], buffer, sz_towrite );
    if (sz_write == -1)
    {
      fprintf(stderr, "*** Kaapi error while writing events' file, errrno:%s\n", strerror(errno) );
      fflush(stderr);
      return EINVAL;
    }
    sz_towrite -= sz_write;
    buffer += sz_write;
  }
  evb->pos = 0;
  return 0;
}


/* Write all buffers in the list */
int kaapi_event_fencebuffers(void)
{
  kaapi_event_buffer_t* evb;
  int err = 0;

  pthread_mutex_lock(&mutex_listevt);
  while (listevt_head !=0)
  {
    /* pick up atomically */
    evb = listevt_head;
    listevt_head = evb->next;
    if (listevt_head ==0)
      listevt_tail = 0;
    evb->next = 0;
    /* release lock when writing */
    pthread_mutex_unlock(&mutex_listevt);
  
    err = _kaapi_write_evb(evb);
    if (err) return err;
  
    /* free buffer */
    pthread_mutex_lock(&mutex_listevtfree_head);
    evb->next = listevtfree_head;
    listevtfree_head = evb;
    pthread_mutex_unlock(&mutex_listevtfree_head);
    
    pthread_mutex_lock(&mutex_listevt);
  }
  pthread_mutex_unlock(&mutex_listevt);
  return err;
}


/* infinite loop to write generated buffer
*/
static void* _kaapi_event_flushimator(void* arg)
{
printf("Pthread %s started\n", __FUNCTION__);
  kaapi_event_buffer_t* evb;
  while (1)
  {    
    pthread_mutex_lock(&mutex_listevt);
    while (listevt_head ==0)
    {
      if (finalize_flushimator)
        goto exit_fromterm;
      pthread_cond_wait(&signal_thread, &mutex_listevt);
    }
    /* pick up atomically */
    evb = listevt_head;
    listevt_head = evb->next;
    if (listevt_head ==0)
      listevt_tail = 0;
    evb->next = 0;

    /* release lock when writing */
    pthread_mutex_unlock(&mutex_listevt);

//printf("%i:: write buffer @:%p\n", evb->ident, (void*)evb);
    _kaapi_write_evb(evb);
    
    /* free buffer */
    pthread_mutex_lock(&mutex_listevtfree_head);
    evb->next = listevtfree_head;
    listevtfree_head = evb;
    pthread_mutex_unlock(&mutex_listevtfree_head);    
  }

exit_fromterm:
  pthread_mutex_unlock(&mutex_listevt);
  return 0;
}


/**
*/
kaapi_event_buffer_t* kaapi_event_openbuffer(int kid, unsigned int ptype)
{
  kaapi_event_buffer_t* evb = (kaapi_event_buffer_t*)malloc(sizeof(kaapi_event_buffer_t));
  evb->pos   = 0;
  evb->next  = 0;
  evb->kid   = kid;
  evb->ptype = ptype;
  return evb;
}


/**
*/
kaapi_event_buffer_t* kaapi_event_flushbuffer( kaapi_event_buffer_t* evb )
{
  if (evb ==0) return 0;

  /* push buffer in listevt buffer list */
  int tosignal;
  int kid   = evb->kid;
  int ptype = evb->ptype;
  pthread_mutex_lock(&mutex_listevt);
  evb->next = 0;
  if (listevt_tail !=0)
  {
    listevt_tail->next = evb;
    tosignal = 0;
  }
  else { /* signal if list was empty */
    listevt_head = evb;
    tosignal = 1;
  }
  listevt_tail = evb;
//printf("%i:: flush buffer @:%p\n", kid, (void*)evb);
  if (tosignal) 
    pthread_cond_signal(&signal_thread);
  pthread_mutex_unlock(&mutex_listevt);

  /* alloc new buffer if empty free list */
  if (listevtfree_head ==0)
  {
    evb = (kaapi_event_buffer_t*)malloc(sizeof(kaapi_event_buffer_t));
    //printf("%i:: alloc buffer @:%p\n", kid, (void*)evb);
  } 
  else 
  {
    pthread_mutex_lock(&mutex_listevtfree_head);
    if (listevtfree_head ==0)
      evb = (kaapi_event_buffer_t*)malloc(sizeof(kaapi_event_buffer_t));
    else {
      evb = listevtfree_head;
      listevtfree_head = evb->next;
    }
    //printf("%i:: reopen buffer @:%p\n", kid, (void*)evb);
    pthread_mutex_unlock(&mutex_listevtfree_head);    
  }

  evb->next  = 0;
  evb->pos   = 0;
  evb->kid   = kid;
  evb->ptype = ptype;
  return evb;
}


/*
*/
void kaapi_event_closebuffer( kaapi_event_buffer_t* evb )
{
  if (evb ==0) return;
  int tosignal;

  pthread_mutex_lock(&mutex_listevt);
  evb->next = 0;
  if (listevt_tail !=0)
  {
    listevt_tail->next = evb;
    tosignal = 0;
  }
  else {
    listevt_head = evb;
    tosignal = 1;
  }
  listevt_tail = evb;
//printf("%i:: close buffer @:%p\n", evb->ident, (void*)evb);
  if (tosignal)
    pthread_cond_signal(&signal_thread);
  pthread_mutex_unlock(&mutex_listevt);
}


/**
*/
void kaapi_eventrecorder_init(void)
{
  int i;
  kaapi_event_startuptime = 0;
  kaapi_event_startuptime = kaapi_event_date();
  
  for (i=0; i<KAAPI_MAX_PROCESSOR; ++i)
    listfd_setcontainer[i] = -1;

  pthread_mutex_init(&mutex_listevt, 0);
  pthread_mutex_init(&mutex_listevtfree_head, 0);
  pthread_cond_init(&signal_thread, 0);
  
  /* */
  finalize_flushimator = 0;
  pthread_create(&collector_threadid, 0, _kaapi_event_flushimator, 0);
}


/** Finish trace. Assume that threads have reach the barrier and have flush theirs buffers
*/
void kaapi_eventrecorder_fini(void)
{
  void* result;
  int i;
  kaapi_event_buffer_t* evb;

  pthread_mutex_lock(&mutex_listevt);
  finalize_flushimator = 1;
  pthread_cond_signal(&signal_thread);
  pthread_mutex_unlock(&mutex_listevt);

  /* wait terminaison of the collector thread */
  pthread_join(collector_threadid, &result);
  
  /* flush remains buffer */
  pthread_mutex_lock(&mutex_listevt);
  while (listevt_head !=0)
  {
    evb = listevt_head;
    listevt_head = evb->next;
    if (listevt_head ==0)
      listevt_tail = 0;
    evb->next = 0;
    _kaapi_write_evb(evb);
    free(evb);
  }
  pthread_mutex_unlock(&mutex_listevt);
    
  /* close all file descriptors */
  for (i=0; i<KAAPI_MAX_PROCESSOR; ++i)
    if (listfd_setcontainer[i] != -1)
    {
      _kaapi_write_header(i-KAAPI_SHIFT_PROCESSOR);
      close(listfd_setcontainer[i]);
    }

  /* destroy mutexes/conditions */
  pthread_cond_destroy(&signal_thread);
  pthread_mutex_destroy(&mutex_listevt);
  pthread_mutex_destroy(&mutex_listevtfree_head);
}

#if defined(__cplusplus)
}
#endif
