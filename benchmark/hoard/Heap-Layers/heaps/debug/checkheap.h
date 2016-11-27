// -*- C++ -*-

#ifndef HL_CHECKHEAP_H
#define HL_CHECKHEAP_H

#include <cstring>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @class CheckHeap
 * @brief Performs simple checks on memory allocations.
 *
 **/

namespace HL {

  template <class SuperHeap>
  class CheckHeap : public SuperHeap {
  private:

    enum { RECEIVED_A_NULL_OBJECT_FROM_MALLOC = 0 };
    enum { RECEIVED_AN_UNALIGNED_OBJECT_FROM_MALLOC = 0 };

  public:

    inline void * malloc (size_t sz) {
      void * addr = SuperHeap::malloc (sz);
#if !defined(NDEBUG)

      // Check for null (this should in general not happen).
      if (addr == NULL) {
        assert (RECEIVED_A_NULL_OBJECT_FROM_MALLOC);
        printf ("RECEIVED_A_NULL_OBJECT_FROM_MALLOC\n");
        abort();
      }
      // Ensure object size is correct.
      assert (SuperHeap::getSize(addr) >= sz);

      // Check alignment.
      if ((unsigned long) addr % SuperHeap::Alignment != 0) {
        assert (RECEIVED_AN_UNALIGNED_OBJECT_FROM_MALLOC);
        printf ("RECEIVED_AN_UNALIGNED_OBJECT_FROM_MALLOC\n");
        abort();
      }

      // Wipe out the old contents.
      std::memset (addr, 0, SuperHeap::getSize(addr));
#endif
      return addr;
    }

    inline void free (void * ptr) {
#if !defined(NDEBUG)
      // Wipe out the old contents.
      std::memset (ptr, 0, SuperHeap::getSize(ptr));
#endif
      SuperHeap::free (ptr);
    }

  };

}

#endif
