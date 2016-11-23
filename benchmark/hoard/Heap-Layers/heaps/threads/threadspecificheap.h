/* -*- C++ -*- */

#ifndef HL_THREADSPECIFICHEAP_H
#define HL_THREADSPECIFICHEAP_H

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
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

#include <assert.h>
#include <new>

#if !defined(_WIN32) // not implemented for Windows

#include <pthread.h>

#include "wrappers/mmapwrapper.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace HL {

  template <class PerThreadHeap>
  class ThreadSpecificHeap {
  public:

    ThreadSpecificHeap (void)
    {
      // Initialize the heap exactly once.
      pthread_once (&(getOnce()), initializeHeap);
    }

    virtual ~ThreadSpecificHeap()
    {
    }

    inline void * malloc (size_t sz) {
      return getHeap()->malloc (sz);
    }

    inline void free (void * ptr) {
      getHeap()->free (ptr);
    }

    inline size_t getSize (void * ptr) {
      return getHeap()->getSize(ptr);
    }

    enum { Alignment = PerThreadHeap::Alignment };

  private:

    static void initializeHeap() {
      getHeap();
    }

    static pthread_key_t& getHeapKey() {
      static pthread_key_t heapKey;
      static int r = pthread_key_create (&heapKey, deleteHeap);
      return heapKey;
    }

    static pthread_once_t& getOnce() {
      static pthread_once_t initOnce = PTHREAD_ONCE_INIT;
      return initOnce;
    }

    static void deleteHeap (void *) {
      PerThreadHeap * heap = getHeap();
      HL::MmapWrapper::unmap (heap, sizeof(PerThreadHeap));
    }

    // Access the given heap.
    static PerThreadHeap * getHeap() {
      PerThreadHeap * heap =
	(PerThreadHeap *) pthread_getspecific (getHeapKey());
      if (heap == NULL)  {
	// Grab some memory from a source, initialize the heap inside,
	// and store it in the thread-local area.
	void * buf = HL::MmapWrapper::map (sizeof(PerThreadHeap));
	heap = new (buf) PerThreadHeap;
	pthread_setspecific (getHeapKey(), (void *) heap);
      }
      return heap;
    }
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

#endif
