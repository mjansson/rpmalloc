/* Basic platform-independent macro definitions for mutexes,
thread-specific data and parameters for malloc.
Posix threads (pthreads) version.
Copyright (C) 2004 Wolfram Gloger <wg@malloc.de>.

Permission to use, copy, modify, distribute, and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that (i) the above copyright notices and this permission
notice appear in all copies of the software and related documentation,
and (ii) the name of Wolfram Gloger may not be used in any advertising
or publicity relating to the software.

THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

IN NO EVENT SHALL WOLFRAM GLOGER BE LIABLE FOR ANY SPECIAL,
INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY
OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef _WINDOWS_MALLOC_MACHINE_H
#define _WINDOWS_MALLOC_MACHINE_H

#include <Windows.h>

#undef thread_atfork_static

/* Normal Windows mutex.
typedef CRITICAL_SECTION mutex_t;

#define MUTEX_INITIALIZER
#define mutex_init(m)              InitializeCriticalSectionAndSpinCount(m, 4096)
#define mutex_lock(m)              (EnterCriticalSection(m), 0)
#define mutex_trylock(m)           TryEnterCriticalSection(m)
#define mutex_unlock(m)            LeaveCriticalSection(m)*/

typedef int mutex_t;

# define mutex_init(m)              (*(m) = 0)
# define mutex_lock(m)              ((*(m) = 1), 0)
# define mutex_trylock(m)           (*(m) ? 1 : ((*(m) = 1), 0))
# define mutex_unlock(m)            (*(m) = 0)

/* thread specific data */
#define tsd_key_t __declspec(thread) void*

#define tsd_key_create(key, destr) *(key) = 0
#define tsd_setspecific(key, data) key = data
#define tsd_getspecific(key, vptr) key

/* at fork */
#define thread_atfork(prepare, parent, child) do {} while(0)

#define atomic_full_barrier() _ReadWriteBarrier()

#define MSPACES 1

#include <sysdeps/generic/malloc-machine.h>

#endif /* !defined(_WINDOWS_MALLOC_MACHINE_H) */
