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

#ifndef HL_POSIXLOCK_H
#define HL_POSIXLOCK_H

#if !defined(_WIN32)

#include <pthread.h>

/**
 * @class PosixLockType
 * @brief Locking using POSIX mutex objects.
 */

namespace HL {

  class PosixLockType {
  public:

    PosixLockType (void)
    {
      int r = pthread_mutex_init (&mutex, NULL);
      if (r) {
	throw 0;
      }
    }
  
    ~PosixLockType (void)
    {
      pthread_mutex_destroy (&mutex);
    }
  
    void lock (void) {
      pthread_mutex_lock (&mutex);
    }
  
    void unlock (void) {
      pthread_mutex_unlock (&mutex);
    }
  
  private:
    union {
      pthread_mutex_t mutex;
      double _dummy[sizeof(pthread_mutex_t)/sizeof(double) + 1];
    };
  };

}

#endif

#endif
