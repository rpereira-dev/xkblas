/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
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

#include <string.h>
#include <stdio.h>
#include "kaapi_impl.h"

/* TODO: the noprint_data flag does not work.
   - the idea is to link together tasks without intermediate data node.
   - nothing was done.... if set, a set of independant tasks are outputed, which
   is not so convinient.
*/
static int noprint_versionlink = 0;
static int noprint_label = 0;
static int noprint_activationlink __attribute__((unused)) = 0;
static int noprint_label_info = 0;
static int noprint_data = 0;

/*
*/
#define KAAPI_SIZE_DOTCTXT 16

/*
*/
kaapi_hashmap_t          data_info_khm; /* to store name for pointer object (task or data) */
kaapi_hashentries_t*     data_info_mapentries[1<<KAAPI_SIZE_DOTCTXT];


/*
*/
static const char* kaapi_access_mode2string( kaapi_access_mode_t m )
{
  switch (m) {
    case KAAPI_ACCESS_MODE_VOID: return "<void>";
    case KAAPI_ACCESS_MODE_V: return "V";
    case KAAPI_ACCESS_MODE_R: return "R";
    case KAAPI_ACCESS_MODE_W: return "W";
    case KAAPI_ACCESS_MODE_CW: return "CW";
    case KAAPI_ACCESS_MODE_S: return "S";
    case KAAPI_ACCESS_MODE_T : return "T";
    case KAAPI_ACCESS_MODE_P: return "P";
    case KAAPI_ACCESS_MODE_IP: return "IP";
    case KAAPI_ACCESS_MODE_RW: return "RW";
    case KAAPI_ACCESS_MODE_STACK: return "S";
    case KAAPI_ACCESS_MODE_SCRATCH: return "T";
    case KAAPI_ACCESS_MODE_CWP: return "CWP"; 
    case KAAPI_ACCESS_MODE_ICW: return "IWP";    
    case KAAPI_ACCESS_SYNC: return "sync";
  };
  return 0;
}

/**/
static inline void _kaapi_print_data( FILE* file, const void* ptr, unsigned long version)
{
  if (noprint_label)
    fprintf(file,"%lu00%lu [label=\"\", shape=box, style=filled, color=steelblue];\n",
        version, (uintptr_t)ptr
    );
  else
  {
    const char* name = 0;
    kaapi_hashentries_t* entry = kaapi_hashmap_find(&data_info_khm, ptr);
    if (entry !=0)
      name = KAAPI_HASHENTRIES_GET(entry, const char*);
    fprintf(file,"%lu00%lu [label=\"%p v%lu\n%s\", shape=box, style=filled, color=steelblue];\n",
        version, (uintptr_t)ptr, ptr,version-1,name ==0 ? "<no info>": name
    );
  }
}

/**/
static inline void _kaapi_print_write_edge(
    FILE* file,
    const kaapi_task_t* task,
    const void* ptr,
    unsigned long version,
    kaapi_access_mode_t m
)
{
  if (KAAPI_ACCESS_IS_READWRITE(m))
    fprintf(file,"%lu -> %lu00%lu[dir=both,arrowtail=diamond,arrowhead=vee];\n",
        (uintptr_t)task, version, (uintptr_t)ptr );
  else if (KAAPI_ACCESS_IS_CUMULWRITE(m))
  { /* the final version will have tag version+1, not yet generated */
    fprintf(file,"%lu -> %lu00%lu[dir=both,arrowtail=inv,arrowhead=tee];\n",
        (uintptr_t)task, version, (uintptr_t)ptr );
    return;
  }
  else
    fprintf(file,"%lu -> %lu00%lu;\n", (uintptr_t)task, version, (uintptr_t)ptr );

  if (!noprint_versionlink)
  {
    /* add version edge */
    if (version >1)
      fprintf(file,"%lu00%lu -> %lu00%lu [style=dotted];\n",
          version-1, (uintptr_t)ptr, version, (uintptr_t)ptr );
  }

}

/**/
static inline void _kaapi_print_read_edge(
    FILE* file,
    const kaapi_task_t* task,
    const void* ptr,
    unsigned long version,
    kaapi_access_mode_t m
)
{
  if (KAAPI_ACCESS_IS_READWRITE(m))
    fprintf(file,"%lu00%lu -> %lu[dir=both,arrowtail=diamond,arrowhead=vee];\n", version, (uintptr_t)ptr, (uintptr_t)task );
  else
    fprintf(file,"%lu00%lu -> %lu;\n", version, (uintptr_t)ptr, (uintptr_t)task );
}



/** pair of pointer,int
    Used to display tasklist
*/
typedef struct kaapi_pair_print_t {
  void*               ptr;
  int                 tag;
  kaapi_access_mode_t last_mode;
} kaapi_pair_print_t;


/*
*/
static void kaapi_frame_dot_activate_succ_access(
    kaapi_access_t*  sync,
    kaapi_hashmap_t* task_khm,
    kaapi_hashmap_t* readytask_khm
)
{
  kaapi_assert( sync->mode == KAAPI_ACCESS_SYNC );
  kaapi_access_t* access = sync->next;
  while (access !=0)
  {
    int wc;
    kaapi_task_t* next_task = access->task;
    kaapi_hashentries_t* entry = kaapi_hashmap_find(task_khm, next_task);
    if (entry ==0)
    {
      entry = kaapi_hashmap_findinsert(task_khm, next_task);
      kaapi_pair_print_t* pp = KAAPI_HASHENTRIES_GETREF(entry, kaapi_pair_print_t);
      pp->tag = KAAPI_ATOMIC_READ(&next_task->wc);
    }
    kaapi_pair_print_t* pp = KAAPI_HASHENTRIES_GETREF(entry, kaapi_pair_print_t);

    if (pp->tag >0)
    {
#if 0
      fprintf( stdout, "\t[activate???] task:%p name: \"%s\", wc=%i\n",
        (void*)next_task, (next_task->fmt ? next_task->fmt->name : "<undef>"), (int)pp->tag
      );
#endif

      wc = (int)--pp->tag;
    //  kaapi_assert_debug( wc >= 0 );
if (wc <0) wc =0;
      if (wc ==0)
      {
        entry = kaapi_hashmap_insert(readytask_khm, next_task);
#if 0
        fprintf( stdout, "\t=>[activated] task:%p name: \"%s\", wc=%i\n",
          (void*)next_task, (next_task->fmt ? next_task->fmt->name : "<undef>"), (int)pp->tag
        );
#endif
      }
    }
    access = access->next;
  }
}

/*
*/
static void kaapi_frame_dot_activate_succ(
    kaapi_task_t*    task,
    kaapi_hashmap_t* task_khm,
    kaapi_hashmap_t* readytask_khm
)
{
  kaapi_access_mode_t   mode, mode_param;
  kaapi_access_t*       access;
  const kaapi_format_t* fmt;

  unsigned int          i;
  size_t                count_params;

  fmt = kaapi_task_getformat_ref(task);
  kaapi_assert_debug( fmt != 0);
  count_params = kaapi_format_get_count_params( fmt, kaapi_task_getargs(task) );

  for (i=0; i<count_params; ++i)
  {
    mode            = kaapi_format_get_mode_param(fmt, i, kaapi_task_getargs(task));
    mode_param      = KAAPI_ACCESS_GET_MODE( mode );
    if (mode_param & KAAPI_ACCESS_MODE_V)
      continue;

    access = kaapi_format_get_access_param(fmt, i, kaapi_task_getargs(task));    
    kaapi_frame_dot_activate_succ_access(access->sync, task_khm, readytask_khm );
  }
}


/**/
static void _kaapi_dump_dot_visit_task(
  FILE* file,
  kaapi_task_t*    task,
  kaapi_hashmap_t* data_khm,
  kaapi_hashmap_t* task_khm,
  kaapi_hashmap_t* readytask_khm
)
{
  char buffer[128];
  const char* fname = "<empty format>";
  const char* color;
  const char* shape; 
  const kaapi_format_t* fmt = 0;
  kaapi_task_body_t body;

  void* sp = kaapi_task_getargs(task);
  body = task->body;
  fmt = kaapi_task_getformat_ref(task);

  color = fmt->color_dot;
  if (color ==0)
    color = "orange";

  shape = fmt->shape_dot;
  if (shape ==0)
    shape = "ellipse";

  if (body == kaapi_taskmain_body)
  {
      fname = "maintask";
      shape = "doubleoctagon";
  }
  else 
  {
    ssize_t sz = snprintf(buffer, 128, "%s", fmt->name ); 
    if (fmt->get_name)
      fmt->get_name(fmt, kaapi_task_getargs(task), buffer+sz, (int)(128-sz));
    fname = buffer;
  }
#if 0
  else if (body == kaapi_aftersteal_body)
  {
      fname = "aftersteal";
      shape = "doubleoctagon";
  }
  else if (body == kaapi_taskwrite_body)
  {
      fname = "taskwrite";
      shape = "doubleoctagon";
  }
  else if (body == kaapi_tasksteal_body)
  {
      fname = "tasksteal";
      shape = "doubleoctagon";
  }
  else if (body == kaapi_taskadapt_body)
  {
      fname = "taskadapt";
      shape = "doubleoctagon";
  }
  else if (body == kaapi_tasksignaladapt_body)
  {
      fname = "tasksignaladapt";
      shape = "doubleoctagon";
  }
#endif

  if (fmt ==0)
  {
    if (noprint_label)
      fprintf( file, "%lu [label=\"\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, shape, color
      );
    else
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task), shape, color
      );
    return;
  }

  /* print the task */
  if (noprint_label)
    fprintf( file, "%lu [label=\"\", shape=%s, style=filled, color=%s];\n",
      (uintptr_t)task, shape, color );
  else if( noprint_label_info )
    fprintf( file, "%lu [label=\"%s\", shape=%s, style=filled, color=%s];\n",
      (uintptr_t)task, fname, shape, color
    );
  else
  {
#if 0
    fprintf( stdout, "[visit] task:%p name: \"%s\", wc=%i\n",
      (void*)task, fname, (int)KAAPI_ATOMIC_READ(&task->wc)
    );
#endif
    if (1) //task->u.s.flag & KAAPI_TASK_FLAG_DFGOK)
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\\n"
               "wc=%i prio=%u\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task),
        (int)KAAPI_ATOMIC_READ(&task->wc),
        kaapi_task_get_priority(task),
        shape, color
      );
    else
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\","
               "shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task),
        shape, color
      );
  }

  size_t count_params = kaapi_format_get_count_params(fmt, sp );
  kaapi_memory_view_t view;

  for (unsigned int i=0; i < count_params; i++)
  {
    kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, i, sp);
    m = KAAPI_ACCESS_GET_MODE( m );
    if (m & KAAPI_ACCESS_MODE_V)
      continue;

    /* its an access */
    kaapi_access_t* access = kaapi_format_get_access_param(fmt, i, sp);

    /* next task if dfg */
    if ((task_khm !=0) && (access->sync !=0))
    {
      kaapi_access_t* an = access->sync->next;
      while (an !=0)
      {
        kaapi_hashentries_t* entry = kaapi_hashmap_find(task_khm, an->task);
        if (entry ==0)
        {
          entry = kaapi_hashmap_findinsert(task_khm, an->task);
          kaapi_pair_print_t* pp = KAAPI_HASHENTRIES_GETREF(entry, kaapi_pair_print_t);
          pp->tag = KAAPI_ATOMIC_READ(&an->task->wc);
        }
        an = an->next;
      }
    }

    kaapi_format_get_view_param(fmt, i, kaapi_task_getargs(task), &view);
    void* ptr = kaapi_memory_view2pointer(access->data, &view );

    /* find the version info of the data using the hash map */
    kaapi_hashentries_t* entry = kaapi_hashmap_findinsert(data_khm, ptr);
    kaapi_pair_print_t* pp = KAAPI_HASHENTRIES_GETREF(entry, kaapi_pair_print_t);
    if (pp->tag ==0)
    {
      /* display the node */
      pp->tag = 1;
      if (!noprint_data)
        _kaapi_print_data( file, entry->key, (int)pp->tag );
    }

    /* display arrow */
    if (KAAPI_ACCESS_IS_READ(m) && !KAAPI_ACCESS_IS_WRITE(m))
    {
      pp->last_mode = KAAPI_ACCESS_MODE_R;
      if (!noprint_data)
        _kaapi_print_read_edge(
            file,
            task,
            entry->key,
            pp->tag,
            m
        );
    }
    if (KAAPI_ACCESS_IS_WRITE(m))
    {
      pp->tag++;
      pp->last_mode = KAAPI_ACCESS_MODE_W;
      /* display new version */
      if (!noprint_data)
      {
        _kaapi_print_data( file, entry->key, (int)pp->tag );
        _kaapi_print_write_edge(
            file,
            task,
            entry->key,
            pp->tag,
            m
        );
      }
    }
    if (KAAPI_ACCESS_IS_CUMULWRITE(m))
    {
      if (!KAAPI_ACCESS_IS_CUMULWRITE(pp->last_mode))
      {
        pp->tag++;
        if (!noprint_data)
        {
          _kaapi_print_data( file, entry->key, (int)pp->tag );
          if (!noprint_versionlink)
          {
            /* add version edge */
            if (pp->tag >1)
              fprintf(file,"%lu00%lu -> %lu00%lu [style=dotted];\n",
                  (long unsigned int)(pp->tag-1), (uintptr_t)entry->key, (long unsigned int)pp->tag, (uintptr_t)entry->key );
          }
        }
      }
      pp->last_mode = KAAPI_ACCESS_MODE_CW;

      if (!noprint_data)
        _kaapi_print_write_edge(
            file,
            task,
            entry->key,
            pp->tag,
            m
        );
    }
  }

  if (task_khm !=0)
    kaapi_frame_dot_activate_succ(task, task_khm, readytask_khm);
}

/*
*/
void kaapi_dump_dot( kaapi_thread_t* thread, const char* filename )
{
  FILE* file = fopen(filename, "w");
  kaapi_hashmap_t          data_khm; /* to store task & data visited */
  kaapi_hashentries_t*     mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t mapbloc;

  kaapi_hashmap_t          task_khm; /* to store task & data visited */
  kaapi_hashentries_t*     task_mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t task_mapbloc;

  kaapi_hashmap_t          readytask_khm; /* to store task & data visited */
  kaapi_hashentries_t*     readytask_mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t readytask_mapbloc;

  /* */
  kaapi_hashmap_init(&data_khm, mapentries, KAAPI_SIZE_DOTCTXT, &mapbloc);
  kaapi_hashmap_init(&task_khm, task_mapentries, KAAPI_SIZE_DOTCTXT, &task_mapbloc);
  kaapi_hashmap_init(&readytask_khm, readytask_mapentries, KAAPI_SIZE_DOTCTXT, &readytask_mapbloc);

  kaapi_context_t* ctxt = (kaapi_context_t*)thread;

  fprintf(file, "digraph G {\n");

  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
  {
    kaapi_task_t** Task0 = ctxt->queue->data[p]+ctxt->queue->H[p];
    for (int32_t i=0; i<kaapi_queue_size_prio(p, ctxt->queue); ++i)
    {
      kaapi_task_t* t = Task0[i];
      _kaapi_dump_dot_visit_task( file, t, &data_khm, &task_khm, &readytask_khm );
    }
  }

  kaapi_hashentries_t* entry;
  int newttask;
redo_print:
  newttask = 0;
  for (int i=0; i< kaapi_hashmap_sizeentries(&readytask_khm); ++i)
  {
    while ((entry = _pop_hashmap_entry(&readytask_khm, i)) != 0)
    {
      newttask = 1;
      _kaapi_dump_dot_visit_task( file, (kaapi_task_t*)entry->key, &data_khm, &task_khm, &readytask_khm);
    }
  }
  if (newttask) goto redo_print;


  fprintf(file, "\n\n}\n");
  fflush(file);

  kaapi_hashmap_destroy(&task_khm);
  kaapi_hashmap_destroy(&readytask_khm);
  kaapi_hashmap_destroy(&data_khm);
}


/* Follow the activation tree from the list of handle
*/
void kaapi_dump_dot_list_handle( kaapi_thread_t* thread, kaapi_handle_t* first, const char* filename )
{
  FILE* file = fopen(filename, "w");
  kaapi_hashmap_t          data_khm; /* to store task & data visited */
  kaapi_hashentries_t*     mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t mapbloc;

  kaapi_hashmap_t          task_khm; /* to store task & data visited */
  kaapi_hashentries_t*     task_mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t task_mapbloc;

  kaapi_hashmap_t          readytask_khm; /* to store task & data visited */
  kaapi_hashentries_t*     readytask_mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t readytask_mapbloc;

  /* */
  kaapi_hashmap_init(&data_khm, mapentries, KAAPI_SIZE_DOTCTXT, &mapbloc);
  kaapi_hashmap_init(&task_khm, task_mapentries, KAAPI_SIZE_DOTCTXT, &task_mapbloc);
  kaapi_hashmap_init(&readytask_khm, readytask_mapentries, KAAPI_SIZE_DOTCTXT, &readytask_mapbloc);

  fprintf(file, "digraph G {\n");

  kaapi_handle_t* curr = first;
  while (curr != 0)
  {
    kaapi_handle_t* next = (kaapi_handle_t*)curr->sync0.sync;
    if (curr->sync != 0)
    {
      kaapi_access_t* sync = &curr->sync0;
      kaapi_frame_dot_activate_succ_access( sync, &task_khm, &readytask_khm );
    }
    curr = next;
  }

  kaapi_hashentries_t* entry;
  int newttask;
redo_print:
  newttask = 0;
  for (int i=0; i<kaapi_hashmap_sizeentries(&readytask_khm); ++i)
  {
    while ((entry = _pop_hashmap_entry(&readytask_khm, i)) != 0)
    {
      newttask = 1;
      _kaapi_dump_dot_visit_task( file, (kaapi_task_t*)entry->key, &data_khm, &task_khm, &readytask_khm);
    }
  }
  if (newttask) goto redo_print;


  fprintf(file, "\n\n}\n");
  fflush(file);

  kaapi_hashmap_destroy(&task_khm);
  kaapi_hashmap_destroy(&readytask_khm);
  kaapi_hashmap_destroy(&data_khm);
}




/*
*/
static void _kaapi_dump_raw_dot_visit_task(
  FILE* file,
  kaapi_task_t*    task,
  kaapi_hashmap_t* data_khm,
  kaapi_hashmap_t* task_khm
)
{
  char buffer[128];
  const char* fname = "<empty format>";
  const char* color;
  const char* shape;
  const kaapi_format_t* fmt = 0;
  kaapi_task_body_t body;

  void* sp = kaapi_task_getargs(task);
  body = task->body;
  fmt = kaapi_task_getformat_ref(task);

  color = fmt->color_dot;
  if (color ==0)
    color = "orange";

  shape = fmt->shape_dot;
  if (shape ==0)
    shape = "ellipse";

  if (body == kaapi_taskmain_body)
  {
      fname = "maintask";
      shape = "doubleoctagon";
  }
  else
  {
    ssize_t sz = snprintf(buffer, 128, "%s", fmt->name );
    if (fmt->get_name)
      fmt->get_name(fmt, kaapi_task_getargs(task), buffer+sz, (int)(128-sz));
    fname = buffer;
  }

  if (fmt ==0)
  {
    if (noprint_label)
      fprintf( file, "%lu [label=\"\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, shape, color
      );
    else
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task), shape, color
      );
    return;
  }

  /* print the task */
  if (noprint_label)
    fprintf( file, "%lu [label=\"\", shape=%s, style=filled, color=%s];\n",
      (uintptr_t)task, shape, color );
  else if( noprint_label_info )
    fprintf( file, "%lu [label=\"%s\", shape=%s, style=filled, color=%s];\n",
      (uintptr_t)task, fname, shape, color
    );
  else
  {
#if 0
    fprintf( stdout, "[visit] task:%p name: \"%s\", wc=%i\n",
      (void*)task, fname, (int)KAAPI_ATOMIC_READ(&task->wc)
    );
#endif
    if (1) //task->u.s.flag & KAAPI_TASK_FLAG_DFGOK)
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\\n"
               "wc=%i prio=%u\", shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task),
        (int)KAAPI_ATOMIC_READ(&task->wc),
        kaapi_task_get_priority(task),
        shape, color
      );
    else
      fprintf( file, "%lu [label=\"%s\\ntask=%p\\nsp=%p\","
               "shape=%s, style=filled, color=%s];\n",
        (uintptr_t)task, fname, (void*)task, kaapi_task_getargs(task),
        shape, color
      );
  }

  size_t count_params = kaapi_format_get_count_params(fmt, sp );
  for (unsigned int i=0; i < count_params; i++)
  {
    kaapi_access_mode_t m = kaapi_format_get_mode_param(fmt, i, sp);
    m = KAAPI_ACCESS_GET_MODE( m );
    if (m & KAAPI_ACCESS_MODE_V)
      continue;

    /* its an access */
    kaapi_access_t* access = kaapi_format_get_access_param(fmt, i, sp);

    /* next task if dfg */
    if ((task_khm !=0) && (access->next !=0))
    {
      kaapi_access_t* an = access->next;
      kaapi_hashentries_t* entry = kaapi_hashmap_find(data_khm, an->task);
      if (entry ==0)
      {
        entry = kaapi_hashmap_findinsert(data_khm, an->task);
        entry = kaapi_hashmap_findinsert(task_khm, an->task);
      }
    }

    fprintf(file,"%lu[label=\"%s\",shape=box, style=filled, color=steelblue];\n",
          (unsigned long)access,
          kaapi_access_mode2string(m)
    );

    if (KAAPI_ACCESS_IS_READWRITE(m))
      fprintf(file,"%lu -> %lu[dir=both];\n",
          (uintptr_t)task, (uintptr_t)access );
    else if (KAAPI_ACCESS_IS_READ(m))
      fprintf(file,"%lu -> %lu;\n",
          (uintptr_t)access, (uintptr_t)task );
    else if (KAAPI_ACCESS_IS_WRITE(m))
      fprintf(file,"%lu -> %lu;\n",
          (uintptr_t)task, (uintptr_t)access );
    if (access->next !=0)
      fprintf(file,"%lu -> %lu[style=dotted];\n",
          (uintptr_t)access, (uintptr_t)access->next );
  }
}


void kaapi_dump_raw_dot( kaapi_thread_t* thread, const char* filename )
{
  FILE* file = fopen(filename, "w");
  kaapi_hashmap_t          data_khm; /* to store task & data visited */
  kaapi_hashentries_t*     mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t mapbloc;

  kaapi_hashmap_t          task_khm; /* to store task & data visited */
  kaapi_hashentries_t*     task_mapentries[1<<KAAPI_SIZE_DOTCTXT];
  kaapi_hashentries_bloc_t task_mapbloc;

  /* */
  kaapi_hashmap_init(&data_khm, mapentries, KAAPI_SIZE_DOTCTXT, &mapbloc);
  kaapi_hashmap_init(&task_khm, task_mapentries, KAAPI_SIZE_DOTCTXT, &task_mapbloc);

  kaapi_context_t* ctxt = (kaapi_context_t*)thread;

  fprintf(file, "digraph G {\n");

  for (int p=0; p<=KAAPI_TASK_MAX_PRIORITY; ++p)
  {
    kaapi_task_t** Task0 = ctxt->queue->data[p]+ctxt->queue->H[p];
    for (int32_t i=0; i<kaapi_queue_size_prio(p, ctxt->queue); ++i)
    {
      kaapi_task_t* t = Task0[i];
      kaapi_hashentries_t* entry = kaapi_hashmap_find(&data_khm, t);
      if (entry ==0)
      {
        kaapi_hashmap_insert(&data_khm, t);
        kaapi_hashmap_insert(&task_khm, t);
      }
      else 
      {
        printf("**** Warning: task: %p is pushed two times on ready queue \n", (void*)t);
      }
    }
  }

  kaapi_hashentries_t* entry;
  int newttask;
redo_print:
  newttask = 0;
  for (int i=0; i<kaapi_hashmap_sizeentries(&task_khm); ++i)
  {
    while ((entry = _pop_hashmap_entry(&task_khm, i)) != 0)
    {
      newttask = 1;
      _kaapi_dump_raw_dot_visit_task( file, (kaapi_task_t*)entry->key, &data_khm, &task_khm);
    }
  }
  if (newttask) goto redo_print;


  fprintf(file, "\n\n}\n");
  fflush(file);

  kaapi_hashmap_destroy(&task_khm);
  kaapi_hashmap_destroy(&data_khm);
}


/*
*/
void kaapi_dbg_register_name( const void* ptr, const char* name )
{
  kaapi_hashentries_t* entry = kaapi_hashmap_findinsert(&data_info_khm, ptr);
  char* oldname = KAAPI_HASHENTRIES_GET(entry, char*);
  if (oldname !=0) free(oldname);
  KAAPI_HASHENTRIES_SET(entry, strdup(name), const char*);
}

/*
*/
const char* kaapi_dbg_get_name( const void* ptr )
{
  kaapi_hashentries_t* entry = kaapi_hashmap_findinsert(&data_info_khm, ptr);
  return KAAPI_HASHENTRIES_GET(entry, char*);
}

/*
*/
int kaapi_dbg_init(void)
{
  kaapi_hashmap_init(&data_info_khm, data_info_mapentries, KAAPI_SIZE_DOTCTXT, 0);
  return 0;
}

/*
*/
int kaapi_dbg_finalize()
{
  kaapi_hashmap_clear(&data_info_khm);
  kaapi_hashmap_destroy(&data_info_khm);
  return 0;
}
