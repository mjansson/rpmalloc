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

/**
 * @file segheap.h
 * @brief Definition of SegHeap.
 */

#ifndef HL_SEGHEAP_H
#define HL_SEGHEAP_H

/**
 * @class SegHeap
 * @brief A segregated-fits collection of (homogeneous) heaps.
 * @author Emery Berger
 *
 * Note that one extra heap is used for objects that are "too big".
 *
 * @param NumBins The number of bins (subheaps).
 * @param getSizeClass Function to compute size class from size.
 * @param getClassMaxSize Function to compute the largest size for a given size class.
 * @param LittleHeap The subheap class.
 * @param BigHeap The parent class, used for "big" objects.
 *
 * Example:<BR>
 * <TT>
 *  int myFunc (size_t sz); // The size-to-class function.<BR>
 *  size_t myFunc2 (int); // The class-to-size function.<P>
 *  // The heap. Use freelists for these small objects,<BR>
 *  // but defer to malloc for large objects.<P>
 *
 * SegHeap<4, myFunc, myFunc2, freelistHeap<MallocHeap>, MallocHeap> mySegHeap;
 * </TT>
 **/

#include <assert.h>
#include <utility/gcd.h>

namespace HL {

  template <int NumBins,
	    int (*getSizeClass) (const size_t),
	    size_t (*getClassMaxSize) (const int),
	    class LittleHeap,
	    class BigHeap>
  class SegHeap : public LittleHeap {
  public:

    enum { Alignment = gcd<LittleHeap::Alignment, BigHeap::Alignment>::VALUE };

    inline SegHeap()
      : _memoryHeld (0),
	_maxObjectSize (getClassMaxSize(NumBins - 1))
    {
      for (int i = 0; i < NUM_ULONGS; i++) {
        binmap[i] = 0;
      }
    }

    inline size_t getMemoryHeld() const {
      return _memoryHeld;
    }

    size_t getSize (void * ptr) {
      return LittleHeap::getSize (ptr);
    }

    inline void * malloc (const size_t sz) {
      void * ptr = NULL;
      if (sz > _maxObjectSize) {
        goto GET_MEMORY;
      }

      {
        const int sc = getSizeClass(sz);
        assert (sc >= 0);
        assert (sc < NumBins);
        int idx = sc;
        int block = idx2block (idx);
        unsigned long map = binmap[block];
        unsigned long bit = idx2bit (idx);

        for (;;) {
          if (bit > map || bit == 0) {
            do {
              if (++block >= NUM_ULONGS) {
                goto GET_MEMORY;
                // return bigheap.malloc (sz);
              }
            } while ( (map = binmap[block]) == 0);

            idx = block << SHIFTS_PER_ULONG;
            bit = 1;
          }

          while ((bit & map) == 0) {
            bit <<= 1;
            assert(bit != 0);
            idx++;
          }

          assert (idx < NumBins);
          ptr = myLittleHeap[idx].malloc (sz);

          if (ptr == NULL) {
            binmap[block] = map &= ~bit; // Write through
            idx++;
            bit <<= 1;
          } else {
	    _memoryHeld -= sz;
            return ptr;
          }
        }
      }

      GET_MEMORY:
      if (ptr == NULL) {
        // There was no free memory in any of the bins.
        // Get some memory.
        ptr = bigheap.malloc (sz);
      }

      return ptr;
    }


    inline void free (void * ptr) {
      // printf ("Free: %x (%d bytes)\n", ptr, getSize(ptr));
      const size_t objectSize = getSize(ptr); // was bigheap.getSize(ptr)
      if (objectSize > _maxObjectSize) {
        bigheap.free (ptr);
      } else {
        int objectSizeClass = getSizeClass(objectSize);
        assert (objectSizeClass >= 0);
        assert (objectSizeClass < NumBins);
        // Put the freed object into the right sizeclass heap.
        assert (getClassMaxSize(objectSizeClass) >= objectSize);
#if 1
        while (getClassMaxSize(objectSizeClass) > objectSize) {
          objectSizeClass--;
        }
#endif
        assert (getClassMaxSize(objectSizeClass) <= objectSize);
        if (objectSizeClass > 0) {
          assert (objectSize >= getClassMaxSize(objectSizeClass - 1));
        }

        myLittleHeap[objectSizeClass].free (ptr);
        mark_bin (objectSizeClass);
        _memoryHeld += objectSize;
      }
    }


    void clear() {
      for (int i = 0; i < NumBins; i++) {
        myLittleHeap[i].clear();
      }
      for (int j = 0; j < NUM_ULONGS; j++) {
        binmap[j] = 0;
      }
      bigheap.clear();
      _memoryHeld = 0;
    }

  private:

    enum { BITS_PER_ULONG = sizeof(unsigned long) * 8 };
    enum { SHIFTS_PER_ULONG = (BITS_PER_ULONG == 32) ? 5 : 6 };
    enum { MAX_BITS = (NumBins + BITS_PER_ULONG - 1) & ~(BITS_PER_ULONG - 1) };


  private:

    static inline int idx2block (int i) {
      int blk = i >> SHIFTS_PER_ULONG;
      assert (blk < NUM_ULONGS);
      assert (blk >= 0);
      return blk;
    }

    static inline unsigned long idx2bit (int i) {
      unsigned long bit = ((1U << ((i) & ((1U << SHIFTS_PER_ULONG)-1))));
      return bit;
    }


  protected:

    BigHeap bigheap;

    enum { NUM_ULONGS = MAX_BITS / BITS_PER_ULONG };
    unsigned long binmap[NUM_ULONGS];

    inline int get_binmap (int i) const {
      return binmap[i >> SHIFTS_PER_ULONG] & idx2bit(i);
    }

    inline void mark_bin (int i) {
      binmap[i >> SHIFTS_PER_ULONG] |=  idx2bit(i);
    }

    inline void unmark_bin (int i) {
      binmap[i >> SHIFTS_PER_ULONG] &= ~(idx2bit(i));
    }

    size_t _memoryHeld;

    const size_t _maxObjectSize;

    // The little heaps.
    LittleHeap myLittleHeap[NumBins];

  };

}


#endif
