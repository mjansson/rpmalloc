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

#ifndef HL_WINLOCK_H
#define HL_WINLOCK_H

#if defined(_WIN32)

#include <windows.h>
#include <winnt.h>

/**
 * @class WinLockType
 * @brief Locking using Win32 mutexes.
 *
 * Note that this lock type detects whether we are running on a
 * multiprocessor.  If not, then we do not use atomic operations.
 */

namespace HL {

  class WinLockType {
  public:

    WinLockType (void)
      : mutex (0)
    {}

    ~WinLockType (void)
    {
      mutex = 0;
    }

    inline void lock (void) {
      int spinCount = 0;
      while (InterlockedExchange ((long *) &mutex, 1) != 0) {
	while (mutex != 0) {
	  YieldProcessor();
	}
      }
    }

    inline void unlock (void) {
      mutex = 0;
      // InterlockedExchange (&mutex, 0);
    }

  private:
    unsigned int mutex;
    bool onMultiprocessor (void) {
      SYSTEM_INFO infoReturn[1];
      GetSystemInfo (infoReturn);
      if (infoReturn->dwNumberOfProcessors == 1) {
	return FALSE;
      } else {
	return TRUE;
      }
    }
  };

}

#endif

#endif
