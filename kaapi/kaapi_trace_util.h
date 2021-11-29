/*
** kaapi_error.h
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012 INRIA.
**
** Contributors :
**
** fabien.lementec@imag.fr
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
#ifndef _KAAPI_UTIL_H_
#define _KAAPI_UTIL_H_ 1

#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif


/* Parse the interger with unit (b,k,m,g) that represents a size.
   Return true if one was successfully parsed.
*/
bool kaapi_parse_size (char** str, unsigned long *pvalue);

/* Parse an unsigned long long.  Return true if one was
   present and it was successfully parsed.
*/
bool kaapi_parse_unsigned_longlong( char** str, unsigned long long *pvalue );

/* Parse an unsigned long.  Return true if one was
   present and it was successfully parsed.
*/
bool kaapi_parse_unsigned_long( char** str, unsigned long *pvalue );

/* Parse an unsigned int.  Return true if one was
   present and it was successfully parsed.
*/
bool kaapi_parse_unsigned_int( char** str, unsigned int *pvalue );

/* Parse an unsigned short.  Return true if one was
   present and it was successfully parsed.
*/
bool kaapi_parse_unsigned_short( char** str, unsigned short *pvalue );

/* Parse an signed int.  Return true if one was
   present and it was successfully parsed.  
*/
bool kaapi_parse_int( char** str, int *pvalue );

/* Parse an signed short.  Return true if one was
   present and it was successfully parsed.  
*/
bool kaapi_parse_short( char** str, short *pvalue );

/* Parse a list of unsigned int.  Return true if one was
   present and it was successfully parsed.
   The list was allocated by malloc/realloc. The caller should deallocates
   the list using free.
*/
bool kaapi_parse_list_unsigned_int (char** str, unsigned int* count, unsigned int **pvalue);
bool kaapi_parse_list_unsigned_int64 (char** str, unsigned int* count, uint64_t **pvalue);

/* Parse a list of unsigned int.  Return true if one was
   present and it was successfully parsed.
   The list was allocated by malloc/realloc. The caller should deallocates
   the list using free.
*/
bool kaapi_parse_list_unsigned_short (char** str, unsigned short* count, unsigned short **pvalue);


#if KAAPI_PROCBIND_DEFINED
/* parse PROC_BIND value: false,true,master,close,spread and
   return the numerical value komp_proc_bind_t
*/
bool kaapi_parse_procbind (char** str, kaapi_procbind_t* procbind );


/* Parse a list of kaapi_procbind_t.  Return true if one was
   present and it was successfully parsed.  
   The list was allocated by malloc/realloc. The caller should deallocates
   the list using free.
*/
bool kaapi_parse_list_procbind (char** str, unsigned int* count, kaapi_procbind_t **pvalue);
#endif

/* Parse the Bool environment variable. 
   if == true *pvalue =1
   else == false *pvalue = 0
   Return true if one was successfully parsed.
*/
bool kaapi_parse_bool (char** str, int8_t* pvalue);

/* Parse the delay
    <unsigned long> [ms|s|ns|us>
   Return true if one was successfully parsed.
*/
bool kaapi_parse_delay (char** str, uint64_t* pvalue);

/* Parse the ccsync|noaggr.
   Initialize the steal protocol in the runtime parameter rt_param + initialization and 
   dstor function.
*/
struct kaapi_rtparam_t;
bool kaapi_parse_stealprotocol (char** str, struct kaapi_rtparam_t* rt_param);

#if KAAPI_SCHEDPOLICY_DEFINED
/* Parse (static|dynamic|auto|guided|adaptive|steal)[,chunksize]
   If chunksize (unsigned long) not given, then set it to 0.
*/
bool kaapi_parse_schedule (
    char** str,
    kaapi_foreach_attr_policy_t* prun_sched,
    int* prun_sched_modifier
);
#endif

/** Parse : [!] [ low ] [ -|: [ high ] [: stride] ]
    low -> num | <empty>
    high -> num | <empty>
    if empty detected for low, high then it set value to (unsigned int)-1.
    If stride pointer is not null and no stride is specified, then stride is set to 1.
    If stride pointer is null and a stride is specified, then it is an error
    If '!' is present, *negate = 1 else *negate =0
 */
bool kaapi_parse_range(
    char** str,
    unsigned int* index_low,
    unsigned int* index_high,
    int* stride
);


#if defined(_KAAPI_IMPL_H)

#if 0 // kaapi_cpuset_t not exposed in this version 
/*
*/
bool kaapi_parse_oneplace(
    char** str,
    unsigned int* cpu_count,
    kaapi_cpuset_t* places
);

/*
*/
bool kaapi_parse_places(
    char** str,
    unsigned int* count,
    unsigned int* cpu_count,
    kaapi_cpuset_t** places
);

/*
*/
char* kaapi_unparse_places(
    unsigned int count,
    kaapi_cpuset_t* places
);

/*
*/
char* kaapi_unparse_places_r(
    char* buffer, unsigned int size,
    unsigned int count,
    kaapi_cpuset_t* places
);
#endif
#endif

/* parse list of integer separated by sep 
   return the union of (1<<i) if i is the integer in the list
*/
bool kaapi_parse_listkeywords(
  uint64_t* mask, char** str, char sep,
  int count_constants,
  ...
);

#ifdef __cplusplus
}
#endif

#endif
