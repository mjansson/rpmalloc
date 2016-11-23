/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2015 by Emery Berger
  http://www.emeryberger.org
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

#ifndef HL_DLHEAP_H
#define HL_DLHEAP_H

/**
 * @file dlheap.h
 * @brief Contains all the classes required to approximate DLmalloc 2.7.0.
 * @author Emery Berger
 */

#include <assert.h>

#include "heaps/buildingblock/adaptheap.h"
#include "utility/dllist.h"
#include "utility/sllist.h"
#include "heaps/objectrep/coalesceableheap.h"
#include "heaps/buildingblock/coalesceheap.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif


/**
 * @class CoalesceableMmapHeap
 * @brief Adds headers to mmapped objects to allow coalescing.
 * @author Emery Berger
 */

namespace HL {

template <class Mmap>
class CoalesceableMmapHeap : public RequireCoalesceable<Mmap> {
public:
  typedef RequireCoalesceable<Mmap> super;
  typedef typename RequireCoalesceable<Mmap>::Header Header;

  inline void * malloc (const size_t sz) {
    void * buf = super::malloc (sz + sizeof(Header));
    void * ptr = Header::makeObject (buf, 0, sz);
    super::markMmapped (ptr);
    super::markInUse (ptr);
    return ptr;
  }
  inline void free (void * ptr) {
    super::free (Header::getHeader (ptr));
  }
  inline int remove (void * ptr) {
    return super::remove (Header::getHeader (ptr));
  }
};

/**
 * @class SelectMmapHeap
 * @brief Use Mmap (here the superheap) for objects above a certain size.
 * @author Emery Berger
 *
 * @param ThresholdBytes The maximum number of bytes managed by SmallHeap.
 * @param SmallHeap The heap for "small" objects.
 * @param super The heap for "large" objects.
 */

template <int ThresholdBytes, class SmallHeap, class super>
class SelectMmapHeap : public super {
public:
  inline void * malloc (const size_t sz) {
    void * ptr = NULL;
    if (sz <= ThresholdBytes) {
      ptr = sm.malloc (sz);
    }

    // Fall-through: go ahead and try mmap if the small heap is out of memory.

    if (ptr == NULL) {
      ptr = super::malloc (sz);
      super::markMmapped (ptr);
    }
    return ptr;
  }
  inline void free (void * ptr) {
    if (super::isMmapped(ptr)) {
      super::free (ptr);
    } else {
      sm.free (ptr);
    }
  }
  inline int remove (void * ptr) {
    if (super::isMmapped(ptr)) {
      return super::remove (ptr);
    } else {
      return sm.remove (ptr);
    }
  }
  inline void clear (void) {
    sm.clear();
    super::clear();
  }

private:
  SmallHeap sm;
};


// LeaHeap 2.7.0-like threshold scheme
// for managing a small superheap.


// NOTE: THIS HAS BEEN CHANGED NOW!

template <int ThresholdBytes, class super>
class Threshold : public super {
public:

  enum { MIN_LARGE_SIZE = 64 };

#if 1
  Threshold (void)
    : freeAllNextMalloc (FALSE),
      inUse (0),
      maxInUse (0),
      threshold (0)
  {}

  inline void * malloc (const size_t sz) {
    void * ptr = super::malloc (sz);
    if (ptr) {
      const size_t actualSize = super::getSize(ptr);
      inUse += actualSize;
      if (inUse > maxInUse) {
        maxInUse = inUse;
        threshold = 16384 + maxInUse / 2;
      }
#if 0
      if (freed < 0) {
        freed = 0;
      }
#endif
    }
    return ptr;
  }


#if 0
  void * internalMalloc (const size_t sz) {
    if (freeAllNextMalloc || (freed > 0)) {
      freed = 0;
      super::freeAll();
      freeAllNextMalloc = FALSE;
    }
    void * ptr = super::malloc (sz);
    if (ptr != NULL) {
      if (sz < MIN_LARGE_SIZE) {
        freed -= getSize(ptr);
        if (freed < 0) {
          freed = 0;
        }
      }
    }
    return ptr;
  }
#endif


  inline void free (void * ptr) {
    const size_t sz = super::getSize(ptr);
    inUse -= sz;
    super::free (ptr);
    if (super::getMemoryHeld() > threshold) {
      super::freeAll();
    }
  }

private:

  /// The number of bytes in use.
  size_t inUse;

  /// The high-water mark of bytes in use.
  size_t maxInUse;

  size_t threshold;

  // How many bytes have been freed (whose requests were below MIN_LARGE_SIZE).
  //  int freed;

  /// Should we free all in the superheap on the next malloc?
  bool freeAllNextMalloc;

#else
  inline Threshold (void)
  {}

  inline void * malloc (const size_t sz) {
    if ((getMemoryHeld() > ThresholdBytes) ||
      ((sz >= MIN_LARGE_SIZE) && (getMemoryHeld() >= sz))) {
      super::freeAll();
    }
    return super::malloc (sz);
  }
#endif
};


/**
 * @namespace DLBigHeapNS
 * @brief All of the bins & size functions for the "big heap".
 */

namespace DLBigHeapNS
{
  const size_t bins[] = {8U, 16U, 24U, 32U, 40U, 48U, 56U, 64U, 72U, 80U, 88U,
                         96U, 104U, 112U, 120U, 128U, 136U, 144U, 152U, 160U,
                         168U, 176U, 184U, 192U, 200U, 208U, 216U, 224U, 232U,
                         240U, 248U, 256U, 264U, 272U, 280U, 288U, 296U, 304U,
                         312U, 320U, 328U, 336U, 344U, 352U, 360U, 368U, 376U,
                         384U, 392U, 400U, 408U, 416U, 424U, 432U, 440U, 448U,
                         456U, 464U, 472U, 480U, 488U, 496U, 504U, 512U, 576U,
                         640U, 704U, 768U, 832U, 896U, 960U, 1024U, 1088U, 1152U,
                         1216U, 1280U, 1344U, 1408U, 1472U, 1536U, 1600U, 1664U,
                         1728U, 1792U, 1856U, 1920U, 1984U, 2048U, 2112U, 2560U,
                         3072U, 3584U,
                         4096U, 4608U, 5120U, 5632U, 6144U, 6656U, 7168U, 7680U,
                         8192U, 8704U, 9216U, 9728U, 10240U, 10752U, 12288U,
                         16384U, 20480U, 24576U, 28672U, 32768U, 36864U, 40960U,
                         65536U, 98304U, 131072U, 163840U, 262144U, 524288U,
                         1048576U, 2097152U, 4194304U, 8388608U, 16777216U,
                         33554432U, 67108864U, 134217728U, 268435456U, 536870912U,
                         1073741824U, 2147483648U  };

  enum { NUMBINS = sizeof(bins) / sizeof(size_t) };
  enum { BIG_OBJECT = 2147483648U };

  /**
   * @brief Compute the log base two.
   * @param sz The value we want the log of.
   */
  inline int log2 (const size_t sz) {
    int c = 0;
    size_t sz1 = sz;
    while (sz1 > 1) {
      sz1 >>= 1;
      c++;
    }
    return c;
  }

  inline int getSizeClass (const size_t sz);

  inline size_t getClassSize (const int i) {
    assert (i >= 0);
    assert (i < NUMBINS);
#if 0
    if (i < 64) {
      return ((size_t) ((i+1) << 3));
    } else {
      return
        (i < 89) ? ((size_t) ((i - 55) << 6)) :
        (i < 106) ? ((size_t) ((i - 84) << 9)) :
        (i < 114) ? ((size_t) ((i - 103) << 12)) :
        (i < 118) ? ((size_t) ((i - 112) << 15)) :
        (i < 120) ? ((size_t) ((i - 117) * 262144)) :
        (i < 122) ? ((size_t) ((i - 119) * 1048576)) :
        (i < 124) ? ((size_t) ((i - 121) * 4 * 1048576)) :
        (i < 126) ? ((size_t) ((i - 123) * 16 * 1048576)) :
        (i < 128) ? ((size_t) ((i - 125) * 64 * 1048576)) :
        (i < 130) ? ((size_t) ((i - 127) * 256 * 1048576)) :
        ((size_t) ((i - 129) * 1024 * 1048576));
    }
#else
#if 0
    if (i < 64) {
      return (size_t) ((i+1) << 3);
    }
#endif
    return bins[i];
#endif
  }

  inline int getSizeClass (const size_t sz) {
    size_t sz1 = sz - 1;
    if (sz1 <= 513) {
      return (int) (sz1 >> 3);
    } else {
      return (int) ((((sz1 >>  6) <= 32)?  56 + (sz1 >>  6):
		     ((sz1 >>  9) <= 20)?  91 + (sz1 >>  9):
		     ((sz1 >> 12) <= 10)? 110 - 6 + (sz1 >> 12):
		     ((sz1 >> 15) <=  4)? 119 - 6 + (sz1 >> 15):
		     ((sz1 >> 18) <=  2)? 124 - 6 + (sz1 >> 18):
		     126 - 6 + (size_t) log2(sz1>>19)));
    }
  }

 }


/**
 * @namespace DLSmallHeapNS
 * @brief The size functions for the "small" heap (fastbins).
 */

namespace DLSmallHeapNS {
  enum { NUMBINS = 8 };
  inline int getSizeClass (const size_t sz) {
    return (int) ((sz-1) >> 3);
  }
  inline size_t getClassSize (const int i) {
    assert (i >= 0);
    assert (i < NUMBINS);
    return (size_t) (((long) i+1) << 3);
  }
}

#if 0

#include "kingsleyheap.h"

/**
 * @class DLBigHeapType
 * @brief The "big heap" -- a coalescing segregated-fits allocator.
 * @author Emery Berger
 */

template <class super>
class DLBigHeapType :
  public
CoalesceHeap<RequireCoalesceable<
  SegHeap<Kingsley::NUMBINS,
          Kingsley::size2Class,
          Kingsley::class2Size,
          AdaptHeap<DLList, NullHeap<super> >,
          super> > >
{};

#else

template <class super>
class DLBigHeapType :
  public
CoalesceHeap<RequireCoalesceable<
  SegHeap<DLBigHeapNS::NUMBINS,
          DLBigHeapNS::getSizeClass,
          DLBigHeapNS::getClassSize,
          AdaptHeap<DLList, NullHeap<super> >,
          super> > >
{};

#endif

/**
 * @class DLSmallHeapType
 * @brief The "small heap" -- non-coalescing "fastbins" (quicklists).
 * @author Emery Berger
 */

template <class super>
class DLSmallHeapType :
  public RequireCoalesceable<
  StrictSegHeap<DLSmallHeapNS::NUMBINS,
                DLSmallHeapNS::getSizeClass,
                DLSmallHeapNS::getClassSize,
                AdaptHeap<HL::SLList, NullHeap<super> >,
                super> > {};


/**
 * @class LeaHeap
 * @brief This heap approximates the algorithms used by DLmalloc 2.7.0.
 *
 * The whole thing. Big objects are allocated via mmap.
 * Other objects are first allocated from the special thresholded quicklists,
 * or if they're too big, they're allocated from the coalescing big heap.
 *
 * @param Sbrk An sbrk-like heap, for small object allocation.
 * @param Mmap An mmap-like heap, for large object allocation.
 */

template <class Sbrk, class Mmap>
class LeaHeap :
  public
    SelectMmapHeap<128 * 1024,
                   Threshold<4096,
                             DLSmallHeapType<DLBigHeapType<CoalesceableHeap<Sbrk> > > >,
                   CoalesceableMmapHeap<Mmap> >
{};

}

#endif
