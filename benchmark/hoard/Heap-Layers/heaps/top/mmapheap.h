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

#ifndef HL_MMAPHEAP_H
#define HL_MMAPHEAP_H

#if defined(_WIN32)
#include <windows.h>
#else
// UNIX
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <map>
#endif

#include <new>

#include "heaps/buildingblock/freelistheap.h"
#include "heaps/special/zoneheap.h"
#include "heaps/special/bumpalloc.h"
#include "heaps/threads/lockedheap.h"
#include "locks/posixlock.h"
#include "threads/cpuinfo.h"
#include "utility/myhashmap.h"
#include "utility/sassert.h"
#include "wrappers/mmapwrapper.h"
#include "wrappers/stlallocator.h"

#ifndef HL_MMAP_PROTECTION_MASK
#if HL_EXECUTABLE_HEAP
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE | PROT_EXEC)
#else
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#endif
#endif


#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif


/**
 * @class MmapHeap
 * @brief A "source heap" that manages memory via calls to the VM interface.
 * @author Emery Berger
 */

namespace HL {

  class PrivateMmapHeap {
  public:

    /// All memory from here is zeroed.
    enum { ZeroMemory = 1 };

    enum { Alignment = MmapWrapper::Alignment };

#if defined(_WIN32) 

    static inline void * malloc (size_t sz) {
      //    printf ("mmapheap: Size request = %d\n", sz);
#if HL_EXECUTABLE_HEAP
      char * ptr = (char *) VirtualAlloc (NULL, sz, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_EXECUTE_READWRITE);
#else
      char * ptr = (char *) VirtualAlloc (NULL, sz, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
#endif
      return (void *) ptr;
    }
  
    static inline void free (void * ptr, size_t) {
      // No need to keep track of sizes in Windows.
      VirtualFree (ptr, 0, MEM_RELEASE);
    }

    static inline void free (void * ptr) {
      // No need to keep track of sizes in Windows.
      VirtualFree (ptr, 0, MEM_RELEASE);
    }
  
    inline static size_t getSize (void * ptr) {
      MEMORY_BASIC_INFORMATION mbi;
      VirtualQuery (ptr, &mbi, sizeof(mbi));
      return (size_t) mbi.RegionSize;
    }

#else

    static inline void * malloc (size_t sz) {
      // Round up to the size of a page.
      sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);
#if defined(MAP_ALIGN) && defined(MAP_ANON)
      // Request memory aligned to the Alignment value above.
      void * ptr = mmap ((char *) Alignment, sz, HL_MMAP_PROTECTION_MASK, MAP_PRIVATE | MAP_ALIGN | MAP_ANON, -1, 0);
#elif !defined(MAP_ANONYMOUS)
      static int fd = ::open ("/dev/zero", O_RDWR);
      void * ptr = mmap (NULL, sz, HL_MMAP_PROTECTION_MASK, MAP_PRIVATE, fd, 0);
#else
      void * ptr = mmap (NULL, sz, HL_MMAP_PROTECTION_MASK, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
      if (ptr == MAP_FAILED) {
	ptr = NULL;
      }
      return ptr;
    }
    
    static void free (void * ptr, size_t sz)
    {
      if ((long) sz < 0) {
	abort();
      }
      munmap (reinterpret_cast<char *>(ptr), sz);
    }

#endif

  };


  class MmapHeap : public PrivateMmapHeap {
#if !defined(_WIN32)

  private:

    // Note: we never reclaim memory obtained for MyHeap, even when
    // this heap is destroyed.
    class MyHeap : public LockedHeap<PosixLockType, FreelistHeap<BumpAlloc<16384, PrivateMmapHeap> > > {
    };

    typedef MyHashMap<void *, size_t, MyHeap> mapType;

  protected:
    mapType MyMap;

    PosixLockType MyMapLock;

  public:

    enum { Alignment = PrivateMmapHeap::Alignment };

    inline void * malloc (size_t sz) {
      void * ptr = PrivateMmapHeap::malloc (sz);
      MyMapLock.lock();
      MyMap.set (ptr, sz);
      MyMapLock.unlock();
      assert (reinterpret_cast<size_t>(ptr) % Alignment == 0);
      return const_cast<void *>(ptr);
    }

    inline size_t getSize (void * ptr) {
      MyMapLock.lock();
      size_t sz = MyMap.get (ptr);
      MyMapLock.unlock();
      return sz;
    }

#if 0
    // WORKAROUND: apparent gcc bug.
    void free (void * ptr, size_t sz) {
      PrivateMmapHeap::free (ptr, sz);
    }
#endif

    inline void free (void * ptr) {
      assert (reinterpret_cast<size_t>(ptr) % Alignment == 0);
      MyMapLock.lock();
      size_t sz = MyMap.get (ptr);
      PrivateMmapHeap::free (ptr, sz);
      MyMap.erase (ptr);
      MyMapLock.unlock();
    }
#endif
  };

}

#endif
