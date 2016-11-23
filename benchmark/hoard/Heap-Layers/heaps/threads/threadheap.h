/* -*- C++ -*- */

#ifndef HL_THREADHEAP_H
#define HL_THREADHEAP_H

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

#include "threads/cpuinfo.h"

#if !defined(_WIN32)
#include <pthread.h>
#endif

/*

  A ThreadHeap comprises NumHeaps "per-thread" heaps.

  To pick a per-thread heap, the current thread id is hashed (mod NumHeaps).

  malloc gets memory from its hashed per-thread heap.
  free returns memory to its hashed per-thread heap.

  (This allows the per-thread heap to determine the return
  policy -- 'pure private heaps', 'private heaps with ownership',
  etc.)

  NB: We assume that the thread heaps are 'locked' as needed.  */

namespace HL {

  template <int NumHeaps, class PerThreadHeap>
  class ThreadHeap : public PerThreadHeap {
  public:

    enum { Alignment = PerThreadHeap::Alignment };

    inline void * malloc (size_t sz) {
      auto tid = Modulo<NumHeaps>::mod (CPUInfo::getThreadId());
      assert (tid >= 0);
      assert (tid < NumHeaps);
      return getHeap(tid)->malloc (sz);
    }

    inline void free (void * ptr) {
      auto tid = Modulo<NumHeaps>::mod (CPUInfo::getThreadId());
      assert (tid >= 0);
      assert (tid < NumHeaps);
      getHeap(tid)->free (ptr);
    }

    inline size_t getSize (void * ptr) {
      auto tid = Modulo<NumHeaps>::mod (CPUInfo::getThreadId());
      assert (tid >= 0);
      assert (tid < NumHeaps);
      return getHeap(tid)->getSize (ptr);
    }

    
  private:

    // Access the given heap within the buffer.
    inline PerThreadHeap * getHeap (unsigned int index) {
      int ind = (int) index;
      assert (ind >= 0);
      assert (ind < NumHeaps);
      return &ptHeaps[ind];
    }

    PerThreadHeap ptHeaps[NumHeaps];

  };

}


#endif
