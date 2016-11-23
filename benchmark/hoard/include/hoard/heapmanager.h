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

#ifndef HOARD_HEAPMANAGER_H
#define HOARD_HEAPMANAGER_H

#include <cstdlib>

#include "hoardconstants.h"
#include "heaplayers.h"

namespace Hoard {

  template <typename LockType,
	    typename HeapType>
  class HeapManager : public HeapType {
  public:

    enum { Alignment = HeapType::Alignment };

    HeapManager()
    {
      HL::Guard<LockType> g (heapLock);
      
      /// Initialize all heap maps (nothing yet assigned).
      for (auto i = 0; i < HeapType::MaxThreads; i++) {
	HeapType::setTidMap (i, 0);
      }
      for (auto i = 0; i < HeapType::MaxHeaps; i++) {
	HeapType::setInusemap (i, 0);
      }
    }

    /// Set this thread's heap id to 0.
    void chooseZero() {
      HL::Guard<LockType> g (heapLock);
      HeapType::setTidMap (HL::CPUInfo::getThreadId() % Hoard::MaxThreads, 0);
    }

    int findUnusedHeap() {

      HL::Guard<LockType> g (heapLock);
      
      auto tid_original = HL::CPUInfo::getThreadId();
      auto tid = tid_original % HeapType::MaxThreads;
      
      int i = 0;
      while ((i < HeapType::MaxHeaps) && (HeapType::getInusemap(i)))
	i++;
      if (i >= HeapType::MaxHeaps) {
	// Every heap is in use: pick a random heap.
#if defined(_WIN32)
	auto randomNumber = rand();
#else
	auto randomNumber = (int) lrand48();
#endif
	i = randomNumber % HeapType::MaxHeaps;
      }

      HeapType::setInusemap (i, 1);
      HeapType::setTidMap ((int) tid, i);
      
      return i;
    }

    void releaseHeap() {
      // Decrement the ref-count on the current heap.
      
      HL::Guard<LockType> g (heapLock);
      
      // Statically ensure that the number of threads is a power of two.
      enum { VerifyPowerOfTwo = 1 / ((HeapType::MaxThreads & ~(HeapType::MaxThreads-1))) };
      
      auto tid = (int) (HL::CPUInfo::getThreadId() & (HeapType::MaxThreads - 1));
      auto heapIndex = HeapType::getTidMap (tid);
      
      HeapType::setInusemap (heapIndex, 0);
      
      // Prevent underruns (defensive programming).
      
      if (HeapType::getInusemap (heapIndex) < 0) {
	HeapType::setInusemap (heapIndex, 0);
      }
    }
    
    
  private:
    
    // Disable copying.
    
    HeapManager (const HeapManager&);
    HeapManager& operator= (const HeapManager&);
    
    /// The lock, to ensure mutual exclusion.
    LockType heapLock;
  };

}

#endif
