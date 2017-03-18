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

extern void*
calloc(size_t count, size_t size);

extern void
free(void* ptr);

extern void*
malloc(size_t size);

extern void *
realloc(void* ptr, size_t size);

extern void*
aligned_alloc(size_t alignment, size_t size);

extern void*
memalign(size_t alignment, size_t size);

extern int
posix_memalign(void** memptr, size_t alignment, size_t size);


void*
calloc(size_t count, size_t size) {
	return rpcalloc(count, size);
}

void
free(void* ptr) {
	rpfree(ptr);
}

void*
malloc(size_t size) {
	return rpmalloc(size);
}

void*
realloc(void* ptr, size_t size) {
	return rprealloc(ptr, size);
}

void*
aligned_alloc(size_t alignment, size_t size) {
	return rpaligned_alloc(alignment, size);
}

void*
memalign(size_t alignment, size_t size) {
	return rpmemalign(alignment, size);
}

int
posix_memalign(void** memptr, size_t alignment, size_t size) {
	return rpposix_memalign(memptr, alignment, size);
}

#ifdef _WIN32

#else

#include <pthread.h>
#include <stdlib.h>

static pthread_key_t destructor_key;

static void
thread_destructor(void*);

static __attribute__((constructor)) void
initialize_rpmalloc(void) {
	pthread_key_create(&destructor_key, thread_destructor);
	rpmalloc_initialize();
}

static __attribute__((destructor)) void
finalize_rpmalloc(void) {
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

/*
extern int
pthread_create(pthread_t* thread,
               const pthread_attr_t* attr,
               void* (*start_routine)(void*),
               void* arg);

extern int
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
}*/


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
	{ (void *) newf, (void *) oldf }

MAC_INTERPOSE(pthread_create_proxy, pthread_create);

#endif

#endif

