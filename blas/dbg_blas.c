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

#include <assert.h>
#include "common.h"

/* Give name of tile for various output (graph, debug).
   Each tile will be display as name(i,j) where i,j is the position in A.
*/
int xkblas_dbg_setname(
  const char* name,
  xkblas_matrix_descr_t* Ah
)
{
  if (name ==0) return EINVAL;
  if (Ah ==0) return EINVAL;

  size_t Amt = Ah->mt;
  size_t Ant = Ah->nt;
  for (size_t m = 0; m < Amt; m++)
    for (size_t n = 0; n < Ant; n++) 
    {
      char buffer[64];
      snprintf(buffer,64,"%s(%i,%i)",name, (int)m, (int)n);
      kaapi_dbg_register_name( (void*)( (uintptr_t)Ah->addr + Ah->eltsize*(m*Ah->mb + n*Ah->nb*Ah->ld)), buffer );
    }
  return 0;
}


/*
*/
void xkblas_dbg_dump_graph( const char* name )
{
  extern kaapi_handle_t* xkblas_context_get_list_sync0(void);
  static int cnt = 0;
  char buffer[128];
  if (name ==0)
    snprintf(buffer,128,"file%i.dot", cnt++);
  else
    snprintf(buffer,128,"%s.dot", name);
  printf("<<<< Dump graph in file '%s\n", buffer);
  kaapi_dump_dot_list_handle(xkblas_self_thread(), xkblas_context_get_list_sync0(), buffer);
  printf("<<<< End dump graph in file '%s\n", buffer);
}
