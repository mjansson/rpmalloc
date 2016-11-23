// -*- C++ -*-

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

#ifndef HL_ONEHEAP_H
#define HL_ONEHEAP_H

//
// Wrap a single instance of a heap.
//

namespace HL {

  template <class SuperHeap>
  class OneHeap {
  public:
    OneHeap (void)
      : theHeap (getHeap())
    {}
    
    inline void * malloc (const size_t sz) {
      return theHeap->malloc (sz);
    }
    inline void free (void * ptr) {
      theHeap->free (ptr);
    }
    inline int remove (void * ptr) {
      return theHeap->remove (ptr);
    }
    inline void clear (void) {
      theHeap->clear();
    }
    inline size_t getSize (void * ptr) {
      return theHeap->getSize (ptr);
    }
    
    enum { Alignment = SuperHeap::Alignment };
    
  private:
    
    SuperHeap * theHeap;
    
    inline static SuperHeap * getHeap (void) {
      static SuperHeap theHeap;
      return &theHeap;
    }
  };
  
}

#endif
