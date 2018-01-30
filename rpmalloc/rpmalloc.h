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

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__) || defined(__GNUC__)
# define RPMALLOC_ATTRIBUTE __attribute__((__malloc__))
# define RPMALLOC_RESTRICT
# define RPMALLOC_CDECL
#elif defined(_MSC_VER)
# define RPMALLOC_ATTRIBUTE
# define RPMALLOC_RESTRICT __declspec(restrict)
# define RPMALLOC_CDECL __cdecl
#else
# define RPMALLOC_ATTRIBUTE
# define RPMALLOC_RESTRICT
# define RPMALLOC_CDECL
#endif

//! Flag to rpaligned_realloc to not preserve content in reallocation
#define RPMALLOC_NO_PRESERVE    1

typedef struct rpmalloc_global_statistics_t {
	//! Current amount of virtual memory mapped (only if ENABLE_STATISTICS=1)
	size_t mapped;
	//! Current amount of memory in global caches for small and medium sizes (<64KiB)
	size_t cached;
	//! Curren amount of memory in global caches for large sizes (>=64KiB)
	size_t cached_large;
	//! Total amount of memory mapped (only if ENABLE_STATISTICS=1)
	size_t mapped_total;
	//! Total amount of memory unmapped (only if ENABLE_STATISTICS=1)
	size_t unmapped_total;
} rpmalloc_global_statistics_t;

typedef struct rpmalloc_thread_statistics_t {
	//! Amount of memory currently requested in allocations (only if ENABLE_STATISTICS=1)
	size_t requested;
	//! Amount of memory actually allocated in memory blocks (only if ENABLE_STATISTICS=1)
	size_t allocated;
	//! Current number of bytes available for allocation from active spans
	size_t active;
	//! Current number of bytes available in thread size class caches
	size_t sizecache;
	//! Current number of bytes available in thread span caches
	size_t spancache;
	//! Current number of bytes in pending deferred deallocations
	size_t deferred;
	//! Total number of bytes transitioned from thread cache to global cache
	size_t thread_to_global;
	//! Total number of bytes transitioned from global cache to thread cache
	size_t global_to_thread;
} rpmalloc_thread_statistics_t;

typedef struct rpmalloc_config_t {
	//! Map memory pages for the given number of bytes. The returned address MUST be
	//  aligned to the rpmalloc span size, which will always be a power of two.
	//  Optionally the function can store an alignment offset in the offset variable
	//  in case it performs alignment and the returned pointer is offset from the
	//  actual start of the memory region due to this alignment. The alignment offset
	//  will be passed to the memory unmap function.
	void* (*memory_map)(size_t size, size_t* offset);
	//! Unmap the memory pages starting at address and spanning the given number of bytes.
	//  The address, size and offset variables will always be a value triple as used
	//  in and returned by an earlier call to memory_map
	void (*memory_unmap)(void* address, size_t size, size_t offset);
	//! Size of memory pages. If set to 0, rpmalloc will use system calls to determine the page size.
	//  The page size MUST be a power of two in [512,16384] range (2^9 to 2^14).
	size_t page_size;
	//! Size of a span of memory pages. MUST be a multiple of page size, and in [512,262144] range (unless 0).
	//  Set to 0 to use the default span size. All memory mapping requests to memory_map will be made with
	//  size set to a multiple of the span size.
	size_t span_size;
	//! Debug callback if memory guards are enabled. Called if a memory overwrite is detected
	void (*memory_overwrite)(void* address);
} rpmalloc_config_t;

extern int
rpmalloc_initialize(void);

extern int
rpmalloc_initialize_config(const rpmalloc_config_t* config);

extern const rpmalloc_config_t*
rpmalloc_config(void);

extern void
rpmalloc_finalize(void);

extern void
rpmalloc_thread_initialize(void);

extern void
rpmalloc_thread_finalize(void);

extern void
rpmalloc_thread_collect(void);

extern int
rpmalloc_is_thread_initialized(void);

extern void
rpmalloc_thread_statistics(rpmalloc_thread_statistics_t* stats);

extern void
rpmalloc_global_statistics(rpmalloc_global_statistics_t* stats);

extern RPMALLOC_RESTRICT void*
rpmalloc(size_t size) RPMALLOC_ATTRIBUTE;

extern void
rpfree(void* ptr);

extern RPMALLOC_RESTRICT void*
rpcalloc(size_t num, size_t size) RPMALLOC_ATTRIBUTE;

extern void*
rprealloc(void* ptr, size_t size);

extern void*
rpaligned_realloc(void* ptr, size_t alignment, size_t size, size_t oldsize, unsigned int flags);

extern RPMALLOC_RESTRICT void*
rpaligned_alloc(size_t alignment, size_t size) RPMALLOC_ATTRIBUTE;

extern RPMALLOC_RESTRICT void*
rpmemalign(size_t alignment, size_t size) RPMALLOC_ATTRIBUTE;

extern int
rpposix_memalign(void **memptr, size_t alignment, size_t size);

extern size_t
rpmalloc_usable_size(void* ptr);

#ifdef __cplusplus
}
#endif
