// -*- C++ -*-

/**
 * @file   macwrapper.cpp
 * @brief  Replaces malloc family on Macs with custom versions.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2010-2012 by Emery Berger, University of Massachusetts Amherst.
 */

#ifndef __APPLE__
#error "This file is for use on Mac OS only."
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc/malloc.h>
#include <errno.h>

#include <unistd.h>

/*
  To use this library,
  you only need to define the following allocation functions:
  
  - xxmalloc
  - xxfree
  - xxmalloc_usable_size
  - xxmalloc_lock
  - xxmalloc_unlock
  
  See the extern "C" block below for function prototypes and more
  details. YOU SHOULD NOT NEED TO MODIFY ANY OF THE CODE HERE TO
  SUPPORT ANY ALLOCATOR.


  LIMITATIONS:

  - This wrapper assumes that the underlying allocator will do "the
    right thing" when xxfree() is called with a pointer internal to an
    allocated object. Header-based allocators, for example, need not
    apply.

  - This wrapper also assumes that there is some way to lock all the
    heaps used by a given allocator; however, such support is only
    required by programs that also call fork(). In case your program
    does not, the lock and unlock calls given below can be no-ops.

*/


#include <assert.h>

extern "C" {

  void * xxmalloc (size_t);
  void   xxfree (void *);

  // Takes a pointer and returns how much space it holds.
  size_t xxmalloc_usable_size (void *);

  // Locks the heap(s), used prior to any invocation of fork().
  void xxmalloc_lock ();

  // Unlocks the heap(s), after fork().
  void xxmalloc_unlock ();

}

#include "macinterpose.h"

//////////
//////////

// All replacement functions get the prefix macwrapper_.

#define MACWRAPPER_PREFIX(n) macwrapper_##n

extern "C" {

  void * MACWRAPPER_PREFIX(malloc) (size_t sz) {
#if defined(__APPLE__)
    // Mac OS ABI requires 16-byte alignment, so we round up the size
    // to the next multiple of 16.
    if (sz < 16) {
      sz = 16;
    }
    if (sz % 16 != 0) {
      sz += 16 - (sz % 16);
    }
#endif
    void * ptr = xxmalloc(sz);
    return ptr;
  }

  size_t MACWRAPPER_PREFIX(malloc_usable_size) (void * ptr) {
    if (ptr == NULL) {
      return 0;
    }
    auto objSize = xxmalloc_usable_size (ptr);
    return objSize;
  }

  void   MACWRAPPER_PREFIX(free) (void * ptr) {
    xxfree (ptr);
  }

  size_t MACWRAPPER_PREFIX(malloc_good_size) (size_t sz) {
    auto * ptr = MACWRAPPER_PREFIX(malloc)(sz);
    auto objSize = MACWRAPPER_PREFIX(malloc_usable_size)(ptr);
    MACWRAPPER_PREFIX(free)(ptr);
    return objSize;
  }

  static void * _extended_realloc (void * ptr, size_t sz, bool isReallocf) 
  {
    // NULL ptr = malloc.
    if (ptr == NULL) {
      return MACWRAPPER_PREFIX(malloc)(sz);
    }

    // 0 size = free. We return a small object.  This behavior is
    // apparently required under Mac OS X and optional under POSIX.
    if (sz == 0) {
      MACWRAPPER_PREFIX(free)(ptr);
      return MACWRAPPER_PREFIX(malloc)(1);
    }

    auto objSize = MACWRAPPER_PREFIX(malloc_usable_size)(ptr);

    // Custom logic here to ensure we only do a logarithmic number of
    // reallocations (with a constant space overhead).

    // Don't change size if the object is shrinking by less than half.
    if ((objSize / 2 < sz) && (sz <= objSize)) {
      // Do nothing.
      return ptr;
    }
#if 0
    // If the object is growing by less than 2X, double it.
    if ((objSize < sz) && (sz < objSize * 2)) {
      sz = objSize * 2;
    }
#endif

    auto * buf = MACWRAPPER_PREFIX(malloc)((size_t) (sz));

    if (buf != NULL) {
      // Successful malloc.
      // Copy the contents of the original object
      // up to the size of the new block.
      auto minSize = (objSize < sz) ? objSize : sz;
      memcpy (buf, ptr, minSize);
      MACWRAPPER_PREFIX(free) (ptr);
    } else {
      if (isReallocf) {
	// Free the old block if the new allocation failed.
	// Specific behavior for Mac OS X reallocf().
	MACWRAPPER_PREFIX(free) (ptr);
      }
    }

    // Return a pointer to the new one.
    return buf;
  }

  void * MACWRAPPER_PREFIX(realloc) (void * ptr, size_t sz) {
    return _extended_realloc (ptr, sz, false);
  }

  void * MACWRAPPER_PREFIX(reallocf) (void * ptr, size_t sz) {
    return _extended_realloc (ptr, sz, true);
  }

  void * MACWRAPPER_PREFIX(calloc) (size_t elsize, size_t nelems) {
    auto n = nelems * elsize;
    if (n == 0) {
      n = 1;
    }
    auto * ptr = MACWRAPPER_PREFIX(malloc) (n);
    if (ptr) {
      memset (ptr, 0, n);
    }
    return ptr;
  }

  char * MACWRAPPER_PREFIX(strdup) (const char * s)
  {
    char * newString = NULL;
    if (s != NULL) {
      auto len = strlen(s) + 1UL;
      if ((newString = (char *) MACWRAPPER_PREFIX(malloc)(len))) {
	memcpy (newString, s, len);
      }
    }
    return newString;
  }

  void * MACWRAPPER_PREFIX(memalign) (size_t alignment, size_t size)
  {
    // Check for non power-of-two alignment, or mistake in size.
    if ((alignment == 0) ||
	(alignment & (alignment - 1)))
      {
	return NULL;
      }
    // Try to just allocate an object of the requested size.
    // If it happens to be aligned properly, just return it.
    auto * ptr = MACWRAPPER_PREFIX(malloc)(size);
    if (((size_t) ptr & (alignment - 1)) == (size_t) ptr) {
      // It is already aligned just fine; return it.
      return ptr;
    }
    // It was not aligned as requested: free the object.
    MACWRAPPER_PREFIX(free)(ptr);
    // Now get a big chunk of memory and align the object within it.
    // NOTE: this assumes that the underlying allocator will be able
    // to free the aligned object, or ignore the free request.
    auto * buf = MACWRAPPER_PREFIX(malloc)(2 * alignment + size);
    auto * alignedPtr = (void *) (((size_t) buf + alignment - 1) & ~(alignment - 1));
    return alignedPtr;
  }

  int MACWRAPPER_PREFIX(posix_memalign)(void **memptr, size_t alignment, size_t size)
  {
    // Check for non power-of-two alignment.
    if ((alignment == 0) ||
	(alignment & (alignment - 1)))
      {
	return EINVAL;
      }
    auto * ptr = MACWRAPPER_PREFIX(memalign) (alignment, size);
    if (!ptr) {
      return ENOMEM;
    } else {
      *memptr = ptr;
      return 0;
    }
  }

  void * MACWRAPPER_PREFIX(valloc) (size_t sz)
  {
    // Equivalent to memalign(pagesize, sz).
    void * ptr = MACWRAPPER_PREFIX(memalign) (PAGE_SIZE, sz);
    return ptr;
  }

}


/////////
/////////

extern "C" {
  // operator new
  void * _Znwm (unsigned long);
  void * _Znam (unsigned long);

  // operator delete
  void _ZdlPv (void *);
  void _ZdaPv (void *);

  // nothrow variants
  // operator new nothrow
  void * _ZnwmRKSt9nothrow_t ();
  void * _ZnamRKSt9nothrow_t ();
  // operator delete nothrow
  void _ZdaPvRKSt9nothrow_t (void *);

  void _malloc_fork_prepare ();
  void _malloc_fork_parent ();
  void _malloc_fork_child ();
}

static malloc_zone_t theDefaultZone;

extern "C" {

  unsigned MACWRAPPER_PREFIX(malloc_zone_batch_malloc)(malloc_zone_t *,
						       size_t sz,
						       void ** results,
						       unsigned num_requested)
  {
    for (unsigned i = 0; i < num_requested; i++) {
      results[i] = MACWRAPPER_PREFIX(malloc)(sz);
      if (results[i] == NULL) {
	return i;
      }
    }
    return num_requested;
  }

  void MACWRAPPER_PREFIX(malloc_zone_batch_free)(malloc_zone_t *,
						 void ** to_be_freed,
						 unsigned num)
  {
    for (unsigned i = 0; i < num; i++) {
      MACWRAPPER_PREFIX(free)(to_be_freed[i]);
    }
  }

  bool MACWRAPPER_PREFIX(malloc_zone_check)(malloc_zone_t *) {
    // Just return true for all zones.
    return true;
  }

  void MACWRAPPER_PREFIX(malloc_zone_print)(malloc_zone_t *, bool) {
    // Do nothing.
  }

  void MACWRAPPER_PREFIX(malloc_zone_log)(malloc_zone_t *, void *) {
    // Do nothing.
  }

  const char * MACWRAPPER_PREFIX(malloc_get_zone_name)(malloc_zone_t *) {
    return theDefaultZone.zone_name;
  }

  void MACWRAPPER_PREFIX(malloc_set_zone_name)(malloc_zone_t *, const char *) {
    // do nothing.
  }

  malloc_zone_t * MACWRAPPER_PREFIX(malloc_create_zone)(vm_size_t,
							unsigned)
  {
    return &theDefaultZone;
  }

  void MACWRAPPER_PREFIX(malloc_destroy_zone) (malloc_zone_t *) {
    // Do nothing.
  }
  
  malloc_zone_t * MACWRAPPER_PREFIX(malloc_zone_from_ptr) (const void *) {
    return NULL;
  }
  
  void * MACWRAPPER_PREFIX(malloc_default_zone) () {
    return (void *) &theDefaultZone;
  }

  malloc_zone_t *
  MACWRAPPER_PREFIX(malloc_default_purgeable_zone)() {
    return &theDefaultZone;
  }

  void MACWRAPPER_PREFIX(malloc_zone_free_definite_size) (malloc_zone_t *, void * ptr, size_t) {
    MACWRAPPER_PREFIX(free)(ptr);
  }

  void MACWRAPPER_PREFIX(malloc_zone_register) (malloc_zone_t *) {
  }

  void MACWRAPPER_PREFIX(malloc_zone_unregister) (malloc_zone_t *) {
  }

  int MACWRAPPER_PREFIX(malloc_jumpstart)(int) {
    return 1;
  }

  void * MACWRAPPER_PREFIX(malloc_zone_malloc) (malloc_zone_t *, size_t size) {
    return MACWRAPPER_PREFIX(malloc) (size);
  }
  
  void * MACWRAPPER_PREFIX(malloc_zone_calloc) (malloc_zone_t *, size_t n, size_t size) {
    return MACWRAPPER_PREFIX(calloc) (n, size);
  }
  
  void * MACWRAPPER_PREFIX(malloc_zone_valloc) (malloc_zone_t *, size_t size) {
    return MACWRAPPER_PREFIX(valloc) (size);
  }
  
  void * MACWRAPPER_PREFIX(malloc_zone_realloc) (malloc_zone_t *, void * ptr, size_t size) {
    return MACWRAPPER_PREFIX(realloc) (ptr, size);
  }
  
  void * MACWRAPPER_PREFIX(malloc_zone_memalign) (malloc_zone_t *, size_t alignment, size_t size) {
    return MACWRAPPER_PREFIX(memalign) (alignment, size);
  }
  
  void MACWRAPPER_PREFIX(malloc_zone_free) (malloc_zone_t *, void * ptr) {
    MACWRAPPER_PREFIX(free)(ptr);
  }

  size_t MACWRAPPER_PREFIX(internal_malloc_zone_size) (malloc_zone_t *, const void * ptr) {
    return MACWRAPPER_PREFIX(malloc_usable_size)((void *) ptr);
  }

  void MACWRAPPER_PREFIX(_malloc_fork_prepare)() {
    /* Prepare the malloc module for a fork by insuring that no thread is in a malloc critical section */
    xxmalloc_lock();
  }

  void MACWRAPPER_PREFIX(_malloc_fork_parent)() {
    /* Called in the parent process after a fork() to resume normal operation. */
    xxmalloc_unlock();
  }

  void MACWRAPPER_PREFIX(_malloc_fork_child)() {
    /* Called in the child process after a fork() to resume normal operation.  In the MTASK case we also have to change memory inheritance so that the child does not share memory with the parent. */
    xxmalloc_unlock();
  }

}

extern "C" void vfree (void *);
extern "C" int malloc_jumpstart (int);

// Now interpose everything.

MAC_INTERPOSE(macwrapper_malloc, malloc);
MAC_INTERPOSE(macwrapper_valloc, valloc);
MAC_INTERPOSE(macwrapper_free, free);

MAC_INTERPOSE(macwrapper_realloc, realloc);
MAC_INTERPOSE(macwrapper_reallocf, reallocf);
MAC_INTERPOSE(macwrapper_calloc, calloc);
MAC_INTERPOSE(macwrapper_malloc_good_size, malloc_good_size);
MAC_INTERPOSE(macwrapper_strdup, strdup);
MAC_INTERPOSE(macwrapper_posix_memalign, posix_memalign);
MAC_INTERPOSE(macwrapper_malloc_default_zone, malloc_default_zone);
MAC_INTERPOSE(macwrapper_malloc_default_purgeable_zone, malloc_default_purgeable_zone);


#if 1
// Zone allocation calls.
MAC_INTERPOSE(macwrapper_malloc_zone_batch_malloc, malloc_zone_batch_malloc);
MAC_INTERPOSE(macwrapper_malloc_zone_batch_free, malloc_zone_batch_free);
MAC_INTERPOSE(macwrapper_malloc_zone_malloc, malloc_zone_malloc);
MAC_INTERPOSE(macwrapper_malloc_zone_calloc, malloc_zone_calloc);
MAC_INTERPOSE(macwrapper_malloc_zone_valloc, malloc_zone_valloc);
MAC_INTERPOSE(macwrapper_malloc_zone_realloc, malloc_zone_realloc);
MAC_INTERPOSE(macwrapper_malloc_zone_memalign, malloc_zone_memalign);
MAC_INTERPOSE(macwrapper_malloc_zone_free, malloc_zone_free);
#endif

#if 1
// Zone access, etc.
MAC_INTERPOSE(macwrapper_malloc_get_zone_name, malloc_get_zone_name);
MAC_INTERPOSE(macwrapper_malloc_create_zone, malloc_create_zone);
MAC_INTERPOSE(macwrapper_malloc_destroy_zone, malloc_destroy_zone);
MAC_INTERPOSE(macwrapper_malloc_zone_check, malloc_zone_check);
MAC_INTERPOSE(macwrapper_malloc_zone_print, malloc_zone_print);
MAC_INTERPOSE(macwrapper_malloc_zone_log, malloc_zone_log);
MAC_INTERPOSE(macwrapper_malloc_set_zone_name, malloc_set_zone_name);
MAC_INTERPOSE(macwrapper_malloc_zone_from_ptr, malloc_zone_from_ptr);
MAC_INTERPOSE(macwrapper_malloc_zone_register, malloc_zone_register);
MAC_INTERPOSE(macwrapper_malloc_zone_unregister, malloc_zone_unregister);
MAC_INTERPOSE(macwrapper_malloc_jumpstart, malloc_jumpstart);
#endif

MAC_INTERPOSE(macwrapper__malloc_fork_prepare, _malloc_fork_prepare);
MAC_INTERPOSE(macwrapper__malloc_fork_parent, _malloc_fork_parent);
MAC_INTERPOSE(macwrapper__malloc_fork_child, _malloc_fork_child);
MAC_INTERPOSE(macwrapper_free, vfree);
MAC_INTERPOSE(macwrapper_malloc_usable_size, malloc_size);
MAC_INTERPOSE(macwrapper_malloc, _Znwm);
MAC_INTERPOSE(macwrapper_malloc, _Znam);

MAC_INTERPOSE(macwrapper_malloc, _ZnwmRKSt9nothrow_t);
MAC_INTERPOSE(macwrapper_malloc, _ZnamRKSt9nothrow_t);

MAC_INTERPOSE(macwrapper_free, _ZdlPv);
MAC_INTERPOSE(macwrapper_free, _ZdaPv);
MAC_INTERPOSE(macwrapper_free, _ZdaPvRKSt9nothrow_t);


/*
  not implemented, from libgmalloc:

__interpose_malloc_freezedry
__interpose_malloc_get_all_zones
__interpose_malloc_printf
__interpose_malloc_zone_print_ptr_info

*/

// A class to initialize exactly one malloc zone with the calls used
// by our replacement.

static const char * theOneTrueZoneName = "DefaultMallocZone";

class initializeDefaultZone {
public:
  initializeDefaultZone() {
    theDefaultZone.size    = MACWRAPPER_PREFIX(internal_malloc_zone_size);
    theDefaultZone.malloc  = MACWRAPPER_PREFIX(malloc_zone_malloc);
    theDefaultZone.calloc  = MACWRAPPER_PREFIX(malloc_zone_calloc);
    theDefaultZone.valloc  = MACWRAPPER_PREFIX(malloc_zone_valloc);
    theDefaultZone.free    = MACWRAPPER_PREFIX(malloc_zone_free);
    theDefaultZone.realloc = MACWRAPPER_PREFIX(malloc_zone_realloc);
    theDefaultZone.destroy = MACWRAPPER_PREFIX(malloc_destroy_zone);
    theDefaultZone.zone_name = theOneTrueZoneName;
    theDefaultZone.batch_malloc = MACWRAPPER_PREFIX(malloc_zone_batch_malloc);
    theDefaultZone.batch_free   = MACWRAPPER_PREFIX(malloc_zone_batch_free);
    theDefaultZone.introspect   = NULL;
    theDefaultZone.version      = 1;
    theDefaultZone.memalign     = MACWRAPPER_PREFIX(malloc_zone_memalign);
    theDefaultZone.free_definite_size = MACWRAPPER_PREFIX(malloc_zone_free_definite_size);
    theDefaultZone.pressure_relief = NULL;
    // Unregister and reregister the default zone.  Unregistering swaps
    // the specified zone with the last one registered which for the
    // default zone makes the more recently registered zone the default
    // zone.  The default zone is then re-registered to ensure that
    // allocations made from it earlier will be handled correctly.
    // Things are not guaranteed to work that way, but it's how they work now.
    malloc_zone_t *default_zone = malloc_default_zone();
    malloc_zone_unregister(default_zone);
    malloc_zone_register (&theDefaultZone);
  }
};

// Force initialization of the default zone.

static initializeDefaultZone initMe;


