// -*- C++ -*-

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

#ifndef HL_HYBRIDHEAP_H
#define HL_HYBRIDHEAP_H

#include <assert.h>

#include <heaplayers.h>

/**
 * @class HybridHeap
 * Objects no bigger than BigSize are allocated and freed to SmallHeap.
 * Bigger objects are passed on to the super heap.
 */

namespace HL {

  template <int BigSize, class SmallHeap, class BigHeap>
  class HybridHeap : public SmallHeap {
  public:

    HybridHeap (void)
    {
    }

    enum { Alignment = gcd<(int) SmallHeap::Alignment, (int) BigHeap::Alignment>::value };

    MALLOC_FUNCTION INLINE void * malloc (size_t sz) {
      void * ptr;
      if (sz <= BigSize) {
        ptr = SmallHeap::malloc (sz);
      } else {
        ptr = slowPath (sz);
      }
      assert (SmallHeap::getSize(ptr) >= sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }

    inline void free (void * ptr) {
      if (SmallHeap::getSize(ptr) <= BigSize) {
        SmallHeap::free (ptr);
      } else {
        bm.free (ptr);
      }
    }

    inline void clear (void) {
      bm.clear();
      SmallHeap::clear();
    }


  private:

    MALLOC_FUNCTION NO_INLINE
    void * slowPath (size_t sz) {
      return bm.malloc (sz);
    }


    HL::sassert<(BigSize > 0)> checkBigSizeNonZero;

    BigHeap bm;
  };

}

#endif
