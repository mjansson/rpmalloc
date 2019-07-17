/* malloc.c  -  Memory allocator  -  Public Domain  -  2016 Mattias Jansson
 *
 * This library provides a cross-platform lock free thread caching malloc implementation in C11.
 * The latest source code is always available at
 *
 * https://github.com/mjansson/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

//
// This file provides overrides for the standard library malloc entry points for C and new/delete operators for C++
// It also provides automatic initialization/finalization of process and threads
//

#ifndef ARCH_64BIT
#  if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
#    define ARCH_64BIT 1
_Static_assert(sizeof(size_t) == 8, "Data type size mismatch");
_Static_assert(sizeof(void*) == 8, "Data type size mismatch");
#  else
#    define ARCH_64BIT 0
_Static_assert(sizeof(size_t) == 4, "Data type size mismatch");
_Static_assert(sizeof(void*) == 4, "Data type size mismatch");
#  endif
#endif

#ifdef _MSC_VER
#pragma warning (disable : 4100)
#undef malloc
#undef free
#undef calloc

extern inline void* RPMALLOC_CDECL malloc(size_t size) { return rpmalloc(size); }
extern inline void* RPMALLOC_CDECL calloc(size_t count, size_t size) { return rpcalloc(count, size); }
extern inline void* RPMALLOC_CDECL realloc(void* ptr, size_t size) { return rprealloc(ptr, size); }
extern inline void* RPMALLOC_CDECL reallocf(void* ptr, size_t size) { return rprealloc(ptr, size); }
extern inline void* RPMALLOC_CDECL aligned_alloc(size_t alignment, size_t size) { return rpaligned_alloc(alignment, size); }
extern inline void* RPMALLOC_CDECL memalign(size_t alignment, size_t size) { return rpmemalign(alignment, size); }
extern inline int RPMALLOC_CDECL posix_memalign(void** memptr, size_t alignment, size_t size) { retunrrpposix_memalign(memptr, alignment, size); }
extern inline void RPMALLOC_CDECL free(void* ptr) { return rpfree(ptr); }
extern inline void RPMALLOC_CDECL cfree(void* ptr) { return rpfree(ptr); }
extern inline size_t RPMALLOC_CDECL malloc_usable_size(void* ptr) { return rpmalloc_usable_size(ptr); }
extern inline size_t RPMALLOC_CDECL malloc_size(void* ptr) { return rpmalloc_usable_size(ptr); }

// Overload the C++ operators using the mangled names (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)
// operators delete and delete[]
extern void _ZdlPv(void* p) { rpfree(p); }
extern void _ZdaPv(void* p) { rpfree(p); }
#if ARCH_64BIT
// 64-bit operators new and new[], normal and aligned
extern void* _Znwm(uint64_t size) { return rpmalloc(size); }
extern void* _Znam(uint64_t size) { return rpmalloc(size); }
extern void* _Znwmm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern void* _Znamm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#else
// 32-bit operators new and new[], normal and aligned
extern void* _Znwj(uint32_t size) { return rpmalloc(size); }
extern void* _Znaj(uint32_t size) { return rpmalloc(size); }
extern inline void* _Znwjj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern inline void* _Znajj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#endif

#else

#define RPALIAS(fn) __attribute__((alias(#fn), used, visibility("default")));

// Alias the C++ operators using the mangled names (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)

// operators delete and delete[]
void _ZdlPv(void* p) RPALIAS(rpfree)
void _ZdaPv(void* p) RPALIAS(rpfree)

#if ARCH_64BIT
// 64-bit operators new and new[], normal and aligned
void* _Znwm(uint64_t size) RPALIAS(rpmalloc)
void* _Znam(uint64_t size) RPALIAS(rpmalloc)
extern inline void* _Znwmm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern inline void* _Znamm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#else
// 32-bit operators new and new[], normal and aligned
void* _Znwj(uint32_t size) RPALIAS(rpmalloc)
void* _Znaj(uint32_t size) RPALIAS(rpmalloc)
extern inline void* _Znwjj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern inline void* _Znajj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#endif

void* malloc(size_t size) RPALIAS(rpmalloc)
void* calloc(size_t count, size_t size) RPALIAS(rpcalloc)
void* realloc(void* ptr, size_t size) RPALIAS(rprealloc)
void* reallocf(void* ptr, size_t size) RPALIAS(rprealloc)
void* aligned_alloc(size_t alignment, size_t size) RPALIAS(rpaligned_alloc)
void* memalign(size_t alignment, size_t size) RPALIAS(rpmemalign)
int posix_memalign(void** memptr, size_t alignment, size_t size) RPALIAS(rpposix_memalign)
void free(void* ptr) RPALIAS(rpfree)
void cfree(void* ptr) RPALIAS(rpfree)
size_t malloc_usable_size(void* ptr) RPALIAS(rpmalloc_usable_size)
size_t malloc_size(void* ptr) RPALIAS(rpmalloc_usable_size)

#endif

extern inline void* RPMALLOC_CDECL
reallocarray(void* ptr, size_t count, size_t size) {
	size_t total;
#if ENABLE_VALIDATE_ARGS
#ifdef _MSC_VER
	int err = SizeTMult(count, size, &total);
	if ((err != S_OK) || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#else
	int err = __builtin_umull_overflow(count, size, &total);
	if (err || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#endif
#else
	total = count * size;
#endif
	return realloc(ptr, total);
}

extern inline void* RPMALLOC_CDECL
valloc(size_t size) {
	get_thread_heap();
	if (!size)
		size = _memory_page_size;
	size_t total_size = size + _memory_page_size;
#if ENABLE_VALIDATE_ARGS
	if (total_size < size) {
		errno = EINVAL;
		return 0;
	}
#endif
	void* buffer = rpmalloc(total_size);
	if ((uintptr_t)buffer & (_memory_page_size - 1))
		return (void*)(((uintptr_t)buffer & ~(_memory_page_size - 1)) + _memory_page_size);
	return buffer;
}

extern inline void* RPMALLOC_CDECL
pvalloc(size_t size) {
	get_thread_heap();
	size_t aligned_size = size;
	if (aligned_size % _memory_page_size)
		aligned_size = (1 + (aligned_size / _memory_page_size)) * _memory_page_size;
#if ENABLE_VALIDATE_ARGS
	if (aligned_size < size) {
		errno = EINVAL;
		return 0;
	}
#endif
	return valloc(size);
}


#ifdef _WIN32

#include <Windows.h>

#if !defined(BUILD_DYNAMIC) || !BUILD_DYNAMIC

#include <fibersapi.h>

static DWORD fls_key;

static void NTAPI
thread_destructor(void* value) {
	if (value)
		rpmalloc_thread_finalize();
}

static void
initializer(void) {
	rpmalloc_initialize();
#if !defined(BUILD_DYNAMIC) || !BUILD_DYNAMIC
    fls_key = FlsAlloc(&thread_destructor);
#endif
}

#endif

#if defined(BUILD_DYNAMIC) && BUILD_DYNAMIC

__declspec(dllexport) BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void)sizeof(reserved);
	(void)sizeof(instance);
	if (reason == DLL_PROCESS_ATTACH) {
		rpmalloc_initialize();
	} else if (reason == DLL_PROCESS_DETACH) {
		rpmalloc_finalize();
	} else if (reason == DLL_THREAD_ATTACH) {
		rpmalloc_thread_initialize();
	} else if (reason == DLL_THREAD_DETACH) {
		rpmalloc_thread_finalize();
	}
	return TRUE;
}

#endif

#else

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

static pthread_key_t destructor_key;

static void
thread_destructor(void*);

static void __attribute__((constructor))
initializer(void) {
	rpmalloc_initialize();
	pthread_key_create(&destructor_key, thread_destructor);
}

static void __attribute__((destructor))
finalizer(void) {
	rpmalloc_finalize();
}

typedef struct {
	void* (*real_start)(void*);
	void* real_arg;
} thread_starter_arg;

static void*
thread_starter(void* argptr) {
	thread_starter_arg* arg = argptr;
	void* (*real_start)(void*) = arg->real_start;
	void* real_arg = arg->real_arg;
	rpmalloc_thread_initialize();
	rpfree(argptr);
	pthread_setspecific(destructor_key, (void*)1);
	return (*real_start)(real_arg);
}

static void
thread_destructor(void* value) {
	(void)sizeof(value);
	rpmalloc_thread_finalize();
}

#ifdef __APPLE__

static int
pthread_create_proxy(pthread_t* thread,
                     const pthread_attr_t* attr,
                     void* (*start_routine)(void*),
                     void* arg) {
	rpmalloc_initialize();
	thread_starter_arg* starter_arg = rpmalloc(sizeof(thread_starter_arg));
	starter_arg->real_start = start_routine;
	starter_arg->real_arg = arg;
	return pthread_create(thread, attr, thread_starter, starter_arg);
}

typedef struct interpose_s {
	void* new_func;
	void* orig_func;
} interpose_t;

#define MAC_INTERPOSE(newf, oldf) __attribute__((used)) \
static const interpose_t macinterpose##newf##oldf \
__attribute__ ((section("__DATA, __interpose"))) = \
	{ (void*)newf, (void*)oldf }

MAC_INTERPOSE(pthread_create_proxy, pthread_create);

#else

#include <dlfcn.h>

int
pthread_create(pthread_t* thread,
               const pthread_attr_t* attr,
               void* (*start_routine)(void*),
               void* arg) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
	char fname[] = "pthread_create";
#else
	char fname[] = "_pthread_create";
#endif
	void* real_pthread_create = dlsym(RTLD_NEXT, fname);
	rpmalloc_thread_initialize();
	thread_starter_arg* starter_arg = rpmalloc(sizeof(thread_starter_arg));
	starter_arg->real_start = start_routine;
	starter_arg->real_arg = arg;
	return (*(int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))real_pthread_create)(thread, attr, thread_starter, starter_arg);
}

#endif

#endif

#if defined(__GLIBC__) && defined(__linux__)

void* __libc_malloc(size_t size) RPALIAS(rpmalloc)
void* __libc_calloc(size_t count, size_t size) RPALIAS(rpcalloc)
void* __libc_realloc(void* p, size_t size) RPALIAS(rprealloc)
void __libc_free(void* p) RPALIAS(rpfree)
void __libc_cfree(void* p) RPALIAS(rpfree)
void* __libc_memalign(size_t align, size_t size) RPALIAS(rpmemalign)
int __posix_memalign(void** p, size_t align, size_t size) RPALIAS(rpposix_memalign)

extern void* __libc_valloc(size_t size);
extern void* __libc_pvalloc(size_t size);

void*
__libc_valloc(size_t size) {
	return valloc(size);
}

void*
__libc_pvalloc(size_t size) {
	return pvalloc(size);
}

#endif

#ifdef _MSC_VER

extern inline void* RPMALLOC_CDECL
_expand(void* block, size_t size) {
	return realloc(block, size);
}

extern inline void* RPMALLOC_CDECL
_recalloc(void* block, size_t count, size_t size) {
	if (!block)
		return rpcalloc(count, size);
	size_t newsize = count * size;
	size_t oldsize = rpmalloc_usable_size(block);
	void* newblock = rprealloc(block, newsize);
	if (newsize > oldsize)
		memset((char*)newblock + oldsize, 0, newsize - oldsize);
	return newblock;
}

extern inline void* RPMALLOC_CDECL
_aligned_malloc(size_t size, size_t alignment) {
	return aligned_alloc(alignment, size);
}

extern inline void* RPMALLOC_CDECL
_aligned_realloc(void* block, size_t size, size_t alignment) {
	size_t oldsize = rpmalloc_usable_size(block);
	return rpaligned_realloc(block, alignment, size, oldsize, 0);
}

extern inline void* RPMALLOC_CDECL
_aligned_recalloc(void* block, size_t count, size_t size, size_t alignment) {
	size_t newsize = count * size;
	if (!block) {
		block = rpaligned_alloc(count, newsize);
		memset(block, 0, newsize);
		return block;
	}
	size_t oldsize = rpmalloc_usable_size(block);
	void* newblock = rpaligned_realloc(block, alignment, newsize, oldsize, 0);
	if (newsize > oldsize)
		memset((char*)newblock + oldsize, 0, newsize - oldsize);
	return newblock;
}

extern inline void RPMALLOC_CDECL
_aligned_free(void* block) {
	free(block);
}

extern inline size_t RPMALLOC_CDECL
_msize(void* ptr) {
	return rpmalloc_usable_size(ptr);
}

extern inline size_t RPMALLOC_CDECL
_aligned_msize(void* block, size_t alignment, size_t offset) {
	return rpmalloc_usable_size(block);
}

extern inline intptr_t RPMALLOC_CDECL
_get_heap_handle(void) {
	return 0;
}

extern inline int RPMALLOC_CDECL
_heap_init(void) {
	return 1;
}

extern inline void RPMALLOC_CDECL
_heap_term() {
}

extern inline int RPMALLOC_CDECL
_set_new_mode(int flag) {
	(void)sizeof(flag);
	return 0;
}

#ifndef NDEBUG

extern inline int RPMALLOC_CDECL
_CrtDbgReport(int reportType, char const* fileName, int linenumber, char const* moduleName, char const* format, ...) {
	return 0;
}

extern inline int RPMALLOC_CDECL
_CrtDbgReportW(int reportType, wchar_t const* fileName, int lineNumber, wchar_t const* moduleName, wchar_t const* format, ...) {
	return 0;
}

extern inline int RPMALLOC_CDECL
_VCrtDbgReport(int reportType, char const* fileName, int linenumber, char const* moduleName, char const* format, va_list arglist) {
	return 0;
}

extern inline int RPMALLOC_CDECL
_VCrtDbgReportW(int reportType, wchar_t const* fileName, int lineNumber, wchar_t const* moduleName, wchar_t const* format, va_list arglist) {
	return 0;
}

extern inline int RPMALLOC_CDECL
_CrtSetReportMode(int reportType, int reportMode) {
	return 0;
}

extern inline void* RPMALLOC_CDECL
_malloc_dbg(size_t size, int blockUse, char const* fileName, int lineNumber) {
	return malloc(size);
}

extern inline void* RPMALLOC_CDECL
_expand_dbg(void* block, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return _expand(block, size);
}

extern inline void* RPMALLOC_CDECL
_calloc_dbg(size_t count, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return calloc(count, size);
}

extern inline void* RPMALLOC_CDECL
_realloc_dbg(void* block, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return realloc(block, size);
}

extern inline void* RPMALLOC_CDECL
_recalloc_dbg(void* block, size_t count, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return _recalloc(block, count, size);
}

extern inline void* RPMALLOC_CDECL
_aligned_malloc_dbg(size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return aligned_alloc(alignment, size);
}

extern inline void* RPMALLOC_CDECL
_aligned_realloc_dbg(void* block, size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return _aligned_realloc(block, size, alignment);
}

extern inline void* RPMALLOC_CDECL
_aligned_recalloc_dbg(void* block, size_t count, size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return _aligned_recalloc(block, count, size, alignment);
}

extern inline void RPMALLOC_CDECL
_free_dbg(void* block, int blockUse) {
	free(block);
}

extern inline void RPMALLOC_CDECL
_aligned_free_dbg(void* block) {
	free(block);
}

extern inline size_t RPMALLOC_CDECL
_msize_dbg(void* ptr) {
	return malloc_usable_size(ptr);
}

extern inline size_t RPMALLOC_CDECL
_aligned_msize_dbg(void* block, size_t alignment, size_t offset) {
	return malloc_usable_size(block);
}

#endif  // NDEBUG

extern void* _crtheap = (void*)1;

#endif
