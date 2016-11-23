// -*- C++ -*-

#ifndef HL_FREESLLIST_H_
#define HL_FREESLLIST_H_

#include <assert.h>

/**
 * @class FreeSLList
 * @brief A "memory neutral" singly-linked list,
 *
 * Uses the free space in objects to store
 * the pointers.
 */


class FreeSLList {
public:

  inline void clear (void) {
    head.next = NULL;
  }

  class Entry;
  
  /// Get the head of the list.
  inline Entry * get (void) {
    const Entry * e = head.next;
    if (e == NULL) {
      return NULL;
    }
    head.next = e->next;
    return const_cast<Entry *>(e);
  }

  inline Entry * remove (void) {
    const Entry * e = head.next;
    if (e == NULL) {
      return NULL;
    }
    head.next = e->next;
    return const_cast<Entry *>(e);
  }
  
  inline void insert (void * e) {
    Entry * entry = reinterpret_cast<Entry *>(e);
    entry->next = head.next;
    head.next = entry;
  }

  class Entry {
  public:
    Entry (void)
      : next (NULL)
    {}
    Entry * next;
  private:
    Entry (const Entry&);
    Entry& operator=(const Entry&);
  };
  
private:
  Entry head;
};


#endif




