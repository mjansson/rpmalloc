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

#ifndef HOARD_FIXEDREQUESTHEAP_H
#define HOARD_FIXEDREQUESTHEAP_H

/**
 * @class FixedRequestHeap
 * @brief Always grabs the same size, regardless of the request size.
 */

namespace Hoard {
  
  template <size_t RequestSize,
	    class SuperHeap>
  class FixedRequestHeap : public SuperHeap {
  public:
    inline void * malloc (size_t) {
      return SuperHeap::malloc (RequestSize);
    }
    inline static size_t getSize (void *) {
      return RequestSize;
    }
  };

}

#endif
