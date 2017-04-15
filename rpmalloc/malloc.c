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

//This file provides overrides for the standard library malloc style entry points

extern void*
malloc(size_t size);

extern void*
calloc(size_t count, size_t size);

extern void *
realloc(void* ptr, size_t size);

extern void*
valloc(size_t size);

extern void*
pvalloc(size_t size);

extern void*
aligned_alloc(size_t alignment, size_t size);

extern void*
memalign(size_t alignment, size_t size);

extern int
posix_memalign(void** memptr, size_t alignment, size_t size);

extern void
free(void* ptr);

extern void
cfree(void* ptr);

extern size_t
malloc_usable_size(void* ptr);

extern size_t
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

//TODO: Injection from rpmalloc compiled as DLL not yet implemented

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
		rpmalloc_initialize();
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

void*
calloc(size_t count, size_t size) {
	initializer();
	return rpcalloc(count, size);
}

void
free(void* ptr) {
	if (!is_initialized || !rpmalloc_is_thread_initialized())
		return;
	rpfree(ptr);
}

void
cfree(void* ptr) {
	free(ptr);
}

void*
malloc(size_t size) {
	initializer();
	return rpmalloc(size);
}

void*
realloc(void* ptr, size_t size) {
	initializer();
	return rprealloc(ptr, size);
}

void*
valloc(size_t size) {
	initializer();
	if (!size)
		size = page_size;
	size_t total_size = size + page_size;
	void* buffer = rpmalloc(total_size);
	if ((uintptr_t)buffer & (page_size - 1))
		return (void*)(((uintptr_t)buffer & ~(page_size - 1)) + page_size);
	return buffer;
}

void*
pvalloc(size_t size) {
	if (size % page_size)
		size = (1 + (size / page_size)) * page_size;
	return valloc(size);
}

void*
aligned_alloc(size_t alignment, size_t size) {
	initializer();
	return rpaligned_alloc(alignment, size);
}

void*
memalign(size_t alignment, size_t size) {
	initializer();
	return rpmemalign(alignment, size);
}

int
posix_memalign(void** memptr, size_t alignment, size_t size) {
	initializer();
	return rpposix_memalign(memptr, alignment, size);
}

size_t
malloc_usable_size(void* ptr) {
	if (!rpmalloc_is_thread_initialized())
		return 0;
	return rpmalloc_usable_size(ptr);
}

size_t
malloc_size(void* ptr) {
	return malloc_usable_size(ptr);
}
