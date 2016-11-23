// -*- C++ -*-

#ifndef HL_ILOG2_H
#define HL_ILOG2_H

#if defined(_WIN32)
#include <windows.h>
#endif

namespace HL {

  /// Quickly calculate the CEILING of the log (base 2) of the argument.
#if defined(_WIN32)
  static inline unsigned int ilog2 (size_t sz)
  {
    DWORD index;
    _BitScanReverse (&index, sz);
    if (!(sz & (sz-1))) {
      return index;
    } else {
      return index+1;
    }
  }
#elif defined(__GNUC__) && defined(__i386__)
  static inline unsigned int ilog2 (size_t sz)
  {
    sz = (sz << 1) - 1;
    asm ("bsrl %0, %0" : "=r" (sz) : "0" (sz));
    return (unsigned int) sz;
  }
#elif defined(__GNUC__)
  // Just use the intrinsic.
  static inline unsigned int ilog2 (const size_t sz)
  {
    return ((unsigned int) (sizeof(size_t) * 8UL) - (unsigned int) __builtin_clzl((sz << 1) - 1UL) - 1);
  }
#else
  static inline unsigned int ilog2 (size_t v) {
    int log = 0;
    unsigned int value = 1;
    while (value < v) {
      value <<= 1;
      log++;
    }
    return log;
  }
#endif


}

#endif
