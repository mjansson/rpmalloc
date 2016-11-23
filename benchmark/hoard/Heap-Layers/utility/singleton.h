// -*- C++ -*-

#ifndef HL_SINGLETON_H
#define HL_SINGLETON_H

#include <new>

namespace HL {

  template <class C>
  class singleton {
  public:
    
    static inline C& getInstance() {
      static char buf[sizeof(C)];
      static C * theSingleton = new (buf) C;
      return *theSingleton;
    }

  };

}

#endif

