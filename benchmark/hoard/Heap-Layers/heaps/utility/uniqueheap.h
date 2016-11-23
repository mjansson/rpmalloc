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

#ifndef HL_UNIQUEHEAP_H
#define HL_UNIQUEHEAP_H

#include <cstdlib>
#include <new>

/**
 *
 * @class UniqueHeap
 * @brief Instantiates one instance of a class used for every malloc & free.
 * @author Emery Berger
 *
 */

namespace HL {

  template <class SuperHeap, class Child = int>
  class UniqueHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    /**
     * Ensure that the super heap gets created,
     * and add a reference for every instance of unique heap.
     */
    UniqueHeap() 
    {
      volatile SuperHeap * forceCreationOfSuperHeap = getSuperHeap();
      addRef();
    }

    /**
     * @brief Delete one reference to the unique heap.
     * When the number of references goes to zero,
     * delete the super heap.
     */
    ~UniqueHeap() {
      int r = delRef();
      if (r <= 0) {
	getSuperHeap()->SuperHeap::~SuperHeap();
      }
    }

    // The remaining public methods are just
    // thin wrappers that route calls to the static object.

    inline void * malloc (size_t sz) {
      return getSuperHeap()->malloc (sz);
    }
  
    inline void free (void * ptr) {
      getSuperHeap()->free (ptr);
    }
  
    inline size_t getSize (void * ptr) {
      return getSuperHeap()->getSize (ptr);
    }

    inline int remove (void * ptr) {
      return getSuperHeap()->remove (ptr);
    }

    inline void clear() {
      getSuperHeap()->clear();
    }

#if 0
    inline int getAllocated() {
      return getSuperHeap()->getAllocated();
    }

    inline int getMaxAllocated() {
      return getSuperHeap()->getMaxAllocated();
    }

    inline int getMaxInUse() {
      return getSuperHeap()->getMaxInUse();
    }
#endif

  private:

    /// Add one reference.
    void addRef() {
      getRefs() += 1;
    }

    /// Delete one reference count.
    int delRef() {
      getRefs() -= 1;
      return getRefs();
    }

    /// Internal accessor for reference count.
    int& getRefs() {
      static int numRefs = 0;
      return numRefs;
    }

    SuperHeap * getSuperHeap() {
      static char superHeapBuffer[sizeof(SuperHeap)];
      static SuperHeap * aSuperHeap = (SuperHeap *) (new ((char *) &superHeapBuffer) SuperHeap);
      return aSuperHeap;
    }

    void doNothing (Child *) {}
  };

}
#endif // _UNIQUE_H_
