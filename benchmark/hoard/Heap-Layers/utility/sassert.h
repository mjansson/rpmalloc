#ifndef HL_SASSERT_H
#define HL_SASSERT_H

/**
 * @class  sassert
 * @brief  Implements compile-time assertion checking.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 *
 * @code
 *   sassert<(1+1 == 2)> CheckOnePlusOneIsTwo;
 * @endcode
 *
 */

namespace HL {

  template <bool assertion>
    class sassert;

  template<> class sassert<true> {
  public:
    enum { VALUE = true };
  };

}

#endif
