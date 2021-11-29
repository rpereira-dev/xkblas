/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** clement.pernet@imag.fr
** fabien.lementec@imag.fr
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

#ifndef _KAAPI_HASHMAP_H_
#define _KAAPI_HASHMAP_H_ 1

#include <stdint.h>
#include "kaapi_error.h"

/* ============================= Basic type ============================ */
/** \ingroup DFG
*/


/* ============================= Hash table for WS ============================ */
/*
*/
#define KAAPI_BLOCENTRIES_SIZE 2048

/* Generic blocs with KAAPI_BLOCENTRIES_SIZE entries
*/
#define KAAPI_DECLARE_BLOCENTRIES(NAME, TYPE) \
typedef struct NAME {\
  TYPE         data[KAAPI_BLOCENTRIES_SIZE]; \
  uintptr_t    pos;  /* next free in data */\
  struct NAME* next; /* link list of bloc */\
  void*        ptr;  /* memory pointer of allocated bloc */\
} NAME


/* HashEntries was typed in order to add more semantics about stored valued.
*/
typedef struct kaapi_hashentries_t {
  const void*                          key;
  struct kaapi_hashentries_t*          next;
  uintptr_t                            u[2]; /* should be enough ! */
} kaapi_hashentries_t;

KAAPI_DECLARE_BLOCENTRIES(kaapi_hashentries_bloc_t, kaapi_hashentries_t);

#if KAAPI_DEBUG
static inline int _kaapi_test_size( size_t s )
{ kaapi_assert( s <= sizeof(((kaapi_hashentries_t*)0)->u)); return 1; }
#define _KAAPI_VERIF_SIZE(type,inst) (_kaapi_test_size(sizeof(type)) ? inst: 0)
#else
#define _KAAPI_VERIF_SIZE(type,inst) inst
#endif

#define KAAPI_HASHENTRIES_GETREF(ptr,type) _KAAPI_VERIF_SIZE(type, ((type*)&(ptr)->u))
#define KAAPI_HASHENTRIES_GET(ptr,type) _KAAPI_VERIF_SIZE(type, *((type*)&(ptr)->u))
#define KAAPI_HASHENTRIES_SET(ptr,value,type) _KAAPI_VERIF_SIZE(type, *((type*)&(ptr)->u) = (value))

/* ========================================================================== */
/* Hashmap
 */
typedef struct kaapi_hashmap_t {
  kaapi_hashentries_t**     entries;
  size_t                    mask;                /* mask = size - 1 where size is 2^k */
  kaapi_hashentries_bloc_t* currentbloc;
  kaapi_hashentries_bloc_t* freebloc;
  kaapi_hashentries_bloc_t* allallocatedbloc;
} kaapi_hashmap_t;



/* ========================================================================== */
/** Compute a hash value from a string
*/
extern uint32_t kaapi_hash_value_len(const char * data, size_t len);

/*
*/
extern uint32_t kaapi_hash_value(const char * data);

/**
 * Compression 64 -> 7 bits
 * Sums the 8 bytes modulo 2, then reduces the resulting degree 7 
 * polynomial modulo X^7 + X^3 + 1
 */
static inline uint32_t kaapi_hash_ulong7(uint64_t v)
{
  v ^= (v >> 32);
  v ^= (v >> 16);
  v ^= (v >> 8);
  if (v & 0x00000080) v ^= 0x00000009;
  return (uint32_t) (v&0x0000007F);
}


/**
 * Compression 64 -> 6 bits
 * Sums the 8 bytes modulo 2, then reduces the resulting degree 7 
 * polynomial modulo X^6 + X + 1
 */
static inline uint32_t kaapi_hash_ulong6(uint64_t v)
{
  v ^= (v >> 32);
  v ^= (v >> 16);
  v ^= (v >> 8);
  if (v & 0x00000040) v ^= 0x00000003;
  if (v & 0x00000080) v ^= 0x00000006;
  return (uint32_t) (v&0x0000003F);
}

/**
 * Compression 64 -> 5 bits
 * Sums the 8 bytes modulo 2, then reduces the resulting degree 7 
 * polynomial modulo X^5 + X^2 + 1
 */
static inline uint32_t kaapi_hash_ulong5(uint64_t v)
{
  v ^= (v >> 32);
  v ^= (v >> 16);
  v ^= (v >> 8);
  if (v & 0x00000020) v ^= 0x00000005;
  if (v & 0x00000040) v ^= 0x0000000A;
  if (v & 0x00000080) v ^= 0x00000014;
  return (uint32_t) (v&0x0000001F);
}


/** Hash value for pointer.
    Used for data flow dependencies
*/
static inline uint32_t kaapi_hash_ulong(uint64_t v)
{
#if 1
  v ^= (v >> 32);
  v ^= (v >> 17);
  v ^= (v >> 8);
  return (uint32_t)v; // (uint32_t) ( v & 0x0000FFFF);
#elif 0
  v ^= v >> 23;
  v *= 0x2127599bf4325c37ULL;
  v ^= v >> 47;
  return (uint32_t)v;
#elif 0
  uint64_t val = v >> 3;
  v = (v & 0xFFFF) ^ (v>>32);
  return (uint32_t)v;
#endif
}

/*
*/
static inline uint64_t _key_to_mask(uint32_t k)
{ return ((uint64_t)1) << (k%64); }

/*
*/
static inline uint64_t _key_to_index(uint32_t k)
{ return k/64; }


static inline size_t kaapi_hashmap_sizeentries(kaapi_hashmap_t* khm)
{
  return khm->mask+1;
}

/*
*/
static inline kaapi_hashentries_t* _get_hashmap_entry(kaapi_hashmap_t* khm, uint32_t key)
{
  kaapi_assert_debug(key <= kaapi_hashmap_sizeentries(khm));
  return khm->entries[key];
}

/*
*/
static inline kaapi_hashentries_t* _pop_hashmap_entry(kaapi_hashmap_t* khm, uint32_t key)
{
  kaapi_hashentries_t* entry = 0;
  kaapi_assert_debug(key <= kaapi_hashmap_sizeentries(khm));
  entry = khm->entries[key];
  khm->entries[key] = (entry == 0 ? 0 : entry->next);
  return entry;
}

/*
*/
static inline void _set_hashmap_entry
(kaapi_hashmap_t* khm, uint32_t key, kaapi_hashentries_t* entries)
{
  kaapi_assert_debug(key <= kaapi_hashmap_sizeentries(khm));
  khm->entries[key] = entries;
}

/* Init hashmap. psize if the power of 2 that correspond to the size.
   I.e. the size of the hashmap is 2^p.
*/
extern int kaapi_hashmap_init(
    kaapi_hashmap_t* khm,
    kaapi_hashentries_t** entries,
    size_t psize,
    kaapi_hashentries_bloc_t* initbloc
);

/*
*/
extern int kaapi_hashmap_clear( kaapi_hashmap_t* khm );

/*
*/
extern int kaapi_hashmap_destroy( kaapi_hashmap_t* khm );

/*
*/
extern kaapi_hashentries_t* kaapi_hashmap_findinsert( kaapi_hashmap_t* khm, const void* ptr );

/*
*/
extern kaapi_hashentries_t* kaapi_hashmap_find( kaapi_hashmap_t* khm, const void* ptr );

/*
*/
extern kaapi_hashentries_t* kaapi_hashmap_insert( kaapi_hashmap_t* khm, const void* ptr );

#endif
