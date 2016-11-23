// -*- C++ -*-

#ifndef HL_HASH_H
#define HL_HASH_H

#include <cstdlib>
#include <stdlib.h>

namespace HL {

  template <class Key>
  class Hash {
  public:
    static size_t hash (Key k);
  };
  
  template <>
  class Hash<size_t> {
  public:
    static inline size_t hash (void * v) {
      return (size_t) v;
    }
  };
  
  template <>
  class Hash<void *> {
  public:
    static inline size_t hash (void * v) {
      return (size_t) ((size_t) v);
    }
  };
  
  template <>
  class Hash<int> {
  public:
    static inline size_t hash (int v) {
      return (size_t) v;
    }
  };

}

#endif
