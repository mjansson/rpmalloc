// -*- C++ -*-

/**
 * @file   dynamichashtable.h
 * @brief  A thread-safe dynamic hash table based on linear probing.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2013 by Emery Berger, University of Massachusetts Amherst.
 **/

#ifndef DYNAMICHASHTABLE_H
#define DYNAMICHASHTABLE_H

#include <new>
#include <stdint.h>

#include "heaplayers.h"
#include "checkpoweroftwo.h"

// LOAD_FACTOR_RECIPROCAL is the reciprocal of the maximum load factor
// for the hash table.  In other words, 1/LOAD_FACTOR_RECIPROCAL is
// how full the hash table can get.
//
// INIT_SIZE is the initial number of elements in the hash table.
//
// SourceHeap is the class that manages memory for the hash table's needs.
//
// LockType is the kind of lock used to synchronize access to the hash table.

template <class VALUE_TYPE,
	  size_t LOAD_FACTOR_RECIPROCAL = 2,
	  size_t INIT_SIZE = 4096,
	  class SourceHeap = HL::MallocHeap,
	  class LockType = HL::PosixLockType>

class DynamicHashTable {

  // When we grow the hash table, we multiply its size by this expansion factor.
  // NOTE: This value *must* be a power of two, which we statically verify.
  enum { ExpansionFactor = 2 };

public:

  DynamicHashTable() :
    _size (INIT_SIZE),
    _entries (allocTable (INIT_SIZE)),
    _numElements (0)
  {
    HL::sassert<(LOAD_FACTOR_RECIPROCAL > 1)> verify0;
    CheckPowerOfTwo<ExpansionFactor> verify1;
    CheckPowerOfTwo<INIT_SIZE> verify2;
    verify0 = verify0;
    verify1 = verify1;
    verify2 = verify2;
  }
  
  ~DynamicHashTable() {
    Guard<LockType> l (_lock);
    _sh.free (_entries);
  }

  /// @brief Get the object with the given key from the map.
  bool get (unsigned long k, VALUE_TYPE& value) {
    Guard<LockType> l (_lock);
    return find (k, value);
  }

  /// @brief Insert the given object into the map.
  void insert (const VALUE_TYPE& s) 
  {
    Guard<LockType> l (_lock);

    // If adding this element would push us over our maximum load
    // factor, grow the hash table.
    if ((_numElements+1) > _size / LOAD_FACTOR_RECIPROCAL) {
      grow();
    } 
    insertOne (s);
    _numElements++;
  }


  /// @brief Erase the entry for a given key.
  bool erase (unsigned long key)
  {
    Guard<LockType> l (_lock);
    unsigned long index;
    bool r = findIndex (key, index);
    if (r) {
      _entries[index].erase();
      _numElements--;
      if (_numElements < _size / (2 * ExpansionFactor * LOAD_FACTOR_RECIPROCAL)) {
	  if (_numElements >= 2 * INIT_SIZE) {
	    // Shrink.
	    shrink();
	  }
      }
    }
    return r;
  }

private:

  void insertOne (const VALUE_TYPE& s) 
  {
    // Put the object in a free spot via linear probing.
    // Note that this loop is guaranteed to terminate because the load
    // factor cannot be 1.0 (i.e., there is always an available slot).
    unsigned long i = s.hashCode() & (_size - 1);
    while (true) {
      if (!_entries[i].isValid()) {
	_entries[i].put (s);
	return;
      }
      i = (i+1) & (_size - 1);
    }
  }

  void grow() 
  {
    // Save old values.
    size_t old_size             = _size;
    StoredObject * old_entries  = _entries;
    unsigned long old_elt_count = _numElements;

    // Make room for a new table, growing the current one.
    _size                      = _size * ExpansionFactor;
    _entries                   = allocTable (_size);

#if 0
    {
      char buf[255];
      sprintf (buf, "GROWING, was %d/%d, now %d/%d\n", old_elt_count, old_size, _numElements, _size);
      fprintf (stderr, buf);
    }
#endif

    if (_entries == NULL) {
      // Failed to allocate space for a bigger table.
      // Give up the ghost.
      abort();
    }

    // Rehash all the elements.
    unsigned long ct = 0;
    for (unsigned long i = 0; i < old_size; i++) {
      VALUE_TYPE v;
      bool isValid = old_entries[i].get (v);
      if (isValid) {
        ct++;
	insertOne (v);
      }
    }

    assert (ct == _numElements);
    _sh.free (old_entries);
  }

  void shrink() 
  {
    // Save old values.
    size_t old_size             = _size;
    StoredObject * old_entries  = _entries;
    unsigned long old_elt_count = _numElements;

    // Make room for a new table, growing the current one.
    _size                      = _size / ExpansionFactor;
    _entries                   = allocTable (_size);

#if 0
    {
      char buf[255];
      sprintf (buf, "SHRINKING, was %d/%d, now %d/%d\n", old_elt_count, old_size, _numElements, _size);
      fprintf (stderr, buf);
    }
#endif


    if (_entries == NULL) {
      // Failed to allocate space for a bigger table.
      // Give up the ghost.
      abort();
    }

    // Rehash all the elements.
    unsigned long ct = 0;
    for (unsigned long i = 0; i < old_size; i++) {
      VALUE_TYPE v;
      bool isValid = old_entries[i].get (v);
      if (isValid) {
        ct++;
	insertOne (v);
      }
    }

    assert (ct == _numElements);
    _sh.free (old_entries);
  }

  /// @brief Find the entry for a given key.
  bool find (unsigned long key, VALUE_TYPE& value)
  {
    unsigned long index;
    bool r = findIndex (key, index);
    if (r) {
      _entries[index].get (value);
    }
    return r;
  }

  bool findIndex (unsigned long key, unsigned long& index) {
    unsigned long i = key & (_size - 1);
    while (true) {
      VALUE_TYPE v;
      bool isValid = _entries[i].get (v);
      if (!isValid) {
	return false;
      }
      // Check the value.
      if (v.hashCode() == key) {
	index = i;
	return true;
      }
      i = (i+1) & (_size - 1);
    }
  }

  class StoredObject {
  private:
    typedef enum { EMPTY, DELETED, OCCUPIED } Status;
  public:
    StoredObject()
      : _status (EMPTY)
    {}
    bool isValid() const {
      return (_status == OCCUPIED);
    }
    void erase() {
      _status = DELETED;
    }
    bool get (VALUE_TYPE& v) const {
      if (_status != OCCUPIED) {
	return false;
      }
      v = _value;
      return true;
    }
    void put (const VALUE_TYPE& v) {
      _value = v;
      _status = OCCUPIED;
    }
      
  private:
    /// Current status of the cell.
    Status _status;

    /// The value (if it is a valid object).
    VALUE_TYPE _value;
  };

  /// Return a new table of the appropriate size.
  StoredObject * allocTable (unsigned long nElts)
  {
    void * ptr = 
      _sh.malloc (nElts * sizeof(StoredObject));
    return new (ptr) StoredObject[nElts];
  }

  /// The lock for the heap itself.  Right now, it's just one big
  /// lock, but ultimately it should be refined.
  LockType _lock;

  /// The heap from which we get memory to hold the hash table.
  SourceHeap _sh;

  /// Current size of the hash table. Always a power of two.
  size_t _size;

  /// The array of entries.
  StoredObject * _entries;

  /// The total number of elements actually in the table.
  size_t _numElements;

};

#endif
