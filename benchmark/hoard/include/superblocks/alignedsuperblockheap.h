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

#ifndef HOARD_ALIGNEDSUPERBLOCKHEAP_H
#define HOARD_ALIGNEDSUPERBLOCKHEAP_H

#include "heaplayers.h"

#include "conformantheap.h"
#include "fixedrequestheap.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    class TheLockType,
	    class MmapSource>
  class SuperblockStore {
  public:

    enum { Alignment = MmapSource::Alignment };

    void * malloc (size_t) {
      if (_freeSuperblocks.isEmpty()) {
	// Get more memory.
	void * ptr = _superblockSource.malloc (ChunksToGrab * SuperblockSize);
	if (!ptr) {
	  return NULL;
	}
	char * p = (char *) ptr;
	for (int i = 0; i < ChunksToGrab; i++) {
	  _freeSuperblocks.insert ((DLList::Entry *) p);
	  p += SuperblockSize;
	}
      }
      return _freeSuperblocks.get();
    }

    void free (void * ptr) {
      _freeSuperblocks.insert ((DLList::Entry *) ptr);
    }

  private:

#if defined(__SVR4)
    enum { ChunksToGrab = 1 };

    // We only get 64K chunks from mmap on Solaris, so we need to grab
    // more chunks (and align them to 64K!) for smaller superblock sizes.
    // Right now, we do not handle this case and just assert here that
    // we are getting chunks of 64K.

    HL::sassert<(SuperblockSize == 65536)> PreventMmapFragmentation;
#else
    enum { ChunksToGrab = 1 };
#endif

    MmapSource _superblockSource;
    DLList _freeSuperblocks;

  };

}


namespace Hoard {

  template <class TheLockType,
	    size_t SuperblockSize,
	    class MmapSource>
  class AlignedSuperblockHeapHelper :
    public ConformantHeap<HL::LockedHeap<TheLockType,
					 FixedRequestHeap<SuperblockSize, 
							  SuperblockStore<SuperblockSize, TheLockType, MmapSource> > > > {};


#if 0

  template <class TheLockType,
	    size_t SuperblockSize>
  class AlignedSuperblockHeap : public AlignedMmap<SuperblockSize,TheLockType> {};


#else

  template <class TheLockType,
	    size_t SuperblockSize,
	    class MmapSource>
  class AlignedSuperblockHeap :
    public AlignedSuperblockHeapHelper<TheLockType, SuperblockSize, MmapSource> {

    HL::sassert<(AlignedSuperblockHeapHelper<TheLockType, SuperblockSize, MmapSource>::Alignment % SuperblockSize == 0)> EnsureProperAlignment;

  };
#endif

}

#endif
