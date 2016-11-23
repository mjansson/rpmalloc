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

#ifndef HOARD_HOARDSUPERBLOCK_H
#define HOARD_HOARDSUPERBLOCK_H

#include <cassert>
#include <cstdlib>

#include "heaplayers.h"
//#include "freesllist.h"

#include "hoardsuperblockheader.h"

namespace Hoard {

  template <class LockType,
	    int SuperblockSize,
	    class HeapType>
  class HoardSuperblock {
  public:

    HoardSuperblock (size_t sz)
      : _header (sz, BufferSize)
    {
      assert (_header.isValid());
      assert (this == (HoardSuperblock *)
	      (((size_t) this) & ~((size_t) SuperblockSize-1)));
    }
    
    /// @brief Find the start of the superblock by bitmasking.
    /// @note  All superblocks <em>must</em> be naturally aligned, and powers of two.
    static inline HoardSuperblock * getSuperblock (void * ptr) {
      return (HoardSuperblock *)
	(((size_t) ptr) & ~((size_t) SuperblockSize-1));
    }

    INLINE size_t getSize (void * ptr) const {
      if (_header.isValid() && inRange (ptr)) {
	return _header.getSize (ptr);
      } else {
	return 0;
      }
    }


    INLINE size_t getObjectSize() const {
      if (_header.isValid()) {
	return _header.getObjectSize();
      } else {
	return 0;
      }
    }

    MALLOC_FUNCTION INLINE void * malloc (size_t) {
      assert (_header.isValid());
      auto * ptr = _header.malloc();
      if (ptr) {
	assert (inRange (ptr));
	assert ((size_t) ptr % HeapType::Alignment == 0);
      }
      return ptr;
    }

    INLINE void free (void * ptr) {
      if (_header.isValid() && inRange (ptr)) {
	// Pointer is in range.
	_header.free (ptr);
      } else {
	// Invalid free.
      }
    }
    
    void clear() {
      if (_header.isValid())
	_header.clear();
    }
    
    // ----- below here are non-conventional heap methods ----- //
    
    INLINE bool isValidSuperblock() const {
      auto b = _header.isValid();
      return b;
    }
    
    INLINE unsigned int getTotalObjects() const {
      assert (_header.isValid());
      return _header.getTotalObjects();
    }
    
    /// Return the number of free objects in this superblock.
    INLINE unsigned int getObjectsFree() const {
      assert (_header.isValid());
      assert (_header.getObjectsFree() >= 0);
      assert (_header.getObjectsFree() <= _header.getTotalObjects());
      return _header.getObjectsFree();
    }
    
    inline void lock() {
      assert (_header.isValid());
      _header.lock();
    }
    
    inline void unlock() {
      assert (_header.isValid());
      _header.unlock();
    }
    
    inline HeapType * getOwner() const {
      assert (_header.isValid());
      return _header.getOwner();
    }

    inline void setOwner (HeapType * o) {
      assert (_header.isValid());
      assert (o != NULL);
      _header.setOwner (o);
    }
    
    inline HoardSuperblock * getNext() const {
      assert (_header.isValid());
      return _header.getNext();
    }

    inline HoardSuperblock * getPrev() const {
      assert (_header.isValid());
      return _header.getPrev();
    }
    
    inline void setNext (HoardSuperblock * f) {
      assert (_header.isValid());
      assert (f != this);
      _header.setNext (f);
    }
    
    inline void setPrev (HoardSuperblock * f) {
      assert (_header.isValid());
      assert (f != this);
      _header.setPrev (f);
    }
    
    INLINE bool inRange (void * ptr) const {
      // Returns true iff the pointer is valid.
      auto ptrValue = (size_t) ptr;
      return ((ptrValue >= (size_t) _buf) &&
	      (ptrValue <= (size_t) &_buf[BufferSize]));
    }
    
    INLINE void * normalize (void * ptr) const {
      auto * ptr2 = _header.normalize (ptr);
      assert (inRange (ptr));
      assert (inRange (ptr2));
      return ptr2;
    }

    typedef Hoard::HoardSuperblockHeader<LockType, SuperblockSize, HeapType> Header;

  private:
    
    
    // Disable copying and assignment.
    
    HoardSuperblock (const HoardSuperblock&);
    HoardSuperblock& operator=(const HoardSuperblock&);
    
    enum { BufferSize = SuperblockSize - sizeof(Header) };
    
    /// The metadata.
    Header _header;

    
    /// The actual buffer. MUST immediately follow the header!
    char _buf[BufferSize];
  };

}


#endif
