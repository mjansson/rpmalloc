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


#ifndef HL_ADAPTHEAP_H
#define HL_ADAPTHEAP_H

#include <assert.h>
#include <stdlib.h>

/**
 * @class AdaptHeap
 * @brief Maintains dictionary entries through freed objects.
 * Sample dictionaries include DLList and SLList.
 */

namespace HL {

  template <class Dictionary, class SuperHeap>
  class AdaptHeap : public SuperHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    /// Allocate an object (remove from the dictionary).
    inline void * malloc (const size_t) {
      void * ptr = (Entry *) dict.get();
      if (ptr) {
        assert (SuperHeap::getSize(ptr) >= sizeof(dict));
      }
      return ptr;
    }

    /// Deallocate the object (return to the dictionary).
    inline void free (void * ptr) {
      if (ptr) {
        assert (SuperHeap::getSize(ptr) >= sizeof(dict));
        Entry * entry = (Entry *) ptr;
        dict.insert (entry);
      }
    }

    /// Remove an object from the dictionary.
    inline int remove (void * ptr) {
      if (ptr) {
        assert (SuperHeap::getSize(ptr) >= sizeof(dict));
        dict.remove ((Entry *) ptr);
      }
      return 1;
    }

    /// Clear the dictionary.
    inline void clear (void) {
      Entry * ptr;
      while ((ptr = (Entry *) dict.get()) != NULL) {
        SuperHeap::free (ptr);
      }
      dict.clear();
      SuperHeap::clear();
    }


  private:

    /// The dictionary object.
    Dictionary dict;

    class Entry : public Dictionary::Entry {};
  };

}

#endif // HL_ADAPTHEAP
