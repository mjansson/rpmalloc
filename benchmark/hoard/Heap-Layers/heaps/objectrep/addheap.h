/* -*- C++ -*- */

#ifndef HL_ADDHEAP_H
#define HL_ADDHEAP_H

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

// Reserve space for a class in the head of every allocated object.

#include <assert.h>
#include "utility/lcm.h"

namespace HL {

  template <class Add, class SuperHeap>
  class AddHeap : public SuperHeap {
  public:

    inline void * malloc (size_t sz) {
      void * ptr = SuperHeap::malloc (sz + HeaderSize);
      void * newPtr = (char *) ptr + HeaderSize;
      return newPtr;
    }

    inline void free (void * ptr) {
      SuperHeap::free (getOriginal(ptr));
    }

    inline size_t getSize (void * ptr) {
      return SuperHeap::getSize (getOriginal(ptr));
    }

  private:

    inline void * getOriginal (void * ptr) {
      void * origPtr = (void *) ((char *) ptr - HeaderSize);
      return origPtr;
    }

    // A size that preserves existing alignment restrictions.
    // Beware: can seriously increase size requirements.
    enum { HeaderSize = lcm<(int) SuperHeap::Alignment, sizeof(Add)>::value };

  };

}
#endif
