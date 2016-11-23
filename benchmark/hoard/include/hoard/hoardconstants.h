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


#ifndef HOARD_HOARDCONSTANTS_H
#define HOARD_HOARDCONSTANTS_H

namespace Hoard {
  
  /// The maximum amount of memory that each TLAB may hold, in bytes.
  enum { MAX_MEMORY_PER_TLAB = 2 * 1024 * 1024UL }; // 2MB
  
  /// The maximum number of threads supported (sort of).
  enum { MaxThreads = 2048 };
  
  /// The maximum number of heaps supported.
  enum { NumHeaps = 128 };
  
  /// Size, in bytes, of the largest object we will cache on a
  /// thread-local allocation buffer.
  enum { LargestSmallObject = 256UL };
    
}

#endif
