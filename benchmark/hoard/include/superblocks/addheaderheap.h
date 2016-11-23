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

#ifndef HOARD_ADDHEADERHEAP_H
#define HOARD_ADDHEADERHEAP_H

#include "heaplayers.h"

namespace Hoard {

  /**
   * @class AddHeaderHeap
   */

  template <class SuperblockType,
	    size_t SuperblockSize,
	    class SuperHeap>
  class AddHeaderHeap {
  private:

    HL::sassert<(((int) SuperHeap::Alignment) % SuperblockSize == 0)> verifySize1;
    HL::sassert<(((int) SuperHeap::Alignment) >= SuperblockSize)> verifySize2;

    SuperHeap theHeap;

  public:

    enum { Alignment = gcd<SuperHeap::Alignment, sizeof(typename SuperblockType::Header)>::value };

    void clear() {
      theHeap.clear();
    }

    MALLOC_FUNCTION INLINE void * malloc (size_t sz) {

      // Allocate extra space for the header,
      // put it at the front of the object,
      // and return a pointer to just past it.
      const size_t headerSize = sizeof(typename SuperblockType::Header);
      void * ptr = theHeap.malloc (sz + headerSize);
      if (ptr == NULL) {
	return NULL;
      }
      typename SuperblockType::Header * p
	= new (ptr) typename SuperblockType::Header (sz, sz);
      assert ((size_t) (p + 1) == (size_t) ptr + headerSize);
      return reinterpret_cast<void *>(p + 1);
    }

    INLINE static size_t getSize (void * ptr) {
      // Find the header (just before the pointer) and return the size
      // value stored there.
      typename SuperblockType::Header * p;
      p = reinterpret_cast<typename SuperblockType::Header *>(ptr);
      return (p - 1)->getSize (ptr);
    }

    INLINE void free (void * ptr) {
      // Find the header (just before the pointer) and free the whole object.
      typename SuperblockType::Header * p;
      p = reinterpret_cast<typename SuperblockType::Header *>(ptr);
      theHeap.free (reinterpret_cast<void *>(p - 1));
    }
  };

}

#endif
