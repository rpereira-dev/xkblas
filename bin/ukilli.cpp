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
      std::cout << " @src:" << KAAPI_EVENT_DATA(event,0,p)
                << ",@dest:" << KAAPI_EVENT_DATA(event,1,p)
                << ",size:" << KAAPI_EVENT_DATA(event,2,u)
                << ", dir:" << DIRTABLE[KAAPI_EVENT_DATA(event,3,i8)[0]];
      break;
      
    case KAAPI_EVT_OFFLOAD_KERN:
      std::cout << " @task:" << KAAPI_EVENT_DATA(event,0,p)
                << ", fmtid:" << KAAPI_EVENT_DATA(event,1,u)
                << ", @arg:" << KAAPI_EVENT_DATA(event,2,p);
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

    default:
      printf("***Unkown event number: %i\n", event->evtno);
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
