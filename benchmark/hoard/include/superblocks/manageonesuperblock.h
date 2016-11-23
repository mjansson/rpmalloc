// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger
  
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
 * @file manageonesuperblock.h
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */


#ifndef HOARD_MANAGEONESUPERBLOCK_H
#define HOARD_MANAGEONESUPERBLOCK_H

/**
 * @class  ManageOneSuperblock
 * @brief  A layer that caches exactly one superblock, thus avoiding costly lookups.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

namespace Hoard {

  template <class SuperHeap>
  class ManageOneSuperblock : public SuperHeap {
  public:

    ManageOneSuperblock()
      : _current (NULL)
    {}

    typedef typename SuperHeap::SuperblockType SuperblockType;

    /// Get memory from the current superblock.
    inline void * malloc (size_t sz) {
      if (_current) {
	void * ptr = _current->malloc (sz);
	if (ptr) {
	  assert (_current->getSize(ptr) >= sz);
	  return ptr;
	}
      }
      // No memory -- get another superblock.
      return slowMallocPath (sz);
    }

    /// Try to free the pointer to this superblock first.
    inline void free (void * ptr) {
      SuperblockType * s = SuperHeap::getSuperblock (ptr);
      if (s == _current) {
	_current->free (ptr);
      } else {
	// It wasn't ours, so free it remotely.
	SuperHeap::free (ptr);
      }
    }

    /// Get the current superblock and remove it.
    SuperblockType * get() {
      if (_current) {
	SuperblockType * s = _current;
	_current = NULL;
	return s;
      } else {
	// There's none cached, so just get one from the superheap.
	return SuperHeap::get();
      }
    }

    /// Put the superblock into the cache.
    inline void put (SuperblockType * s) {
      if (!s || (s == _current) || (!s->isValidSuperblock())) {
	// Ignore if we already are holding this superblock, of if we
	// got a NULL pointer, or if it's invalid.
	return;
      }
      if (_current) {
	// We have one already -- push it out.
	SuperHeap::put (_current);
      }
      _current = s;
    }

  private:

    /// Obtain a superblock and return an object from it.
    void * slowMallocPath (size_t sz) {
      void * ptr = NULL;
      while (!ptr) {
	// If we don't have a superblock, get one.
	if (!_current) {
	  _current = SuperHeap::get();
	  if (!_current) {
	    // Out of memory.
	    return NULL;
	  }
	}
	// Try to allocate memory from it.
	ptr = _current->malloc (sz);
	if (!ptr) {
	  // No memory left: put the superblock away and get a new one next time.
	  SuperHeap::put (_current);
	  _current = NULL;
	}
      }
      return ptr;
    }

    /// The current superblock.
    SuperblockType * _current;

  };

}

#endif
