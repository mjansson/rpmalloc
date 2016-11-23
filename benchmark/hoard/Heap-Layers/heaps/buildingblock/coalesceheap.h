/* -*- C++ -*- */

#ifndef HL_COALESCEHEAP_H
#define HL_COALESCEHEAP_H

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
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

#include "heaplayers.h"

/**
 * @class CoalesceHeap
 * @brief Applies splitting and coalescing.
 * @see CoalesceableHeap
 * @see RequireCoalesceable
 */

namespace HL {

  template <class super>
  class CoalesceHeap : public super {
  public:

    inline void * malloc (const size_t sz)
    {
      void * ptr = super::malloc (sz);
      if (ptr != NULL) {
        super::markInUse (ptr);
        void * splitPiece = split (ptr, sz);
        if (splitPiece != NULL) {
          super::markFree (splitPiece);
          super::free (splitPiece);
        }
      }
      return ptr;
    }


    inline void free (void * ptr)
    {
      // Try to coalesce this object with its predecessor & successor.
      if ((super::getNext(super::getPrev(ptr)) != ptr) || (super::getPrev(super::getNext(ptr)) != ptr)) {
        // We're done with this object.
        super::free (ptr);
        return;
      }
      assert (super::getPrev(super::getNext(ptr)) == ptr);
      // Try to coalesce with the previous object..
      void * prev = super::getPrev(ptr);
      void * next = super::getNext (ptr);
      assert (prev != ptr);

      if (super::isPrevFree(ptr)) {
        assert (super::isFree(prev));
        super::remove (prev);
        coalesce (prev, ptr);
        ptr = prev;
      }
      if (super::isFree(next)) {
        super::remove (next);
        coalesce (ptr, next);
      }

      super::markFree (ptr);

      // We're done with this object.
      super::free (ptr);
    }

  private:


    // Combine the first object with the second.
    inline static void coalesce (void * first, const void * second) {
      // A few sanity checks first.
      assert (super::getNext(first) == second);
      assert (super::getPrev(second) == first);
      // Now coalesce.
      size_t newSize = ((size_t) second - (size_t) first) + super::getSize(second);
      super::setSize (first, newSize);
      setPrevSize (super::getNext(first), newSize);
    }

    // Split an object if it is big enough.
    inline static void * split (void * obj, const size_t requestedSize) {
      assert (super::getSize(obj) >= requestedSize);
      // We split aggressively (for now; this could be a parameter).
      const size_t actualSize = super::getSize(obj);
      if (actualSize - requestedSize >= sizeof(typename super::Header) + sizeof(double)) {
        // Split the object.
        super::setSize(obj, requestedSize);
        void * splitPiece = (char *) obj + requestedSize + sizeof(typename super::Header);
        super::makeObject ((void *) super::getHeader(splitPiece),
          requestedSize,
          actualSize - requestedSize - sizeof(typename super::Header));
        assert (!super::isFree(splitPiece));
        // Now that we have a new successor (splitPiece), we need to
        // mark obj as in use.
        (super::getHeader(splitPiece))->markPrevInUse();
        assert (super::getSize(splitPiece) >= sizeof(double));
        assert (super::getSize(obj) >= requestedSize);
        return splitPiece;
      } else {
        return NULL;
      }
    }

  };

}

#endif
