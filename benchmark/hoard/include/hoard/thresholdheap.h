// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.org
 
  Copyright (c) 1998-2015 Emery Berger
  
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

#ifndef HOARD_THRESHOLDHEAP_H
#define HOARD_THRESHOLDHEAP_H

#include <map>
#include <list>

#include "debugprint.h"

#include "heaplayers.h"
#include "mmapalloc.h"
//#include "exactlyoneheap.h"

using namespace std;

// Free objects to the superheap if the utilization drops below a
// threshold.  For example (assuming Allocated - InUse > MinWaste):
//
//   ThresholdN/D = 1 => always free memory (since InUse/Allocated is always less than 1).
//   ThresholdN/D = 1/3 => free only when caching 1/3 of max memory ever used
//   ThresholdN/D = 0 => never free memory

namespace Hoard {

  template <int ThresholdMinWaste,
	    int ThresholdNumerator,
	    int ThresholdDenominator,
	    class SuperHeap>
  class ThresholdHeap : public SuperHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    ThresholdHeap()
      : _inUse (0),
	_allocated (0),
	_maxAllocated (0)
    {}

    inline void * malloc (size_t sz) {
      // Look for an object of this size (exactly? larger?)
      // in the cache.

      void * ptr = _cache.remove(sz);
      if (ptr == NULL) {
	// If none found, allocate one and return it.
	ptr = SuperHeap::malloc (sz);
	_allocated += SuperHeap::getSize(ptr);
	if (_allocated > _maxAllocated) {
	  _maxAllocated = _allocated;
	}
      }
      _inUse += SuperHeap::getSize(ptr);
      assert (SuperHeap::getSize(ptr) >= sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }

    inline void free (void * ptr) {
      size_t sz = SuperHeap::getSize(ptr);
      DEBUG_PRINT3("freeing an object of size %d: inUse = %d, allocated = %d\n", sz, _inUse, _allocated);
      _inUse -= sz;
      // Add it to the cache.
      _cache.add (sz, ptr);
      // Now decide whether to free up some memory.
      // Note that total cached = _allocated - _inUse.
      // We will free memory whenever total cached > N/D * max allocated,
      // which is (_allocated - _inUse) > N/D * _maxAllocated
      // = D * (_allocated - _inUse) > N * _maxAllocated.
      while ((ThresholdMinWaste < (_allocated - _inUse)) && 
	     (ThresholdDenominator * (_allocated - _inUse) > ThresholdNumerator * _maxAllocated)) {

	DEBUG_PRINT3("crossing threshold: inUse = %d, allocated = %d, max allocated = %d\n", _inUse, _allocated, _maxAllocated);

	// Find an object to free.
	// To minimize the number of calls to the superheap,
	// start with the largest objects and work our way down.
	void * obj = _cache.removeLargest();
	if (!obj) {
	  break;
	}
	size_t objSz = SuperHeap::getSize (obj);
	DEBUG_PRINT1("found a big object in the cache of size %lu.\n", objSz);
	// Free it.
	DEBUG_PRINT3("Freeing %d: inUse = %d, allocated = %d\n", objSz, _inUse, _allocated);
	_allocated -= objSz;
	SuperHeap::free (obj);
      }
      DEBUG_PRINT("Threshold done.\n");
    }

  private:

    class TopHeap : public SizeHeap<BumpAlloc<65536, MmapAlloc> > {
    public:
#if 0
      void * malloc (size_t sz) {
	void * ptr = SizeHeap<BumpAlloc<65536, MmapAlloc> >::malloc (sz);
	char buf[255];
	sprintf (buf, "TopHeap malloc %ld = %p\n", sz, ptr);
	fprintf (stderr, buf);
	return ptr;
      }
#endif
    };
  
    // A heap for local allocations for the containers below.
    class LocalHeap :
      public ExactlyOneHeap<KingsleyHeap<AdaptHeap<DLList, TopHeap>, TopHeap> > {};

    template <class K, class V>
    class CacheHelper {
    private:
      typedef list<V, STLAllocator<V, LocalHeap> > listType;
      typedef pair<const K, listType> mapObject;
      typedef map<K, listType, less<K>, STLAllocator<mapObject, LocalHeap> > mapType;

      /// A map (sz -> list[ptr])
      mapType theMap;

    public:

      // Add an object of the given size.
      // NOTE: Perhaps we should round to the next power of two?
      void add (K sz, V ptr) {
	theMap[sz].push_front (ptr);
      }

      // Remove one object of the given size.
      V remove (K sz) {
	// First, check to see if that exact size exists.
	typename mapType::iterator i = theMap.find (sz);
	if (i != theMap.end()) {
	  V ptr = theMap[sz].front();
	  theMap[sz].pop_front();
	  if (theMap[sz].empty()) {
	    // Last item: free this key.
	    theMap.erase (sz);
	  }
	  return ptr;
	}

	// Otherwise, do first fit: find the first object that is big
	// enough to fit the request, searching from smallest to largest
	// size.

	for (i = theMap.begin();
	     i != theMap.end();
	     ++i) {
	  K key = (*i).first;
	  listType& theList = (*i).second;
	  if (key >= sz) {
	    V ptr = theList.front();
	    theList.pop_front();
	    // If we have just removed the last element from the list,
	    // erase the key entry.
	    if (theList.empty()) {
	      theMap.erase (key);
	    }
	    return ptr;
	  }
	}
	return NULL;
      }

      // Remove one of the largest available objects.
      V removeLargest() {
	// Get the last (largest) element.
	typename mapType::reverse_iterator i;
	i = theMap.rbegin();
	// If we found one (in other words, the list is non-empty),
	// remove it.
	if (i != theMap.rend()) {
	  K key = (*i).first;
	  listType& theList = (*i).second;
	  V ptr = theList.front();
	  theList.pop_front();
	  // If we have just removed the last element from the list,
	  // erase the key entry.
	  if (theList.empty()) {
	    theMap.erase (key);
	  }
	  return ptr;
	}
	return NULL;
      }
    };

    class Cache : public CacheHelper<size_t, void *> {};


    /// Amount of memory in use by a client.
    unsigned long _inUse;

    /// Amount of memory currently allocated from the OS (we can free some).
    unsigned long _allocated;

    /// The most memory we have ever allocated from the OS.
    unsigned long _maxAllocated;

    /// Saved chunks of memory from the OS.
    Cache _cache;

  };

}
#endif
