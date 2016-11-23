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

#ifndef HOARD_BASEHOARDMANAGER_H
#define HOARD_BASEHOARDMANAGER_H

/**
 * @class BaseHoardManager
 * @brief The top of the hoard manager hierarchy.
 *
 */

#include "heaplayers.h"
//#include "sassert.h"

namespace Hoard {

  template <class SuperblockType_>
  class BaseHoardManager {
  public:

    BaseHoardManager (void)
      : _magic (0xedded00d)
    {}

    inline int isValid (void) const {
      return (_magic == 0xedded00d);
    }

    // Export the superblock type.
    typedef SuperblockType_ SuperblockType;

    /// Free an object.
    inline virtual void free (void *) {}

    /// Lock this memory manager.
    inline virtual void lock (void) {}

    /// Unlock this memory manager.
    inline virtual void unlock (void) {};

    /// Return the size of an object.
    static inline size_t getSize (void * ptr) {
      SuperblockType * s = getSuperblock (ptr);
      assert (s->isValidSuperblock());
      return s->getSize (ptr);
    }

    /// @brief Find the superblock corresponding to a pointer via bitmasking.
    /// @note  All superblocks <em>must</em> be naturally aligned, and powers of two.

    static inline SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

  private:

    enum { SuperblockSize = sizeof(SuperblockType) };

    HL::sassert<((SuperblockSize & (SuperblockSize - 1)) == 0)> EnsureSuperblockSizeIsPowerOfTwo;

    const unsigned long _magic;

  };

}

#endif
