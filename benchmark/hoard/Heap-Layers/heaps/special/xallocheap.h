/* -*- C++ -*- */

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

#ifndef HL_XALLOCHEAP_H
#define HL_XALLOCHEAP_H

#include <cstddef>
#include <utility/align.h>

namespace HL {

  template <int ArenaSize, class SuperHeap>
  class XallocHeap : public SuperHeap {

  public:

    inline XallocHeap() {
      start_of_array = (char *) SuperHeap::malloc (ArenaSize);
      end_of_array = start_of_array + HL::align<sizeof(double)>(sizeof(Nuggie));
      size_lval(end_of_array) = 0;
      last_block = NULL;
    }

    inline ~XallocHeap() {
      SuperHeap::free (start_of_array);
    }

    inline void * malloc (size_t size) {
      char * old_end_of_array = end_of_array;
      end_of_array += HL::align<sizeof(double)>(size + sizeof(Nuggie));
      if (old_end_of_array + size >= start_of_array + ArenaSize) {
        // We're out of memory.
        return NULL;
      }
      size_lval(end_of_array) = end_of_array - old_end_of_array;
      clear_use(end_of_array);  /* this is not necessary, cause it will be zero */
      set_use(old_end_of_array);
      last_block = old_end_of_array;
      return old_end_of_array;
    }

    inline void free (void * ptr) {
      char * p = (char *) ptr;
      char * q;
      /* At this point we could check that the in_use bit for this block
	 is set, and also check that the size is right */
      clear_use(p);  /* mark this block as unused */
      if (p == last_block) {
        while (1) {
          q = prev_block(p);
          if (q == p) {
            last_block = NULL;
            end_of_array = p;
            break;  /* only happens when we get to the beginning */
          }
          if (in_use(q)) {
            last_block = q;
            end_of_array = p;
            break;
          }
          p = q;
        }
      }
    }

  private:

    static inline size_t& size_lval (char * x) {
      return (((Nuggie *)(((char *)x) - sizeof(Nuggie)))->size);
    }

    static inline char * prev_block (char * x) {
      return (((char *) x) - (size_lval(x) & (~1)));
    }

    static inline int in_use (char * x) {
      return (size_lval(x) & (1));
    }

    static inline void set_use (char * x) {
      (size_lval(x) |= (1));
    }

    static inline void clear_use (char * x) {
      (size_lval(x) &= (~1));
    }

    class Nuggie {
    public:
      size_t size;
    };

    char * end_of_array;
    char * start_of_array;
    char * last_block;
  };

}


#endif
