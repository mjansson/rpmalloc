// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.org
 
  Copyright (c) 1998-2015 Emery Berger
  
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

#ifndef HL_EXACTLYONEHEAP_H
#define HL_EXACTLYONEHEAP_H

#include "utility/exactlyone.h"

namespace HL {

  template <class Heap>
  class ExactlyOneHeap : public HL::ExactlyOne<Heap> {
  public:

    enum { Alignment = Heap::Alignment };

    inline void * malloc (size_t sz) {
      return (*this)().malloc (sz);
    }
    inline void free (void * ptr) {
      (*this)().free (ptr);
    }
    inline size_t getSize (void * ptr) {
      return (*this)().getSize (ptr);
    }
    inline void clear() {
      (*this)().clear();
    }
  };

}

#endif
