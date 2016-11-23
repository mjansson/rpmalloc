// -*- C++ -*-

#if !defined(HL_BINS4K_H_)
#define HL_BINS4K_H_

#include <cassert>

#include "bins.h"
#include "sassert.h"

namespace HL {

  template <class Header>
    class bins<Header, 4096> { 

    public:
      bins (void) {}

      enum { NUM_BINS = 33 };
      enum { BIG_OBJECT = 4096 - sizeof(Header) };

      static const size_t _bins[NUM_BINS];

      static inline int getSizeClass (size_t sz) {
	assert (sz <= BIG_OBJECT);
	if (sz < 8) {
	  return 0;
	} else if (sz <= 128) {
	  return ((sz + 7) >> 3) - 1;
	} else {
	  return slowLookupSizeClass (sz);
	}
      }

      static inline size_t getClassSize (const int i) {
	assert (i >= 0);
	assert (i < NUM_BINS);
	return _bins[i];
      }

    private:
      
      static int slowLookupSizeClass (const size_t sz) {
	// Find the size class for a given object size
	// (the smallest i such that _bins[i] >= sz).
	int sizeclass = 0;
	while (_bins[sizeclass] < sz) 
	  {
	    sizeclass++;
	    assert (sizeclass < NUM_BINS);
	  }
	return sizeclass;
      }
      
      sassert<(BIG_OBJECT > 0)> verifyHeaderSize;
      
    };
}

template <class Header>
const size_t HL::bins<Header, 4096>::_bins[NUM_BINS] = {8UL, 16UL, 24UL, 32UL, 40UL, 48UL, 56UL, 64UL, 72UL, 80UL, 88UL, 96UL, 104UL, 112UL, 120UL, 128UL, 152UL, 176UL, 208UL, 248UL, 296UL, 352UL, 416UL, 496UL, 592UL, 704UL, 856UL, 1024UL, 1224UL, 1712UL, 2048UL, 3416UL, 4096UL - sizeof(Header)};

#endif

