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

#ifndef HL_PROFILEHEAP_H
#define HL_PROFILEHEAP_H

#include <cstdio>

// Maintain & print memory usage info.
// Requires a superheap with the size() method (e.g., SizeHeap).

namespace HL {

  template <class SuperHeap, int HeapNumber>
  class ProfileHeap : public SuperHeap {
  public:
    
    ProfileHeap (void)
      : memRequested (0),
	maxMemRequested (0)
    {
    }
    
    ~ProfileHeap()
    {
      if (maxMemRequested > 0) {
	stats();
      }
    }
    
    inline void * malloc (size_t sz) {
      void * ptr = SuperHeap::malloc (sz);
      // Notice that we use the size reported by the allocator
      // for the object rather than the requested size.
      memRequested += SuperHeap::getSize(ptr);
      if (memRequested > maxMemRequested) {
	maxMemRequested = memRequested;
      }
      return ptr;
    }
    
    inline void free (void * ptr) {
      memRequested -= SuperHeap::getSize (ptr);
      SuperHeap::free (ptr);
    }
    
  private:
    void stats (void) {
      printf ("Heap: %d\n", HeapNumber);
      printf ("Max memory requested = %d\n", maxMemRequested);
      printf ("Memory still in use = %d\n", memRequested);
    }
    
    unsigned long memRequested;
    unsigned long maxMemRequested;
  };

}

#endif
