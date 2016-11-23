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

#ifndef HOARD_STATISTICS_H
#define HOARD_STATISTICS_H

namespace Hoard {

  class Statistics {
  public:
    Statistics (void)
      : _inUse (0),
	_allocated (0)
    {}
    
    inline unsigned int getInUse() const 	{ return _inUse; }
    inline unsigned int getAllocated() const    { return _allocated; }
    inline void setInUse (unsigned int u) 	{ _inUse = u; }
    inline void setAllocated (unsigned int a) 	{ _allocated = a; }
  
  private:
  
    /// The number of objects in use.
    unsigned int _inUse;
  
    /// The number of objects allocated.
    unsigned int _allocated;
  };

}

#endif
