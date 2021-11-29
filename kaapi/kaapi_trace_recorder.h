/*
** xkaapi
** 
**
** Copyright 2009,2010,2011,2012, 2021 INRIA.
**
** Contributors :
**
** thierry.gautier@inrialpes.fr
** fabien.lementec@gmail.com / fabien.lementec@imag.fr
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
#ifndef _KAAPI_TRACE_RECORDER_H
#define _KAAPI_TRACE_RECORDER_H 1

#include <stdint.h>
#include <stddef.h>
#include "kaapi_trace.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* ========================================================================= */
/* Kaapi recorder sub library                                                */
/* ========================================================================= */
/** Initialize the event recorder sub library.
    Must be called before any other functions.
*/
void kaapi_eventrecorder_init(void);


/** Destroy the event recorder sub library.
*/
void kaapi_eventrecorder_fini(void);

/** Open a new buffer for kprocessor kid.
    Kid must be a valid kid.
    \param ptype the type (KAAP_PROC_TYPE_XXX) of kid
    \param kid the identifier of a kprocessor or a device. Must be unique between threads.
    \retval a new eventbuffer
*/
extern kaapi_event_buffer_t* kaapi_event_openbuffer(int kid, unsigned int ptype);


/** Flush the event buffer and close the associated file descriptor.
*/
extern void kaapi_event_closebuffer( kaapi_event_buffer_t* evb );


/** Fence: write all flushed buffers and return
*/
extern int kaapi_event_fencebuffers(void);


#if defined(__cplusplus)
}
#endif

#endif
