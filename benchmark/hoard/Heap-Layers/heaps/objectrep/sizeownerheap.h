/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2015 by Emery Berger
  http://www.emeryberger.com
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

#ifndef HL_SIZEOWNERHEAP_H_
#define HL_SIZEOWNERHEAP_H_

#include <assert.h>

#include "addheap.h"

/**
 * @class SizeOwnerHeap
 * @brief Adds object size and owner heap information.
 */

namespace HL {

template <class Heap>
class SizeOwner {
public:
  union tag {
    struct {
      size_t size;
      Heap * owner;
    } s;
    double dummy;
  };
};

template <class Super>
class SizeOwnerHeap : public AddHeap<SizeOwner<Super>, Super> {
private:

  typedef AddHeap<SizeOwner<Super>, Super> SuperHeap;

public:

  inline void * malloc (size_t sz) {
    void * ptr = SuperHeap::malloc (sz);
    // Store the requested size.
    SizeOwner<Super> * so = (SizeOwner<Super> *) ptr;
    so->s.size = sz;
    so->s.owner = this;
    // Store the owner.
    return (void *) (so + 1);
  }

  inline void free (void * ptr) {
    void * origPtr = (void *) ((SizeOwner<Super> *) ptr - 1);
    SuperHeap::free (origPtr);
  }

  static inline Super * owner (void * ptr) {
    SizeOwner<Super> * so = (SizeOwner<Super> *) ptr - 1;
    return so->s.owner;
  }

  static inline size_t size (void * ptr) {
    SizeOwner<Super> * so = (SizeOwner<Super> *) ptr - 1;
    return so->s.size;
  }
};

}

#endif
