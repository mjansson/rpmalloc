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

#ifndef HL_NESTEDHEAP_H_
#define HL_NESTEDHEAP_H_

#include <assert.h>

/**
 * @class NestedHeap
 * @brief Hierarchical heaps.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

namespace HL {

template <class SuperHeap>
class NestedHeap : public SuperHeap {
public:

  NestedHeap()
    : parent (NULL),
      child (NULL),
      prev (NULL),
      next (NULL)
  {
  }

  ~NestedHeap()
  {
    clear();
    if (parent != NULL) {
      parent->removeChild (this);
    }
    removeSibling (this);
  }

  inline void clear() {

    // Clear this heap.
    SuperHeap::clear();

#if 0
    //
    // Iterate through all children and delete them.
    //

    if (child != NULL) {
      NestedHeap<SuperHeap> * nextChild = child->next;
      while (child != NULL) {
        NestedHeap<SuperHeap> * prevChild = child->prev;
        delete child;
        child = prevChild;
      }
      child = nextChild;
      while (child != NULL) {
        nextChild = child->next;
        delete child;
        child = nextChild;
      }
    }
    assert (child == NULL);

#else // clear all the children.

    NestedHeap<SuperHeap> * ch = child;
    while (ch != NULL) {
      NestedHeap<SuperHeap> * nextChild = ch->next;
      ch->clear();
      ch = nextChild;
    }
#endif

  }

  void addChild (NestedHeap<SuperHeap> * ch)
  {
    if (child == NULL) {
      child = ch;
      child->prev = NULL;
      child->next = NULL;
    } else {
      assert (child->prev == NULL);
      assert (ch->next == NULL);
      ch->prev = NULL;
      ch->next = child;
      child->prev = ch;
      child = ch;
    }
    child->parent = this;
  }

private:

  void removeChild (NestedHeap<SuperHeap> * ch)
  {
    assert (ch != NULL);
    if (child == ch) {
      if (ch->prev) {
        child = ch->prev;
      } else if (ch->next) {
        child = ch->next;
      } else {
        child = NULL;
      }
    }
    removeSibling (ch);
  }

  inline static void removeSibling (NestedHeap<SuperHeap> * sib)
  {
    if (sib->prev) {
      sib->prev->next = sib->next;
    }
    if (sib->next) {
      sib->next->prev = sib->prev;
    }
  }

  NestedHeap<SuperHeap> * parent;
  NestedHeap<SuperHeap> * child;
  NestedHeap<SuperHeap> * prev;
  NestedHeap<SuperHeap> * next;

};

}

#endif
