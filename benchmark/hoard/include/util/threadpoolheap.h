// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger
  
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

#ifndef HOARD_THREADPOOLHEAP_H
#define HOARD_THREADPOOLHEAP_H

#include <cassert>

#include "heaplayers.h"
#include "array.h"
//#include "cpuinfo.h"

namespace Hoard {

  template <int NumThreads,
	    int NumHeaps,
	    class PerThreadHeap_>
  class ThreadPoolHeap : public PerThreadHeap_ {
  public:
    
    typedef PerThreadHeap_ PerThreadHeap;
    
    enum { MaxThreads = NumThreads };
    enum { NumThreadsMask = NumThreads - 1};
    enum { NumHeapsMask = NumHeaps - 1};
    
    HL::sassert<((NumHeaps & NumHeapsMask) == 0)> verifyPowerOfTwoHeaps;
    HL::sassert<((NumThreads & NumThreadsMask) == 0)> verifyPowerOfTwoThreads;
    
    enum { MaxHeaps = NumHeaps };
    
    ThreadPoolHeap()
    {
      // Note: The tidmap values should be set externally.
      int j = 0;
      for (int i = 0; i < NumThreads; i++) {
	setTidMap(i, j % NumHeaps);
	j++;
      }
    }
    
    inline PerThreadHeap& getHeap (void) {
      auto tid = HL::CPUInfo::getThreadId();
      auto heapno = _tidMap(tid & NumThreadsMask);
      return _heap(heapno);
    }
    
    inline void * malloc (size_t sz) {
      return getHeap().malloc (sz);
    }
    
    inline void free (void * ptr) {
      getHeap().free (ptr);
    }
    
    inline void clear() {
      getHeap().clear();
    }
    
    inline size_t getSize (void * ptr) {
      return PerThreadHeap::getSize (ptr);
    }
    
    void setTidMap (int index, int value) {
      assert ((value >= 0) && (value < MaxHeaps));
      _tidMap(index) = value;
    }
    
    int getTidMap (int index) const {
      return _tidMap(index); 
    }
    
    void setInusemap (int index, int value) {
      _inUseMap(index) = value;
    }
    
    int getInusemap (int index) const {
      return _inUseMap(index);
    }
    
    
  private:
    
    /// Which heap is assigned to which thread, indexed by thread.
    Array<MaxThreads, int> _tidMap;
    
    /// Which heap is in use (a reference count).
    Array<MaxHeaps, int> _inUseMap;
    
    /// The array of heaps we choose from.
    Array<MaxHeaps, PerThreadHeap> _heap;
    
  };
  
}

#endif
