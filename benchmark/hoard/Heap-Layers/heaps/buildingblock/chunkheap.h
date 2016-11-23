/* -*- C++ -*- */

#ifndef HL_CHUNKHEAP_H_
#define HL_CHUNKHEAP_H_

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

#include <assert.h>

/**
 * @class ChunkHeap
 * @brief Allocates memory from the superheap in chunks.
 * @param ChunkSize The minimum size for allocating memory from the superheap.
 */

namespace HL {

  template <int ChunkSize, class SuperHeap>
  class ChunkHeap : public SuperHeap {
  public:

    inline ChunkHeap (void)
      : buffer (NULL),
      eob (NULL)
    {}

    inline void * malloc (const size_t sz) {
      void * ptr = buffer;
      buffer += sz;
      if (buffer <= eob) {
        assert (eob != NULL);
        assert ((size_t) (eob - (char *) ptr + 1) >= sz);
        return ptr;
      }
      buffer -= sz;		// we didn't succeed, back up
      return getMoreMemory(sz);
    }

    inline void clear (void) {
      buffer = NULL;
      eob = NULL;
      SuperHeap::clear ();
    }

  private:

    // Disabled.
    inline int remove (void *);

    void * getMoreMemory (size_t sz) {
      assert (sz > 0);
      // Round sz to the next chunk size.
      size_t reqSize = (((sz-1) / ChunkSize) + 1) * ChunkSize;
      char * buf = (char *) SuperHeap::malloc (reqSize);
      if (buf == NULL) {
        return NULL;
      }
      // If the current end of buffer is not the same as the new buffer,
      // reset the buffer pointer.
      if (eob != buf) {
        buffer = buf;
      }
      else {
        // we still have a bit leftover at the end of previous buffer
        reqSize += eob - buffer;
      }
      eob = buffer + reqSize;

      void * ptr = buffer;
      buffer += sz;
      return ptr;
    }

    /// The current allocation buffer.
    char * buffer;

    /// The end of the buffer.
    char * eob;
  };

}

#endif
