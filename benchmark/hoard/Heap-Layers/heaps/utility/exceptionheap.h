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

#ifndef HL_EXCEPTIONHEAP_H
#define HL_EXCEPTIONHEAP_H

#include <new>

//class std::bad_alloc;

namespace HL {

  template <class Super>
  class ExceptionHeap : public Super {
  public:
    inline void * malloc (size_t sz) throw (std::bad_alloc) {
      void * ptr = Super::malloc (sz);
      if (ptr == NULL) {
	throw new std::bad_alloc;
      }
      return ptr;
    }
  };


  template <class Super>
  class CatchExceptionHeap : public Super {
  public:
    inline void * malloc (size_t sz) {
      void * ptr;
      try {
	ptr = Super::malloc (sz);
      } catch (std::bad_alloc) {
	ptr = NULL;
      }
      return ptr;
    }
  };

}

#endif
