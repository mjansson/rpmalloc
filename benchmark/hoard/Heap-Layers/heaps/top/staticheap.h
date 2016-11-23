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

/*

  StaticHeap: manage a fixed range of memory.

*/

#ifndef HL_STATICHEAP_H
#define HL_STATICHEAP_H

#include <cstddef>

namespace HL {

  template <int MemorySize>
  class StaticHeap {
  public:

    StaticHeap()
      : _ptr (&_buf[0]),
	_remaining (MemorySize)
    {}

    enum { Alignment = 1 };

    inline void * malloc (size_t sz) {
      if (_remaining < sz) {
	return NULL;
      }
      void * p = _ptr;
      _ptr += sz;
      _remaining -= sz;
      return p;
    }

    void free (void *) {}
    int remove (void *) { return 0; }

    int isValid (void * ptr) {
      return (((size_t) ptr >= (size_t) _buf) &&
	      ((size_t) ptr < (size_t) _buf));
    }

  private:

    // Disable copying and assignment.
    StaticHeap (const StaticHeap&);
    StaticHeap& operator= (const StaticHeap&);

    char _buf[MemorySize];
    char * _ptr;
    size_t _remaining;
  };

}

#endif
