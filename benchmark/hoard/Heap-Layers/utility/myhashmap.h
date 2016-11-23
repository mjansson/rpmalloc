// -*- C++ -*-

#ifndef HL_MYHASHMAP_H
#define HL_MYHASHMAP_H

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


#include <assert.h>
#include <new>

#include "hash.h"

namespace HL {

  template <typename Key,
	    typename Value,
	    class Allocator>
  class MyHashMap {

  public:

    MyHashMap (unsigned int size = INITIAL_NUM_BINS)
      : _numBins (size)
    {
      assert (_numBins > 0);
      void * buf = _allocator.malloc (sizeof(ListNodePtr) * _numBins); 
      _bins = new (buf) ListNodePtr[_numBins];
      for (unsigned int i = 0 ; i < _numBins; i++) {
	_bins[i] = NULL;
      }
    }

    void set (Key k, Value v) {
      unsigned int binIndex = (unsigned int) (Hash<Key>::hash(k) % _numBins);
      ListNode * l = _bins[binIndex];
      while (l != NULL) {
	if (l->key == k) {
	  l->value = v;
	  return;
	}
	l = l->next;
      }
      // Didn't find it.
      insert (k, v);
    }

    Value get (Key k) {
      unsigned int binIndex = (unsigned int) (Hash<Key>::hash(k) % _numBins);
      ListNode * l = _bins[binIndex];
      while (l != NULL) {
	if (l->key == k) {
	  return l->value;
	}
	l = l->next;
      }
      // Didn't find it.
      return 0;
    }

    void erase (Key k) {
      unsigned int binIndex = (unsigned int) (Hash<Key>::hash(k) % _numBins);
      ListNode * curr = _bins[binIndex];
      ListNode * prev = NULL;
      while (curr != NULL) {
	if (curr->key == k) {
	  // Found it.
	  if (curr != _bins[binIndex]) {
	    assert (prev->next == curr);
	    prev->next = prev->next->next;
	    _allocator.free (curr);
	  } else {
	    ListNode * n = _bins[binIndex]->next;
	    _allocator.free (_bins[binIndex]);
	    _bins[binIndex] = n;
	  }
	  return;
	}
	prev = curr;
	curr = curr->next;
      }
    }


  private:

    void insert (Key k, Value v) {
      unsigned int binIndex = (unsigned int) (Hash<Key>::hash(k) % _numBins);
      void * ptr = _allocator.malloc (sizeof(ListNode));
      if (ptr) {
	ListNode * l = new (ptr) ListNode;
	l->key = k;
	l->value = v;
	l->next = _bins[binIndex];
	_bins[binIndex] = l;
      }
    }

    enum { INITIAL_NUM_BINS = 511 };

    class ListNode {
    public:
      ListNode (void)
	: next (NULL)
      {}
      Key key;
      Value value;
      ListNode * next;
    };

    unsigned long 	_numBins;

    typedef ListNode * 	ListNodePtr;
    ListNodePtr * 	_bins;
    Allocator 		_allocator;
  };

}

#endif
