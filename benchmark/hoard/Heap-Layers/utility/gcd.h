// -*- C++ -*-

#ifndef HL_GCD_H
#define HL_GCD_H

namespace HL {

  template <int a, int b>
  class gcd;
  
  template <int a> 
  class gcd<a, 0>
  {
  public:
    enum { VALUE = a };
    static const int value = a;
  };
  
  template <int a, int b>
  class gcd
  {
  public:
    enum { VALUE = gcd<b, a%b>::VALUE };
    static const int value = gcd<b, a%b>::value;
  };
  
}

#endif
