/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012 INRIA.
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** francois.broquedis@imag.fr
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include "kaapi.h"
#include "kaapi_impl.h"
#include "kaapi_trace.h"
#include "kaapi_trace_util.h"


#if defined(__cplusplus)
extern "C" {
#endif

/* end of list
 */
static inline int is_eol(const int c)
{
  /* end of list */
  return (c == ',') || (c == 0);
}

/*
*/
static void eat_space(char** str)
{
  while ((*str !=0) && isspace ((int)**str))
    ++*str;
}




/* Parse the interger with unit that represents a size.
   Return true if one was successfully parsed.
*/
bool kaapi_parse_size (
    char **str,
    unsigned long *pvalue
)
{
  char *end;
  unsigned long value, shift = 10;
  
  if (*str == 0)
    return false;
  
  eat_space(str);
  if (*str == 0)
    goto invalid;

  errno = 0;
  value = strtoul (*str, &end, 10);
  if (errno || value == ULONG_MAX)
    goto invalid;

  *str = end;
  eat_space(str);

  if (isalpha((int)**str))
  {
    switch (tolower ((int) **str))
    {
      case 'b':
        shift = 0;
        break;
      case 'k':
        shift = 10;
        break;
      case 'm':
        shift = 20;
        break;
      case 'g':
        shift = 30;
        break;
      default:
        goto invalid;
    }
    ++*str;
    eat_space(str);
  }
  
  if (((value << shift) >> shift) != value)
    goto invalid;
  
  *pvalue = value << shift;
  return true;
  
invalid:
  return false;
}


/* Parse an unsigned long long environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_unsigned_longlong(
    char** str,
    unsigned long long *pvalue
)
{
  char *end;
  unsigned long long value;

  if (*str == 0)
    return false;

  eat_space(str);
  if (**str == '\0')
    goto invalid;

  errno = 0;
  value = strtoul (*str, &end, 10);
  if (errno || value == ULLONG_MAX)
    goto invalid;

  *pvalue = (unsigned long long)value;
  *str = end;
  return true;

 invalid:
  return false;
}


/* Parse an unsigned long environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_unsigned_long(
    char** str,
    unsigned long *pvalue
)
{
  unsigned long long value = 0;
  if (!kaapi_parse_unsigned_longlong(str, &value))
    return false;
  if (value > ULONG_MAX)
     return false;
  *pvalue = (unsigned long)value;
  return true;
}


/* Parse an unsigned int environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_unsigned_int(
    char** str,
    unsigned int *pvalue
)
{
  unsigned long long value = 0;
  if (!kaapi_parse_unsigned_longlong(str, &value))
    return false;
  if (value > UINT_MAX)
     return false;
  *pvalue = (unsigned int)value;
  return true;
}


/* Parse an unsigned int environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_unsigned_short(
    char** str,
    unsigned short *pvalue
)
{
  unsigned long long value = 0;
  if (!kaapi_parse_unsigned_longlong(str, &value))
    return false;
  if (value > USHRT_MAX)
     return false;
  *pvalue = (unsigned short)value;
  return true;
}


/* Parse an unsigned long environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_int (char** str, int *pvalue)
{
  unsigned long value = 0;
  if (!kaapi_parse_unsigned_long(str, &value))
    return false;
  if ((value > INT_MAX) || ((long)value < INT_MIN))
     return false;
  *pvalue = (int)value;
  return true;
}

/* Parse an unsigned long environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool
kaapi_parse_short (char** str, short *pvalue)
{
  unsigned long value = 0;
  if (!kaapi_parse_unsigned_long(str, &value))
    return false;
  if ((value > SHRT_MAX) || ((long)value < SHRT_MIN))
     return false;
  *pvalue = (short)value;
  return true;
}


/* Parse an unsigned long environment varible.  Return true if one was
   present and it was successfully parsed.  */
bool kaapi_parse_list_unsigned_int64 (char** str, unsigned int* count, uint64_t **pvalue)
{
  unsigned long long value;
  uint64_t* list = 0;
  int size = 0;

  if (*str == NULL)
    return false;
  *count = 0;

redo:
  if (!kaapi_parse_unsigned_longlong(str, &value))
    goto invalid;

  if (*count+1 > size)
  {
    size = 2*size;
    if (size < *count+2) size = (*count+2)*2;
    list = (uint64_t*)realloc( list, size*sizeof(uint64_t));
    memset( list + *count, 0, (size - *count)*sizeof(uint64_t));
  }
  list[*count] = (uint64_t)value;
  ++*count;

  eat_space(str);
  if (!is_eol((int)**str))
    goto invalid;

  if (**str == ',')
  {
    ++*str;
    goto redo;
  }

  *pvalue = list;
  return true;

invalid:
  if (list !=0) free(list);
  return false;
}

bool kaapi_parse_list_unsigned_int (char** str, unsigned int* count, unsigned int **pvalue)
{
  uint64_t* values = 0;
  bool err = kaapi_parse_list_unsigned_int64(str, count, &values );
  if (err) {
    *pvalue = (unsigned int*)malloc( sizeof(unsigned int)* *count );
    for (unsigned int i=0; i<*count; ++i)
      (*pvalue)[i] = (unsigned int)values[i];
  }
  return err;
}


/* Parse an unsigned long environment varible.  Return true if one was
   present and it was successfully parsed.  */
/* TODO: factor with previous definition */
bool kaapi_parse_list_unsigned_short (char** str, unsigned short* count, unsigned short **pvalue)
{
  unsigned short value;
  unsigned short* list = 0;
  int size = 0;

  if (*str == NULL)
    return false;
  *count = 0;

redo:
  if (!kaapi_parse_unsigned_short(str, &value))
    goto invalid;

  if (*count+1 > size)
  {
    size = 2*size;
    if (size < *count+2) size = (*count+2)*2;
    list = (unsigned short*)realloc( list, size*sizeof(unsigned short));
    memset( list + *count, 0, (size - *count)*sizeof(unsigned short));
  }
  list[*count] = (unsigned short)value;
  ++*count;

  eat_space(str);
  if (!is_eol((int)**str))
    goto invalid;

  if (**str == ',')
  {
    ++*str;
    goto redo;
  }

  *pvalue = list;
  return true;

invalid:
  if (list !=0) free(list);
  return false;
}


/* Parse the Bool environment variable. 
*/
bool
kaapi_parse_bool (char** str, int8_t* pvalue)
{
  if (*str == 0)
    return false;

  eat_space(str);

  if (strncasecmp (*str, "true", 4) == 0)
  {
    *pvalue = true;
    *str += 4;
  }
  else if (strncasecmp (*str, "false", 5) == 0)
  {
    *pvalue = false;
    *str += 5;
  }
  else
    return false;
  return true;
}

/* Parse the delay
    <unsigned long> [ms|s|ns|us>
*/
bool
kaapi_parse_delay (char** str, uint64_t* pvalue)
{
  if (*str == 0)
    return false;

  long long unsigned int luivalue;
  if (!kaapi_parse_unsigned_longlong(str,&luivalue))
    return false;
  *pvalue = luivalue;

  eat_space(str);

  if (strncasecmp (*str, "s", 1) == 0)
  {
    *pvalue *= 1000000000UL;
    *str += 1;
  }
  else if (strncasecmp (*str, "ms", 2) == 0)
  {
    *pvalue *= 1000000UL;
    *str += 2;
  }
  else if (strncasecmp (*str, "us", 2) == 0)
  {
    *pvalue *= 1000UL;
    *str += 2;
  }
  else if (strncasecmp (*str, "ns", 2) == 0)
  {
    *str += 2;
  }
  else if (*str == 0)
    return true;
  else
    return false;
  return true;
}


/** Dictionnary of predefined identifier
*/
struct _kaapi_constant {
  const char* ident;
  uint64_t    value;
};

struct _kaapi_dico {
  int                     count_constants;
  struct _kaapi_constant* listconstants;
};



/* Return:
   !0: if identifier is successfully parsed.
   else error
*/
static bool _kaapi_parse_ident( char** str, uint64_t* retval, struct _kaapi_dico* kp )
{
  char name[128];
  char* wpos = name;

  if (**str == 0)
    return ENOENT;

  if (isdigit(**str))
    return EINVAL;

  while ( (*str !=0) && isalpha(**str) )
  {
    *wpos = **str;
    ++*str;
    ++wpos;
    if (wpos == name + 127) break;
  }
  *wpos = 0;
  
  /* search value of the ident in the table of constants 
     linear search
  */
  for (int i=0; i<kp->count_constants; ++i)
  {
    if (strcasecmp(name, kp->listconstants[i].ident) == 0)
    {
      *retval = kp->listconstants[i].value;
      return true;
    }
  }
  
  /* not found: return EINVAL */
  return false;
}


/*
*/
bool kaapi_parse_listkeywords(
  uint64_t* mask, char** str, char sep,
  int count_constants,
  ...
)
{
  struct _kaapi_dico kp;
  unsigned long long value;
  va_list va_args;
  
  kp.count_constants = count_constants;
  kp.listconstants = 0;
  if (mask !=0)
    *mask = 0;
  
  /* read array of constants */
  if (count_constants >0)
  {
    kp.listconstants = (struct _kaapi_constant*)alloca( count_constants*sizeof(struct _kaapi_constant) );
    va_start(va_args, count_constants);
    for (int i=0; i<count_constants; ++i)
    {
      kp.listconstants[i].ident = va_arg(va_args, const char*);
      kp.listconstants[i].value = va_arg(va_args, uint64_t);
    }
    va_end(va_args);
  }
  
  while (**str != 0)
  {
    bool err;

    /* lookup next token */
    eat_space( str );
    if (isalpha(**str))
    {
      uint64_t v;
      err = _kaapi_parse_ident( str, &v, &kp );
      value = (unsigned long long)v;
    }
    else
    {
      err = kaapi_parse_unsigned_longlong( str,  &value );
      if (value > 63) /* TODO CONSTANT HERE */
        return EINVAL;
      value = (1UL << value);
    }
    if (err ==false)
      return false;

    if (mask !=0)
      *mask |= value;
      
    /* look for 'sep' */
    eat_space( str );
    if (**str == 0)
      return true;
      
    if (**str != sep)
      return false;
    ++*str;
  }
  
  return true;
}

#if defined(__cplusplus)
}
#endif
