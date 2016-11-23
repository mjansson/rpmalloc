/* -*- C++ -*- */

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


// An "Obstack".

#ifndef HL_OBSTACK_H
#define HL_OBSTACK_H


#include <assert.h>
#include <new>

#include "utility/align.h"
#include "wrappers/mallocinfo.h"

/**
 * @class ObstackHeap
 * @brief Implements obstack functionality (as in the GNU obstack library).
 */

namespace HL {

  template <int ChunkSize, class SuperHeap>
  class ObstackHeap : public SuperHeap {
  public:

    ObstackHeap()
    {
      // Get one chunk and set the current position marker.
      currentChunk = makeChunk (NULL, ChunkSize);
      currentBase = nextPos = (char *) (currentChunk + 1);
      assert (isValid());
    }

    ~ObstackHeap() {
      // Free every chunk.
      assert (isValid());
      ChunkHeader * ch = currentChunk;
      while (ch != NULL) {
        ChunkHeader * pch = ch->getPrevChunk();
        // cout << "Freeing chunk " << ch << endl;
        SuperHeap::free (ch);
        ch = pch;
      }
    }

    // "Grow" the current object.
    // Returns a point in the object just before the current "grow".
    inline void * grow (size_t sz) {
      // If we've grown beyond the confines of this chunk,
      // get a new one.
      assert (isValid());
      if ((int) ((char *) currentChunk->getLimit() - (char *) nextPos) < sz) {
        ChunkHeader * newCurrent = copyToNew(sz);
        if (newCurrent == NULL)
          return NULL;
#if 0
        // Now delete the previous chunk if this was the only object in it.
        if (deleteChunk != NULL) {
          SuperHeap::free (deleteChunk);
        }
#endif
        assert (isValid());
      }
      assert (((int) ((char *) currentChunk->getLimit() - (char *) nextPos) >= sz));
      assert ((char *) (sz + nextPos) <= currentChunk->getLimit());
      // Bump the pointer for the next object.
      void * prevNextPos = nextPos;
      nextPos += sz;
      assert (isValid());
      return prevNextPos;
    }


    inline void * malloc (size_t sz) {
      assert (isValid());
      if (currentChunk == NULL) {
        return NULL;
      }
      //sz = align(sz > 0 ? sz : 1);
      // If this object can't fit in the current chunk,
      // get another one.
      if ((int) ((char *) currentChunk->getLimit() - (char *) nextPos) < sz) {
        // Allocate a chunk that's large enough to hold the requested size.
        currentChunk = makeChunk (currentChunk, sz);
        if (currentChunk == NULL) {
          return NULL;
        }
        currentBase = nextPos = (char *) (currentChunk + 1);
        assert (isValid());
      }
      assert (((int) ((char *) currentChunk->getLimit() - (char *) nextPos) >= sz));
      assert ((char *) (sz + nextPos) <= currentChunk->getLimit());
      // Bump the pointers forward.
      currentBase = nextPos;
      nextPos += sz;
      void * ptr = currentBase;
      finalize();
      assert (isValid());
      return (void *) ptr;
    }

    // Free everything allocated "after" ptr.
    // NB: Freeing NULL safely empties the entire obstack.
    inline void free (void * ptr) {
      assert (isValid());
      // Free every chunk until we find the one that this pointer is in.
      // Then reset the current position to ptr.
      while (currentChunk != NULL &&
	     (((char *) currentChunk > (char *) ptr) ||
	      ((char *) currentChunk->getLimit() < (char *) ptr))) {
        ChunkHeader * pch = currentChunk;
        currentChunk = currentChunk->getPrevChunk();
        SuperHeap::free (pch);
      }
      if (currentChunk != NULL) {
        currentBase = nextPos = (char *) ptr;
        assert (isValid());
      } else {
        if (ptr != NULL) {
          // Something bad has happened -- we tried to free an item that
          // wasn't in any chunk.
          abort();
        } else {
          // Get one chunk.
          currentChunk = makeChunk (NULL, ChunkSize);
          currentBase = nextPos = (char *) (currentChunk + 1);
          assert (isValid());
        }
      }
    }


    inline void * getObjectBase() {
      assert (isValid());
      return currentBase;
    }


    inline void finalize() {
      assert (isValid());
      nextPos = (char *) HL::align<HL::MallocInfo::Alignment>((int) nextPos);
      currentBase = nextPos;
      assert (isValid());
    }


  private:


    inline int objectSize() {
      int diff = (int) (nextPos - currentBase);
      assert (diff >= 0);
      return diff;
    }


    int isValid() {
      // Verify class invariants.
#ifndef NDEBUG
      bool c1 = (currentBase <= nextPos);
      assert (c1);
      bool c2 = (nextPos <= currentChunk->getLimit());
      assert (c2);
      bool c3 = ((char *) currentChunk <= currentChunk->getLimit());
      assert (c3);
      bool c4 = ((char *) currentChunk <= currentBase);
      assert (c4);
      bool c5 = (currentChunk != currentChunk->getPrevChunk());
      assert (c5);
      bool c6 = (objectSize() >= 0);
      assert (c6);
      return (c1 && c2 && c3 && c4 && c5 && c6);
#else
      return 1;
#endif
    }

    // The header for every chunk.
    class ChunkHeader {
    public:
      inline ChunkHeader (ChunkHeader * prev, size_t sz)
        : _pastEnd ((char *) (this + 1) + sz),
        _prevChunk (prev)
      {}

      // Return the end of the current chunk.
      inline char * getLimit() { return _pastEnd; }

      // Return the previous chunk.
      inline ChunkHeader * getPrevChunk() { return _prevChunk; }

    private:
      // Just past the end of this chunk.
      char * _pastEnd;

      // Address of prior chunk.
      ChunkHeader * _prevChunk;
    };


    // Make a new chunk of at least sz bytes.
    inline ChunkHeader * makeChunk (ChunkHeader * ch, size_t sz) {
      // Round up the allocation size to at least one chunk.
      size_t allocSize
        = HL::align<sizeof(double)>((sz > ChunkSize - sizeof(ChunkHeader)) ? sz : ChunkSize - sizeof(ChunkHeader));
      // Make a new chunk.
      ChunkHeader * newChunk
        = new (SuperHeap::malloc (sizeof(ChunkHeader) + allocSize)) ChunkHeader (ch, allocSize);
      return newChunk;
    }


    // Copy the current object to a new chunk.
    // sz = the minimum amount of extra space we need.
    inline ChunkHeader * copyToNew (size_t sz) {
      size_t obj_size = objectSize();
      size_t new_size = obj_size + sz + (obj_size >> 3) + 100;
      //size_t new_size = 2 * (obj_size + sz);
      ChunkHeader * newChunk;
#if 0
      // This variable will hold the chunk to be deleted (if any).
      ChunkHeader * deleteChunk = NULL;
      // If this object was the only one in the chunk,
      // link back past this chunk.
      if (currentBase == (char *) (currentChunk + 1)) {
	newChunk = makeChunk (currentChunk->getPrevChunk(), new_size);
	deleteChunk = currentChunk;
      } else {
	newChunk = makeChunk (currentChunk, new_size);
      }
#else
      newChunk = makeChunk (currentChunk, new_size);
#endif
      if (newChunk == NULL) {
        currentChunk = NULL;
        return NULL;
      }
      // Copy the current object to the new chunk.
      memcpy ((char *) (newChunk + 1), currentBase, obj_size);
      currentChunk = newChunk;
      currentBase = (char *) (currentChunk + 1);
      nextPos = currentBase + obj_size;
#if 0
      if (deleteChunk != NULL) {
	SuperHeap::free (deleteChunk);
      }
#endif
      return currentChunk;
    }


    // Current position in the current chunk.
    char * currentBase;

    // Where to add the next character to the current object.
    char * nextPos;

    // My current chunk.
    ChunkHeader * currentChunk;

  };

#if 0
  // The C version of the above (just large enough to hold all of the data structures).
  struct obstack {
    void * _dummy[3];
  };
#endif

}

#endif // HL_OBSTACK_H
