// -*- C++ -*-

#ifndef HL_LCM_H
#define HL_LCM_H

#include "gcd.h"

namespace HL {

  template <int a, int b>
  class lcm;

  template <int a, int b>
  class lcm
  {
  public:
    enum { VALUE = (a * b) / (gcd<a, b>::VALUE) };
    static const int value = (a * b) / (gcd<a, b>::value);
  };

}

#endif
