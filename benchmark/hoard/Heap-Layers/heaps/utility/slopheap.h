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

#ifndef HL_SLOPHEAP_H
#define HL_SLOPHEAP_H

#include <cstddef>

/**
 * @class SlopHeap
 * 
 * SlopHeap is designed to guarantee that you always have an extra N bytes
 * available after the most recent malloc. This is necessary for the current
 * coalescing support, which can look past the last allocated object.
 *
 * @param SuperHeap The parent heap.
 * @param SLOP The amount of extra memory required, in bytes.
 */
  
namespace HL {
  
  template <class SuperHeap, int SLOP = 16>
  class SlopHeap : public SuperHeap {
  public:
    SlopHeap (void)
      : remaining (0),
	ptr (NULL)
    {}
  
    inline void * malloc (const size_t nbytes) {

      // Put the usual case up front.
      if (nbytes <= remaining) {
	remaining -= nbytes;
	char * p = ptr;
	ptr += nbytes;
	return (void *) p;
      }
    
      //
      // We don't have enough space to satisfy the current
      // request, so get more memory.
      //

      return getMoreMemory(nbytes);
    }
  
    inline void clear (void) {
      ptr = NULL;
      remaining = 0;
      SuperHeap::clear ();
    }

    inline void free (void *) {}

  private:

    // Disabled.
    inline int remove (void *);

    void * getMoreMemory (size_t nbytes) {
      char * newptr = (char *) SuperHeap::malloc (nbytes + SLOP);

      if (newptr == NULL) {
	return NULL;
      }

      //
      // If this new memory is contiguous with the previous one,
      // reclaim the "slop".
      //
    
      if ((ptr != NULL) && (ptr + remaining + SLOP == newptr)) {
	remaining += SLOP;
      } else {
	ptr = newptr;
	remaining = 0;
      }
      char * p = ptr;
      ptr += nbytes;

      return (void *) p;
    }

    char * ptr;
    size_t remaining;

  };
  
}

#endif
