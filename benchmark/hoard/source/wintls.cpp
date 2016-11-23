// -*- C++ -*-

/*
  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.org
 
  Copyright (c) 1998-2015 Emery Berger

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
 * This file leverages compiler support for thread-local variables for
 * access to thread-local heaps, when available. It also intercepts
 * thread completions to flush these local heaps, returning any unused
 * memory to the global Hoard heap. On Windows, this happens in
 * DllMain. On Unix platforms, we interpose our own versions of
 * pthread_create and pthread_exit.
 */
#if defined(_WIN32)

#include <new>

#pragma warning(disable: 4447) // Disable weird warning about threading model.

// Windows TLS functions.

#include "VERSION.h"
#include "hoardheap.h"
#include "hoardtlab.h"

using namespace Hoard;

#define USE_DECLSPEC_THREADLOCAL 1

#if USE_DECLSPEC_THREADLOCAL
__declspec(thread) TheCustomHeapType * threadLocalHeap = NULL;
#else
DWORD LocalTLABIndex;
#endif

extern HoardHeapType * getMainHoardHeap();

static TheCustomHeapType * initializeCustomHeap()
{
  // Allocate a per-thread heap.
  TheCustomHeapType * heap;
  void * mh = getMainHoardHeap()->malloc(sizeof(TheCustomHeapType));
  heap = new (mh) TheCustomHeapType (getMainHoardHeap());

  // Store it in the appropriate thread-local area.
#if USE_DECLSPEC_THREADLOCAL
  threadLocalHeap = heap;
#else
  TlsSetValue (LocalTLABIndex, heap);
#endif

  return heap;
}

bool isCustomHeapInitialized() {
#if USE_DECLSPEC_THREADLOCAL
  return (threadLocalHeap != NULL);
#else
  return (TlsGetValue(LocalTLABIndex) != NULL);
#endif
}

TheCustomHeapType * getCustomHeap() {
#if USE_DECLSPEC_THREADLOCAL
  if (threadLocalHeap != NULL)
    return threadLocalHeap;
  initializeCustomHeap();
  return threadLocalHeap;
#else
  auto p = TlsGetValue(LocalTLABIndex);
  if (p == NULL) {
    initializeCustomHeap();
    p = TlsGetValue(LocalTLABIndex);
  }
  return (TheCustomHeapType *) p;
#endif
}

extern "C" void InitializeWinWrapper();
extern "C" void FinalizeWinWrapper();

static int np;

extern "C" void hoardInitialize(void) {
	np = HL::CPUInfo::computeNumProcessors();
	getCustomHeap();
}

extern "C" void hoardFinalize(void) {
}

extern "C" void hoardThreadInitialize(void) {
	if (np == 1) {
		// We have exactly one processor - just assign the thread to
		// heap 0.
		getMainHoardHeap()->chooseZero();
	}
	else {
		getMainHoardHeap()->findUnusedHeap();
	}
	getCustomHeap();
}

extern "C" void hoardThreadFinalize(void) {
	getCustomHeap()->clear();

#if USE_DECLSPEC_THREADLOCAL
	auto * heap = threadLocalHeap;
#else
	auto * heap = (TheCustomHeapType *)TlsGetValue(LocalTLABIndex);
#endif

	if (np != 1) {
		// If we're on a multiprocessor box, relinquish the heap
		// assigned to this thread.
		getMainHoardHeap()->releaseHeap();
	}
}

//
// Intercept thread creation and destruction to flush the TLABs.
//

extern "C" {

  BOOL APIENTRY DllMain (HANDLE hinstDLL,
			 DWORD fdwReason,
			 LPVOID lpreserved)
  {
    static int np = HL::CPUInfo::computeNumProcessors();

    switch (fdwReason) {
      
    case DLL_PROCESS_ATTACH:
      {
	//	fprintf (stderr, "Using the Hoard scalable memory manager (http://www.hoard.org).\n");
	//InitializeWinWrapper();
	getCustomHeap();
      }
      break;
      
    case DLL_THREAD_ATTACH:
      if (np == 1) {
	// We have exactly one processor - just assign the thread to
	// heap 0.
	getMainHoardHeap()->chooseZero();
      } else {
	getMainHoardHeap()->findUnusedHeap();
      }
      getCustomHeap();
      break;
      
    case DLL_THREAD_DETACH:
      {
	// Dump the memory from the TLAB.
	getCustomHeap()->clear();
	
#if USE_DECLSPEC_THREADLOCAL
	auto * heap = threadLocalHeap;
#else
	auto * heap = (TheCustomHeapType *) TlsGetValue(LocalTLABIndex);
#endif
	
	if (np != 1) {
	  // If we're on a multiprocessor box, relinquish the heap
	  // assigned to this thread.
	  getMainHoardHeap()->releaseHeap();
	}

      }
      break;
      
    case DLL_PROCESS_DETACH:
      //FinalizeWinWrapper();
      break;
      
    default:
      return TRUE;
    }

    return TRUE;
  }
}

#endif // _WIN32
