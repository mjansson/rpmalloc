// -*- C++ -*-

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

#ifndef HL_LOCALMALLOCHEAP_H
#define HL_LOCALMALLOCHEAP_H

#include <cstddef>
#include <cassert>

#if !defined(_WIN32) // not implemented for Windows

#include <dlfcn.h>

#include "wrappers/mallocinfo.h"

#if defined(__SVR4)
extern "C" size_t malloc_usable_size (void *);
#else
extern "C" size_t malloc_usable_size (void *) throw ();
#endif

extern "C" {
  
  typedef void * mallocFunction (size_t);
  typedef void freeFunction (void *);
  typedef size_t msizeFunction (void *);

}


namespace HL {

  class LocalMallocHeap {
  public:

    enum { Alignment = HL::MallocInfo::Alignment };

    LocalMallocHeap (void)
      : _freefn (NULL),
      	_msizefn (NULL),
      	_mallocfn (NULL),
      	_initialized (false),
        _initializing (false)
    {}

    inline void * malloc (size_t sz) {
      if (_initializing) {
      	return NULL;
      }
      activate();
      return (*_mallocfn)(sz);
    }

    inline void free (void * ptr) {
      if (_initializing) {
      	return;
      }
      activate();
      (*_freefn)(ptr);
    }

    inline size_t getSize (void * ptr) {
      if (_initializing) {
      	return 0;
      }
      activate();
      return (*_msizefn)(ptr);
    }

  private:

    void activate() {
      if (_initialized) {
      	return;
      }
      activateSlowPath();
    }

    void activateSlowPath() {
      _initializing = true;

      if (!_initialized) {

      	// We haven't initialized anything yet.
      	// Initialize all of the malloc shim functions.
      	
      	_freefn = (freeFunction *)
      	  ((unsigned long) dlsym (RTLD_NEXT, "free"));
      	_msizefn = (msizeFunction *)
      	  ((unsigned long) dlsym (RTLD_NEXT, "malloc_usable_size"));
      	_mallocfn = (mallocFunction *)
      	  ((unsigned long) dlsym (RTLD_NEXT, "malloc"));
      	
      	if (!(_freefn && _msizefn && _mallocfn)) {
      	  fprintf (stderr, "Serious problem!\n");
      	  abort();
      	}
	
      	assert (_freefn);
      	assert (_msizefn);
      	assert (_mallocfn);

      	_initialized = true;
      }
      _initializing = false;
    }

    // Shim functions below.

    freeFunction *   _freefn;
    msizeFunction *  _msizefn;
    mallocFunction * _mallocfn;

    // Has everything been initialized yet?
    bool _initialized;

    // Are we in the process of initializing?
    bool _initializing;

  };

}

#endif
#endif
