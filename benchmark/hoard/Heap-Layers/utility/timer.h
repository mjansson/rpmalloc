/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2014 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
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

#ifndef HL_TIMER_H
#define HL_TIMER_H

/**
 * @class Timer
 * @brief A portable class for high-resolution timing.
 *
 * This class simplifies timing measurements across a number of platforms.
 * 
 * @code
 *  Timer t;
 *  t.start();
 *  // do some work
 *  t.stop();
 *  cout << "That took " << (double) t << " seconds." << endl;
 * @endcode
 *
 */

/* Updated to use new C++11 high-resolution timer classes. */

#if (__cplusplus < 201103)
#include "timer-old.h"
#else

#include <chrono>

namespace HL {

  class Timer {
  public:

    Timer() {
      _start = _end;
      _elapsed = 0.0;
    }

    void start() {
      _start = std::chrono::high_resolution_clock::now();
      _end = _start;
    }

    void stop() {
      _end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> elapsed_seconds = _end - _start;
      _elapsed = elapsed_seconds.count();
    }

    operator double() {
      return _elapsed;
    }

  private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _start, _end;
    double _elapsed;
  };

}

#endif // version of C++

#endif
