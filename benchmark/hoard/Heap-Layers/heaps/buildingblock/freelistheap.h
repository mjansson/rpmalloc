/* -*- C++ -*- */

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

#ifndef HL_FREELISTHEAP_H
#define HL_FREELISTHEAP_H

/**
 * @class FreelistHeap
 * @brief Manage freed memory on a linked list.
 * @warning This is for one "size class" only.
 *
 * Note that the linked list is threaded through the freed objects,
 * meaning that such objects must be at least the size of a pointer.
 */

#include <assert.h>
#include "utility/freesllist.h"

#ifndef NULL
#define NULL 0
#endif

namespace HL {

  template <class SuperHeap>
  class FreelistHeap : public SuperHeap {
  public:

    inline void * malloc (size_t sz) {
      // Check the free list first.
      void * ptr = _freelist.get();
      // If it's empty, get more memory;
      // otherwise, advance the free list pointer.
      if (ptr == 0) {
        ptr = SuperHeap::malloc (sz);
      }
      return ptr;
    }

    inline void free (void * ptr) {
      if (ptr == 0) {
        return;
      }
      _freelist.insert (ptr);
    }

    inline void clear (void) {
      void * ptr;
      while ((ptr = _freelist.get())) {
        SuperHeap::free (ptr);
      }
    }

  private:

    FreeSLList _freelist;

  };

}

#endif
