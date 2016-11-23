// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2015 by Emery Berger
  http://www.emeryberger.com
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

#ifndef HL_KINGSLEYHEAP_H
#define HL_KINGSLEYHEAP_H

#include "utility/ilog2.h"
#include "heaps/combining/strictsegheap.h"

/**
 * @file kingsleyheap.h
 * @brief Classes to implement a Kingsley (power-of-two, segregated fits) allocator.
 */

/**
 * @namespace Kingsley
 * @brief Functions to implement KingsleyHeap.
 */

namespace Kingsley {

  inline size_t class2Size (const int i) {
    auto sz = (size_t) (1ULL << (i+3));
    return sz;
  }

  inline int size2Class (const size_t sz) {
    auto cl = (int) HL::ilog2 ((sz < 8) ? 8 : sz) - 3;
    return cl;
  }

  enum { NUMBINS = 29 };

}

/**
 * @class KingsleyHeap
 * @brief The Kingsley-style allocator.
 * @param PerClassHeap The heap to use for each size class.
 * @param BigHeap The heap for "large" objects.
 * @see Kingsley
 */

namespace HL {

template <class PerClassHeap, class BigHeap>
  class KingsleyHeap :
   public StrictSegHeap<Kingsley::NUMBINS,
                        Kingsley::size2Class,
                        Kingsley::class2Size,
                        PerClassHeap,
                        BigHeap> {};

}

#endif
