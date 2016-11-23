// -*- C++ -*-

#ifndef HL_MODULO_H
#define HL_MODULO_H

/// A templated class that provides faster modulo functions when the
/// argument is a power of two.

#include <stdlib.h>
#include "checkpoweroftwo.h"

namespace HL {

  template <size_t Modulus>
  class Modulo;

  template <size_t Modulus>
  class Modulo {
  public:
    template <class TYPE>
    static TYPE mod (TYPE m) {
      if (IsPowerOfTwo<Modulus>::VALUE) {
	return m & (Modulus - 1);
      } else {
	return m % Modulus;
      }
    }
  };

}

#endif
