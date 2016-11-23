// -*- C++ -*-

#ifndef HL_FRED_H
#define HL_FRED_H

/// A thread-wrapper of childlike simplicity :).

#if defined(_WIN32)

#include <windows.h>
#include <process.h>

#elif defined(__SVR4)

#include <thread.h>
#include <pthread.h>
#include <unistd.h>

#else

#include <pthread.h>
#include <unistd.h>

#endif

extern "C" typedef void * (*ThreadFunctionType) (void *);

namespace HL {

  class Fred {
  public:

    Fred() {
#if !defined(_WIN32)
      pthread_attr_init (&attr);
#endif
    }

    ~Fred() {
#if !defined(_WIN32)
      pthread_attr_destroy (&attr);
#endif
    }

    void create (ThreadFunctionType function, void * arg) {
#if defined(_WIN32)
      t = CreateThread (0, 0, (LPTHREAD_START_ROUTINE) *function, (LPVOID) arg, 0, 0);
#else
      pthread_create (&t, &attr, function, arg);
#endif
    }

    void join() {
#if defined(_WIN32)
      WaitForSingleObject (t, INFINITE);
#else
      pthread_join (t, NULL);
#endif
    }

    static void yield() {
#if defined(_WIN32)
      Sleep (0);
#elif defined(__SVR4)
      thr_yield();
#else
      sched_yield();
#endif
    }


    static void setConcurrency (int n) {
#if defined(_WIN32)
#elif defined(__SVR4)
      thr_setconcurrency (n);
#else
      pthread_setconcurrency (n);
#endif
    }


  private:

    Fred (const Fred&);
    Fred& operator=(const Fred&);

#if defined(_WIN32)
    typedef HANDLE FredType;
#else
    typedef pthread_t FredType;
    pthread_attr_t attr;
#endif

    FredType t;
  };

}


#endif
