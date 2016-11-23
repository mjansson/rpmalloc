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

#ifndef HOARD_RELEASEHEAP_H
#define HOARD_RELEASEHEAP_H

#if defined(_WIN32)
#include <windows.h>
#else
// Assume UNIX
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "heaplayers.h"
// #include "sassert.h"

namespace Hoard {

  template <class SuperHeap>
  class ReleaseHeap : public SuperHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    ReleaseHeap() {
      // This heap is only safe for use when its superheap delivers
      // page-aligned memory.  Otherwise, it would run the risk of
      // releasing memory that is still in use.
      sassert<(Alignment % 4096 == 0)> ObjectsMustBePageAligned;
    }

    inline void free (void * ptr) {
      // Tell the OS it can release memory associated with this object.
      MmapWrapper::release (ptr, SuperHeap::getSize(ptr));
      // Now give it to the superheap.
      SuperHeap::free (ptr);
    }

  };

}

#endif
