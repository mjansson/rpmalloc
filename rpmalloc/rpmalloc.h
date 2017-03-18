/* rpmalloc.h  -  Memory allocator  -  Public Domain  -  2016 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform lock free thread caching malloc implementation in C11.
 * The latest source code is always available at
 *
 * https://github.com/rampantpixels/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <stddef.h>

typedef struct rpmalloc_global_statistics_t {
	size_t mapped;
	size_t cache;
} rpmalloc_global_statistics_t;

typedef struct rpmalloc_thread_statistics_t {
	size_t requested;
	size_t allocated;
	size_t active;
	size_t sizecache;
	size_t spancache;
	size_t deferred;
} rpmalloc_thread_statistics_t;

extern int
rpmalloc_initialize(void);

extern void
rpmalloc_finalize(void);

extern void
rpmalloc_thread_initialize(void);

extern void
rpmalloc_thread_finalize(void);

extern void
rpmalloc_thread_collect(void);

extern rpmalloc_thread_statistics_t
rpmalloc_thread_statistics(void);

extern rpmalloc_global_statistics_t
rpmalloc_global_statistics(void);

extern void*
rpmalloc(size_t size);

extern void
rpfree(void* ptr);

extern void*
rpcalloc(size_t num, size_t size);

extern void*
rprealloc(void* ptr, size_t size);

extern void*
rpaligned_alloc(size_t alignment, size_t size);

extern void*
rpmemalign(size_t alignment, size_t size);

extern int
rpposix_memalign(void **memptr, size_t alignment, size_t size);

extern size_t
rpmalloc_usable_size(void* ptr);
