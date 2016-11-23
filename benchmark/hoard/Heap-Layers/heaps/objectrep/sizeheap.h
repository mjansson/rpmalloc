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

#ifndef HL_SIZEHEAP_H
#define HL_SIZEHEAP_H

/**
 * @file sizeheap.h
 * @brief Contains UseSizeHeap and SizeHeap.
 */

#include <assert.h>

#include "wrappers/mallocinfo.h"
#include "heaps/objectrep/addheap.h"
#include "utility/gcd.h"

namespace HL {

  /**
   * @class SizeHeap
   * @brief Allocates extra room for the size of an object.
   */

  template <class SuperHeap>
  class SizeHeap : public SuperHeap {

  private:
    struct freeObject {
      size_t _sz;
      size_t _magic;
      //      char _buf[HL::MallocInfo::Alignment];
    };

  public:

    enum { Alignment = gcd<(int) SuperHeap::Alignment,
	   (int) sizeof(freeObject)>::value };

    virtual ~SizeHeap (void) {}

    inline void * malloc (size_t sz) {
      freeObject * p = (freeObject *) SuperHeap::malloc (sz + sizeof(freeObject));
      p->_sz = sz;
      p->_magic = 0xcafebabe;
      return (void *) (p + 1);
    }

    inline void free (void * ptr) {
      assert (getHeader(ptr)->_magic == 0xcafebabe);
      SuperHeap::free (getHeader(ptr));
    }

    inline static size_t getSize (const void * ptr) {
      assert (getHeader(ptr)->_magic == 0xcafebabe);
      size_t size = getHeader(ptr)->_sz;
      return size;
    }

  private:

    inline static void setSize (void * ptr, size_t sz) {
      assert (getHeader(ptr)->_magic == 0xcafebabe);
      getHeader(ptr)->_sz = sz;
    }

    inline static freeObject * getHeader (const void * ptr) {
      return ((freeObject *) ptr - 1);
    }

  };

}

#endif
