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

#ifndef _KAAPI_FORMAT_H_
#define _KAAPI_FORMAT_H_ 1
#if !defined(__STDC_NO_COMPLEX__)
#include <complex.h>
#endif

#include "kaapi_atomic.h"

#define KAAPI_FORMAT_MAX             128

/** Cache table of all formats: id -> entrypoint[HOST_ARCH]
*/
extern kaapi_task_bodyfnc_t kaapi_all_formats_fnc[KAAPI_FORMAT_MAX];

/** Global table of all formats: id -> entrypoint[HOST_ARCH]
*/
extern kaapi_format_t* kaapi_all_formats[KAAPI_FORMAT_MAX];

/** Count the number of task formats
*/
extern kaapi_atomic_t kaapi_count_format_byfmtid;


/* ======================== Format for task/data structure ============================ */

/** \ingroup TASK
    Kaapi task format
    The format should be 1/ declared 2/ register before any use in task.
    The format object is only used in order to interpret stack of task.    
*/
struct kaapi_format {
  /* only if it is a format of a task  */
  int                        mask_arch;                        /* ?? */
  kaapi_task_bodyfnc_t       default_body;                     /* iff a task used on current node */
  kaapi_task_bodyfnc_t       entrypoint[KAAPI_PROC_TYPE_MAX];  /* maximum architecture considered in the configuration */

  kaapi_format_id_t          fmtid;                                   /* identifier of the format */
  void*                      key;                                     /* system wide key used */
  short                      isinit;                                  /* ==1 iff initialize */
  char*                      name;                                    /* debug information */
  const char*                shape_dot;                               /* name for DOT */
  const char*                color_dot;                               /* color for DOT */
  
  uint32_t                   size;                                    /* sizeof the object */  
  void                       (*cstor)( void* dest);
  void                       (*dstor)( void* dest);
  void                       (*cstorcopy)( void* dest, const void* src);
  void                       (*copy)( void* dest, const void* src);
  void                       (*assign)( void* dest, const kaapi_memory_view_t* view_dest, const void* src, const kaapi_memory_view_t* view_src);



  /* case of format for a structure or for a task with flag= KAAPI_FORMAT_FUNC_FIELD
     - the unsigned int argument is the index of the parameter 
     - the last argument is the pointer to the sp data of the task
  */
  kaapi_format_id_t            (*get_fmt_id)(const struct kaapi_format*, const void* );
  kaapi_fmt_fnc_get_name         get_name;
  kaapi_fmt_fnc_get_size         get_size;
  kaapi_fmt_fnc_task_copy        task_copy;
  kaapi_fmt_fnc_get_count_params get_count_params;
  kaapi_fmt_fnc_get_mode_param   get_mode_param;
  kaapi_fmt_fnc_get_data_param   get_data_param;
  kaapi_fmt_fnc_get_access_param get_access_param;
  kaapi_fmt_fnc_set_access_param set_access_param;
  kaapi_fmt_fnc_get_fmt_param    get_fmt_param;
  kaapi_fmt_fnc_get_view_param   get_view_param;
  kaapi_fmt_fnc_set_view_param   set_view_param;
  kaapi_fmt_fnc_reducor          reducor;
  kaapi_fmt_fnc_redinit          redinit;
  kaapi_fmt_fnc_get_splitter	 get_splitter;
  kaapi_fmt_fnc_get_affinity	 get_affinity;
  kaapi_fmt_fnc_get_cost         get_cost;

  /* fields to link the format is the internal tables */
  kaapi_format_t          *next_bybody;  /* link in hash table */
  kaapi_format_t          *next_byfmtid; /* link in hash table */
  
};


/* Helper to interpret the format
*/
static inline kaapi_format_id_t kaapi_format_get_fmt_id(
    const kaapi_format_t* fmt, const void* sp
)
{
  if (fmt->get_fmt_id ==0)
    return fmt->fmtid;
  else
    return (*fmt->get_fmt_id)(fmt, sp);
}


/*
*/
static inline size_t kaapi_format_get_size(const kaapi_format_t* fmt, const void* sp)
{ return (*fmt->get_size)(fmt, sp); }

/*
*/
static inline void kaapi_format_task_copy(
  const kaapi_format_t* fmt, void* sp_dest, const void* sp_src
)
{ (*fmt->task_copy)(fmt, sp_dest, sp_src); }

/*
*/
static inline unsigned int kaapi_format_get_count_params(
  const kaapi_format_t* fmt, const void* sp
)
{ return (*fmt->get_count_params)(fmt, sp); }

/*
*/
static inline kaapi_access_mode_t kaapi_format_get_mode_param (
    const kaapi_format_t* fmt, unsigned int ith, const void* sp
)
{ return (*fmt->get_mode_param)(fmt, ith, sp); }

/*
*/
static inline void* kaapi_format_get_data_param  (
  const kaapi_format_t* fmt, unsigned int ith, const void* sp
)
{
  kaapi_assert_debug( KAAPI_ACCESS_GET_MODE(kaapi_format_get_mode_param(fmt, ith, sp)) == KAAPI_ACCESS_MODE_V );
  return fmt->get_data_param( fmt, ith, sp );
}

/*
*/
static inline kaapi_access_t* kaapi_format_get_access_param  (
    const kaapi_format_t* fmt, unsigned int ith, const void* sp
)
{
  kaapi_assert_debug( KAAPI_ACCESS_GET_MODE(kaapi_format_get_mode_param(fmt, ith, sp)) != KAAPI_ACCESS_MODE_V );
  return (*fmt->get_access_param)(fmt, ith, sp);
}

/*
*/
static inline const kaapi_format_t* kaapi_format_get_fmt_param  (
    const kaapi_format_t* fmt, unsigned int ith, const void* sp
)
{ return (*fmt->get_fmt_param)(fmt, ith, sp); }

/*
*/
static inline void kaapi_format_get_view_param (
    const kaapi_format_t* fmt, unsigned int ith, const void* sp, kaapi_memory_view_t* view
)
{ (*fmt->get_view_param)(fmt, ith, sp, view); }

/*
*/
static inline void kaapi_format_set_access_param  (
    const kaapi_format_t* fmt, unsigned int ith, void* sp, const kaapi_access_t* a
)
{
  kaapi_assert_debug( KAAPI_ACCESS_GET_MODE(kaapi_format_get_mode_param(fmt, ith, sp)) != KAAPI_ACCESS_MODE_V );
  (*fmt->set_access_param)(fmt, ith, sp, a);
}

/*
*/
static inline void kaapi_format_set_view_param (
    const kaapi_format_t* fmt, unsigned int ith, void* sp, const kaapi_memory_view_t* view
)
{ (*fmt->set_view_param)(fmt, ith, sp, view); }

/*
*/
static inline void kaapi_format_reduce_param (
    const kaapi_format_t* fmt, unsigned int ith, void* sp, const void* value
)
{ (*fmt->reducor)(fmt, ith, sp, value); }

/*
*/
static inline void kaapi_format_redinit_neutral (
    const kaapi_format_t* fmt, unsigned int ith, const void* sp, void* value
)
{ (*fmt->redinit)(fmt, ith, sp, value); }

/*
*/
static inline kaapi_adaptivetask_splitter_t kaapi_format_get_splitter(
    const kaapi_format_t* fmt, const void* sp
)
{ return (*fmt->get_splitter)(fmt, sp); }

/*
*/
static inline int kaapi_format_get_affinity(
    const kaapi_format_t* fmt, const void* sp, kaapi_task_t* task, uint16_t flag
)
{ return (*fmt->get_affinity)(fmt, sp, task, flag); }

/*
*/
static inline int kaapi_format_get_cost(
    const kaapi_format_t* fmt, const void* sp, kaapi_task_t* task,
    double* flops, double* data
)
{
  if (fmt->get_cost ==0)
  {
    *flops = 0; *data = 0;
    return ENOSYS;
  }
  (*fmt->get_cost)(fmt, sp, task, flops, data);
  return 0;
}


/*
*/
static inline int kaapi_format_task_has_body_on_arch
(
  const kaapi_format_t*	const	fmt, 
  unsigned int arch
)
{
  return  (fmt->mask_arch & (1U << arch)) != 0;
}


/** Initialise format module
*/
extern int kaapi_format_init(void);

/** Finalize format module
*/
extern void kaapi_format_finalize(void);


#define KAAPI_DECLEXTERN_BASICTYPEFORMAT( formatobject ) \
  extern kaapi_format_t formatobject##_object;

KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_schar_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_char_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_shrt_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_int_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_long_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_llong_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_int8_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_int16_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_int32_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_int64_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uchar_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_ushrt_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uint_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_ulong_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_ullong_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uint8_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uint16_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uint32_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_uint64_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_flt_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_dbl_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_ldbl_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_flt_format)
#if !defined(__STDC_NO_COMPLEX__)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_scplx_format)
KAAPI_DECLEXTERN_BASICTYPEFORMAT(kaapi_dcplx_format)
#endif
#endif
