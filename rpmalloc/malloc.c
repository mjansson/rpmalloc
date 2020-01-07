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

#if (defined(__GNUC__) || defined(__clang__)) && !defined(__MACH__)
#pragma GCC visibility push(default)
#endif

#define USE_IMPLEMENT 1
#define USE_INTERPOSE 0
#define USE_ALIAS 0

#if defined(__APPLE__) && ENABLE_PRELOAD
#undef USE_INTERPOSE
#define USE_INTERPOSE 1

typedef struct interpose_t {
	void* new_func;
	void* orig_func;
} interpose_t;

#define MAC_INTERPOSE_PAIR(newf, oldf) 	{ (void*)newf, (void*)oldf }
#define MAC_INTERPOSE_SINGLE(newf, oldf) \
__attribute__((used)) static const interpose_t macinterpose##newf##oldf \
__attribute__ ((section("__DATA, __interpose"))) = MAC_INTERPOSE_PAIR(newf, oldf)

#endif

#if !defined(_WIN32) && !USE_INTERPOSE
#undef USE_IMPLEMENT
#undef USE_ALIAS
#define USE_IMPLEMENT 0
#define USE_ALIAS 1
#endif

#ifdef _MSC_VER
#pragma warning (disable : 4100)
#undef malloc
#undef free
#undef calloc
#endif

#if ENABLE_OVERRIDE

#if USE_IMPLEMENT

extern inline void* RPMALLOC_CDECL malloc(size_t size) { return rpmalloc(size); }
extern inline void* RPMALLOC_CDECL calloc(size_t count, size_t size) { return rpcalloc(count, size); }
extern inline void* RPMALLOC_CDECL realloc(void* ptr, size_t size) { return rprealloc(ptr, size); }
extern inline void* RPMALLOC_CDECL reallocf(void* ptr, size_t size) { return rprealloc(ptr, size); }
extern inline void* RPMALLOC_CDECL aligned_alloc(size_t alignment, size_t size) { return rpaligned_alloc(alignment, size); }
extern inline void* RPMALLOC_CDECL memalign(size_t alignment, size_t size) { return rpmemalign(alignment, size); }
extern inline int RPMALLOC_CDECL posix_memalign(void** memptr, size_t alignment, size_t size) { return rpposix_memalign(memptr, alignment, size); }
extern inline void RPMALLOC_CDECL free(void* ptr) { rpfree(ptr); }
extern inline void RPMALLOC_CDECL cfree(void* ptr) { rpfree(ptr); }
extern inline size_t RPMALLOC_CDECL malloc_usable_size(void* ptr) { return rpmalloc_usable_size(ptr); }
extern inline size_t RPMALLOC_CDECL malloc_size(void* ptr) { return rpmalloc_usable_size(ptr); }

// Overload the C++ operators using the mangled names (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)
// operators delete and delete[]
extern void _ZdlPv(void* p); void _ZdlPv(void* p) { rpfree(p); }
extern void _ZdaPv(void* p); void _ZdaPv(void* p) { rpfree(p); }
#if ARCH_64BIT
// 64-bit operators new and new[], normal and aligned
extern void* _Znwm(uint64_t size); void* _Znwm(uint64_t size) { return rpmalloc(size); }
extern void* _Znam(uint64_t size); void* _Znam(uint64_t size) { return rpmalloc(size); }
extern void* _Znwmm(uint64_t size, uint64_t align); void* _Znwmm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern void* _Znamm(uint64_t size, uint64_t align); void* _Znamm(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#else
// 32-bit operators new and new[], normal and aligned
extern void* _Znwj(uint32_t size); void* _Znwj(uint32_t size) { return rpmalloc(size); }
extern void* _Znaj(uint32_t size); void* _Znaj(uint32_t size) { return rpmalloc(size); }
extern void* _Znwjj(uint64_t size, uint64_t align); void* _Znwjj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
extern void* _Znajj(uint64_t size, uint64_t align); void* _Znajj(uint64_t size, uint64_t align) { return rpaligned_alloc(align, size); }
#endif

#endif

#if USE_INTERPOSE

__attribute__((used)) static const interpose_t macinterpose_malloc[]
__attribute__ ((section("__DATA, __interpose"))) = {
	//new and new[]
	MAC_INTERPOSE_PAIR(rpmalloc, _Znwm),
	MAC_INTERPOSE_PAIR(rpmalloc, _Znam),
	//delete and delete[]
	MAC_INTERPOSE_PAIR(rpfree, _ZdlPv),
	MAC_INTERPOSE_PAIR(rpfree, _ZdaPv),
	MAC_INTERPOSE_PAIR(rpmalloc, malloc),
	MAC_INTERPOSE_PAIR(rpmalloc, calloc),
	MAC_INTERPOSE_PAIR(rprealloc, realloc),
	MAC_INTERPOSE_PAIR(rprealloc, reallocf),
#if defined(__MAC_10_15) && __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_15
	MAC_INTERPOSE_PAIR(rpaligned_alloc, aligned_alloc),
#endif
	MAC_INTERPOSE_PAIR(rpmemalign, memalign),
	MAC_INTERPOSE_PAIR(rpposix_memalign, posix_memalign),
	MAC_INTERPOSE_PAIR(rpfree, free),
	MAC_INTERPOSE_PAIR(rpfree, cfree),
	MAC_INTERPOSE_PAIR(rpmalloc_usable_size, malloc_usable_size),
	MAC_INTERPOSE_PAIR(rpmalloc_usable_size, malloc_size)
};

#endif

#if USE_ALIAS

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
extern inline void* _Znwjj(uint32_t size, uint32_t align) { return rpaligned_alloc(align, size); }
extern inline void* _Znajj(uint32_t size, uint32_t align) { return rpaligned_alloc(align, size); }
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

#endif // ENABLE_OVERRIDE

#if ENABLE_PRELOAD

#ifdef _WIN32

#if defined(BUILD_DYNAMIC_LINK) && BUILD_DYNAMIC_LINK

__declspec(dllexport) BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void)sizeof(reserved);
	(void)sizeof(instance);
	if (reason == DLL_PROCESS_ATTACH)
		rpmalloc_initialize();
	else if (reason == DLL_PROCESS_DETACH)
		rpmalloc_finalize();
	else if (reason == DLL_THREAD_ATTACH)
		rpmalloc_thread_initialize();
	else if (reason == DLL_THREAD_DETACH)
		rpmalloc_thread_finalize();
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

MAC_INTERPOSE_SINGLE(pthread_create_proxy, pthread_create);

#else

#include <dlfcn.h>

int
pthread_create(pthread_t* thread,
               const pthread_attr_t* attr,
               void* (*start_routine)(void*),
               void* arg) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__) || defined(__HAIKU__)
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

#endif

#if ENABLE_OVERRIDE

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

#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(__MACH__)
#pragma GCC visibility pop
#endif
