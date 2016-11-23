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

#ifndef HL_BUMPALLOC_H
#define HL_BUMPALLOC_H

#include <cstddef>

#include "utility/gcd.h"
#include "utility/sassert.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

/**
 * @class BumpAlloc
 * @brief Obtains memory in chunks and bumps a pointer through the chunks.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

namespace HL {

  template <size_t ChunkSize,
	    class SuperHeap,
	    size_t Alignment_ = 1UL>
  class BumpAlloc : public SuperHeap {
  public:

    enum { Alignment = Alignment_ };

    BumpAlloc()
      : _bump (NULL),
	_remaining (0)
    {
      sassert<((int) gcd<ChunkSize, Alignment>::VALUE == Alignment)> 
	verifyAlignmentSatisfiable;
      sassert<((int) gcd<SuperHeap::Alignment, Alignment>::VALUE == Alignment)>
	verifyAlignmentFromSuperHeap;
      sassert<((Alignment & (Alignment-1)) == 0)>
	verifyPowerOfTwoAlignment;
      verifyAlignmentSatisfiable = verifyAlignmentSatisfiable;
      verifyAlignmentFromSuperHeap = verifyAlignmentFromSuperHeap;
      verifyPowerOfTwoAlignment = verifyPowerOfTwoAlignment;
    }

    inline void * malloc (size_t sz) {
      // Round up the size if necessary.
      size_t newSize = (sz + Alignment - 1UL) & ~(Alignment - 1UL);

      // If there's not enough space left to fulfill this request, get
      // another chunk.
      if (_remaining < newSize) {
      	refill(newSize);
      }
      // Bump that pointer.
      char * old = _bump;
      _bump += newSize;
      _remaining -= newSize;

      assert ((size_t) old % Alignment == 0);
      return old;
    }

    /// Free is disabled (we only bump, never reclaim).
    inline bool free (void *) { return false; }

  private:

    /// The bump pointer.
    char * _bump;

    /// How much space remains in the current chunk.
    size_t _remaining;

    // Get another chunk.
    void refill (size_t sz) {
      if (sz < ChunkSize) {
      	sz = ChunkSize;
      }
      _bump = (char *) SuperHeap::malloc (sz);
      assert ((size_t) _bump % Alignment == 0);
      _remaining = sz;
    }

  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
