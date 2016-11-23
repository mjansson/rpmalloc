// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger
  
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

#ifndef HOARD_BUMPALLOC_H
#define HOARD_BUMPALLOC_H

/**
 * @class BumpAlloc
 * @brief Obtains memory in chunks and bumps a pointer through the chunks.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#include "mallocinfo.h"


namespace Hoard {

  template <int ChunkSize,
    class SuperHeap>
    class BumpAlloc : public SuperHeap {
  public:

    enum { Alignment = HL::MallocInfo::Alignment };

    BumpAlloc (void)
      : _bump (NULL),
	_remaining (0)
    {}

    inline void * malloc (size_t sz) {
      if (sz < HL::MallocInfo::MinSize) {
	sz = HL::MallocInfo::MinSize;
      }
      sz = HL::align<HL::MallocInfo::Alignment>(sz);
      // If there's not enough space left to fulfill this request, get
      // another chunk.
      if (_remaining < sz) {
	refill(sz);
      }
      if (_bump) {
	char * old = _bump;
	_bump += sz;
	_remaining -= sz;
	assert ((size_t) old % Alignment == 0);
	return old;
      } else {
	// We were unable to get memory.
	return NULL;
      }
    }

    /// Free is disabled (we only bump, never reclaim).
    inline void free (void *) {}

  private:

    /// The bump pointer.
    char * _bump;

    /// How much space remains in the current chunk.
    size_t _remaining;

    // Get another chunk.
    void refill (size_t sz) {
      // Always get at least a ChunkSize worth of memory.
      if (sz < ChunkSize) {
	sz = ChunkSize;
      }
      _bump = (char *) SuperHeap::malloc (sz);
      assert ((size_t) _bump % Alignment == 0);
      if (_bump) {
	_remaining = sz;
      } else {
	_remaining = 0;
      }
    }

  };

}

#endif
