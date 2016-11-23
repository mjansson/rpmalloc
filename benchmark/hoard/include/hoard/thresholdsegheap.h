// -*- C++ -*-

#ifndef HOARD_THRESHOLD_SEGHEAP_H
#define HOARD_THRESHOLD_SEGHEAP_H

namespace Hoard {

  // Allows superheap to hold at least ThresholdSlop but no more than
  // ThresholdFraction% more memory than client currently holds.

  template <int ThresholdFraction, // % over current allowed in superheap.
	    int ThresholdSlop,     // constant amount allowed in superheap.
	    int NumBins,
	    int (*getSizeClass) (const size_t),
	    size_t (*getClassMaxSize) (const int),
	    size_t MaxObjectSize,
	    class LittleHeap,
	    class BigHeap>
  class ThresholdSegHeap : public BigHeap
  {
  public:

    ThresholdSegHeap()
      : _currLive (0),
	_maxLive (0),
	_maxFraction (1.0 + (double) ThresholdFraction / 100.0),
	_cleared (false)
    {}

    size_t getSize (void * ptr) {
      return BigHeap::getSize(ptr);
    }

    void * malloc (size_t sz) {
      if (sz >= MaxObjectSize) {
	return BigHeap::malloc (sz);
      }
      // Once the amount of cached memory in the superheap exceeds the
      // desired threshold over max live requested by the client, dump
      // it all.
      const int sizeClass = getSizeClass (sz);
      const size_t maxSz = getClassMaxSize (sizeClass);
      if (sizeClass >= NumBins) {
	return BigHeap::malloc (maxSz);
      } else {
	void * ptr = _heap[sizeClass].malloc (maxSz);
	if (ptr == NULL) {
	  return BigHeap::malloc (maxSz);
	}
	assert (getSize(ptr) <= maxSz);
	_currLive += getSize (ptr);
	if (_currLive >= _maxLive) {
	  _maxLive = _currLive;
	  _cleared = false;
	}
	return ptr;
      }
    }

    void free (void * ptr) {
      // Update current live memory stats, then free the object.
      size_t sz = getSize(ptr);
      if (sz >= MaxObjectSize) {
	BigHeap::free (ptr);
	return;
      }
      int cl = getSizeClass (sz);
      if (cl >= NumBins) {
	BigHeap::free (ptr);
	return;
      }
      if (_currLive < sz) {
	_currLive = 0;
      } else {
	_currLive -= sz;
      }
      _heap[cl].free (ptr);
      bool crossedThreshold = (double) _maxLive > _maxFraction * (double) _currLive;
      if ((_currLive > ThresholdSlop) && crossedThreshold && !_cleared)
	{
	  // When we drop below the threshold, clear the heap.
	  for (int i = 0; i < NumBins; i++) {
	    _heap[i].clear();
	  }
	  // We won't clear again until we reach maxlive again.
	  _cleared = true;
	  _maxLive = _currLive;
	}
    }

  private:

    /// The current amount of live memory held by a client of this heap.
    unsigned long _currLive;

    /// The maximum amount of live memory held by a client of this heap.
    unsigned long _maxLive;

    /// Maximum fraction calculation.
    const double _maxFraction;

    /// Have we already cleared out the superheap?
    bool _cleared;

    LittleHeap _heap[NumBins];
  };

}

#endif

