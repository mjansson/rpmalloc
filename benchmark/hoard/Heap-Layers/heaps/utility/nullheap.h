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

#ifndef HL_NULLHEAP_H
#define HL_NULLHEAP_H

#include <assert.h>

/**
 * @class NullHeap
 * @brief A source heap that does nothing.
 */

namespace HL {

  template <class SuperHeap>
  class NullHeap : public SuperHeap {
  public:
    inline void * malloc (size_t) const { return 0; }
    inline void free (void *) const {}
    inline int remove (void *) const { return 0; }
    inline void clear (void) const {}
    inline size_t getSize (void *) const { return 0; }
  };

}

#endif
