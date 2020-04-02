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

#include "kaapi_impl.h"
#include "kaapi_format.h"
#include <string.h> // memset
#include <stdio.h> // snprintf
#include <inttypes.h>

/* we assume table of default fncs do not pollute cache as if all format entry have been loaded
*/
kaapi_task_bodyfnc_t kaapi_all_formats_fnc[KAAPI_FORMAT_MAX] __attribute__((aligned(KAAPI_CACHE_LINE ))) =
{
/* 128 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


static int kaapi_all_formats_count = 0;
kaapi_format_t* kaapi_all_formats[KAAPI_FORMAT_MAX] =
{
/* 128 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static kaapi_format_t* kaapi_all_formats_byfmtid[KAAPI_FORMAT_MAX] =
{
/* 128 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


/**
*/
static kaapi_format_t* kaapi_all_formats_bybody[KAAPI_FORMAT_MAX] =
{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

kaapi_atomic_t kaapi_count_format_byfmtid = {0};

/**
*/
kaapi_format_t* kaapi_format_allocate( )
{
  kaapi_format_t* fmt = (kaapi_format_t*)malloc( sizeof(kaapi_format_t) );
  return fmt;
}


/** Should be serialized
*/
static kaapi_format_id_t kaapi_format_register(
        kaapi_format_t*           fmt,
        void*                     key,
        const char*               name
)
{
  uint8_t        entry;
  kaapi_format_t* head;

  memset( fmt, 0, sizeof(kaapi_format_t) );

  kaapi_assert(++kaapi_all_formats_count < KAAPI_FORMAT_MAX);
  fmt->fmtid  = kaapi_all_formats_count;
  fmt->key    = key;
  fmt->name   = strdup(name);
  fmt->isinit = 1;

  fmt->shape_dot = "ellipse";
  fmt->color_dot = "orange";
  kaapi_all_formats[fmt->fmtid] = fmt;

  /* register it into hashmap: fmtid -> fmt */
  uint64_t id = kaapi_hash_ulong( (uint64_t)key );
  entry = (uint8_t) (id % (kaapi_format_id_t)KAAPI_FORMAT_MAX);
  head =  kaapi_all_formats_byfmtid[entry];
  fmt->next_byfmtid = head;
  kaapi_all_formats_byfmtid[entry] = fmt;

  KAAPI_ATOMIC_INCR(&kaapi_count_format_byfmtid);

  return fmt->fmtid;
}


/** TODO:
    - utilisation d'une autre structure de chainage que le format: 3 archi possible
    mais qu'un champ de link => seulement une archi dans la table de hash...
    - 
*/
kaapi_task_body_t kaapi_format_taskregister_body( 
        kaapi_format_t*             fmt,
        kaapi_task_bodyfnc_t        body,
        unsigned int                archi
)
{
  uint8_t         entry;
  kaapi_format_t* head;
  kaapi_assert_debug( archi < KAAPI_PROC_TYPE_MAX);

  if (body ==0) return fmt->fmtid;
  
  if (fmt->entrypoint[archi] == body) return fmt->fmtid;
  fmt->entrypoint[archi]    = body;
  if (archi == KAAPI_PROC_TYPE_DEFAULT)
  {
    fmt->default_body = body;
    kaapi_all_formats_fnc[fmt->fmtid] = body;
  }

  fmt->mask_arch |= (1U << archi);
  
  /* TG: if registration of different bodies for the same format 
     in an entry of the hashmap with conflict, then it seems that 
     some format object will be lost.
     See bug 15020.
  */
  if (archi == KAAPI_PROC_TYPE_HOST)
  {
    /* register it into hashmap: body -> fmt */
    entry = (uint8_t)kaapi_hash_ulong((unsigned long)body)%KAAPI_FORMAT_MAX;
    head =  kaapi_all_formats_bybody[entry];
    fmt->next_bybody = head;
    kaapi_all_formats_bybody[entry] = fmt;
  }

  /* already registered into hashmap: fmtid -> fmt */
  return fmt->fmtid;
}


/**
*/
kaapi_format_id_t kaapi_format_taskregister_func(
    kaapi_format_t*                fmt,
    void*                          key,
    kaapi_task_bodyfnc_t           body_cpu,
    kaapi_task_bodyfnc_gpu_t       body_gpu,
    const char*                    name,
    kaapi_fmt_fnc_get_name         get_name,
    kaapi_fmt_fnc_get_size         get_size,
    kaapi_fmt_fnc_task_copy        task_copy,
    kaapi_fmt_fnc_get_count_params get_count_params,
    kaapi_fmt_fnc_get_mode_param   get_mode_param,
    kaapi_fmt_fnc_get_data_param   get_data_param,
    kaapi_fmt_fnc_get_access_param get_access_param,
    kaapi_fmt_fnc_set_access_param set_access_param,
    kaapi_fmt_fnc_get_fmt_param    get_fmt_param,
    kaapi_fmt_fnc_get_view_param   get_view_param,
    kaapi_fmt_fnc_set_view_param   set_view_param,
    kaapi_fmt_fnc_reducor          reducor,
    kaapi_fmt_fnc_redinit          redinit,
    kaapi_fmt_fnc_get_splitter	   get_splitter,
    kaapi_fmt_fnc_get_affinity     get_affinity,
    kaapi_fmt_fnc_get_cost         get_cost
)
{
  kaapi_format_register( fmt, key, name );
  
  fmt->get_name         = get_name;
  fmt->get_size         = get_size;
  fmt->task_copy        = task_copy;
  fmt->get_count_params = get_count_params;
  fmt->get_mode_param   = get_mode_param;
  fmt->get_data_param   = get_data_param;
  fmt->get_access_param = get_access_param;
  fmt->set_access_param = set_access_param;
  fmt->get_fmt_param    = get_fmt_param;
  fmt->get_view_param   = get_view_param;
  fmt->set_view_param   = set_view_param;
  fmt->reducor          = reducor;
  fmt->redinit          = redinit;
  fmt->get_splitter	    = get_splitter;
  fmt->get_affinity     = get_affinity;
  fmt->get_cost         = get_cost;

  memset(fmt->entrypoint, 0, sizeof(fmt->entrypoint));
  
  if (body_cpu !=0)
    kaapi_format_taskregister_body(fmt, body_cpu, KAAPI_PROC_TYPE_HOST);

  fmt->entrypoint[KAAPI_PROC_TYPE_GPU] = (kaapi_task_bodyfnc_t)body_gpu;

  return fmt->fmtid;
}


/**
*/
kaapi_format_id_t kaapi_format_structregister( 
    kaapi_format_t*             fmt,
    const char*                 name,
    size_t                      size,
    void                       (*cstor)( void* dest),
    void                       (*dstor)( void* dest),
    void                       (*cstorcopy)( void* dest, const void* src),
    void                       (*copy)( void* dest, const void* src),
    void                       (*assign)( void* dest, const kaapi_memory_view_t* dview, const void* src, const kaapi_memory_view_t* sview)
)
{
  kaapi_format_register( fmt, (void*)(uintptr_t)kaapi_hash_value(name), name );
  fmt->size      = (uint32_t)size;
  fmt->cstor     = cstor;
  fmt->dstor     = dstor;
  fmt->cstorcopy = cstorcopy;
  fmt->copy      = copy;
  fmt->assign    = assign;

  /* already registered into hashmap: fmtid -> fmt */  
  return fmt->fmtid;
}


/**
*/
kaapi_format_t* kaapi_format_resolve_byfmid( kaapi_format_id_t fmtid )
{
  if (fmtid >= KAAPI_FORMAT_MAX) return 0;
  return kaapi_all_formats[fmtid];
}

/**
*/
kaapi_format_t* kaapi_format_resolve_bykey( void* key )
{
  uint8_t	  entry;
  kaapi_format_t* head;

  if (key ==0) return 0;
  entry = (uint8_t) (kaapi_hash_ulong((uint64_t)key) % (kaapi_format_id_t)KAAPI_FORMAT_MAX);
  head =  kaapi_all_formats_byfmtid[entry];

  for (; head; head = head->next_byfmtid)
  {
    /* here we may be only need to look at the current architecture */
    if (head->key == key)
      return head;
  }

  return 0;
}

/**
*/
kaapi_format_t* kaapi_format_resolve_bybody( kaapi_task_bodyfnc_t body )
{
  uint8_t	  entry;
  kaapi_format_t* head;

  entry = (uint8_t)kaapi_hash_ulong((unsigned long)body)%KAAPI_FORMAT_MAX;
  head =  kaapi_all_formats_bybody[entry];

  for (; head; head = head->next_bybody)
  {
    /* here we may be only need to look at the current architecture */
    if (head->default_body == body)
      return head;
  }

  return 0;
}


/**
*/
kaapi_format_t* kaapi_format_resolvebyfmit(kaapi_format_id_t key)
{
  uint8_t         entry = (uint8_t)(key & (kaapi_format_id_t)KAAPI_FORMAT_MAX);
  kaapi_format_t* head =  kaapi_all_formats_byfmtid[entry];

  for (; head; head = head->next_byfmtid)
    if (head->fmtid == key)
      return head;

  return 0;
}


/* Helper to interpret the format
*/
void kaapi_format_get_name(
    const kaapi_format_t* fmt, const void* sp, char* buffer, int size
)
{
  if (fmt ==0)
  {
    snprintf(buffer, size, "<null>");
    return;
  }
  /* this is a special function used to produce a specific name for task that have virtual get_name function */
  if (fmt->get_name ==0)
    snprintf(buffer, size, "%s", fmt->name );
  else
    (*fmt->get_name)(fmt, sp, buffer, size);
}


/*
*/
kaapi_task_bodyfnc_t kaapi_format_get_task_bodyfnc_by_arch
(
  const kaapi_format_t*	const	fmt, 
  unsigned int arch
)
{ return fmt->entrypoint[arch]; }


/**
*/
#define KAAPI_DECL_BASICTYPEFORMAT( formatobject, type, fmt ) \
  kaapi_format_t* formatobject;\
  static void formatobject##_cstor(void* dest)  { *(type*)dest = 0; }\
  static void formatobject##_dstor(void* dest) { *(type*)dest = 0; }\
  static void formatobject##_cstorcopy( void* dest, const void* src) { *(type*)dest = *(type*)src; } \
  static void formatobject##_copy( void* dest, const void* src) { *(type*)dest = *(type*)src; } \
  static void formatobject##_assign( void* dest, const kaapi_memory_view_t* dview, const void* src, const kaapi_memory_view_t* sview) { \
    kaapi_memory_copy( kaapi_make_pointer(dest, kaapi_local_asid), dview, \
                       kaapi_make_pointer((void*)src, kaapi_local_asid), sview); \
  }

#define KAAPI_REGISTER_BASICTYPEFORMAT( formatobject, type, fmt ) \
  formatobject = kaapi_format_allocate(); \
  kaapi_format_structregister( formatobject, \
                               #type, \
                               sizeof(type), \
                               &formatobject##_cstor, &formatobject##_dstor, &formatobject##_cstorcopy, \
                               &formatobject##_copy, &formatobject##_assign );


/** Predefined format
*/
KAAPI_DECL_BASICTYPEFORMAT(kaapi_schar_format, char, "%hhu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_char_format, char, "%hhu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_shrt_format, short, "%hi")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_int_format, int, "%i")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_long_format, long, "%li")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_llong_format, long long, "%lli")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_int8_format, int8_t, "%"PRIi8)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_int16_format, int16_t, "%"PRIi16)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_int32_format, int32_t, "%"PRIi32)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_int64_format, int64_t, "%"PRIi64)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uchar_format, unsigned char, "%hhu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_ushrt_format, unsigned short, "%hu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uint_format, unsigned int, "%u")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_ulong_format, unsigned long, "%lu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_ullong_format, unsigned long long, "%llu")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uint8_format, uint8_t, "%"PRIu8)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uint16_format, uint16_t, "%"PRIu16)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uint32_format, uint32_t, "%"PRIu32)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_uint64_format, uint64_t, "%"PRIu64)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_flt_format, float, "%e")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_dbl_format, double, "%e")  
KAAPI_DECL_BASICTYPEFORMAT(kaapi_ldbl_format, long double, "%Le")  
kaapi_format_t* kaapi_voidp_format;
#if !defined(__STDC_NO_COMPLEX__)
KAAPI_DECL_BASICTYPEFORMAT(kaapi_scplx_format, float complex, "<complex32>")
KAAPI_DECL_BASICTYPEFORMAT(kaapi_dcplx_format, double complex, "<complex64>")
#endif

/* void pointer format */
static void voidp_type_cstor(void* addr)
{
  /* printf("%s\n", __FUNCTION__); */
  *(void**)addr = 0;
}

/*
*/
static void voidp_type_dstor(void* addr)
{
  /* printf("%s\n", __FUNCTION__); */
  *(void**)addr = 0;
}

/*
*/
static void voidp_type_cstorcopy(void* daddr, const void* saddr)
{
  /* TODO: missing views */
  /* printf("%s\n", __FUNCTION__); */
  kaapi_abort(__LINE__,__FILE__, "Not implemented");
}

/*
*/
static void voidp_type_copy(void* daddr, const void* saddr)
{
  /* TODO: missing views */
  /* printf("%s\n", __FUNCTION__); */
  kaapi_abort(__LINE__,__FILE__, "Not implemented");
}

/*
*/
static void voidp_type_assign
(
 void* daddr, const kaapi_memory_view_t* dview,
 const void* saddr, const kaapi_memory_view_t* sview
)
{
  memcpy(daddr, saddr, kaapi_memory_view_size(dview));
}

/*
*/
static int called = 0;
int kaapi_format_init(void)
{
  if (++called >1) return 0;

  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_schar_format, signed char, "%hhi")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_char_format, char, "%hhi")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_shrt_format, short, "%hi")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_int_format, int, "%i")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_long_format, long, "%li")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_llong_format, long long, "%lli")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_int8_format, int8_t, "%"PRIi8)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_int16_format, int16_t, "%"PRIi16)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_int32_format, int32_t, "%"PRIi32)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_int64_format, int64_t, "%"PRIi64)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uchar_format, unsigned char, "%hhu")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_ushrt_format, unsigned short, "%hu")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uint_format, unsigned int, "%u")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_ulong_format, unsigned long, "%lu")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_ullong_format, unsigned long long, "%llu")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uint8_format, uint8_t, "%"PRIu8)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uint16_format, uint16_t, "%"PRIu16)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uint32_format, uint32_t, "%"PRIu32)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_uint64_format, uint64_t, "%"PRIu64)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_flt_format, float, "%e")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_dbl_format, double, "%e")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_ldbl_format, long double, "%le")
#if !defined(__STDC_NO_COMPLEX__)
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_scplx_format, float complex, "<complex32>")
  KAAPI_REGISTER_BASICTYPEFORMAT(kaapi_dcplx_format, double complex, "<complex64>")
#endif

  kaapi_voidp_format= kaapi_format_allocate();
  kaapi_format_structregister
  ( 
    kaapi_voidp_format,
    "kaapi_voidp_format",
    sizeof(void*),
    voidp_type_cstor,
    voidp_type_dstor,
    voidp_type_cstorcopy,
    voidp_type_copy,
    voidp_type_assign
  );
  return kaapi_taskformat_init();
}


void kaapi_format_finalize(void)
{
  if (--called >0) return;
  size_t i;

  size_t nfmt = sizeof(kaapi_all_formats_bybody)/sizeof(kaapi_format_t*);
  for (i=0; i<nfmt; ++i)
  {
    kaapi_format_t* head =  kaapi_all_formats_byfmtid[i];
    kaapi_format_t* next = 0;
    for (; head; head = next)
    {
      next = head->next_byfmtid;
      if (head->name !=0) 
      { 
        free((char*)head->name); 
        head->name = 0; 
      }
      free(head);
    }
    kaapi_all_formats_byfmtid[i] = 0;
  }
  kaapi_taskformat_finalize();

  // reset format in the same state for the next kaapi_init()...
  memset( kaapi_all_formats_fnc, 0, sizeof(kaapi_all_formats_fnc) );
  kaapi_all_formats_count = 0;
  memset( kaapi_all_formats, 0, sizeof(kaapi_all_formats) );
  memset( kaapi_all_formats_byfmtid, 0, sizeof(kaapi_all_formats_byfmtid) );
  memset( kaapi_all_formats_bybody, 0, sizeof(kaapi_all_formats_bybody) );
  KAAPI_ATOMIC_WRITE(&kaapi_count_format_byfmtid,0);
}
