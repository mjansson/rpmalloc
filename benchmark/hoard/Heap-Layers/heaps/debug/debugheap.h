/* -*- C++ -*- */


#ifndef HL_DEBUGHEAP_H_
#define HL_DEBUGHEAP_H_

#include <assert.h>
#include <stdlib.h>

/**
 *
 *
 */

namespace HL {

  template <class Super,
	    char freeChar = 'F'>
  class DebugHeap : public Super {
  private:

    enum { CANARY = 0xdeadbeef };

  public:

    // Fill with A's.
    inline void * malloc (size_t sz) {
      // Add a guard area at the end.
      void * ptr;
      ptr = Super::malloc (sz + sizeof(size_t));
      if (ptr == NULL) {
	return NULL;
      }
      size_t realSize = Super::getSize (ptr);
      assert (realSize >= sz);
      for (size_t i = 0; i < realSize; i++) {
        ((char *) ptr)[i] = 'A';
      }
      size_t * canaryLocation =
	(size_t *) ((char *) ptr + realSize - sizeof(size_t));
      *canaryLocation = (size_t) CANARY;
      return ptr;
    }

    // Fill with F's.
    inline void free (void * ptr) {
      if (ptr) {
	size_t realSize = Super::getSize(ptr);
	// Check for the canary.
	size_t * canaryLocation =
	  (size_t *) ((char *) ptr + realSize - sizeof(size_t));
	size_t storedCanary = *canaryLocation;
	if (storedCanary != CANARY) {
	  abort();
	}
	for (unsigned int i = 0; i < realSize; i++) {
	  ((char *) ptr)[i] = freeChar;
	}
	Super::free (ptr);
      }
    }
  };

}

#endif
