/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
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

#ifndef HL_RECURSIVELOCK_H
#define HL_RECURSIVELOCK_H

#include <cassert>
#include "threads/cpuinfo.h"

/**
 * @class RecursiveLockType
 * @brief Implements a recursive lock using some base lock representation.
 * @param BaseLock The base lock representation.
 */

namespace HL {

  template <class BaseLock>
  class RecursiveLockType : public BaseLock {
  public:

    inline RecursiveLockType (void)
      : _tid (-1),
	_recursiveDepth (0)
    {}

    inline void lock() {
      auto currthread = CPUInfo::getThreadId();
      if (_tid == currthread) {
	_recursiveDepth++;
      } else {
	BaseLock::lock();
	_tid = currthread;
	_recursiveDepth++;
      }
    }

    inline void unlock (void) {
      auto currthread = (int) CPUInfo::getThreadId();
      if (_tid == currthread) {
	_recursiveDepth--;
	if (_recursiveDepth == 0) {
	  _tid = -1;
	  BaseLock::unlock();
	}
      } else {
	// We tried to unlock it but we didn't lock it!
	// This should never happen.
	assert (0);
	abort();
      }
    }

  private:
    int _tid;	                /// The lock owner's thread id. -1 if unlocked.
    int _recursiveDepth;	/// The recursion depth of the lock.
  };

}




#endif
