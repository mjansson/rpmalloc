/* -*- C++ -*- */

#ifndef HL_LOGHEAP_H_
#define HL_LOGHEAP_H_

/**
 * @file logheap.h
 * @brief Contains the implementations of Log and LogHeap.
 */

#include <assert.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>

// UNIX-specific for now...
#if defined(unix)
#include <unistd.h>
#include <sys/types.h>
#endif

#include <fstream>
#include <ios>


using namespace std;

namespace HL {

  template <class Obj,
	    int MAX_ENTRIES = 300000>
  class Log {
  public:

    Log() :
      numEntries (0)
    {
#if defined(unix)
      sprintf (filename, "theLog-%d", getpid());
#else
      sprintf (filename, "theLog");
#endif
    }

    void writeLog() {
      {
        ofstream outfile (filename, ios_base::app);
      }
      write(filename);
    }

    void dump() {
      write (filename);
      numEntries = 0;
    }

    int append (const Obj& o) {
      if (numEntries < MAX_ENTRIES) {
        entries[numEntries] = o;
        numEntries++;
        return 1;
      } else {
        return 0;
      }
    }

    void write (char * fname) {
      ofstream outfile (fname, ios_base::app);
      for (int i = 0; i < numEntries; i++) {
        outfile << entries[i] << endl;
      }
    }

  private:

    int numEntries;
    Obj entries[MAX_ENTRIES];
    char filename[255];
  };


  template <class SuperHeap>
  class LogHeap : public SuperHeap {
  public:

    LogHeap()
      : allDone (false)
    {}

    inline void * malloc (size_t sz) {
      void * ptr = SuperHeap::malloc (sz);
      if (!allDone) {
        MemoryRequest m;
        m.malloc (ptr, sz);
        if (!log.append (m)) {
          allDone = true;
          log.dump();
          log.append(m);
          allDone = false;
        }
      }
      return ptr;
    }

    void write() {
      allDone = true;
      log.writeLog();
    }

    inline void free (void * ptr) {
      if (!allDone) {
        MemoryRequest m;
        m.free (ptr);
        log.append (m);
      }
      SuperHeap::free (ptr);
    }

  private:

    class MemoryRequest {
    public:

      MemoryRequest()
        :
#if 0
        _sec (LONG_MAX),
        _usec (LONG_MAX),
#endif
        _size (0),
        _address (INVALID)
      {}

      enum { FREE_OP = 0,
	     MALLOC_OP,
	     REALLOC_OP,
	     REFREE_OP,
	     ALLOCATE_OP,
	     DEALLOCATE_OP,
	     INVALID
      };

      friend std::ostream& operator<< (std::ostream& os, MemoryRequest& m) {
        switch (m.getType()) {
        case FREE_OP:
          os << "F\t" << (void *) m.getAddress();
          break;
        case MALLOC_OP:
          os << "M\t" << m.getSize() << "\t" << (void *) m.getAddress();
          break;
        default:
          abort();
        }

        return os;
      }

      void malloc (void * addr,
		   size_t sz)
      {
        assert ((((unsigned long) addr) & 7) == 0);
        _size = sz;
        _address = (unsigned long) addr | MALLOC_OP;
        //      markTime (_sec, _usec);
        // printf ("malloc %d (%f)\n", sz, getTime());
      }


      void free (void * addr)
      {
        assert ((((unsigned long) addr) & 7) == 0);
        _address = (unsigned long) addr | FREE_OP;
        //      markTime (_sec, _usec);
        // printf ("free %d (%f)\n", _address, getTime());
      }


      void allocate (int sz)
      {
        _address = ALLOCATE_OP;
        _size = sz;
        markTime (_sec, _usec);
        // printf ("allocate %d (%f)\n", sz, getTime());
      }


      void deallocate (int sz)
      {
        _address = DEALLOCATE_OP;
        _size = sz;
        markTime (_sec, _usec);
        // printf ("allocate %d (%f)\n", sz, getTime());
      }


      // Set sec & usec to the current time.
      void markTime (long& /* sec */, long& /* usec */)
      {
#if 0
#ifdef __SVR4 // Solaris
        hrtime_t t;
        t = gethrtime();
        sec = *((long *) &t);
        usec = *((long *) &t + 1);
#else
        struct timeval tv;
        struct timezone tz;
        gettimeofday (&tv, &tz);
        sec = tv.tv_sec;
        usec = tv.tv_usec;
#endif
#endif
      }

      int getType() {
        return _address & 7;
      }

      int getAllocated() {
        return _size;
      }

      int getDeallocated() {
        return _size;
      }

      unsigned long getAddress() {
        return _address & ~7;
      }

      int getSize() {
        return _size;
      }

#if 0
      double getTime() {
        return (double) _sec + (double) _usec / 1000000.0;
      }

      long getSeconds() {
        return _sec;
      }

      long getUseconds() {
        return _usec;
      }

      friend int operator< (MemoryRequest& m, MemoryRequest& n) {
        return ((m._sec < n._sec)
          || ((m._sec == n._sec)
          && (m._usec < n._usec)));
      }

      friend int operator== (MemoryRequest& m, MemoryRequest& n) {
        return ((m._sec == n._sec) && (m._usec == n._usec));
      }
#endif

    private:
      int	_size;     	// in bytes
      unsigned long _address;  // The address returned by malloc/realloc
#if 1
      long	_sec;      	// seconds as returned by gettimeofday
      long	_usec;     	// microseconds as returned by gettimeofday
#endif
    };

    Log<MemoryRequest> log;

    bool allDone;


  };

}

#endif // HL_LOGHEAP_H_
