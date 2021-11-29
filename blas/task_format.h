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

#include "kaapi_format.h"

/* This file define format for task. THIS FILE SHOULD INCLUDED BY TASK FILE IMPLEMENTION
*/
static void PNAME(format_get_name)(
  const kaapi_format_t* fmt, const void* sp, char* buffer, int size
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  snprintf(buffer, size, "%s", STRNAME);
}

static size_t
PNAME(format_get_size)(const kaapi_format_t* fmt, const void* sp)
{ return sizeof(NAME(Arg)); }

static void
PNAME(format_task_copy)(const kaapi_format_t* fmt, void* sp_dest, const void* sp_src)
{
  NAME(Arg) *task_src = (NAME(Arg) *)sp_src;
  NAME(Arg) *task_dest = (NAME(Arg) *)sp_dest;
  *task_dest = *task_src;
}

static
unsigned int PNAME(format_get_count_params)(const kaapi_format_t* fmt, const void* sp)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  return NPARAM;
}

static
kaapi_access_mode_t PNAME(format_get_mode_param)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  kaapi_access_mode_t mode[SIZE_NPARAM] = MODE_PARAM;
  return mode[i];
}

static
void* PNAME(format_get_data_param)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  kaapi_access_t* addr[SIZE_NPARAM] = ADDR_PARAM;
  return addr[i]->data;
}

static
kaapi_access_t* PNAME(format_get_access_param)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  kaapi_access_t* addr[SIZE_NPARAM] = ADDR_PARAM;
  return addr[i];
}

static
void PNAME(format_set_access_param)(
  const kaapi_format_t* fmt, unsigned int i, void* sp, const kaapi_access_t* a
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  kaapi_access_t* addr[SIZE_NPARAM] = ADDR_PARAM;
  *addr[i] = *a;
}

static
const kaapi_format_t* PNAME(format_get_fmt_param)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp
)
{
  return FORMAT_TYPE;
}

static
void PNAME(format_get_view_param)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp,
  kaapi_memory_view_t* view
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  struct { size_t* m; size_t* n; size_t* ld; } vp[SIZE_NPARAM] = VIEW_PARAM;

  kaapi_memory_view_make2d(view,
    0,
    *vp[i].m, *vp[i].n, *vp[i].ld,
    SIZEOF_TYPE, KAAPI_MEMORY_STORAGE_COLMAJOR);
}

static
void PNAME(format_set_view_param)(
  const kaapi_format_t* fmt, unsigned int i, void* sp, const kaapi_memory_view_t* view
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
  struct { size_t* m; size_t* n; size_t* ld; } vp[SIZE_NPARAM] = VIEW_PARAM;
  *vp[i].m = view->size[0];
  *vp[i].n = view->size[1];
  *vp[i].ld = view->ld;
}

static
void PNAME(format_reducor)(
  const kaapi_format_t* fmt, unsigned int i, void* sp, const void* v
)
{
  abort();
}

static
void PNAME(format_redinit)(
  const kaapi_format_t* fmt, unsigned int i, const void* sp, void* v
)
{
  abort();
}

static kaapi_adaptivetask_splitter_t  PNAME(fnc_get_splitter)(
  const kaapi_format_t* fmt, const void* sp
)
{
  return 0;
}

static int PNAME(fnc_get_affinity)(
  const kaapi_format_t* fmt, const void* sp, kaapi_task_t* t, uint16_t flag
)
{
  return -1;
}

static void PNAME(fnc_get_cost)(
  const kaapi_format_t* fmt, const void* sp, kaapi_task_t* t,
  double *f, double* df, double *data
)
{
  NAME(Arg) *arg = (NAME(Arg) *)sp;
#if defined(PRECISION_z)||defined(PRECISION_d)
  if (df) { *df = TASK_FLOPS; }
#else
  if (f) { *f = TASK_FLOPS; }
#endif
  if (data) { *data = TASK_DATA; }
}


void NAME(register_format)(void)
{
  kaapi_format_t* fmt = kaapi_format_allocate();
  NAME(task_fmtid) = kaapi_format_taskregister_func(
         fmt,
         (void*)NAME(task_body_cpu), /* key */
         NAME(task_body_cpu), /* body CPU */
  #if KAAPI_USE_CUDA && (NO_GPU_IMPL==0)
         NAME(task_body_gpu), /* body GPU */
  #else
         0, /* body GPU */
  #endif
         STRNAME,
         0, /*PNAME(format_get_name), */
         PNAME(format_get_size),
         PNAME(format_task_copy),
         PNAME(format_get_count_params),
         PNAME(format_get_mode_param),
         PNAME(format_get_data_param),
         PNAME(format_get_access_param),
         PNAME(format_set_access_param),
         PNAME(format_get_fmt_param),
         PNAME(format_get_view_param),
         PNAME(format_set_view_param),
         PNAME(format_reducor),
         PNAME(format_redinit),
         PNAME(fnc_get_splitter),
         PNAME(fnc_get_affinity),
         PNAME(fnc_get_cost)
  );
#ifdef DOT_SHAPE
  fmt->shape_dot = DOT_SHAPE;
#endif
#ifdef DOT_COLOR
  fmt->color_dot = DOT_COLOR;
#endif
}

/* undefined used macro */
#undef STRNAME
#undef TASK_NAME
#undef NAME
#undef PNAME
#undef NPARAM
#undef SIZE_NPARAM
#undef MODE_PARAM
#undef ADDR_PARAM
#undef VIEW_PARAM
#undef FORMAT_TYPE
#undef SIZEOF_TYPE
#undef DOT_COLOR
#undef DOT_SHAPE
#undef TASK_FLOPS
#undef TASK_DATA


