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

/**
 *
 * @class  ThreadLocalAllocationBuffer
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @brief  An allocator, meant to be used for thread-local allocation.
 */

#ifndef HOARD_TLAB_H
#define HOARD_TLAB_H

#include "heaplayers.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  template <int NumBins,
	    int (*getSizeClass) (size_t),
	    size_t (*getClassSize) (int),
	    size_t LargestObject,
	    size_t LocalHeapThreshold,
	    class SuperblockType,
	    unsigned int SuperblockSize,
	    class ParentHeap>

  class ThreadLocalAllocationBuffer {

    enum { DesiredAlignment = HL::MallocInfo::Alignment };

  public:

    enum { Alignment = ParentHeap::Alignment };

    ThreadLocalAllocationBuffer (ParentHeap * parent)
      : _parentHeap (parent),
      	_localHeapBytes (0)
    {
      sassert<gcd<Alignment, DesiredAlignment>::value == DesiredAlignment> verifyAlignment;
      sassert<(Alignment >= 2 * sizeof(size_t))> verifyCanHoldTwoPointers;
      verifyAlignment = verifyAlignment;
      verifyCanHoldTwoPointers = verifyCanHoldTwoPointers;
    }

    ~ThreadLocalAllocationBuffer() {
      clear();
    }

    inline static size_t getSize (void * ptr) {
      return getSuperblock(ptr)->getSize (ptr);
    }

    inline void * malloc (size_t sz) {
      if (sz < Alignment) {
      	sz = Alignment;
      }
      // Get memory from the local heap,
      // and deduct that amount from the local heap bytes counter.
      if (sz <= LargestObject) {
      	auto c = getSizeClass (sz);
      	auto * ptr = _localHeap(c).get();
      	if (ptr) {
      	  assert (_localHeapBytes >= sz);
      	  _localHeapBytes -= getClassSize (c); // sz; 
      	  assert (getSize(ptr) >= sz);
      	  assert ((size_t) ptr % Alignment == 0);
      	  return ptr;
      	}
      }

      // No more local memory (for this size, at least).
      // Now get the memory from our parent.
      auto * ptr = _parentHeap->malloc (sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }


    inline void free (void * ptr) {
      if (!ptr) {
	return;
      }
      auto * s = getSuperblock (ptr);
      // If this isn't a valid superblock, just return.

      if (s->isValidSuperblock()) {

      	ptr = s->normalize (ptr);
      	auto sz = s->getObjectSize ();

      	if ((sz <= LargestObject) && (sz + _localHeapBytes <= LocalHeapThreshold)) {
      	  // Free small objects locally, unless we are out of space.

      	  assert (getSize(ptr) >= sizeof(HL::SLList::Entry *));
      	  auto c = getSizeClass (sz);

      	  _localHeap(c).insert ((HL::SLList::Entry *) ptr);
      	  _localHeapBytes += getClassSize(c); // sz;
      	  
      	} else {

      	  // Free it to the parent.
      	  _parentHeap->free (ptr);
      	}

      } else {
      	// Illegal pointer.
      }
    }

    void clear() {
      // Free every object to the 'parent' heap.
      int i = NumBins - 1;
      while ((_localHeapBytes > 0) && (i >= 0)) {
      	auto sz = getClassSize (i);
      	while (!_localHeap(i).isEmpty()) {
      	  auto * e = _localHeap(i).get();
      	  _parentHeap->free (e);
      	  _localHeapBytes -= sz;
      	}
      	i--;
      }
    }

    static inline SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

  private:

    // Disable assignment and copying.

    ThreadLocalAllocationBuffer (const ThreadLocalAllocationBuffer&);
    ThreadLocalAllocationBuffer& operator=(const ThreadLocalAllocationBuffer&);

    /// Padding to prevent false sharing and ensure alignment.
    double _pad[128 / sizeof(double)];

    /// This heap's 'parent' (where to go for more memory).
    ParentHeap * _parentHeap;

    /// The number of bytes we currently have on this thread.
    size_t _localHeapBytes;

    /// The local heap itself.
    Array<NumBins, HL::SLList> _localHeap;
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

