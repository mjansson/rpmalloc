// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
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

#ifndef HL_SLLIST_H_
#define HL_SLLIST_H_

#include <assert.h>

/**
 * @class SLList
 * @brief A "memory neutral" singly-linked list.
 * @author Emery Berger
 */

namespace HL {

  class SLList {
  public:
    
    inline SLList (void) {
      clear();
    }

    class Entry;
  
    /// Clear the list.
    inline void clear (void) {
      head.next = NULL;
    }

  /// Is the list empty?
  inline bool isEmpty (void) const {
    return (head.next == NULL);
  }

    /// Get the head of the list.
    inline Entry * get (void) {
      const Entry * e = head.next;
      if (e == NULL) {
	return NULL;
      }
      head.next = e->next;
      return (Entry *) e;
    }

  private:

    /**
     * @brief Remove one item from the list.
     * @warning This method aborts the program if called.
     */
    inline void remove (Entry *) {
      abort();
    }

  public:

    /// Inserts an entry into the head of the list.
    inline void insert (void * ePtr) {
      Entry * e = (Entry *) ePtr;
      e->next = head.next;
      head.next = e;
    }

    /// An entry in the list.
    class Entry {
    public:
      inline Entry (void)
	: next (NULL)
      {}
      //  private:
      //    Entry * prev;
    public:
      Entry * next;
    };

  private:

    /// The head of the list.
    Entry head;

  };

}

#endif
