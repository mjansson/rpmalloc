#ifndef HL_EXACTLYONE_H
#define HL_EXACTLYONE_H

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.org
 
  Copyright (c) 1998-2015 Emery Berger
  
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

/**
 * @class ExactlyOne
 * @brief Creates a singleton of type CLASS, accessed through ().
 * @author Emery Berger <http://www.emeryberger.org>
 */

#include <new>

namespace HL {

  template <class CLASS>
    class ExactlyOne {
  public:

    inline CLASS& operator()() {
      // We store the singleton in a double buffer to force alignment.
      static double buf[(sizeof(CLASS) + sizeof(double) - 1) / sizeof(double)];
      static CLASS * theOneTrueInstancePtr = new (buf) CLASS;
      return *theOneTrueInstancePtr;
    }

  };

}

#endif

