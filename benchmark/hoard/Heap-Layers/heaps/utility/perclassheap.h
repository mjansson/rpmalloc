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

#ifndef HL_PERCLASSHEAP_H
#define HL_PERCLASSHEAP_H

#include <new>

/**
 * @class PerClassHeap
 * @brief Enable the use of one heap for all class memory allocation.
 * 
 * This class contains one instance of the SuperHeap argument.  The
 * example below shows how to make a subclass of Foo that uses a
 * FreelistHeap to manage its memory, overloading operators new and
 * delete.
 * 
 * <TT>
 *   class NewFoo : public Foo, PerClassHeap<FreelistHeap<MallocHeap> > {};
 * </TT>
 */

namespace HL {

  template <class SuperHeap>
    class PerClassHeap {
  public:
    inline void * operator new (size_t sz) {
      return getHeap()->malloc (sz);
    }
    inline void operator delete (void * ptr) {
      getHeap()->free (ptr);
    }
    inline void * operator new[] (size_t sz) {
      return getHeap()->malloc (sz);
    }
    inline void operator delete[] (void * ptr) {
      getHeap()->free (ptr);
    }
    // For some reason, g++ needs placement new to be overridden
    // as well, at least in conjunction with use of the STL.
    // Otherwise, this should be superfluous.
    inline void * operator new (size_t, void * p) { return p; }
    inline void * operator new[] (size_t, void * p) { return p; }

  private:
    inline static SuperHeap * getHeap (void) {
      static SuperHeap theHeap;
      return &theHeap;
    }
  };

}

#endif
