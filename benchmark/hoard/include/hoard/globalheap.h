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

#ifndef HOARD_GLOBALHEAP_H
#define HOARD_GLOBALHEAP_H

#include "hoardsuperblock.h"
#include "processheap.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    int EmptinessClasses,
	    class MmapSource,
	    class LockType>
  class GlobalHeap {
  
    class bogusThresholdFunctionClass {
    public:
      static inline bool function (unsigned int, unsigned int, size_t) {
	// We *never* cross the threshold for the global heap, since
	// it is the "top."
	return false;
      }
    };
  
  public:

    GlobalHeap (void) 
      : _theHeap (getHeap())
    {
    }
  
    typedef ProcessHeap<SuperblockSize, EmptinessClasses, LockType, bogusThresholdFunctionClass, MmapSource> SuperHeap;
    typedef HoardSuperblock<LockType, SuperblockSize, GlobalHeap> SuperblockType;
  
    void put (void * s, size_t sz) {
      assert (s);
      assert (((SuperblockType *) s)->isValidSuperblock());
      _theHeap->put ((typename SuperHeap::SuperblockType *) s,
		     sz);
    }

    SuperblockType * get (size_t sz, void * dest) {
      auto * s = 
	reinterpret_cast<SuperblockType *>
	(_theHeap->get (sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
	assert (s->isValidSuperblock());
      }
      return s;
    }

  private:

    SuperHeap * _theHeap;

    inline static SuperHeap * getHeap (void) {
      static double theHeapBuf[sizeof(SuperHeap) / sizeof(double) + 1];
      static auto * theHeap = new (&theHeapBuf[0]) SuperHeap;
      return theHeap;
    }

    // Prevent copying.
    GlobalHeap (const GlobalHeap&);
    GlobalHeap& operator=(const GlobalHeap&);

  };

}

#endif
