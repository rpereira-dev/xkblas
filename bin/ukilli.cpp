/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012 INRIA.
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
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

#define __STDC_FORMAT_MACROS 
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <queue>
#include <list>
#include <set>
#include <map>
#include <stack>
#include "kaapi.h"
#include "kaapi_trace_reader.h"
#include "kaapi_trace_util.h"



/* Type of function to process data
*/
typedef void (*kaapi_fnc_event)( int, const char** );

#define DOT_OPTIONS_NODATA     0x1
#define DOT_OPTIONS_NOLABEL    0x2
#define DOT_OPTIONS_CREGION    0x4

struct katracereader_options {
  std::string output; /* output file */
  unsigned int stealevent;
  unsigned int gputrace;
  unsigned int gputransfer;
  unsigned int dotoption;         /* 0x1: no data, 0x2: no label, 0x4: one per //region */
  unsigned int vitecompatibility; /* 1 if uses ViTE, 0 for Paje */
  unsigned int task_filter_count;
  uint64_t*    fmtid_values;

  katracereader_options(): 
    output(""), 
    stealevent(0), 
    gputrace(0), 
    gputransfer(0),
    dotoption(0),
    vitecompatibility(0),
    task_filter_count(0),
    fmtid_values(0)
  {}
};



typedef struct katracereader_options katracereader_options_t;

katracereader_options_t katracereader_options;


/*
*/
static std::string Binary(uint64_t value)
{
	char digits[] = "0123456789";
	std::string output;
  do {
    output = digits[value % 2] + output;
    value /= 2;
  } while(value !=0);
	return output;
}


/*
*/
kaapi_eventfile_header_t print_header;

static void supp_eol( char* buffer, int size, const char* name)
{
  while ((size >0) && (*name != 0))
  {
    char c = *name++;
    if (c == '\n') c = ' ';
    *buffer++ = c;
    --size;
  }
  *buffer = 0;
}

static void print_traceheader(FileSet* fs)
{
  if (GetHeader(fs, &print_header) ==0)
  {
    int cnt;
    kaapi_assert( print_header.trace_version == __KAAPI_TRACE_VERSION__ );

    std::cout << "Kaapi version number :" << print_header.version << std::endl;
    std::cout << "Kaapi minor version  :" << print_header.minor_version << std::endl;
    std::cout << "Kaapi package        :" << print_header.package << std::endl;
    std::cout << "Trace format version :" << print_header.trace_version << std::endl;
    std::cout << "Hostname             :" << print_header.hostname << std::endl;
    std::cout << "Kaapi cpucount used  :" << print_header.cpucount << std::endl;
    std::cout << "Kaapi gpucount used  :" << print_header.gpucount << std::endl;
    std::cout << "GPUSET used          :" << Binary(print_header.gpuset) << std::endl;
    std::cout << "#K/H2D/D2H/D2D       :" << (int)print_header.s_kern << '/' 
                                          << (int)print_header.s_h2d << '/' 
                                          << (int)print_header.s_d2h << '/' 
                                          << (int)print_header.s_d2d << std::endl;
    std::cout << "Event mask           :" << Binary(print_header.event_mask) << ", ";
    uint64_t b = print_header.event_mask;
    const char* sep="  ";
    for (int i=0; i<KAAPI_EVT_LAST; ++i)
    {
       if (b & (1UL<<i)) 
       { 
         std::cout << sep << kaapi_event_name[i];
         sep="| ";
       }
    } 
    std::cout << std::endl;
    std::cout << "Event clock          :" << print_header.event_date_unit << std::endl;
    std::cout << "Perfctr mask         :" << Binary(print_header.perf_mask) << std::endl;
    std::cout << "Perfctr task mask    :" << Binary(print_header.task_perf_mask) << std::endl;
    std::cout << "Perfctr uncore mask  :" << Binary(print_header.uncore_perf_mask) << std::endl;

#if KAAPI_USE_PERFCOUNTER==1
    /* register : max perf counter in the low 8 bits, base for papi counter in bit 8-15 */
    int count_perfcounter = print_header.perfcounter_count & 0xFF;
    int papi_perfcounter __attribute__((unused)) = (print_header.perfcounter_count >> 8) & 0xFF ;

    std::cout << "Perfcounter/Proc (#papi:" << papi_perfcounter << "):" << std::endl;
    for (cnt=0; cnt<count_perfcounter; ++cnt)
    {
      if (kaapi_perf_idset_test(&print_header.perf_mask, cnt))
        std::cout << "\t" << (cnt < 10 ? " ":"") << "[" << cnt << "]: " << print_header.perfcounter_name[cnt] << std::endl;
    }

    std::cout << "Perfcounter/Task (#papi:" << papi_perfcounter << "):" << std::endl;
    for (cnt=0; cnt<count_perfcounter; ++cnt)
    {
      if (kaapi_perf_idset_test(&print_header.task_perf_mask, cnt))
        std::cout << "\t" << (cnt < 10 ? " ":"") << "[" << cnt << "]: " << print_header.perfcounter_name[cnt] << std::endl;
    }

    std::cout << "Perfcounter/Uncore:" << std::endl;
    for (cnt=0; cnt<count_perfcounter; ++cnt)
    {
      if (kaapi_perf_idset_test(&print_header.uncore_perf_mask, cnt))
        std::cout << "\t" << (cnt < 10 ? " ":"") << "[" << cnt << "]: " << print_header.perfcounter_name[cnt] << std::endl;
    }
#endif

    std::cout << "#Task types          :" << print_header.taskfmt_count << std::endl;
    for (cnt=0; cnt<print_header.taskfmt_count; ++cnt)
      if (print_header.fmtdefs[cnt].fmtid !=0)
      {
        char buffer[128];
        supp_eol( buffer, 128, print_header.fmtdefs[cnt].name);
        std::cout << "\t[" << print_header.fmtdefs[cnt].fmtid << "]: " << buffer
                  << ", color:" << print_header.fmtdefs[cnt].color << std::endl;
      }
  }
}

static inline const char* int2affinitykind(uint8_t v)
{
  switch (v) {
    case 0: return "no";
    case 1: return "data";
    case 2: return "node";
    case 3: return "core";
    default: return "invalid";
  }
}

static const char* DIRTABLE[] = {
  "H2H",
  "H2D",
  "D2H",
  "D2D"
};

static const char* PTYPETABLE[] = {
  "HOST",
  "GPU",
  "HIP",
  "INTERNAL"
};

/* Print human readable version of each event */
static void callback_print_event(
  void* context,
  char* container_name,
  const kaapi_event_t* event
)
{
  char buffer[128];
  kaapi_event_get_name( event->evtno, event->kind, buffer, 128);
  std::cout << event->date << ": " << event->kid << " -> " << (int)event->evtno << "=" << buffer << " ";
  switch (event->evtno) {
    case KAAPI_EVT_KPROC:
    {
      int ptype = KAAPI_EVENT_DATA(event,0,i);
      std::cout << "ptype:" << PTYPETABLE[ptype] << ", core:" << KAAPI_EVENT_DATA(event,1,u);
    } break;

    case KAAPI_EVT_KPROC_INFO:
      break;
    
    case KAAPI_EVT_TASK_EXEC:
      std::cout << " @task:" << KAAPI_EVENT_DATA(event,0,p)
                << ", fmtid:" << KAAPI_EVENT_DATA(event,1,u)
                << ", @arg:" << KAAPI_EVENT_DATA(event,2,p);
      break;

    case KAAPI_EVT_TASK_INFO:
      break;

    case KAAPI_EVT_TASK_USERATTR:
      break;
      
    case KAAPI_EVT_SCHED:
      break;

    case KAAPI_EVT_STEAL_REQUEST:
      break;
    
    case KAAPI_EVT_OFFLOAD_CPY:
      if (event->kind==0)
        std::cout << " id:" << KAAPI_EVENT_DATA(event,0,u)
                  << " @src:" << KAAPI_EVENT_DATA(event,1,i32)[0]
                  << ",@dest:" << KAAPI_EVENT_DATA(event,1,i32)[1]
                  << ",size:" << KAAPI_EVENT_DATA(event,2,u)
                  << ", dir:" << DIRTABLE[KAAPI_EVENT_DATA(event,3,i8)[0]]
                  << ", stream:" << (int)KAAPI_EVENT_DATA(event,3,i8)[1];
      else
        std::cout << " id:" << KAAPI_EVENT_DATA(event,0,u);
      break;
      
    case KAAPI_EVT_OFFLOAD_KERN:
      if (event->kind==0)
        std::cout << " taskid:" << KAAPI_EVENT_DATA(event,0,u)
                  << ", @task:" << KAAPI_EVENT_DATA(event,1,p)
                  << ", fmtid:" << KAAPI_EVENT_DATA(event,2,u)
                  << ", stream:" << (int)KAAPI_EVENT_DATA(event,3,i8)[0];
      else
        std::cout << " taskid:" << KAAPI_EVENT_DATA(event,0,u);
      break;
          
    case KAAPI_EVT_TASKSYNC:
      break;

    case KAAPI_EVT_PERFCOUNTER:
      std::cout << " id:" << KAAPI_EVENT_DATA(event,0,u)
#if KAAPI_USE_PERFCOUNTER==1
                << "<" << kaapi_tracelib_perfid_to_name((unsigned int)KAAPI_EVENT_DATA(event,0,u))
                << ">, value: " << KAAPI_EVENT_DATA(event,1,u);
#else
                ;
#endif
      break;

    case KAAPI_EVT_TASK_PERFCOUNTER:
      std::cout << "@task:" << KAAPI_EVENT_DATA(event,0,p);
      for (int i=0; i<2; ++i)
      {
        if (KAAPI_EVENT_DATA(event,1,i8)[i] == (uint8_t)-1) break;
        std::cout << ", cntidx:" << (short)KAAPI_EVENT_DATA(event,1,i8)[i] << ", value:" << event->u.data[i+2].u;
      }
    break;

    case KAAPI_EVT_PERF_UNCORE:
      std::cout << "socket:" << KAAPI_EVENT_DATA(event,0,i);
      for (int i=0; i<2; ++i)
      {
        if (KAAPI_EVENT_DATA(event,1,i8)[i] == (uint8_t)-1) break;
        std::cout << ", cntidx:" << (short)KAAPI_EVENT_DATA(event,1,i8)[i] << ", value:" << event->u.data[i+2].u;
      }
    break;
    
    case KAAPI_EVT_CALL:
      if (event->kind==0)
        std::cout << " name:" << KAAPI_EVENT_DATA(event,0,c8)
                  << ", d1:" << KAAPI_EVENT_DATA(event,1,u)
                  << ", d2:" << KAAPI_EVENT_DATA(event,2,u)
                  << ", d3:" << KAAPI_EVENT_DATA(event,3,u);
      else
        std::cout << " d0:" << KAAPI_EVENT_DATA(event,0,u)
                  << ", d1:" << KAAPI_EVENT_DATA(event,1,u)
                  << ", d2:" << KAAPI_EVENT_DATA(event,2,u)
                  << ", d3:" << KAAPI_EVENT_DATA(event,3,u);
     break;

    default:
      printf("***Unknown event number: %i\n", event->evtno);
      break;
  }
  std::cout << std::endl;
}


/*
*/
static void fnc_print_header( int count, const char** filenames )
{
  FileSet* fs;
  fs = OpenFiles( count, filenames );
  print_traceheader(fs);
  CloseFiles(fs);
}


/*
*/
static void fnc_print_evt( int count, const char** filenames )
{
  FileSet* fs;
  fs = OpenFiles( count, filenames );
  print_traceheader(fs);
  ReadFiles(fs, 0, callback_print_event );
  CloseFiles(fs);
}


/*
 * Data structure for CVS output
 */
/*
*/
struct access_t {
  access_t(kaapi_access_mode_t m, uint64_t p, int nu=-1)
   : mode(m), ptr(p), size((uint64_t)-1), numaid(nu)
  {}
  access_t(kaapi_access_mode_t m, uint64_t p, uint64_t s, int nu=-1)
   : mode(m), ptr(p), size(s), numaid(nu)
  {}
  kaapi_access_mode_t mode;
  uint64_t            ptr;
  uint64_t            size;
  int                 numaid;
};

/* */
struct perfctr_t {
  perfctr_t( uint8_t i, kaapi_perf_counter_t v )
   : idx(i), value(v)
  {}
  uint8_t              idx;
  kaapi_perf_counter_t value;
};

/* */
struct event_t {
  event_t() : t_push(0), t_start(0) {}
  event_t(uint64_t s) : t_push(0), t_start(s) {}
  uint64_t              t_push;  // date where event is pushed into stream
  uint64_t              t_start; // date where event is trigger
};

/* */
struct state_t : public event_t {
  state_t() : event_t(), t_stop(0) {}
  uint64_t delay() const {
    if (t_stop < t_start) return (uint64_t)-1;
    return t_stop - t_start;
  }
  uint64_t               t_stop;
};

/* */
struct task_info : public state_t {
  task_info( )
    : state_t(), t_delay(0), kid(0), fmtid(0), arg(0), param(), perfctr()
  {
  }
  uint64_t               t_async;
  uint64_t               t_delay;
  int                    kid;
  uint64_t               task;
  uint64_t               fmtid;
  uint64_t               arg;
  std::vector<access_t>  param;
  std::vector<perfctr_t> perfctr;
};

/*
*/
struct task_cpy : public state_t {
  task_cpy( )
    : state_t(), src_kid(0), dest_kid(0), size(0), dir(0)
  {
  }
  uint64_t               t_delay;
  int                    kid;
  uint64_t               task;
  int                    src_kid;
  int                    dest_kid;
  uint64_t               size;
  uint8_t                dir;
  uint8_t                stream;
};

/*
*/
struct task_ker : public state_t {
  task_ker( )
    : state_t()
  {
  }
  uint64_t               t_delay;
  int                    kid;
  uint64_t               taskid;
  uint64_t               task;
  uint64_t               fmtid;
  uint8_t                stream;
};

/*
*/
struct call_info : public state_t {
  call_info( )
    : state_t(), nparam(0)
  {
  }
  int                    kid;
  int                    level;
  char                   name[8];
  uint64_t               param[16];
  uint8_t                nparam;
};


/* */
struct kproc_t : public state_t {
  kproc_t() : state_t(), kid(-1), type(-1), stack() {}
  kproc_t(int k, int t) : state_t(), kid(k), type(t), stack() {}
  int kid;
  int type;
  std::stack<task_info*> stack;
};


/* */
struct parallel_region_t {
  parallel_region_t()
   : fout(0), nproc(0)
  {}
  virtual ~parallel_region_t() 
  {}

  FILE* fout;
  char filename[128];
  int nproc;
  
  std::map<uint64_t,kproc_t>      container_kproc;  // all K-proc
  std::map<uint64_t,task_info*>   container_task;   // all tasks
  std::map<uint64_t,task_cpy*>    container_task_cpy;   // all task_cpy
  std::map<uint64_t,task_ker*>    container_task_ker;   // all tasks
  std::map<uint64_t,std::list<call_info*> > container_call_info;
  std::map<uint64_t,uint64_t> container_translatetaskaddr;
  
  virtual void flush( kproc_t* ) {};
  virtual void flush( task_info* ) {};
  virtual void flush( task_cpy* ) {};
  virtual void flush( task_ker* ) {};
  virtual void flush( call_info* ) {};

  virtual int openfile(kaapi_eventfile_header_t* header) = 0;
  virtual int closefile(int cpucount) = 0;
};

/* Gobal
*/
FileSet* all_fs;
static kaapi_eventfile_header_t file_header;
static std::map<uint64_t, char*> all_fmtname;
static parallel_region_t* the_parallel_region;

/*
*/
static void callback_main(
  void* context,
  char* name,
  const kaapi_event_t* event
)
{
  int8_t evtno = event->evtno;
  int8_t kind  = event->kind;
  uint16_t kid = event->kid;
  
  if (event->date ==0)
  {
    fprintf(stdout,"***%i:: Bad date: %" PRIu64 "\n", kid, event->date);
    return;
  }
  switch(evtno) {
    case KAAPI_EVT_KPROC:
    {
      int type = KAAPI_EVENT_DATA(event,0,i); /* 0(KAAPI_PROC_TYPE_HOST): CPU, 1: GPU, 2: HIP, 3: Internal*/
      if ((type <0)||(type>KAAPI_PROC_TYPE_INTERNAL))
      {
          printf("***Unknown type of kproc: %i.\n", type);
          break;
      }
      if (kind==0)
      { /* kproc start */
        if (the_parallel_region->container_kproc.find(kid) != the_parallel_region->container_kproc.end())
        {
          printf("***Bad kproc start event: kid: %i. Already started !\n", kid);
          break;
        }
        else
        {
          kproc_t kp(kid, type);
          kp.t_start = event->date;
          the_parallel_region->container_kproc.insert( std::make_pair( kid, kp ) );
        }
      }
      else if (kind==1) {
        /* kproc stop */
        std::map<uint64_t,kproc_t>::iterator iter = the_parallel_region->container_kproc.find(kid);
        if (iter == the_parallel_region->container_kproc.end())
        {
          printf("***Unknown kproc: kid: %i.\n", kid);
          break;
        }
        else
        {
          iter->second.t_stop = event->date;
          the_parallel_region->flush( &iter->second );
        }
      }
    } break;

    case KAAPI_EVT_TASK_EXEC:
    {
      uint64_t task = KAAPI_EVENT_DATA(event,0,u);
      std::map<uint64_t,task_info*>::iterator iter = the_parallel_region->container_task.find(task);
      task_info* ti = 0;
      if (iter != the_parallel_region->container_task.end())
        ti = iter->second;
      if (kind==0)
      { /* push */
        if (iter != the_parallel_region->container_task.end())
        {
          printf("***Bad Push of new task: %p. Already exist !\n", (void*)task);
          break;
        }
        ti = new task_info;
        uint64_t fmtid = KAAPI_EVENT_DATA(event,1,u);
        uint64_t arg = KAAPI_EVENT_DATA(event,2,u);
        ti->t_push = event->date;
        ti->kid  = kid;
        ti->task = task;
        the_parallel_region->container_task.insert(std::make_pair(task, ti));
        ti->fmtid = fmtid;
        ti->arg = arg;
      }
      else if (kind==1)
      { /* async start */
        if (iter == the_parallel_region->container_task.end())
        { printf("***Unknown task: %p!\n", (void*)task); break; }
        ti->t_async = event->date;
      }
      else if (kind==2)
      { /* start */
        if (iter == the_parallel_region->container_task.end())
        { printf("***Unknown task: %p!\n", (void*)task); break; }
        ti->t_start = event->date;
      }
      else if (kind==3)
      { /* stop */
        if (iter == the_parallel_region->container_task.end())
        { printf("***Unknown task: %p!\n", (void*)task); break; }
        ti->t_stop = event->date;
        ti->t_delay = KAAPI_EVENT_DATA(event,3,u);
        the_parallel_region->flush( ti );
        the_parallel_region->container_task.erase( task );
      }
    } break;

    case KAAPI_EVT_TASK_INFO:
    {
      if (kind==0)
      {
      }
      else if (kind==1)
      {
      }
    } break;
    case KAAPI_EVT_TASK_USERATTR:
    {
      if (kind==0) {
      }
      else if (kind==1) {
      }
      else if (kind==2) {
      }
      else if (kind==3) {
      }
    } break;
    case KAAPI_EVT_SCHED:
    {
      if (kind==0)
      {
      }
      else
      {
      }
    } break;
    case KAAPI_EVT_STEAL_REQUEST:
    {
      if (kind==0) {
      }
      else if (kind==1) {
      }
    } break;

    case KAAPI_EVT_OFFLOAD_CPY:
    {
      uint64_t taskid = KAAPI_EVENT_DATA(event,0,u);

      std::map<uint64_t,task_cpy*>::iterator iter = the_parallel_region->container_task_cpy.find(taskid);
      task_cpy* ti = 0;
      if (iter != the_parallel_region->container_task_cpy.end())
        ti = iter->second;
      if (kind==0)
      { /* push */
        if (iter != the_parallel_region->container_task_cpy.end())
        {
          printf("***Bad Push of new task cpy: %p. Already exist !\n", (void*)taskid);
          break;
        }
        ti = new task_cpy;
        int src_kid   = KAAPI_EVENT_DATA(event,1,i32)[0];
        int dest_kid  = KAAPI_EVENT_DATA(event,1,i32)[1];
        uint64_t size = KAAPI_EVENT_DATA(event,2,u);
        int dir       = KAAPI_EVENT_DATA(event,3,i8)[0];
        int stream    = KAAPI_EVENT_DATA(event,3,i8)[1];
        ti->t_push    = event->date;
        ti->kid       = kid;
        ti->task      = taskid;
        ti->src_kid   = src_kid;
        ti->dest_kid  = dest_kid;
        ti->size      = size;
        ti->dir       = dir;
        ti->stream    = stream;
        the_parallel_region->container_task_cpy.insert(std::make_pair(taskid, ti));
      }
      else if (kind==1)
      { /* begin */
        if (iter == the_parallel_region->container_task_cpy.end())
        { printf("***Unkown task cpy: %p!\n", (void*)taskid); break; }
        ti->t_start = event->date;
      }
      else if (kind==2)
      { /* end */
        if (iter == the_parallel_region->container_task_cpy.end())
        { printf("***Unkown task cpy: %p!\n", (void*)taskid); break; }
        ti->t_stop = event->date;
        ti->t_delay = KAAPI_EVENT_DATA(event,1,u);
        the_parallel_region->flush( ti );
        the_parallel_region->container_task_cpy.erase( taskid );
      }
    } break;

    case KAAPI_EVT_OFFLOAD_KERN:
    {
      uint64_t taskid  = KAAPI_EVENT_DATA(event,0,u);

      std::map<uint64_t,task_ker*>::iterator iter = the_parallel_region->container_task_ker.find(taskid);
      task_ker* ti = 0;
      if (iter != the_parallel_region->container_task_ker.end())
        ti = iter->second;
      if (kind==0)
      { /* push */
        if (iter == the_parallel_region->container_task_ker.end())
        {
          ti = new task_ker;
          the_parallel_region->container_task_ker.insert(std::make_pair(taskid, ti));
        }
        uint64_t task  = KAAPI_EVENT_DATA(event,1,u);
        uint64_t fmtid = KAAPI_EVENT_DATA(event,2,u);
        int stream     = KAAPI_EVENT_DATA(event,3,i8)[0];
        ti->t_push     = event->date;
        ti->kid        = kid;
        ti->taskid     = taskid;
        ti->task       = task;
        ti->fmtid      = fmtid;
        ti->stream     = stream;
        //printf("Insert: taskid:%i\n", taskid );
      }
      else if (kind==1)
      { /* begin */
        if (iter == the_parallel_region->container_task_ker.end())
        {
          ti = new task_ker;
          ti->t_push =0;
          the_parallel_region->container_task_ker.insert(std::make_pair(taskid, ti));
        }
        ti->t_start = event->date;
      }
      else if (kind==2)
      { /* end */
        if (iter == the_parallel_region->container_task_ker.end())
        { fprintf(stdout, "***%i::%" PRIu64 " [end] Unknown task ker: %p\n", kid, event->date, (void*)taskid); break; }
        ti->t_stop = event->date;
        ti->t_delay = KAAPI_EVENT_DATA(event,1,u);
        the_parallel_region->flush( ti );
        the_parallel_region->container_task_ker.erase( taskid );
        //printf("Erase: taskid:%i\n", taskid );
      }
    } break;

    case KAAPI_EVT_TASKSYNC:
    {
      if (kind==0)
      { /* begin */
      }
      else if (kind==1)
      { /* flush all call_info with t_stop */
        int i=0;
        std::list<call_info*>& l = the_parallel_region->container_call_info[kid];
        for (std::list<call_info*>::iterator iter = l.begin(); iter != l.end(); ++iter, ++i)
        {
           (*iter)->t_stop = event->date; 
           (*iter)->level = i;
           the_parallel_region->flush( *iter );
        }
        l.clear();
      }
    } break;

    case KAAPI_EVT_PERFCOUNTER:
    case KAAPI_EVT_TASK_PERFCOUNTER:
    case KAAPI_EVT_PERF_UNCORE:
    {
      if (kind==0)
      { /* begin */
      }
      else if (kind==1)
      { /* end */
      }
      else if (kind==2)
      { /* switch */
      }
    } break;
    
    case KAAPI_EVT_CALL:
    { 
      call_info* ci = 0;
      if (kind==0)
      { /* begin */
        const char* name  = KAAPI_EVENT_DATA(event,0,c8);
        ci = new call_info;
        ci->t_start    = event->date;
        ci->t_stop     = 0;
        ci->kid        = kid;
        bzero(ci->name, 8); 
        strncpy(ci->name, KAAPI_EVENT_DATA(event,0,c8),7);
        ci->param[0]   = KAAPI_EVENT_DATA(event,1,u);
        ci->param[1]   = KAAPI_EVENT_DATA(event,2,u);
        ci->param[2]   = KAAPI_EVENT_DATA(event,3,u);
        ci->nparam     = 3;
        the_parallel_region->container_call_info[kid].push_back( ci );
      }
      else if (kind==1)
      { /* end */ 
      }
      else 
      { /* info kind=2 or 3 */
        ci = the_parallel_region->container_call_info[kid].back();
        ci->param[ci->nparam++]   = KAAPI_EVENT_DATA(event,0,u);
        ci->param[ci->nparam++]   = KAAPI_EVENT_DATA(event,1,u);
        ci->param[ci->nparam++]   = KAAPI_EVENT_DATA(event,2,u);
        ci->param[ci->nparam++]   = KAAPI_EVENT_DATA(event,3,u);
      }
    } break;


    {
      if (kind==0)
      { /* begin */
      }
      else if (kind==1)
      { /* end */
      }
    } break;

    case KAAPI_EVT_LOOP:
      break;
    case KAAPI_EVT_ENERGY:
      break;
  };
  
}


/* More specific to CSV:
*/
struct csv_parallel_region_t : public parallel_region_t {
  csv_parallel_region_t()
    : parallel_region_t()
  {}

  void flush( kproc_t* );
  void flush( task_info* );
  void flush( task_cpy* );
  void flush( task_ker* );
  void flush( call_info* );

  int openfile(kaapi_eventfile_header_t* header);
  int closefile(int cpucount);
public:
  static FILE* fout_parallel;
  static FILE* fout_task;
  static FILE* fout_cpy;
  static FILE* fout_kern;
  static FILE* fout_call;
};

FILE* csv_parallel_region_t::fout_parallel = 0;
FILE* csv_parallel_region_t::fout_task = 0;
FILE* csv_parallel_region_t::fout_cpy = 0;
FILE* csv_parallel_region_t::fout_kern = 0;
FILE* csv_parallel_region_t::fout_call = 0;

int csv_parallel_region_t::openfile(kaapi_eventfile_header_t* header)
{
  if (csv_parallel_region_t::fout_task ==0)
  {
    char filename[128];
    sprintf(filename, "task.csv");
    csv_parallel_region_t::fout_task = fopen(filename,"w");
    if (csv_parallel_region_t::fout_task ==0)
    {
      fprintf(stderr,"*** Cannot open file '%s'\n",filename);
      exit(-1);
    }
    fprintf(csv_parallel_region_t::fout_task,
      "Resource,Type,Push,Start,End,Duration,Name,TaskId"
    );
#if 0
    if (!kaapi_perf_idset_empty(&rastello_header.task_perf_mask))
    {
      int count_perfcounter = rastello_header.perfcounter_count & 0xFF;
      for (unsigned int i=0; i< count_perfcounter; ++i)
      {
        if (kaapi_perf_idset_test(&rastello_header.task_perf_mask, i))
        {
          fprintf(csv_parallel_region_t::fout_task, ",%s", rastello_header.perfcounter_name[i] );
        }
      }
    }
#endif
    fprintf(csv_parallel_region_t::fout_task, "\n");
  }

  if (csv_parallel_region_t::fout_kern ==0)
  {
    char filename[128];
    sprintf(filename, "kern.csv");
    csv_parallel_region_t::fout_kern = fopen(filename,"w");
    if (csv_parallel_region_t::fout_kern ==0)
    {
      fprintf(stderr,"*** Cannot open file '%s'\n",filename);
      exit(-1);
    }
    fprintf(csv_parallel_region_t::fout_kern,
      "Resource,Type,Stream,Push,Start,End,Duration,Name,TaskId"
    );
#if 0 /* here performance counter from GPU may be added */
#endif
    fprintf(csv_parallel_region_t::fout_kern, "\n");
  }

  if (csv_parallel_region_t::fout_cpy ==0)
  {
    char filename[128];
    sprintf(filename, "cpy.csv");
    csv_parallel_region_t::fout_cpy = fopen(filename,"w");
    if (csv_parallel_region_t::fout_cpy ==0)
    {
      fprintf(stderr,"*** Cannot open file '%s'\n",filename);
      exit(-1);
    }
    fprintf(csv_parallel_region_t::fout_cpy,
      "Resource,Type,Stream,Push,Start,End,Duration,Size,Dir"
    );
#if 0 /* here performance counter from GPU may be added */
#endif
    fprintf(csv_parallel_region_t::fout_cpy, "\n");
  }

  if (csv_parallel_region_t::fout_parallel ==0)
  {
    char filename[128];
    sprintf(filename, "parallel.csv");
    csv_parallel_region_t::fout_parallel = fopen(filename,"w");
    if (csv_parallel_region_t::fout_parallel ==0)
    {
      fprintf(stderr,"*** Cannot open file '%s'\n",filename);
      exit(-1);
    }
    fprintf(csv_parallel_region_t::fout_parallel,
      "Resource,Type,Start,End,Duration,Name"
    );
#if 0 /* here performance counter from GPU may be added */
#endif
    fprintf(csv_parallel_region_t::fout_parallel, "\n");
  }

  if (csv_parallel_region_t::fout_call==0)
  {
    char filename[128];
    sprintf(filename, "call.csv");
    csv_parallel_region_t::fout_call = fopen(filename,"w");
    if (csv_parallel_region_t::fout_call ==0)
    {
      fprintf(stderr,"*** Cannot open file '%s'\n",filename);
      exit(-1);
    }
    fprintf(csv_parallel_region_t::fout_call,
      "Resource,Level,Start,End,Duration,Name,NParam"
    );
    for (int i=0; i<16; ++i)
      fprintf(csv_parallel_region_t::fout_call, ",Param%i",i);

    fprintf(csv_parallel_region_t::fout_call, "\n");
  }


  for (int cnt=0; cnt<header->taskfmt_count; ++cnt)
    if (header->fmtdefs[cnt].fmtid !=0)
    {
      all_fmtname.insert( std::make_pair(header->fmtdefs[cnt].fmtid, header->fmtdefs[cnt].name) );
#if 0
      fprintf(stdout, "insert fmtid: %" PRIu64 " -> name: %s\n", header->fmtdefs[cnt].fmtid, header->fmtdefs[cnt].name);
#endif
    }

  return 0;
}

/* name of type of kproc */
static const char* type_name[] = {
  "CPU",  /* KAAPI_PROC_TYPE_CPU */
  "CUDA", /* KAAPI_PROC_TYPE_CUDA*/
  "HIP",  /* KAAPI_PROC_TYPE_HIP*/
  "INT",  /* KAAPI_PROC_TYPE_INTERNAL*/
};

static inline double ns2s( uint64_t v )
{ return double(v)*1e-9; }

/* "Resource,Type,Start,End,Duration,Name" */
void csv_parallel_region_t::flush( kproc_t* kp)
{
  char name[32];
  snprintf(name,32,"%s-%i",type_name[kp->type], kp->kid);
#if 0
  fprintf(csv_parallel_region_t::fout_parallel,
    "%i,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
    kp->kid, type_name[kp->type], kp->t_start, kp->t_stop, kp->delay(), name 
  );
#else
  fprintf(csv_parallel_region_t::fout_parallel,
    "%i,%s,%.15f,%.15f,%.15f,%s\n",
    kp->kid, type_name[kp->type], ns2s(kp->t_start), ns2s(kp->t_stop), ns2s(kp->delay()), name 
  );
#endif
}

/*  "Resource,Type,Push,Start,End,Duration,Name,TaskId" */
void csv_parallel_region_t::flush( task_info* ti)
{
  const char* name;
  std::map<uint64_t, char*>::iterator iter = all_fmtname.find( ti->fmtid );
  if (iter == all_fmtname.end())
    name = "unkown";
  else
    name = iter->second;

#if 0
  fprintf(csv_parallel_region_t::fout_task,
    "%i,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%"  PRIu64 ",%s,%lu\n",
    ti->kid,type_name[the_parallel_region->container_kproc[ti->kid].type],
    ti->t_push, ti->t_start, ti->t_stop, ti->t_delay, name, ti->task 
  );
#else
  fprintf(csv_parallel_region_t::fout_task,
    "%i,%s,%.15f,%.15f,%.15f,%.15f,%s,%lu\n",
    ti->kid,type_name[the_parallel_region->container_kproc[ti->kid].type],
    ns2s(ti->t_push), ns2s(ti->t_start), ns2s(ti->t_stop), ns2s(ti->t_delay), name, ti->task 
  );
#endif
}

/* "Resource,Type,Stream,Push,Start,End,Duration,Size,Dir" */
void csv_parallel_region_t::flush( task_cpy* ti )
{
  std::map<uint64_t,kproc_t>::iterator iter = the_parallel_region->container_kproc.find(ti->kid);
  if (iter == the_parallel_region->container_kproc.end())
  { fprintf(stderr,"*** warning: unkown kid %i\n", ti->kid); return; }
  int type = iter->second.type;
#if 0
  fprintf(csv_parallel_region_t::fout_cpy,
    "%i,%s,%i,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%"  PRIu64 ",%"  PRIu64 ",%i\n",
    ti->kid,type_name[type], ti->stream,
    ti->t_push, ti->t_start, ti->t_stop, ti->t_delay, ti->size, ti->dir 
  );
#else
  fprintf(csv_parallel_region_t::fout_cpy,
    "%i,%s,%i,%.15f,%.15f,%.15f,%.15f,%"  PRIu64 ",%i\n",
    ti->kid,type_name[type], ti->stream,
    ns2s(ti->t_push), ns2s(ti->t_start), ns2s(ti->t_stop), ns2s(ti->t_delay), ti->size, ti->dir 
  );
#endif
}

/* "Resource,Type,Stream,Push,Start,End,Duration,Name,TaskId" */
void csv_parallel_region_t::flush( task_ker* ti)
{
  const char* name;
  std::map<uint64_t, char*>::iterator iter = all_fmtname.find( ti->fmtid );
  if (iter == all_fmtname.end())
    name = "unkown";
  else
    name = iter->second;

#if 0
  fprintf(csv_parallel_region_t::fout_kern,
    "%i,%s,%i,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%"  PRIu64 ",%s,%lu\n",
    ti->kid,type_name[the_parallel_region->container_kproc[ti->kid].type], ti->stream,
    ti->t_push, ti->t_start, ti->t_stop, ti->t_delay,name, ti->task 
  );
#else
  fprintf(csv_parallel_region_t::fout_kern,
    "%i,%s,%i,%.15f,%.15f,%.15f,%.15f,%s,%lu\n",
    ti->kid,type_name[the_parallel_region->container_kproc[ti->kid].type], ti->stream,
    ns2s(ti->t_push), ns2s(ti->t_start), ns2s(ti->t_stop), ns2s(ti->t_delay),name, ti->task
  );
#endif
}


/* "Resource,Type,Stream,Push,Start,End,Duration,Name,TaskId" */
void csv_parallel_region_t::flush( call_info* ci)
{
#if 0
  fprintf(csv_parallel_region_t::fout_call,
    "%i,%i,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%i",
    ci->kid, ci->level, ci->t_start, ci->t_stop, ci->delay(), ci->name, ci->nparam
  );
#else
  fprintf(csv_parallel_region_t::fout_call,
    "%i,%i,%.15f,%.15f,%.15f,%s,%i",
    ci->kid, ci->level, ns2s(ci->t_start), ns2s(ci->t_stop), ns2s(ci->delay()), ci->name, ci->nparam
  );
#endif
  for (int i=0; i<ci->nparam; ++i)
    fprintf(csv_parallel_region_t::fout_call,",%lu",ci->param[i]);

  for (int i=ci->nparam; i<16; ++i)
    fprintf(csv_parallel_region_t::fout_call,",0");
  fprintf(csv_parallel_region_t::fout_call,"\n");
}




/*
*/
int csv_parallel_region_t::closefile(int cpucount)
{
  return 0;
}


/* Parse the event files to generate CVS version.
*/
static void fnc_csv( int count, const char** filenames )
{
  the_parallel_region = new csv_parallel_region_t;
  all_fs = OpenFiles( count, filenames );
  if (all_fs ==0)
    return;
  if (GetHeader(all_fs, &file_header) !=0)
    return;

  the_parallel_region->openfile( &file_header);

  /* generate dot graph: one per parallel
     region if katracereader_options.dotoption & DOT_OPTIONS_CREGION
  */
  ReadFiles(all_fs, 0, callback_main);
  
  /* close & umap */
  CloseFiles(all_fs);

  the_parallel_region->closefile(0);

  if (csv_parallel_region_t::fout_parallel)
  {
    fclose(csv_parallel_region_t::fout_parallel);
    fprintf(stdout,"*** File 'parallels.csv' generated\n");
    csv_parallel_region_t::fout_parallel = 0;
  }
  if (csv_parallel_region_t::fout_task)
  {
    fclose(csv_parallel_region_t::fout_task);
    fprintf(stdout,"*** File 'task.csv' generated\n");
    csv_parallel_region_t::fout_task = 0;
  }
  if (csv_parallel_region_t::fout_cpy)
  {
    fclose(csv_parallel_region_t::fout_cpy);
    fprintf(stdout,"*** File 'cpy.csv' generated\n");
    csv_parallel_region_t::fout_cpy = 0;
  }
  if (csv_parallel_region_t::fout_kern)
  {
    fclose(csv_parallel_region_t::fout_kern);
    fprintf(stdout,"*** File 'kern.csv' generated\n");
    csv_parallel_region_t::fout_kern = 0;
  }
  if (csv_parallel_region_t::fout_call)
  {
    fclose(csv_parallel_region_t::fout_call);
    fprintf(stdout,"*** File 'call.csv' generated\n");
    csv_parallel_region_t::fout_call = 0;
  }
  all_fmtname.clear();
  delete the_parallel_region;
  the_parallel_region = 0;
}




/*
*/
static void print_usage(const char* msg = 0)
{
  if (msg)
  {
    fprintf(stderr, "*** Error: %s\n", msg );
  }
  fprintf(stderr, "[katracereader] merge and convert internal Kaapi trace format to human readeable formats\n");
  fprintf(stderr, "*** options: Only one of the following options may be selected at a time.\n");
  fprintf(stderr, "  -a | --display-data  : display all data associated to each events.\n");
  fprintf(stderr, "  -e | --display-header: display header of the trace file.\n");
//  fprintf(stderr, "  -m: display the mapping of tasks\n");
//  fprintf(stderr, "  -s: display stats about runtime of all the tasks\n");
//  fprintf(stderr, "  --paje          : output Paje format for Gantt diagram, one row per core.\n");
//  fprintf(stderr, "                    Output filename is paje-gantt.trace.\n");
  fprintf(stderr, "  --vite               : output Paje format compatible with ViTE.\n");
  fprintf(stderr, "                         Output filename is vite-gantt.trace.\n");
  fprintf(stderr, "  --csv                : output csv format about tasks and threads state.\n");
  fprintf(stderr, "  --dot                : output dot format for each parallel region.\n");
  fprintf(stderr, "    --dot-nolabel      : do not output label.\n");
  fprintf(stderr, "    --dot-cregion      : output graph accross parallel regions.\n");
//  fprintf(stderr, "     --dot-nodata : do not output data node.\n");
  fprintf(stderr, "  -s | --somp          : output file with SOMP trace format .\n");
//  fprintf(stderr, "  -r | --rastello      : output Rastello format compatible with CORSE team simulator.\n");
//  fprintf(stderr, "                         Output filename is rastello_<n>.c, one per parallel region.\n");
//  fprintf(stderr, "  --steal-event   : include steal events in trace.\n");
//  fprintf(stderr, "  --gpu-trace     : include GPU trace information.\n");
//  fprintf(stderr, "  --gpu-transfer  : include GPU transfers.\n");
//  fprintf(stderr, "  --stat          : display stats, see documentation.\n");
//  fprintf(stderr, "  --timestep      : simulator, see documentation.\n");
//  fprintf(stderr, "  --perproc       : report info per processor with --stat or --timestep.\n");
//  fprintf(stderr, "  --task          : report task info with --stat & --timestep.\n");
//  fprintf(stderr, "  --tcompute      : report compute time info with --stat & --timestep.\n");
//  fprintf(stderr, "  --tidle         : report idle time info with --stat & --timestep.\n");
//  fprintf(stderr, "  --tstealop      : report steal operation info with --stat & --timestep.\n");
//  fprintf(stderr, "  --tefficiency   : report efficiency with --stat & --timestep.\n");
  fprintf(stderr, "  --tdumptasktime      : dump task time in task.dump file.\n");
  fprintf(stderr, "    --taskfilter <list>: list of task's format id to keep during parsing.\n");
  exit(1);
}


/*
*/
static kaapi_fnc_event parse_option( const int argc, const char** argv, int* count )
{
  char option ='h';
  int i;
  
  for(i= 1; i < argc; i++)
  {
    if ((strcmp(argv[i], "--help") ==0) || (strcmp(argv[i], "-h") ==0))
    {
      option = 'H';
      break; /* end of options */
    }
    else if ((strcmp(argv[i], "--display-data") ==0)||(strcmp(argv[i], "-a") ==0))
      option = 'a';
    else if ((strcmp(argv[i], "--display-header") ==0)||(strcmp(argv[i], "-e") ==0))
      option = 'h';
    else if ((strcmp(argv[i], "--somp") ==0) || (strcmp(argv[i], "-s") ==0))
      option = 'o';
    else if ((strcmp(argv[i], "--rastello") ==0) || (strcmp(argv[i], "-r") ==0))
      option = 'r';
    else if ((strcmp(argv[i], "--csv") ==0) || (strcmp(argv[i], "-c") ==0))
      option = 'c';
    else if (strcmp(argv[i], "--paje") ==0)
    {
      option = 'p';
      katracereader_options.vitecompatibility  = 0;
      katracereader_options.output = "paje-gantt.trace";
    }
    else if (strcmp(argv[i], "--vite") ==0)
    {
      option = 'p';
      katracereader_options.vitecompatibility  = 1;
      katracereader_options.output = "vite-gantt.trace";
    }
    else if (strcmp(argv[i], "--taskfilter") ==0)
    {
      const char* str = argv[++i];
      if (str ==0)
        print_usage("taskfilter requires list of integers");

      bool err = kaapi_parse_list_unsigned_int64 ((char**)&str,
          &katracereader_options.task_filter_count,
          &katracereader_options.fmtid_values);
      if (!err)
        print_usage("incorrect value in taskfilter's list of integers");
    }

    else
      break; /* end of options */
  }
  
  *count = i;
  
  switch (option) {
  case'h':
    return fnc_print_header;

  case 'a':
    return fnc_print_evt;

  case 'c':
    return fnc_csv;

  case 'H':
  default:
    print_usage();
  }
  return 0;
}


/* main entry point : Kaapi initialization
*/
int main(int argc, char** argv)
{
  int count= 0;
  
  if (argc <2)
    print_usage();
  
  kaapi_fnc_event function = parse_option( argc, (const char**)argv, &count );

  if (function == 0)
    return -1;
  if ((argc-count) <= 0)
    print_usage();

//  function( argc-2, (const char**)(argv+2) );
  function( (argc-count), (const char**)(argv+count) );
  
  return 0;
}
