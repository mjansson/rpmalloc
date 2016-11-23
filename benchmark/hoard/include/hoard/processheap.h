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

#ifndef HOARD_PROCESSHEAP_H
#define HOARD_PROCESSHEAP_H

#include <cstdlib>

#include "alignedsuperblockheap.h"
#include "conformantheap.h"
#include "emptyhoardmanager.h"
#include "hoardmanager.h"
#include "hoardsuperblock.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    int EmptinessClasses,
	    class LockType,
	    class ThresholdClass,
	    class MmapSource>
  class ProcessHeap :
    public ConformantHeap<
    HoardManager<AlignedSuperblockHeap<LockType, SuperblockSize, MmapSource>,
		 EmptyHoardManager<HoardSuperblock<LockType, SuperblockSize, ProcessHeap<SuperblockSize, EmptinessClasses, LockType, ThresholdClass, MmapSource> > >,
		 HoardSuperblock<LockType, SuperblockSize, ProcessHeap<SuperblockSize, EmptinessClasses, LockType, ThresholdClass, MmapSource> >,
		 EmptinessClasses,
		 LockType,
		 ThresholdClass,
		 ProcessHeap<SuperblockSize, EmptinessClasses, LockType, ThresholdClass, MmapSource> > > {
  
  public:
  
    ProcessHeap (void) {}
    
    // Disable allocation from this heap.
    inline void * malloc (size_t);

  private:

    // Prevent copying or assignment.
    ProcessHeap (const ProcessHeap&);
    ProcessHeap& operator=(const ProcessHeap&);

  };

}

#endif
