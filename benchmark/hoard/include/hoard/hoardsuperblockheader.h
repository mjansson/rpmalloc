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

#ifndef HOARD_HOARDSUPERBLOCKHEADER_H
#define HOARD_HOARDSUPERBLOCKHEADER_H

#include <stdio.h>


#if defined(_WIN32)
#pragma warning( push )
#pragma warning( disable: 4355 ) // this used in base member initializer list
#endif

#include "heaplayers.h"

#include <cstdlib>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblock;

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeaderHelper {
  public:

    enum { Alignment = 16 };

  public:

    HoardSuperblockHeaderHelper (size_t sz, size_t bufferSize, char * start)
      : _magicNumber (MAGIC_NUMBER ^ (size_t) this),
	_objectSize (sz),
	_objectSizeIsPowerOfTwo (!(sz & (sz - 1)) && sz),
	_totalObjects ((unsigned int) (bufferSize / sz)),
	_owner (NULL),
	_prev (NULL),
	_next (NULL),
	_reapableObjects (_totalObjects),
	_objectsFree (_totalObjects),
	_start (start),
	_position (start)
    {
      assert ((HL::align<Alignment>((size_t) start) == (size_t) start));
      assert (_objectSize >= Alignment);
      assert ((_totalObjects == 1) || (_objectSize % Alignment == 0));
    }

    virtual ~HoardSuperblockHeaderHelper() {
      clear();
    }

    inline void * malloc() {
      assert (isValid());
      void * ptr = reapAlloc();
      assert ((ptr == NULL) || ((size_t) ptr % Alignment == 0));
      if (!ptr) {
	ptr = freeListAlloc();
	assert ((ptr == NULL) || ((size_t) ptr % Alignment == 0));
      }
      if (ptr != NULL) {
	assert (getSize(ptr) >= _objectSize);
	assert ((size_t) ptr % Alignment == 0);
      }
      return ptr;
    }

    inline void free (void * ptr) {
      assert ((size_t) ptr % Alignment == 0);
      assert (isValid());
      _freeList.insert (reinterpret_cast<FreeSLList::Entry *>(ptr));
      _objectsFree++;
      if (_objectsFree == _totalObjects) {
	clear();
      }
    }

    void clear() {
      assert (isValid());
      // Clear out the freelist.
      _freeList.clear();
      // All the objects are now free.
      _objectsFree = _totalObjects;
      _reapableObjects = _totalObjects;
      _position = (char *) (HL::align<Alignment>((size_t) _start));
    }

    /// @brief Returns the actual start of the object.
    INLINE void * normalize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      void * p;

      // Optimization note: the modulo operation (%) is *really* slow on
      // some architectures (notably x86-64). To reduce its overhead, we
      // optimize for the case when the size request is a power of two,
      // which is often enough to make a difference.

      if (_objectSizeIsPowerOfTwo) {
	p = (void *) ((size_t) ptr - (offset & (_objectSize - 1)));
      } else {
	p = (void *) ((size_t) ptr - (offset % _objectSize));
      }
      return p;
    }


    size_t getSize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      size_t newSize;
      if (_objectSizeIsPowerOfTwo) {
	newSize = _objectSize - (offset & (_objectSize - 1));
      } else {
	newSize = _objectSize - (offset % _objectSize);
      }
      return newSize;
    }

    size_t getObjectSize() const {
      return _objectSize;
    }

    unsigned int getTotalObjects() const {
      return _totalObjects;
    }

    unsigned int getObjectsFree() const {
      return _objectsFree;
    }

    HeapType * getOwner() const {
      return _owner;
    }

    void setOwner (HeapType * o) {
      _owner = o;
    }

    bool isValid() const {
      return (_magicNumber == (MAGIC_NUMBER ^ (size_t) this));
    }

    HoardSuperblock<LockType, SuperblockSize, HeapType> * getNext() const {
      return _next;
    }

    HoardSuperblock<LockType, SuperblockSize, HeapType> * getPrev() const {
      return _prev;
    }

    void setNext (HoardSuperblock<LockType, SuperblockSize, HeapType> * n) {
      _next = n;
    }

    void setPrev (HoardSuperblock<LockType, SuperblockSize, HeapType> * p) {
      _prev = p;
    }

    void lock() {
      _theLock.lock();
    }

    void unlock() {
      _theLock.unlock();
    }

  private:

    MALLOC_FUNCTION INLINE void * reapAlloc() {
      assert (isValid());
      assert (_position);
      // Reap mode.
      if (_reapableObjects > 0) {
	auto * ptr = _position;
	_position = ptr + _objectSize;
	_reapableObjects--;
	_objectsFree--;
	assert ((size_t) ptr % Alignment == 0);
	return ptr;
      } else {
	return NULL;
      }
    }

    MALLOC_FUNCTION INLINE void * freeListAlloc() {
      assert (isValid());
      // Freelist mode.
      auto * ptr = reinterpret_cast<char *>(_freeList.get());
      if (ptr) {
	assert (_objectsFree >= 1);
	_objectsFree--;
      }
      return ptr;
    }

    enum { MAGIC_NUMBER = 0xcafed00d };

    /// A magic number used to verify validity of this header.
    const size_t _magicNumber;

    /// The object size.
    const size_t _objectSize;

    /// True iff size is a power of two.
    const bool _objectSizeIsPowerOfTwo;

    /// Total objects in the superblock.
    const unsigned int _totalObjects;

    /// The lock.
    LockType _theLock;

    /// The owner of this superblock.
    HeapType * _owner;

    /// The preceding superblock in a linked list.
    HoardSuperblock<LockType, SuperblockSize, HeapType> * _prev;

    /// The succeeding superblock in a linked list.
    HoardSuperblock<LockType, SuperblockSize, HeapType> * _next;
    
    /// The number of objects available to be 'reap'ed.
    unsigned int _reapableObjects;

    /// The number of objects available for (re)use.
    unsigned int _objectsFree;

    /// The start of reap allocation.
    const char * _start;

    /// The cursor into the buffer following the header.
    char * _position;

    /// The list of freed objects.
    FreeSLList _freeList;
  };

  // A helper class that pads the header to the desired alignment.

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeader : public HoardSuperblockHeaderHelper<LockType, SuperblockSize, HeapType> {
  public:

    HoardSuperblockHeader (size_t sz, size_t bufferSize)
      : HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> (sz, bufferSize, (char *) (this + 1))
    {
      sassert<((sizeof(HoardSuperblockHeader) % Parent::Alignment) == 0)> verifySize;
      verifySize = verifySize;
    }

  private:

    typedef HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> Parent;

    char _dummy[Parent::Alignment - (sizeof(Parent) % Parent::Alignment)];
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#pragma warning( pop )
#endif

#endif
