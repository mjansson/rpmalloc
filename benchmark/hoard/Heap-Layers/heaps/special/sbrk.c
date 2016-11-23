#include <assert.h>
#include <windows.h>
#include <process.h>

/* Sbrk implementation for Win32 by Emery Berger, http://www.cs.utexas.edu/users/emery */

void * sbrk (long size) {

/* Reserve up to 1 GB of RAM */
/*
    We pre-reserve a very large chunk of memory
    and commit pages as we go. */

#define PRE_RESERVE 1024 * 1024 * 1024

  static long remainingReserved = PRE_RESERVE;

  static int initialized = 0;
  static char * currentPosition = NULL;
  static char * nextPage = NULL;
  static long remainingCommitted = 0;
  static long pageSize;
  void * p;

  if (!initialized) {

    /*
      Do one-time initialization stuff:
      get the page size from the system,
      reserve a large range of memory,
      and initialize the appropriate variables.

    */

    SYSTEM_INFO sSysInfo;
    LPVOID base;
    GetSystemInfo(&sSysInfo);

    pageSize = sSysInfo.dwPageSize;

    /* Reserve pages in the process's virtual address space. */

#if 1
    base = VirtualAlloc(NULL, remainingReserved, MEM_RESERVE, PAGE_NOACCESS);
#else
    base = VirtualAlloc(NULL, remainingReserved, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    remainingCommitted =  PRE_RESERVE;
#endif

    if (base == NULL )
      exit (1); /* VirtualAlloc reserve failed */

    currentPosition = nextPage = (char *) base;
    initialized = 1;
  }

  if (size < 0) {

#if 0
  /* Uncommit pages if possible.
     Round the size down to a multiple of the page size,
     and decommit those pages.
   */


    int bytesToUncommit = (-size & ~(pageSize - 1));

    if (bytesToUncommit > PRE_RESERVE - remainingReserved) {
      /* Error -- the user has tried to free memory that we never
         even reserved. */
      return currentPosition;
    }

    if (bytesToUncommit > 0) {

      int result = VirtualFree (nextPage - bytesToUncommit, bytesToUncommit, MEM_DECOMMIT);
      if (result == 0) {
        /* Error -- don't change a thing. */
        return currentPosition;
      }
      remainingCommitted -= bytesToUncommit;
    }

    currentPosition -= size;
    remainingReserved += size;
#endif
    return currentPosition;

  }

  if (size > 0) {
    void * p;
    if (size > remainingCommitted) {

      /* Commit some more pages.
         We round up to an even number of pages.
         Note that page size must be a power of two.
      */

      int bytesToCommit = (size - remainingCommitted + pageSize - 1) & ~(pageSize - 1); 
      int * result;

      result = VirtualAlloc((LPVOID) nextPage, bytesToCommit, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

      if (result == NULL )
        exit (1); /* VirtualAlloc commit failed */

      nextPage += bytesToCommit;
      remainingCommitted += bytesToCommit;
    }

    p = currentPosition;
    currentPosition += size;
    remainingCommitted -= size;
    remainingReserved -= size;
    return p;
  }

  assert (size == 0);
  return currentPosition;

}
