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

/**
 * @file coalesceableheap.h
 * @brief Coalescing support, via RequireCoalesceable and CoalesceableHeap.
 * @author Emery Berger
 */

#ifndef HL_COALESCEABLEHEAP_H
#define HL_COALESCEABLEHEAP_H

#include <assert.h>

#define MULTIPLE_HEAP_SUPPORT 0

/**
 * @class RequireCoalesceable
 * @brief Provides support for coalescing objects.
 */

namespace HL {

template <class SuperHeap>
class RequireCoalesceable : public SuperHeap {
public:

  // Some thin wrappers over Header methods.
  inline static int getHeap (void * ptr)          { return Header::getHeader(ptr)->getHeap(); }
  inline static void setHeap (void * ptr, int h)  { Header::getHeader(ptr)->setHeap(h); }
  inline static int getPrevHeap (void * ptr)      { return Header::getHeader(ptr)->getPrevHeap(); }
  inline static void setPrevHeap (void * ptr, int h) { Header::getHeader(ptr)->setPrevHeap(h); }

  inline static size_t getSize (const void * ptr)       { return Header::getHeader(ptr)->getSize(); }

  inline static void setSize (void * ptr, const size_t sz) { Header::getHeader(ptr)->setSize(sz); }
  inline static size_t getPrevSize (void * ptr)   { return Header::getHeader(ptr)->getPrevSize(); }
  inline static void setPrevSize (void * ptr, const size_t sz) { Header::getHeader(ptr)->setPrevSize(sz); }
  inline static void markFree (void * ptr)        { Header::getHeader(ptr)->markFree(); }
  inline static void markInUse (void * ptr)       { Header::getHeader(ptr)->markInUse(); }
  inline static void markPrevInUse (void * ptr)   { Header::getHeader(ptr)->markPrevInUse(); }
  inline static void markMmapped (void * ptr)     { Header::getHeader(ptr)->markMmapped(); }
  inline static int isFree (void * ptr)           { return Header::getHeader(ptr)->isFree(); }
  inline static int isPrevFree (void * ptr)       { return Header::getHeader(ptr)->isPrevFree(); }
  inline static int isMmapped (void * ptr)        { return Header::getHeader(ptr)->isMmapped(); }
  inline static void * getNext (const void * ptr)       { return Header::getHeader(ptr)->getNext(); }
  inline static void * getPrev (const void * ptr)       { return Header::getHeader(ptr)->getPrev(); }

  // The Header for every object, allocated or freed.
  class Header {
    friend class RequireCoalesceable<SuperHeap>;
  public:

    //
    // Initialize a new object in a given buffer, with a previous & current object size.
    // Returns the start of the object (i.e., just past the header).
    //
    inline static void * makeObject (void * buf, const size_t prevsz, const size_t sz) {
      *((Header *) buf) = Header (prevsz, sz);
      Header * nextHeader = (Header *) ((char *) ((Header *) buf + 1) + sz);
      // Header * nextHeader = h->getNextHeader();
      // nextHeader->markPrevInUse();
      nextHeader->setPrevSize (sz);
      // return Header::getObject (h);
      return ((Header *) buf + 1);
    }


    inline void sanityCheck (void) {
#ifndef NDEBUG
      int headerSize = sizeof(Header);
      assert (headerSize <= sizeof(double));
      assert (getSize() == getNextHeader()->getPrevSize());
      assert (isFree() == getNextHeader()->isPrevFree());
      assert (getNextHeader()->getPrev() == getObject(this));
#if 0
      if (isPrevFree()) {
        assert (getPrevSize() == getHeader(getPrev())->getSize());
      }
#endif
#endif
    }

    // Get the header for a given object.
    inline static Header * getHeader (const void * ptr) { return ((Header *) ptr - 1); }

    // Get the object for a given header.
    inline static void * getObject (const Header * hd)  { return (void *) (hd + 1); }

    inline void setSize (const size_t sz)    { _size = sz; }
    inline void setPrevSize (const size_t sz){ _prevSize = sz; }

//  private:
    inline size_t getPrevSize (void) const { return _prevSize; }

    inline void markFree (void) {
      // printf ("markFree\n");
      getNextHeader()->markPrevFree();
    }
    inline void markInUse (void)       {
      // printf ("markInUse\n");
      getNextHeader()->markPrevInUse();
    }
    inline void markMmapped (void)     { _isMmapped = IS_MMAPPED; }
    inline void markNotMmapped (void)  { _isMmapped = NOT_MMAPPED; }
    inline int isFree (void) const     {
      // printf ("isFree\n");
      return getNextHeader()->isPrevFree();
    }
    inline int isNextFree (void) const {
      // printf ("isNextFree\n");
      return getNextHeader()->getNextHeader()->isPrevFree();
    }
    inline int isMmapped (void) const  { return (_isMmapped != NOT_MMAPPED); }
    inline void * getPrev (void) const {
      // fprintf (stderr, "coalesceableheap.h: %x, %d\n", this, getPrevSize());
      return ((char *) this) - getPrevSize();
    }
    inline void * getNext (void) const {
      // printf ("getNext\n");
      return ((char *) (this + 2)) + getSize();
    }

    inline void markPrevFree (void)    { _prevStatus = PREV_FREE; }
    inline void markPrevInUse (void)   { _prevStatus = PREV_INUSE; }
    inline int isPrevFree (void) const { return (_prevStatus != PREV_INUSE); }
    inline size_t getSize (void) const { return _size; }

#if MULTIPLE_HEAP_SUPPORT
    inline int getHeap (void) const { return _currHeap; }
    inline void setHeap (int h)     { _currHeap = h; }
    inline int getPrevHeap (void) const { return _prevHeap; }
    inline void setPrevHeap (int h) { _prevHeap = h; }
#else
    inline int getHeap (void) const { return 0; }
    inline void setHeap (int)       {  }
    inline int getPrevHeap (void) const { return 0; }
    inline void setPrevHeap (int)   {  }
#endif


  private:

    explicit inline Header (void) {}
    explicit inline Header (const size_t prevsz, const size_t sz)
      :
      _prevSize (prevsz),
      _size (sz),
      // Assume that objects are NOT mmapped.
      _isMmapped (NOT_MMAPPED)
#if MULTIPLE_HEAP_SUPPORT
      , _prevHeap (0),
      _currHeap (0)
#endif
    {
      assert (sizeof(Header) <= sizeof(double));
    }

    inline Header * getNextHeader (void) const {
      // printf ("H\n");
      return ((Header *) ((char *) (this + 1) + getSize()));
    }

#if !(MULTIPLE_HEAP_SUPPORT) // original

    // Is the previous object free or in use?
    enum { PREV_INUSE = 0, PREV_FREE = 1 };
    unsigned int _prevStatus : 1;

    // Is the current object mmapped?
    enum { NOT_MMAPPED = 0, IS_MMAPPED = 1 };
    unsigned int _isMmapped : 1;

    // The size of the previous object.
    enum { NUM_BITS_STOLEN_FROM_PREVSIZE = 2 };
    size_t _prevSize : sizeof(size_t) * 8 - NUM_BITS_STOLEN_FROM_PREVSIZE;

    // The size of the current object.
    enum { NUM_BITS_STOLEN_FROM_SIZE = 0 };
    size_t _size; // : sizeof(size_t) * 8 - NUM_BITS_STOLEN_FROM_SIZE;


#else // new support for scalability...

    // Support for 2^5 = 32 heaps.
    enum { NUM_BITS_FOR_HEAP = 5 };

    enum { NUM_BITS_STOLEN_FROM_SIZE = NUM_BITS_FOR_HEAP + 1 };     // 1 for isMmapped
    enum { NUM_BITS_STOLEN_FROM_PREVSIZE = NUM_BITS_FOR_HEAP + 1 }; // 1 for isPrevFree

    // Max object size.
    enum { MAX_OBJECT_SIZE = 1 << (sizeof(size_t) * 8 - NUM_BITS_STOLEN_FROM_SIZE) };

    //// word 1 ////

    // The size of the previous object.
    size_t _prevSize : sizeof(size_t) * 8 - NUM_BITS_STOLEN_FROM_PREVSIZE;

    // What's the previous heap?
    unsigned int _prevHeap : NUM_BITS_FOR_HEAP;

    // Is the previous object free or in use?
    enum { PREV_FREE = 0, PREV_INUSE = 1 };
    unsigned int _prevStatus : 1;

    //// word 2 ////

    // The size of the current object.
    size_t _size : sizeof(size_t) * 8 - NUM_BITS_STOLEN_FROM_SIZE;

    // What's the current heap?
    unsigned int _currHeap : NUM_BITS_FOR_HEAP;

    // Is the current object mmapped?
    enum { NOT_MMAPPED = 0, IS_MMAPPED = 1 };
    unsigned int _isMmapped : 1;

#endif
  };

  inline static void * makeObject (void * buf, const size_t prevsz, const size_t sz) {
    return Header::makeObject (buf, prevsz, sz);
  }

  inline static Header * getHeader (const void * ptr) {
    return Header::getHeader (ptr);
  }

};


/**
 * @class CoalesceableHeap
 * @brief Manages coalesceable memory.
 */

template <class SuperHeap>
class CoalesceableHeap : public RequireCoalesceable<SuperHeap> {
public:

  typedef typename RequireCoalesceable<SuperHeap>::Header Header;

  inline CoalesceableHeap (void)
  { }

  inline void * malloc (const size_t sz) {
    void * buf = SuperHeap::malloc (sz + sizeof(Header));
    if (buf) {
      Header * header = (Header *) buf;

      //
      // Record the size of this object in the current header
      // and the next.
      //

      header->setSize (sz);

      // Below was:
      // Header * nextHeader = Header::getHeader (header->getNext());
      Header * nextHeader = (Header *) ((char *) (header + 1) + sz);

      nextHeader->setPrevSize (sz);

#if 0
      //
      // As long as the source of this memory
      // is always zeroed, these assertions will hold.
      // The easiest way to do this is to use a ZeroHeap.
      //
      assert (header->isMmapped() == FALSE);
      assert (nextHeader->getSize() ==  0);
      assert (!nextHeader->isFree());
      assert (!nextHeader->isPrevFree());

#else

      // If the memory is not zeroed, we need this section of code.

      //
      // Assume that everything allocated is NOT mmapped.
      // It is the responsibility of a child layer
      // to mark mmapped objects as such.
      //

      header->markNotMmapped ();

      nextHeader->setSize (0);

      //
      // Mark the subsequent "object" as in use in order to prevent
      // accidental coalescing.
      //

      // (nextHeader + 1)->markPrevInUse();
      nextHeader->markInUse ();
#endif

      // Below was:
      // return Header::getObject (header);
      return (header + 1);
    }
    return NULL;
  }

  inline void free (void * ptr) {
    assert (SuperHeap::isFree(ptr));
    SuperHeap::free ((Header *) ptr - 1);
  }


};

}

#endif // HL_COALESCEABLEHEAP_H

