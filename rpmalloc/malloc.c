/* malloc.c  -  Memory allocator  -  Public Domain  -  2016 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform lock free thread caching malloc implementation in C11.
 * The latest source code is always available at
 *
 * https://github.com/rampantpixels/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "rpmalloc.h"

#ifndef ENABLE_VALIDATE_ARGS
//! Enable validation of args to public entry points
#define ENABLE_VALIDATE_ARGS      0
#endif

#if ENABLE_VALIDATE_ARGS
//! Maximum allocation size to avoid integer overflow
#define MAX_ALLOC_SIZE            (((size_t)-1) - 4096)
#endif

#ifdef _MSC_VER
#pragma warning (disable : 4100)
#undef malloc
#undef free
#undef calloc
#endif

//This file provides overrides for the standard library malloc style entry points

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
malloc(size_t size);

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
calloc(size_t count, size_t size);

extern void* RPMALLOC_CDECL
realloc(void* ptr, size_t size);

extern void* RPMALLOC_CDECL
reallocf(void* ptr, size_t size);

extern void*
reallocarray(void* ptr, size_t count, size_t size);

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
valloc(size_t size);

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
pvalloc(size_t size);

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
aligned_alloc(size_t alignment, size_t size);

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
memalign(size_t alignment, size_t size);

extern int RPMALLOC_CDECL
posix_memalign(void** memptr, size_t alignment, size_t size);

extern void RPMALLOC_CDECL
free(void* ptr);

extern void RPMALLOC_CDECL
cfree(void* ptr);

extern size_t RPMALLOC_CDECL
malloc_usable_size(
#if defined(__ANDROID__)
	const void* ptr
#else
	void* ptr
#endif
	);

extern size_t RPMALLOC_CDECL
malloc_size(void* ptr);

#ifdef _WIN32

#include <windows.h>

static size_t page_size;
static int is_initialized;

static void
initializer(void) {
	if (!is_initialized) {
		is_initialized = 1;
		SYSTEM_INFO system_info;
		memset(&system_info, 0, sizeof(system_info));
		GetSystemInfo(&system_info);
		page_size = system_info.dwPageSize;
		rpmalloc_initialize();
	}
	rpmalloc_thread_initialize();
}

static void
finalizer(void) {
	rpmalloc_thread_finalize();
	if (is_initialized) {
		is_initialized = 0;
		rpmalloc_finalize();
	}
}

#else

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

static size_t page_size;
static pthread_key_t destructor_key;
static int is_initialized;

static void
thread_destructor(void*);

static void __attribute__((constructor))
initializer(void) {
	if (!is_initialized) {
		is_initialized = 1;
		page_size = (size_t)sysconf(_SC_PAGESIZE);
		pthread_key_create(&destructor_key, thread_destructor);
		if (rpmalloc_initialize())
			abort();
	}
	rpmalloc_thread_initialize();
}

static void __attribute__((destructor))
finalizer(void) {
	rpmalloc_thread_finalize();
	if (is_initialized) {
		is_initialized = 0;
		rpmalloc_finalize();
	}
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
	rpmalloc_thread_initialize();
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
#if defined(__linux__) || defined(__APPLE__)
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

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
malloc(size_t size) {
	initializer();
	return rpmalloc(size);
}

void* RPMALLOC_CDECL
realloc(void* ptr, size_t size) {
	initializer();
	return rprealloc(ptr, size);
}

void* RPMALLOC_CDECL
reallocf(void* ptr, size_t size) {
	initializer();
	return rprealloc(ptr, size);
}

void* RPMALLOC_CDECL
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

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
calloc(size_t count, size_t size) {
	initializer();
	return rpcalloc(count, size);
}

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
valloc(size_t size) {
	initializer();
	if (!size)
		size = page_size;
	size_t total_size = size + page_size;
#if ENABLE_VALIDATE_ARGS
	if (total_size < size) {
		errno = EINVAL;
		return 0;
	}
#endif
	void* buffer = rpmalloc(total_size);
	if ((uintptr_t)buffer & (page_size - 1))
		return (void*)(((uintptr_t)buffer & ~(page_size - 1)) + page_size);
	return buffer;
}

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
pvalloc(size_t size) {
	size_t aligned_size = size;
	if (aligned_size % page_size)
		aligned_size = (1 + (aligned_size / page_size)) * page_size;
#if ENABLE_VALIDATE_ARGS
	if (aligned_size < size) {
		errno = EINVAL;
		return 0;
	}
#endif
	return valloc(size);
}

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
aligned_alloc(size_t alignment, size_t size) {
	initializer();
	return rpaligned_alloc(alignment, size);
}

RPMALLOC_RESTRICT void* RPMALLOC_CDECL
memalign(size_t alignment, size_t size) {
	initializer();
	return rpmemalign(alignment, size);
}

int RPMALLOC_CDECL
posix_memalign(void** memptr, size_t alignment, size_t size) {
	initializer();
	return rpposix_memalign(memptr, alignment, size);
}

void RPMALLOC_CDECL
free(void* ptr) {
	if (!is_initialized || !rpmalloc_is_thread_initialized())
		return;
	rpfree(ptr);
}

void RPMALLOC_CDECL
cfree(void* ptr) {
	free(ptr);
}

size_t RPMALLOC_CDECL
malloc_usable_size(
#if defined(__ANDROID__)
	const void* ptr
#else
	void* ptr
#endif
	) {
	if (!rpmalloc_is_thread_initialized())
		return 0;
	return rpmalloc_usable_size((void*)(uintptr_t)ptr);
}

size_t RPMALLOC_CDECL
malloc_size(void* ptr) {
	return malloc_usable_size(ptr);
}

#ifdef _MSC_VER

extern void* RPMALLOC_CDECL
_expand(void* block, size_t size) {
	return realloc(block, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_recalloc(void* block, size_t count, size_t size) {
	initializer();
	if (!block)
		return rpcalloc(count, size);
	size_t newsize = count * size;
	size_t oldsize = rpmalloc_usable_size(block);
	void* newblock = rprealloc(block, newsize);
	if (newsize > oldsize)
		memset((char*)newblock + oldsize, 0, newsize - oldsize);
	return newblock;
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_malloc(size_t size, size_t alignment) {
	return aligned_alloc(alignment, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_realloc(void* block, size_t size, size_t alignment) {
	initializer();
	size_t oldsize = rpmalloc_usable_size(block);
	return rpaligned_realloc(block, alignment, size, oldsize, 0);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_recalloc(void* block, size_t count, size_t size, size_t alignment) {
	initializer();
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

void RPMALLOC_CDECL
_aligned_free(void* block) {
	free(block);
}

extern size_t RPMALLOC_CDECL
_msize(void* ptr) {
	return malloc_usable_size(ptr);
}

extern size_t RPMALLOC_CDECL
_aligned_msize(void* block, size_t alignment, size_t offset) {
	return malloc_usable_size(block);
}

extern intptr_t RPMALLOC_CDECL
_get_heap_handle(void) {
	return 0;
}

extern int RPMALLOC_CDECL
_heap_init(void) {
	initializer();
	return 1;
}

extern void RPMALLOC_CDECL
_heap_term() {
}

extern int RPMALLOC_CDECL
_set_new_mode(int flag) {
	(void)sizeof(flag);
	return 0;
}

#ifndef NDEBUG

extern int RPMALLOC_CDECL
_CrtDbgReport(int reportType, char const* fileName, int linenumber, char const* moduleName, char const* format, ...) {
	return 0;
}

extern int RPMALLOC_CDECL
_CrtDbgReportW(int reportType, wchar_t const* fileName, int lineNumber, wchar_t const* moduleName, wchar_t const* format, ...) {
	return 0;
}

extern int RPMALLOC_CDECL
_VCrtDbgReport(int reportType, char const* fileName, int linenumber, char const* moduleName, char const* format, va_list arglist) {
	return 0;
}

extern int RPMALLOC_CDECL
_VCrtDbgReportW(int reportType, wchar_t const* fileName, int lineNumber, wchar_t const* moduleName, wchar_t const* format, va_list arglist) {
	return 0;
}

extern int RPMALLOC_CDECL
_CrtSetReportMode(int reportType, int reportMode) {
	return 0;
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_malloc_dbg(size_t size, int blockUse, char const* fileName, int lineNumber) {
	return malloc(size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_expand_dbg(void* block, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return _expand(block, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_calloc_dbg(size_t count, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return calloc(count, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_realloc_dbg(void* block, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return realloc(block, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_recalloc_dbg(void* block, size_t count, size_t size, int blockUse, char const* fileName, int lineNumber) {
	return _recalloc(block, count, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_malloc_dbg(size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return aligned_alloc(alignment, size);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_realloc_dbg(void* block, size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return _aligned_realloc(block, size, alignment);
}

extern RPMALLOC_RESTRICT void* RPMALLOC_CDECL
_aligned_recalloc_dbg(void* block, size_t count, size_t size, size_t alignment, char const* fileName, int lineNumber) {
	return _aligned_recalloc(block, count, size, alignment);
}

extern void RPMALLOC_CDECL
_free_dbg(void* block, int blockUse) {
	free(block);
}

extern void RPMALLOC_CDECL
_aligned_free_dbg(void* block) {
	free(block);
}

extern size_t RPMALLOC_CDECL
_msize_dbg(void* ptr) {
	return malloc_usable_size(ptr);
}

extern size_t RPMALLOC_CDECL
_aligned_msize_dbg(void* block, size_t alignment, size_t offset) {
	return malloc_usable_size(block);
}

#endif  // NDEBUG

extern void* _crtheap = (void*)1;

#endif
