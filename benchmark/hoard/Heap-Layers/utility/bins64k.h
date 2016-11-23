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

#if !defined(HL_BINS64K_H)
#define HL_BINS64K_H

#include <cstdlib>
#include <assert.h>

#include "bins.h"
#include "./ilog2.h"
#include "sassert.h"

namespace HL {

  template <class Header>
  class bins<Header, 65536> {
  public:

    bins (void)
    {
#ifndef NDEBUG
      for (int i = sizeof(double); i < BIG_OBJECT; i++) {
	int sc = getSizeClass(i);
	assert (getClassSize(sc) >= i);
	assert (getClassSize(sc-1) < i);
	assert (getSizeClass(getClassSize(sc)) == sc);
      }
#endif
    }

    enum { BIG_OBJECT = 8192 };
    enum { NUM_BINS = 11 };

    static inline int getSizeClass (size_t sz) {
      sz = (sz < sizeof(double)) ? sizeof(double) : sz;
      return (int) HL::ilog2(sz) - 3;
    }

    static inline size_t getClassSize (int i) {
      assert (i >= 0);
      return (sizeof(double) << i);
    }

  private:

    sassert<(BIG_OBJECT > 0)> verifyHeaderSize;
  };

}



#endif
