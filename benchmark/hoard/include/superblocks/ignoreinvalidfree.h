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

#ifndef HOARD_IGNOREINVALIDFREE_H
#define HOARD_IGNOREINVALIDFREE_H

namespace Hoard {

  // A class that checks to see if the object to be freed is inside a
  // valid superblock. If not, it drops the object on the floor. We do
  // this in the name of robustness (turning a segfault or data
  // corruption into a potential memory leak) and because on some
  // systems, it's impossible to catch the first few allocated objects.

  template <class SuperHeap>
  class IgnoreInvalidFree : public SuperHeap {
  public:
    INLINE void free (void * ptr) {
      if (ptr) {
	typename SuperHeap::SuperblockType * s = SuperHeap::getSuperblock (ptr);
	if (!s || (!s->isValidSuperblock())) {
	  // We encountered an invalid free, so we drop it.
	  return;
	}
	SuperHeap::free (ptr);
      }
    }

    INLINE size_t getSize (void * ptr) {
      if (ptr) {
	typename SuperHeap::SuperblockType * s = SuperHeap::getSuperblock (ptr);
	if (!s || (!s->isValidSuperblock())) {
	  return 0;
	}
	return SuperHeap::getSize (ptr);
      } else {
	return 0;
      }
    }

  };

}

#endif
