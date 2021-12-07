/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012 INRIA.
**
** Contributors :
**
** Thierry Gautier, thierry.gautier@inrialpes.fr
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
#define __STDC_LIMIT_MACROS 1
#include <stdint.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include <queue>
#include <string>
#include <iostream>

#include "kaapi_trace_reader.h"

/* Reader for one file */
struct file_event {
  int                        fd;
  char                       name[128]; /* container name */
  kaapi_eventfile_header_t*  header;    /* pointer to the header of the file, at least format v1, may be new format */
  kaapi_event_t*             base;      /* base for event */
  size_t                     rpos;      /* next position to read event */
  size_t                     end;       /* past the last position to read event */
  void*                      addr;      /* memory mapped file */
  size_t                     size;      /* file size */
};

/* Compare (less) for priority queue
*/
struct next_event_t {
  next_event_t( uint64_t d=0, int f=0 )
   : date(d), fds(f) 
  {}
  uint64_t date;
  int      fds;  /* index in file_event set */
  
};
struct compare_event {
  bool operator()( const next_event_t& e1, const next_event_t& e2)
  { return e1.date > e2.date; }
};


/* Set of files 
*/
typedef char InternalPerfCtrName[32];
struct FileSet {
  kaapi_eventfile_header_t        header;          /* common header for all files */
  kaapi_event_t                   top_event;
  std::vector<kaapi_fmttrace_def> fmtdefs;  /* fmtdefs definition per file, here merge of all defs*/
  uint64_t                        tmax;
  uint64_t                        tmin;
  int                             perfctrcnt;
  std::vector<std::string>        perfcounter_name;
  std::vector<char*>              perfcounter_name_extC;
  std::vector<std::string>        filenames;
  std::vector<file_event>         fds;
  std::priority_queue<next_event_t,std::vector<next_event_t>,compare_event> eventqueue;
};


/*
*/
FileSet* OpenFiles( int count, const char** filenames )
{
  int err;
  struct stat fd_stat;
  FileSet*    fdset;
    
  fdset = new FileSet; 
  fdset->filenames.resize( count );
  fdset->fds.resize( count );
  fdset->tmin = UINT64_MAX;
  fdset->tmax = 0;
  fdset->perfctrcnt = -1;
  fdset->perfcounter_name.resize(0);
  fdset->perfcounter_name_extC.resize(0);

  /* open all files */
  int c = 0;
  for (int i=0; i<count; ++i)
  {
    fdset->fds[c].fd = open(filenames[i], O_RDONLY);
    if (fdset->fds[c].fd == -1) 
    {
      fprintf(stderr, "*** cannot open file '%s'\n", filenames[i]);
      exit(1);
    }
#if 1
    fprintf(stdout, "*** file '%s'\n", filenames[i]);
#endif
    fdset->filenames[i] = std::string(filenames[i]);
  
    /* memory map the file */
    err = fstat(fdset->fds[c].fd, &fd_stat);
    if (err !=0)
    {
      fprintf(stderr, "*** cannot read information about file '%s'\n", 
          filenames[i]);
      return 0;
    }

    if (fd_stat.st_size ==0) 
      continue;

    fdset->fds[c].base = 0;
    fdset->fds[c].rpos = 0;
    fdset->fds[c].size = fd_stat.st_size;
    fdset->fds[c].addr = (void*)mmap(
          0, 
          fdset->fds[c].size, 
          PROT_READ|PROT_WRITE, 
          MAP_PRIVATE,
          fdset->fds[c].fd,
          0
    );
    if (fdset->fds[c].addr == (void*)-1)
    {
      fprintf(stderr, "*** cannot map file '%s', error=%i, msg=%s\n", 
          filenames[i],
          errno,
          strerror(errno)
      );
      return 0;
    }
    fdset->fds[c].header = (kaapi_eventfile_header_t*)fdset->fds[c].addr;
//printf("Header: kid: %i, numaid: %i\n", fdset->fds[c].header->kid, fdset->fds[c].header->numaid );

    /* warn if __TRACE_VERSION__ is to high */
    if (fdset->fds[c].header->trace_version < __KAAPI_TRACE_VERSION__)
    {
      fprintf(stderr, "*** File '%s' was generated with a Kaapi trace recorder version older than the reader know. Please use the reader version provided with Kaapi for generating the event file(s).\n", filenames[i]);
      fflush(stderr);
      exit(1);
    }
    if (fdset->fds[c].header->trace_version == __KAAPI_TRACE_VERSION__)
    {
      fdset->fds[c].base   = (kaapi_event_t*)(((kaapi_eventfile_header*)fdset->fds[c].header)+1);
      fdset->fds[c].end    = (fdset->fds[c].size-sizeof(kaapi_eventfile_header)) / sizeof(kaapi_event_t);
      int pfcc = ((kaapi_eventfile_header*)fdset->fds[c].header)->perfcounter_count & 0xFF;
      if (fdset->perfctrcnt ==-1)
      {
        fdset->perfctrcnt = pfcc;
        if (pfcc>0) {
          fdset->perfcounter_name.resize(pfcc);
          fdset->perfcounter_name_extC.resize(pfcc);
        }
#if KAAPI_USE_PERFCOUNTER==1
        for (int kk=0; kk<pfcc; ++kk)
        {
#if 0
printf("Header perfctr name: %s\n", ((kaapi_eventfile_header*)fdset->fds[c].header)->perfcounter_name[kk] );
#endif
          fdset->perfcounter_name_extC[kk] = strdup(((kaapi_eventfile_header*)fdset->fds[c].header)->perfcounter_name[kk]);
          fdset->perfcounter_name[kk] = std::string( fdset->perfcounter_name_extC[kk] );
#if 0
std::cout << "In vector = " << fdset->perfcounter_name[kk] << std::endl;
#endif
        }
#endif
      }
      else if (fdset->perfctrcnt != -2)
      {
        if (fdset->perfctrcnt != pfcc)
        {
          fprintf(stderr,"*** Trace file: '%s' does not have same number of performance counters. Do not print them.\n",
            filenames[i]);
          fdset->perfctrcnt = -2;
        }
      }

      /* copy first header */
      if (c==0) fdset->header = *fdset->fds[c].header;
      { /* merge task fmd defs */
//printf("#task fmtdefs:%i\n", fdset->fds[c].header->taskfmt_count );
        for (int kk=0; kk< fdset->fds[c].header->taskfmt_count; ++kk)
        {
          int fmtid = fdset->fds[c].header->fmtdefs[kk].fmtid;
          if (kk >= (int)fdset->fmtdefs.size())
          {
            int size = (int)fdset->fmtdefs.size();
            fdset->fmtdefs.resize( kk+1 );
            for (int ll=size; ll<kk+1; ++ll)
              fdset->fmtdefs[ll].fmtid = 0;
          }
          //if (fdset->fmtdefs[kk].fmtid ==0)
            fdset->fmtdefs[kk] = fdset->fds[c].header->fmtdefs[kk];
        }
      }
    }
    else {
      fprintf(stderr, "*** error: Cannot read this version of event trace\n");
      exit(1);
    }
    
    fdset->fds[c].rpos   = 0;
    fdset->filenames[c]  = filenames[i];

    /* update min/max */
    if (fdset->fds[c].base[0].date < fdset->tmin)
      fdset->tmin = fdset->fds[c].base[0].date;
    if (fdset->tmax < fdset->fds[c].base[fdset->fds[c].end-1].date)
      fdset->tmax = fdset->fds[c].base[fdset->fds[c].end-1].date;

    /* insert date of first event in queue */
    fdset->eventqueue.push( next_event_t(fdset->fds[c].base->date, c) );

    /* */
    ++c;
  }
  
  fdset->fds.resize(c);
  fdset->filenames.resize(c);
  return fdset;
}
  


/* Read and call callback on each event, ordered by date
*/
int ReadFiles(
  FileSet* fdset,
  void* arg,
  void (*callback)( void* arg, char* name, const kaapi_event_t* event)
)
{
  if (callback ==0) return EINVAL;
  
  /* sort loop ! */
  while (!fdset->eventqueue.empty())
  {
    next_event_t ne = fdset->eventqueue.top();
    fdset->eventqueue.pop();
    file_event* fe = &fdset->fds[ne.fds];
    

    /* The container name is passed in/out: event can initialize them */
    callback( arg, fe->name, &fe->base[fe->rpos++] );
    
    if (fe->rpos < fe->end)
      fdset->eventqueue.push( next_event_t(fe->base[fe->rpos].date, ne.fds) );
  }
  return 0;
}


const kaapi_event_t* TopEvent(FileSet* fdset )
{
  if (fdset->eventqueue.empty()) 
    return 0;

  next_event_t ne = fdset->eventqueue.top();
  file_event* fe = &fdset->fds[ne.fds];
  fdset->top_event = fe->base[fe->rpos];
  return &fdset->top_event;
}

int EmptyEvent(FileSet* fdset)
{
  if (fdset->eventqueue.empty()) 
    return 1;
  return 0;
}

void NextEvent(FileSet* fdset)
{
  if (fdset->eventqueue.empty()) 
    return;

  next_event_t ne = fdset->eventqueue.top();
  fdset->eventqueue.pop();
  file_event* fe = &fdset->fds[ne.fds];
  fe->rpos++;
    
  if (fe->rpos < fe->end)
    fdset->eventqueue.push( next_event_t(fe->base[fe->rpos].date, ne.fds) );
}

/* 
*/
int GetProcessorCount(struct FileSet* fdset )
{
  if (fdset == 0) 
    return -1;
  return (int)fdset->fds.size();
}


/*
*/
uint32_t GetProcessorId( struct FileSet* fdset, int i)
{
  if (fdset == 0) 
    return -1;
  if ((i<0) || (i>(int)fdset->fds.size()))
    return -1;
  return fdset->fds[i].header->kid;
}

/* 
*/
int GetPerfCounterCount(struct FileSet* fdset )
{
  if (fdset == 0) 
    return -1;
  return (fdset->perfctrcnt & 0xFF);
}

const char** GetPerfCounterName(struct FileSet* fdset, int* size )
{
  if (fdset == 0) 
    return 0;
#if 0
std::cout << "Return: #perfname:" << fdset->perfcounter_name.size() << std::endl;
  for (unsigned i=0; i<fdset->perfcounter_name.size(); ++i)
{
std::cout << "Return: perfname[" << i << "]=" << fdset->perfcounter_name[i] << std::endl;
}
#endif
  *size = (int)fdset->perfcounter_name_extC.size();
  return (const char**)&fdset->perfcounter_name_extC[0];
}


/*
*/
int GetHeader(struct FileSet* fdset, kaapi_eventfile_header_t* header )
{
  if (fdset == 0) 
    return EINVAL;
  *header = fdset->header;
  /* recopy fmtdefs */
  int cnt = 0;
  for (int i=0; (i<(int)fdset->fmtdefs.size()) && (cnt < KAAPI_FORMAT_MAX); ++i)
    if (fdset->fmtdefs[i].fmtid !=0)
      header->fmtdefs[cnt++] = fdset->fmtdefs[i];
  header->taskfmt_count = cnt;

  return 0;
}

/* Return the [min,max] date value
*/
int GetInterval(struct FileSet* fdset, uint64_t* tmin, uint64_t* tmax )
{
  if (fdset == 0) 
    return EINVAL;
  if (fdset->fds.size() ==0)
    return EINVAL;
  *tmin = fdset->tmin;
  *tmax = fdset->tmax;
  return 0;
}


/* Read and call callback on each event, ordered by date
*/
int CloseFiles(FileSet* fdset )
{
  if (fdset ==0) return EINVAL;

  int count = (int)fdset->fds.size();  
  for (int i=0; i<count; ++i)
  {
    close(fdset->fds[i].fd);
    munmap(fdset->fds[i].addr, fdset->fds[i].size );
  }
  delete fdset;
  return 0;
}

