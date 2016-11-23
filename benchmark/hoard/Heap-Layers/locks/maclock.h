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

#ifndef HL_MACLOCK_H
#define HL_MACLOCK_H

#if defined(__APPLE__)

#include <libkern/OSAtomic.h>

/**
 * @class MacLockType
 * @brief Locking using OS X spin locks.
 */

namespace HL {

  class MacLockType {
  public:

    MacLockType()
      : mutex (0)
    {}

    ~MacLockType()
    {
      mutex = 0;
    }

    inline void lock() {
      OSSpinLockLock (&mutex);
    }

    inline void unlock() {
      OSSpinLockUnlock (&mutex);
    }

  private:

    OSSpinLock mutex;

  };

}

#endif

#endif
