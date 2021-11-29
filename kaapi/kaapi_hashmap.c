/*
** Copyright 2009-2013,2018,2019 INRIA
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
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

#include "kaapi_impl.h"
#include <string.h>

/*
*/
uint32_t kaapi_hash_value_len(const char* s, size_t len)
{
  /* oat */

  uint32_t h = 0;

  for (; len; ++s, --len)
  {
    h += *s;
    h += (h << 10);
    h ^= (h >>  6);
  }

  h += (h <<  3);
  h ^= (h >> 11);
  h += (h << 15);

  return h;
}

/*
*/
uint32_t kaapi_hash_value(const char* s)
{
  if (*s == 0) return 0;
  return kaapi_hash_value_len(s, strlen(s));
}


/*
*/
int kaapi_hashmap_init(
    kaapi_hashmap_t* khm,
    kaapi_hashentries_t** entries,
    size_t powsize,
    kaapi_hashentries_bloc_t* initbloc
)
{
  khm->entries = entries;
  kaapi_assert_debug( powsize <= 20 );

  khm->mask    = (1<<powsize) - 1;  /* mask for euclidian division */
  khm->currentbloc = initbloc;
  khm->freebloc = 0;
  khm->allallocatedbloc = 0;
  if (initbloc !=0)
    khm->currentbloc->pos = 0;
  /* */
  memset( khm->entries, 0, (khm->mask+1)*sizeof(kaapi_hashentries_t*));
  return 0;
}


/*
*/
int kaapi_hashmap_clear( kaapi_hashmap_t* khm )
{
  if (khm->currentbloc !=0)
    khm->currentbloc->pos = 0;
  if (khm->allallocatedbloc !=0)
  {
    kaapi_assert_debug( khm->freebloc ==0);
    khm->freebloc = khm->allallocatedbloc;
  }
  khm->allallocatedbloc = 0;
  khm->currentbloc = 0;
  memset( khm->entries, 0, (khm->mask+1)*sizeof(kaapi_hashentries_t*));

  return 0;
}


/*
*/
static void _kaapi_hashmap_destroy_list( kaapi_hashentries_bloc_t* list )
{
  while (list !=0)
  {
    kaapi_hashentries_bloc_t* curr = list;
    list = curr->next;
    free (curr);
  }
}


/*
*/
int kaapi_hashmap_destroy( kaapi_hashmap_t* khm )
{
  _kaapi_hashmap_destroy_list(khm->allallocatedbloc);
  _kaapi_hashmap_destroy_list(khm->freebloc);
  khm->currentbloc = 0;
  khm->freebloc = 0;
  khm->allallocatedbloc = 0;
  return 0;
}


/*
*/
static kaapi_hashentries_bloc_t* kaapi_hashmap_allocate_bloc(kaapi_hashmap_t* khm)
{
  khm->currentbloc = khm->freebloc;
  if (khm->currentbloc ==0)
  {
    khm->currentbloc = (kaapi_hashentries_bloc_t*)malloc( sizeof(kaapi_hashentries_bloc_t) );
    khm->currentbloc->next = khm->allallocatedbloc;
    khm->allallocatedbloc = khm->currentbloc;
//printf("@:%p, Alloc: bloc @:%p size:%i\n", khm, khm->currentbloc, sizeof(kaapi_hashentries_bloc_t));
  }
  else
  {
    khm->freebloc = khm->currentbloc->next;
  }
  khm->currentbloc->pos = 0;
  return khm->currentbloc;
}



/*
*/
static kaapi_hashentries_t* _kaapi_hashmap_find( kaapi_hashmap_t* khm, const void* ptr, const uint32_t hkey )
{
  kaapi_hashentries_t* list_hash = _get_hashmap_entry(khm, hkey);
  kaapi_hashentries_t* entry = list_hash;
  while (entry != 0)
  {
    if (entry->key == ptr) return entry;
    entry = entry->next;
  }
  return 0;
}

/*
*/
kaapi_hashentries_t* kaapi_hashmap_find( kaapi_hashmap_t* khm, const void* ptr )
{
  const uint32_t hkey = kaapi_hash_ulong((unsigned long)ptr) & khm->mask;
  return _kaapi_hashmap_find(khm, ptr, hkey);
}

/*
*/
static kaapi_hashentries_t* _kaapi_hashmap_insert(
  kaapi_hashmap_t* khm, const void* ptr, const uint32_t hkey
)
{
  kaapi_hashentries_t* list_hash = _get_hashmap_entry( khm, hkey );
  kaapi_hashentries_t* entry;

  /* allocate new entry */
  if (khm->currentbloc == 0) 
    khm->currentbloc = kaapi_hashmap_allocate_bloc(khm);

  entry = &khm->currentbloc->data[khm->currentbloc->pos++];
  entry->key = ptr;
  memset( &entry->u, 0, sizeof(entry->u) );

  if (khm->currentbloc->pos == KAAPI_BLOCENTRIES_SIZE)
    khm->currentbloc = 0;

  entry->next = list_hash;
  _set_hashmap_entry(khm, hkey, entry);
  return entry;
}

/*
*/
kaapi_hashentries_t* kaapi_hashmap_insert( kaapi_hashmap_t* khm, const void* ptr )
{
  const uint32_t hkey = kaapi_hash_ulong((unsigned long)ptr) & khm->mask;
  return _kaapi_hashmap_insert( khm, ptr, hkey );
}

/*
*/
kaapi_hashentries_t* kaapi_hashmap_findinsert( kaapi_hashmap_t* khm, const void* ptr )
{
  const uint32_t hkey = kaapi_hash_ulong((unsigned long)ptr) & khm->mask;
  kaapi_hashentries_t* entry = _kaapi_hashmap_find(khm, ptr, hkey);
  if (entry !=0) return entry;
  return _kaapi_hashmap_insert( khm, ptr, hkey );
}


