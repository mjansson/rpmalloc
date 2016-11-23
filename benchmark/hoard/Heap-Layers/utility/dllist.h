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

#ifndef HL_DLLIST_H
#define HL_DLLIST_H

#include <assert.h>

/**
 *
 * @class DLList
 * @brief A "memory neutral" doubly-linked list.
 * @author Emery Berger
 */

namespace HL {

class DLList {
public:

  inline DLList (void) {
    clear();
  }

  class Entry;
  
  /// Clear the list.
  inline void clear (void) {
    head.setPrev (&head);
    head.setNext (&head);
  }

  /// Is the list empty?
  inline bool isEmpty (void) const {
    return (head.getNext() == &head);
  }

  /// Get the head of the list.
  inline Entry * get (void) {
    const Entry * e = head.next;
    if (e == &head) {
      return NULL;
    }
    head.next = e->next;
    head.next->prev = &head;
    return (Entry *) e;
  }

  /// Remove one item from the list.
  inline void remove (Entry * e) {
    e->remove();
  }

  /// Insert an entry into the head of the list.
  inline void insert (Entry * e) {
    e->insert (&head, head.next);
  }

  /// An entry in the list.
  class Entry {
  public:
    //  private:
    inline void setPrev (Entry * p) { assert (p != NULL); prev = p; }
    inline void setNext (Entry * p) { assert (p != NULL); next = p; }
    inline Entry * getPrev (void) const { return prev; }
    inline Entry * getNext (void) const { return next; }
    inline void remove (void) const {
      prev->setNext(next);
      next->setPrev(prev);
    }
    inline void insert (Entry * p, Entry * n) {
      prev = p;
      next = n;
      p->setNext (this);
      n->setPrev (this);
    }
    Entry * prev;
    Entry * next;
  };


private:

  /// The head of the list.
  Entry head;

};

}

#endif
