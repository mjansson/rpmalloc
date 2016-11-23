// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef HOARD_CHECK_H
#define HOARD_CHECK_H

/**
 * @class Check
 * @brief Checks preconditions and postconditions on construction and destruction.
 *
 * Example usage:
 * 
 * in a method of ThisClass:
 * 
 * void foo() {
 *   Check<ThisClass, ThisClassChecker> t (this);
 *   ....
 * }
 *
 * and defined in ThisClass:
 *
 * class ThisClassChecker {
 * public:
 *   static void precondition (ThisClass * obj) { ... }
 *   static void postcondition (ThisClass * obj) { ... }
 *
 **/

namespace Hoard {

  template <class TYPE, class CHECK>
  class Check {
  public:
#ifndef NDEBUG
    Check (TYPE * t)
      : _object (t)
#else
    Check (TYPE *)
#endif
    {
#ifndef NDEBUG
      CHECK::precondition (_object);
#endif
    }
    
    ~Check() {
#ifndef NDEBUG
      CHECK::postcondition (_object);
#endif
    }
    
  private:
    Check (const Check&);
    Check& operator=(const Check&);
    
#ifndef NDEBUG
    TYPE * _object;
#endif
    
  };

}
#endif
