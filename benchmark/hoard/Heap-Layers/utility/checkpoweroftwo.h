// -*- C++ -*-

/**
 * @file   checkpoweroftwo.h
 * @brief  Check statically if a number is a power of two.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 *
 **/


#ifndef HL_CHECKPOWEROFTWO_H
#define HL_CHECKPOWEROFTWO_H

#include "sassert.h"

/**
 * @class IsPowerOfTwo
 * @brief Sets value to 1 iff the template argument is a power of two.
 *
 **/

namespace HL {

  template <unsigned long Number>
  class IsPowerOfTwo {
  public:
    enum { VALUE = (!(Number & (Number - 1)) && Number) };
  };

  /**
   * @class CheckPowerOfTwo
   * @brief Template meta-program: fails if number is not a power of two.
   *
   **/
  template <unsigned long V>
  class CheckPowerOfTwo {
    enum { Verify = HL::sassert<IsPowerOfTwo<V>::VALUE>::VALUE };
  };
  
}

#endif
