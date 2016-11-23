// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
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

#ifndef HL_KINGSLEYHEAP_H
#define HL_KINGSLEYHEAP_H

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

  size_t class2Size (const int i);

#if defined(__sparc) && defined(__GNUC__)
  inline int popc (int v) {
    int r;
    asm volatile ("popc %1, %0"
                  : "=r" (r)
                  : "r" (v));
    return r;
  }
#endif

  /**
   * A speed optimization:
   * we use this array to quickly return the size class of objects
   * from 8 to 128 bytes.
   */
  const int cl[16] = { 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };

  inline int size2Class (const size_t sz) {
#if defined(__sparc) && defined(__GNUC__)
    // Adapted from _Hacker's Delight_, by Henry Warren (p.80)
    size_t x = sz;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >> 16);
    return popc(x) - 3;
#else
    if (sz < 128 ) {
      assert (class2Size(cl[sz >> 3]) >= sz);
      return cl[(sz - 1) >> 3];
    } else {
      //
      // We know that the object is at least 128 bytes long,
      // so we can avoid iterating 4 times.
      //
      int c = 4;
      size_t sz1 = ((sz - 1) >> 4);
      while (sz1 > 7) {
        sz1 >>= 1;
        c++;
      }
      assert (class2Size(c) >= sz);
      return c;
    }
#endif

  }

  inline size_t class2Size (const int i) {
    return (size_t) (1 << (i+3));
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
