// -*- C++ -*-

#ifndef HL_ALIGN_H
#define HL_ALIGN_H

#include <stdlib.h>

#include "sassert.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace HL {

  /// @name  align
  /// @brief Rounds up a value to the next multiple of v.
  /// @note  Argument must be a power of two.
  template <size_t Alignment>
  inline size_t align (size_t v) {
    sassert<((Alignment & (Alignment-1)) == 0)> isPowerOfTwo;
    isPowerOfTwo = isPowerOfTwo;
    return ((v + (Alignment-1)) & ~(Alignment-1));
  }

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
