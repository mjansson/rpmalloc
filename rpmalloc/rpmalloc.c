/* rpmalloc.c  -  Memory allocator  -  Public Domain  -  2016 Mattias Jansson
 *
 * This library provides a cross-platform lock free thread caching malloc implementation in C11.
 * The latest source code is always available at
 *
 * https://github.com/mjansson/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "rpmalloc.h"

#include <stdint.h>
#include <errno.h>

#if defined(_MSC_VER) && !defined(__clang__)
#  define _Static_assert static_assert
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

///
/// Implementation controls
///

#ifndef ENABLE_NULL_CHECKS
//! Enable checking for null when mapping new memory
#define ENABLE_NULL_CHECKS        0
#endif
#ifndef ENABLE_ASSERTS
//! Enable asserts (for debugging)
#define ENABLE_ASSERTS            1
#endif
#ifndef ENABLE_THREAD_CACHE
//! Enable per-thread cache
#define ENABLE_THREAD_CACHE       1
#endif
#ifndef ENABLE_GLOBAL_CACHE
//! Enable global cache shared between all threads, requires thread cache
#define ENABLE_GLOBAL_CACHE       1
#endif

#if ENABLE_THREAD_CACHE
#ifndef ENABLE_UNLIMITED_CACHE
//! Unlimited thread and global cache
#define ENABLE_UNLIMITED_CACHE    0
#endif
#ifndef ENABLE_UNLIMITED_THREAD_CACHE
//! Unlimited cache disables any thread cache limitations
#define ENABLE_UNLIMITED_THREAD_CACHE ENABLE_UNLIMITED_CACHE
#endif
#if !ENABLE_UNLIMITED_THREAD_CACHE && !defined(THREAD_CACHE_MAX_CHUNKS)
//! Maximum number of chunks in thread cache
#define THREAD_CACHE_MAX_CHUNKS   4
#else
#undef THREAD_CACHE_MAX_CHUNKS
#define THREAD_CACHE_MAX_CHUNKS   ((size_t)-1)
#endif
#endif

///
/// Preconfigured limits and sizes
///

//! Number of buckets in heap map
#define HEAP_MAP_SIZE             47
//! Chunk size multiplier
#define CHUNK_SHIFT               22
//! Size of a chunk (default multiplier of 22 yields 4MiB chunks)
#define CHUNK_SIZE                (1 << CHUNK_SHIFT)
//! Chunk header size (includes size of first span header)
#define CHUNK_HEADER_SIZE         128
//! Span size multiplier
#define SPAN_SHIFT                16
//! Size of a span (default span multiplier of 16 yields 64KiB spans)
#define SPAN_SIZE                 (1 << SPAN_SHIFT)
//! Span header size
#define SPAN_HEADER_SIZE          128
//! Mask to span start address
#define SPAN_MASK                 (~(SPAN_SIZE - 1))
//! Number of spans in a chunk (first span includes chunk header)
#define SPAN_COUNT                (CHUNK_SIZE / SPAN_SIZE)
//! Granularity of a small allocation block (must be power of two)
#define SMALL_GRANULARITY         16
//! Small granularity shift count
#define SMALL_GRANULARITY_SHIFT   4
//! Number of small block size classes
#define SMALL_CLASS_COUNT         65
//! Maximum size of a small block
#define SMALL_SIZE_LIMIT          (SMALL_GRANULARITY * (SMALL_CLASS_COUNT - 1))
//! Granularity of a medium allocation block
#define MEDIUM_GRANULARITY        512
//! Medium granularity shift count
#define MEDIUM_GRANULARITY_SHIFT  9
//! Number of medium block size classes
#define MEDIUM_CLASS_COUNT        61
//! Maximum size of a medium block
#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY * MEDIUM_CLASS_COUNT))
//! Total number of small + medium size classes
#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)
//! Maximum size of a large block
#define LARGE_SIZE_LIMIT          (CHUNK_SIZE - CHUNK_HEADER_SIZE)
//! ABA protection size in lists (also becomes limit of smallest page size)
#define ABA_SIZE                  512

#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64) || defined(__aarch64__) || defined(__arm64__) \
 || defined(__powerpc64__) || defined(__POWERPC64__) || defined(_WIN64) || defined(__LP64__) || defined(_LP64)
#define ARCH_64BIT 1
#else
#define ARCH_32BIT 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile void* atomicptr_t;
typedef volatile size_t atomicsize_t;
#else
#include <stdatomic.h>
typedef volatile _Atomic(void*) atomicptr_t;
typedef volatile _Atomic(size_t) atomicsize_t;
#endif

typedef struct span_s span_t;
typedef struct chunk_s chunk_t;
typedef struct free_s free_t;
typedef struct heap_s heap_t;
typedef struct size_class_s size_class_t;

#define SPAN_TYPE_SMALL 0
#define SPAN_TYPE_LARGE 1
#define SPAN_TYPE_HUGE  2
#define SPAN_TYPE_COUNT 3

#define CHUNK_STATE_FREE 0
#define CHUNK_STATE_PARTIAL 1
#define CHUNK_STATE_FULL 2

#define SPAN_FLAG_ALIGNED_BLOCKS 4U

//! A span is a collection of memory blocks of the same size. The span is owned
//  by (and contained in) a chunk. The span control structure is located at the
//  start of the span memory area, followed by the memory blocks. The chunk for
//  a span can be reached by offsetting the span start memory address with the
//  chunk index multiplied by span size.
struct span_s {
	//! Free list
	void*        free;
	//! Used count
	uint16_t     used_count;
	//! Deferred list size
	uint16_t     defer_size;
	//! Block count
	uint16_t     block_count;
	//! Block size
	uint16_t     block_size;
	//! Initialization limit
	uint16_t     initialized_count;
	//! Span index in chunk
	uint16_t     chunk_index;
	//! Span type
	uint32_t     type:2;
	//! Size class
	uint32_t     size_class:8;
	//! Flags
	uint32_t     flags:4;
	//! Span count for large blocks
	uint32_t     span_count:16;
	//! Unused
	uint32_t     unused_bits:2;
	//! Owning thread ID
	uintptr_t    thread;
	//! Free list deferred from other threads
	atomicptr_t  free_defer;
	//! Previous span
	span_t*      prev;
	//! Next span
	span_t*      next;
	//! Link to next span in deferred span list
	span_t*      next_deferred_span;
};

//! A chunk is a collection of spans, which can be of different types. A chunk
//  is always owned by a heap. Span control blocks are located at the start of each span.
struct chunk_s {
	//! A chunk always starts with the first span header
	span_t       first_span;
	//! Owning heap
	heap_t*      heap;
	//! List of free spans in increasing size order
	span_t*      free;
	//! Number of free spans
	uint32_t     free_count;
	//! Number of initialized spans
	uint32_t     initialized_count;
	//! State
	uint32_t     state;
	//! Offset to start of memory mapped region
	uint32_t     mapped_offset;
	//! Size in bytes of memory mapped region
	size_t       mapped_size;
	//! Previous chunk
	chunk_t*     prev;
	//! Next chunk
	chunk_t*     next;
};

//! Free lists for a size class
struct free_s {
	//! Free list
	void*   free;
	//! Partial small type span list
	span_t* partial;
};

//! A heap maintains ownership of all chunks allocated by the heap
struct heap_s {
	//! Owning thread ID
	uintptr_t    thread;
	//! Free list for each size class
	free_t       free[SIZE_CLASS_COUNT];
	//! Chunk list of partially used chunks (double linked)
	chunk_t*     partial_chunk;
#if RPMALLOC_FIRST_CLASS_HEAPS
	//! Chunk list of fully used chunks (double linked)
	chunk_t*     full_chunk;
#endif
#if ENABLE_THREAD_CACHE
	//! Chunk list of completely free chunks (single linked)
	chunk_t*     free_chunk;
	//! Number of free chunks
	size_t       free_chunk_count;
#endif
	//! List of deferred free spans
	atomicptr_t  free_span_deferred;
	//! Identifier
	size_t       id;
	//! Align offset in memory mapping
	size_t       align_offset;
	//! Child heap count
	atomicsize_t child_count;
	//! Master heap
	heap_t*      master;
	//! Next heap in map
	heap_t*      next;
	//! Next heap in orphan list
	heap_t*      next_orphan;
};

//! Size class data
struct size_class_s {
	//! Size of blocks in this class
	uint16_t block_size;
	//! Number of blocks in each span
	uint16_t block_count;
};

///
/// Validation
///

_Static_assert(sizeof(chunk_t) <= CHUNK_HEADER_SIZE, "Invalid chunk header size");
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "Invalid span header size");
_Static_assert(MEDIUM_SIZE_LIMIT < ((SPAN_SIZE - CHUNK_HEADER_SIZE) / 2), "Invalid block size configuration");
_Static_assert(sizeof(size_class_t) == 4, "Size class size mismatch");
#ifdef ARCH_64BIT
_Static_assert(sizeof(atomicsize_t) == 8, "Atomic size mismatch");
#else
_Static_assert(sizeof(atomicsize_t) == 4, "Atomic size mismatch");
#endif

#if ENABLE_ASSERTS
#include <stdio.h>
#include <stdlib.h>
static void
rpmalloc_assert_fail_handler(const char* msg, const char* function, const char* file, int line) {
	fprintf(stderr, "\n*** Assert failed: %s (%s %s:%d) ***\n", msg, function, file, line);
	fflush(stderr);
	abort();
}
#define rpmalloc_assert_fail(x) do { rpmalloc_assert_fail_handler(#x, __FUNCTION__, __FILE__, __LINE__); } while (0)
#define rpmalloc_assert_fail_return(x, ret) rpmalloc_assert_fail(x)
#define rpmalloc_assert(x) do { if (!(x)) rpmalloc_assert_fail_handler(#x, __FUNCTION__, __FILE__, __LINE__); } while (0)
#else
#define rpmalloc_assert_fail(msg) do {} while (0)
#define rpmalloc_assert_fail_return(msg, ret) do { return (ret); } while (0)
#define rpmalloc_assert(x) do {} while (0)
#endif

#if ENABLE_VALIDATE_ARGS
#define rpmalloc_validate_size(size) do { if (size >= MAX_ALLOC_SIZE) { errno = EINVAL; return 0; } } while (0)
#define rpmalloc_validate_alignment(align) do { if ((align >= SPAN_SIZE) || (align & (align - 1))) { errno = EINVAL; return 0; } } while (0)
#if PLATFORM_WINDOWS
#define rpmalloc_safe_mult(lhs, rhs, res) do { int err = SizeTMult(lhs, rhs, &res); if (err != S_OK) { errno = EINVAL; return 0; } } while (0)
#define rpmalloc_safe_add(lhs, rhs, res) do { int err = SizeTAdd(lhs, rhs, &res); if (err != S_OK) { errno = EINVAL; return 0; } } while (0)
#else
#define rpmalloc_safe_mult(lhs, rhs, res) do { int err = __builtin_umull_overflow(lhs, rhs, &res); if (err) { errno = EINVAL; return 0; } } while (0)
#define rpmalloc_safe_add(lhs, rhs, res) do { int err = __builtin_uadd_overflow(lhs, rhs, &res); if (err) { errno = EINVAL; return 0; } } while (0)
#endif
#else
#define rpmalloc_validate_size(size) do { (void)sizeof(size); } while (0)
#define rpmalloc_validate_alignment(align) do { (void)sizeof(align); } while (0)
#define rpmalloc_safe_mult(lhs, rhs, res) res = (lhs) * (rhs)
#define rpmalloc_safe_add(lhs, rhs, res) res = (lhs) + (rhs)
#endif

///
/// Atomic access abstraction
///

#if defined(_MSC_VER) && !defined(__clang__)
#ifdef ARCH_64BIT
static size_t atomicsize_incr(atomicsize_t* src) { return _InterlockedIncrement64((volatile LONG64*)src); }
#else
static size_t atomicsize_incr(atomicsize_t* src) { return _InterlockedIncrement((volatile LONG*)src); }
#endif
static void   atomicsize_store(atomicsize_t* dst, size_t val) { *dst = val; }

static void* atomicptr_load(atomicptr_t* src) { return (void*)*src; }
static void  atomicptr_store(atomicptr_t* dst, void* val) { *dst = val; }
static void  atomicptr_store_release(atomicptr_t* dst, void* val) { *dst = val; }
static int   atomicptr_cas(atomicptr_t* dst, void* val, void* ref) { return (_InterlockedCompareExchangePointer((void* volatile*)dst, val, ref) == ref) ? 1 : 0; }
static int   atomicptr_cas_acquire(atomicptr_t* dst, void* val, void* ref) { return atomicptr_cas(dst, val, ref); }
static void* atomicptr_exchange(atomicptr_t* dst, void* val) { return InterlockedExchangePointer((void* volatile*)dst, val); }

#else

static size_t atomicsize_incr(atomicsize_t* src) { return atomic_fetch_add_explicit(src, 1, memory_order_relaxed) + 1; }
static void   atomicsize_store(atomicsize_t* dst, size_t val) { atomic_store_explicit(dst, val, memory_order_relaxed); }

static void* atomicptr_load(atomicptr_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static void  atomicptr_store(atomicptr_t* dst, void* val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static void  atomicptr_store_release(atomicptr_t* dst, void* val) { atomic_store_explicit(dst, val, memory_order_release); }
static int   atomicptr_cas(atomicptr_t* dst, void* val, void* ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_relaxed, memory_order_relaxed); }
static int   atomicptr_cas_acquire(atomicptr_t* dst, void* val, void* ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_acquire, memory_order_relaxed); }
static void* atomicptr_exchange(atomicptr_t* dst, void* val) { return atomic_exchange_explicit(dst, val, memory_order_relaxed); }

#endif

#define INVALID_POINTER ((void*)((uintptr_t)-1))

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

#if ENABLE_NULL_CHECKS
#  define CHECK_NULL(x) (!(x))
#  define CHECK_NOT_NULL(x) (!!(x))
#else
#  define CHECK_NULL(x) 0
#  define CHECK_NOT_NULL(x) 1
#endif

///
/// Global data
///

//! Size classes
static size_class_t size_class[SIZE_CLASS_COUNT];
//! Medium size class re-index
static uint16_t medium_class_map[MEDIUM_CLASS_COUNT];

//! OS memory page size in bytes
static size_t os_page_size;
//! OS memory page size shift such that page size == 1 << page size shift
static size_t os_page_size_shift;
//! OS huge memory page size in bytes
static size_t os_huge_page_size;
//! OS memory map granularity
static size_t os_mmap_granularity;
//! Support huge pages
static int os_huge_pages;

//! All heaps
static atomicptr_t heap_map[HEAP_MAP_SIZE];
//! Orphaned heaps
static atomicptr_t heap_orphan;
//! Heap orphan list ABA counter
static atomicsize_t heap_orphan_counter;
//! Heap ID counter
static atomicsize_t heap_id;

#if ENABLE_GLOBAL_CACHE
//! Global cache
static atomicptr_t global_cache;
//! Global cache counter
static atomicsize_t global_cache_counter;
#endif

///
/// Forward declarations
///

static chunk_t*
rpmalloc_heap_allocate_chunk(heap_t* heap);

static void
rpmalloc_heap_free_chunk(heap_t* heap, chunk_t* chunk);

static void
rpmalloc_heap_defer_free_span(heap_t* heap, span_t* span);

static void
rpmalloc_heap_collect_free_span(heap_t* heap);

extern void
rpmalloc_heap_validate_integrity();

///
/// Memory mapping
///

//! Map new pages to virtual memory
static void*
rpmalloc_mmap(size_t size, size_t* offset) {
	//We assume huge pages are aligned to addresses which are a multiple of huge page size
	int use_huge_pages = (os_huge_pages && (size >= os_huge_page_size));
	size_t granularity = (use_huge_pages && (os_mmap_granularity < os_huge_page_size)) ? os_huge_page_size : os_mmap_granularity;
	//Either size is a heap (a single memory page), a chunk or a huge block - we only need to align chunks and huge blocks to span granularity, and only if larger than mmap granularity
	size_t padding = ((size >= CHUNK_SIZE) && (SPAN_SIZE > granularity)) ? SPAN_SIZE : 0;
	rpmalloc_assert(size >= os_page_size);
#ifdef _WIN32
	//Ok to MEM_COMMIT - according to MSDN, "actual physical pages are not allocated unless/until the virtual addresses are actually accessed"
	void* ptr = VirtualAlloc(0, size + padding, (use_huge_pages ? MEM_LARGE_PAGES : 0) | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!ptr) {
		rpmalloc_assert_fail_return("Failed to map virtual memory block", 0);
	}
#else
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED;
#  if defined(__APPLE__)
	int fd = (int)VM_MAKE_TAG(240U);
	if (use_huge_pages)
		fd |= VM_FLAGS_SUPERPAGE_SIZE_2MB;
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, flags, fd, 0);
#  elif defined(MAP_HUGETLB)
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, (use_huge_pages ? MAP_HUGETLB : 0) | flags, -1, 0);
#  else
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, flags, -1, 0);
#  endif
	if ((ptr == MAP_FAILED) || !ptr) {
		rpmalloc_assert_fail("Failed to map virtual memory block");
		return 0;
	}
#endif
	if (padding) {
		size_t final_padding = padding - ((uintptr_t)ptr & ~SPAN_MASK);
		rpmalloc_assert(final_padding <= SPAN_MASK);
		rpmalloc_assert(final_padding <= padding);
		rpmalloc_assert(!(final_padding % 8));
		ptr = pointer_offset(ptr, final_padding);
		*offset = final_padding >> 3;
	}
	rpmalloc_assert((size < SPAN_MASK) || !((uintptr_t)ptr & ~SPAN_MASK));
	return ptr;
}

//! Unmap pages from virtual memory
static void
rpmalloc_unmap(void* address, size_t size, size_t offset, size_t release) {
	rpmalloc_assert(release || (offset == 0));
	rpmalloc_assert(!release || (release >= size));
	rpmalloc_assert(size >= os_page_size);
	//Padding is always one span size
	if (release && (size >= CHUNK_SIZE))
		release += SPAN_SIZE;
	if (release && offset) {
		offset <<= 3;
		address = pointer_offset(address, -(int32_t)offset);
	}
#ifdef _WIN32
	if (!VirtualFree(address, release ? 0 : size, release ? MEM_RELEASE : MEM_DECOMMIT))
		rpmalloc_assert_fail("Failed to unmap virtual memory block");
#else
	if (release) {
		if (munmap(address, release))
			rpmalloc_assert_fail("Failed to unmap virtual memory block");
	} else {
#if defined(POSIX_MADV_FREE)
		if (posix_madvise(address, size, POSIX_MADV_FREE))
#endif
		if (posix_madvise(address, size, POSIX_MADV_DONTNEED))
			rpmalloc_assert_fail("Failed to madvise virtual memory block as free");
	}
#endif
}

///
/// Global cache
///
#if ENABLE_GLOBAL_CACHE

static chunk_t*
rpmalloc_global_cache_pop(void) {
	uintptr_t chunkptr;
	do {
		void* old_cache = atomicptr_load(&global_cache);
		chunkptr = (uintptr_t)old_cache & ~(ABA_SIZE - 1);
		if (chunkptr) {
			chunk_t* chunk = (chunk_t*)chunkptr;
			//By accessing the chunk before it is swapped out of list we assume that a contending thread
			//does not manage to traverse the chunk to being unmapped before we access it
			void* new_cache = (void*)((uintptr_t)chunk->next | ((uintptr_t)atomicsize_incr(&global_cache_counter) & (ABA_SIZE - 1)));
			if (atomicptr_cas(&global_cache, new_cache, old_cache))
				return chunk;
		}
	} while (chunkptr);
	return 0;
}

static void
rpmalloc_global_cache_push(chunk_t* chunk) {
	void* old_cache;
	void* new_cache;
	do {
		old_cache = atomicptr_load(&global_cache);
		chunk->next = (chunk_t*)((uintptr_t)old_cache & ~(ABA_SIZE - 1));
		new_cache = (void*)((uintptr_t)chunk | ((uintptr_t)atomicsize_incr(&global_cache_counter) & (ABA_SIZE - 1)));
	} while (!atomicptr_cas(&global_cache, new_cache, old_cache));
	//rpmalloc_unmap(chunk, chunk->mapped_size, chunk->mapped_offset, chunk->mapped_size);
}

#endif

///
/// Thread local heap
///

//! Current thread heap
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
static pthread_key_t thread_heap;
#else
#  ifdef _MSC_VER
#    define _Thread_local __declspec(thread)
#    define TLS_MODEL
#  else
#    define TLS_MODEL __attribute__((tls_model("initial-exec")))
#    if !defined(__clang__) && defined(__GNUC__)
#      define _Thread_local __thread
#    endif
#  endif
static _Thread_local heap_t* thread_heap TLS_MODEL;
#endif

//! Get the current thread heap
static inline heap_t*
rpmalloc_thread_heap_raw(void) {
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	return pthread_getspecific(_memory_thread_heap);
#else
	return thread_heap;
#endif
}

//! Get the current thread heap and initialize if needed when preloading
static inline heap_t*
rpmalloc_thread_heap(void) {
#if ENABLE_PRELOAD
	heap_t* heap = rpmalloc_thread_heap_raw();
	if (heap)
		return heap;
	rpmalloc_initialize();
#endif
	return rpmalloc_thread_heap_raw();
}

//! Fast thread ID
static inline uintptr_t 
rpmalloc_thread_id(void) {
#if defined(_WIN32)
	return (uintptr_t)NtCurrentTeb();
#elif defined(__GNUC__) || defined(__clang__)
	uintptr_t tid;
#  if defined(__i386__)
	__asm__("movl %%gs:0, %0" : "=r" (tid) : : );
#  elif defined(__MACH__)
	__asm__("movq %%gs:0, %0" : "=r" (tid) : : );
#  elif defined(__x86_64__)
	__asm__("movq %%fs:0, %0" : "=r" (tid) : : );
#  elif defined(__arm__)
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r" (tid));
#  elif defined(__aarch64__)
	asm volatile ("mrs %0, tpidr_el0" : "=r" (tid));
#  else
	tid = (uintptr_t)rpmalloc_thread_heap_raw();
#  endif
	return tid;
#else
	return (uintptr_t)rpmalloc_thread_heap_raw();
#endif
}

//! Set the current thread heap
static void
rpmalloc_thread_heap_set(heap_t* heap) {
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	pthread_setspecific(thread_heap, heap);
#else
	thread_heap = heap;
#endif
	if (heap)
		heap->thread = rpmalloc_thread_id();
}

///
/// Layout utility functions
///

static span_t*
rpmalloc_span_from_block(void* block) {
	return (span_t*)((uintptr_t)block & SPAN_MASK);
}

static chunk_t*
rpmalloc_chunk_from_span(span_t* span) {
	return (chunk_t*)((uintptr_t)span - (SPAN_SIZE * span->chunk_index));
}

///
/// Free list control
///

//! Pop head block off list and update head
static void*
rpmalloc_free_list_pop(void** list) {
	void* block = *list;
	*list = *((void**)block);
	return block;
}

//! Initialize a (partial) free list up to next system memory page, while reserving the first block
//  as allocated, returning number of blocks in list
static uint32_t
rpmalloc_free_list_partial_init(void** list, void** first_block, void* block_start,
                                uint32_t block_count, uint32_t block_size) {
	*first_block = block_start;
	if (block_count > 1) {
		void* free_block = pointer_offset(block_start, block_size);
		void* block_end = pointer_offset(block_start, (size_t)block_size * block_count);
		//If block size is less than half a memory page, bound init to next memory page boundary
		if (block_size < (os_page_size >> 1)) {
			void* page_end = pointer_offset(block_start, os_page_size);
			if (page_end < block_end)
				block_end = page_end;
		}
		*list = free_block;
		block_count = 2;
		void* next_block = pointer_offset(free_block, block_size);
		while (next_block < block_end) {
			*((void**)free_block) = next_block;
			free_block = next_block;
			++block_count;
			next_block = pointer_offset(next_block, block_size);
		}
		*((void**)free_block) = 0;
	} else {
		*list = 0;
	}
	return block_count;
}

///
/// Chunk control
///

static void
rpmalloc_span_double_link_list_with_tail_add(span_t** head, span_t* span);

//! Pop head chunk from double linked list
static void
rpmalloc_chunk_double_link_list_pop_head(chunk_t** head) {
	*head = (*head)->next;
}

//! Unlink chunk from double linked list
static void
rpmalloc_chunk_double_link_list_remove(chunk_t** head, chunk_t* chunk) {
	if (*head == chunk) {
		rpmalloc_chunk_double_link_list_pop_head(head);
		return;
	}
	chunk->prev->next = chunk->next;
	if (chunk->next)
		chunk->next->prev = chunk->prev;
}

//! Link chunk to double linked list
static void
rpmalloc_chunk_double_link_list_add(chunk_t** head, chunk_t* chunk) {
	if (*head)
		(*head)->prev = chunk;
	chunk->next = *head;
	*head = chunk;
}

//! Add a span as free in the chunk
static void
rpmalloc_chunk_add_free_span(chunk_t* chunk, span_t* span) {
	chunk->free_count += span->span_count;
	if (chunk->free_count == chunk->initialized_count) {
		if (chunk->state == CHUNK_STATE_PARTIAL)
			rpmalloc_chunk_double_link_list_remove(&chunk->heap->partial_chunk, chunk);
#if RPMALLOC_FIRST_CLASS_HEAPS
		else //if (chunk->state == CHUNK_STATE_FULL)
			rpmalloc_chunk_double_link_list_remove(&chunk->heap->full_chunk, chunk);
#endif
		rpmalloc_heap_free_chunk(chunk->heap, chunk);
		return;
	}
	//If chunk is previously fully used, add it to heap partial list
	if (chunk->state == CHUNK_STATE_FULL) {
		chunk->state = CHUNK_STATE_PARTIAL;
#if RPMALLOC_FIRST_CLASS_HEAPS
		rpmalloc_chunk_double_link_list_remove(&chunk->heap->full_chunk, chunk);
#endif
		rpmalloc_chunk_double_link_list_add(&chunk->heap->partial_chunk, chunk);
	}
	if (!chunk->free || (chunk->free->span_count >= span->span_count)) {
		rpmalloc_span_double_link_list_with_tail_add(&chunk->free, span);
	} else {
		span_t* tail = chunk->free->prev;
		if (tail->span_count <= span->span_count) {
			// Span is new tail
			tail->next = span;
			span->prev = tail;
			chunk->free->prev = span;
		} else {
			// Find correct slot in sorted list
			span_t* prev = tail->prev;
			while (prev->span_count > span->span_count)
				prev = prev->prev;
			span->next = prev->next;
			span->prev = prev;
			prev->next->prev = span;
			prev->next = span;
		}
	}
}

//! Update chunk state
static void
rpmalloc_chunk_check_transition_partial_to_full(chunk_t* chunk) {
	if ((chunk->state == CHUNK_STATE_PARTIAL) && !chunk->free && (chunk->initialized_count == SPAN_COUNT)) {
		chunk->state = CHUNK_STATE_FULL;
		rpmalloc_chunk_double_link_list_remove(&chunk->heap->partial_chunk, chunk);
#if RPMALLOC_FIRST_CLASS_HEAPS
		rpmalloc_chunk_double_link_list_add(&chunk->heap->full_chunk, chunk);
#endif
	}
}

///
/// Span control
///

//! Add a span to double linked list at the head
static void
rpmalloc_span_double_link_list_add(span_t** head, span_t* span) {
	if (*head) {
		span->next = *head;
		(*head)->prev = span;
	} else {
		span->next = 0;
	}
	*head = span;
}

//! Add a span to double linked list and maintain link to tail
static void
rpmalloc_span_double_link_list_with_tail_add(span_t** head, span_t* span) {
	//Maintain a link to the tail of the list
	span_t* tail = span;
	if (*head)
		tail = (*head)->prev;
	rpmalloc_span_double_link_list_add(head, span);
	span->prev = tail;
}

//! Pop head span from double linked list
static void
rpmalloc_span_double_link_list_pop_head(span_t** head) {
	*head = (*head)->next;
}

//! Pop head span from double linked list and maintain link to tail
static void
rpmalloc_span_double_link_list_with_tail_pop_head(span_t** head) {
	span_t* tail = (*head)->prev;
	rpmalloc_span_double_link_list_pop_head(head);
	if (*head)
		(*head)->prev = tail;
}

//! Remove a span from double linked list
static void
rpmalloc_span_double_link_list_remove(span_t** head, span_t* span) {
	if (*head == span) {
		rpmalloc_span_double_link_list_pop_head(head);
	} else {
		if (span->next)
			span->next->prev = span->prev;
		span->prev->next = span->next;
	}
}

//! Pop head span from double linked list and maintain link to tail
static void
rpmalloc_span_double_link_list_with_tail_remove(span_t** head, span_t* span) {
	if (*head == span) {
		rpmalloc_span_double_link_list_with_tail_pop_head(head);
	} else {
		span_t* tail = (*head)->prev;
		if (tail == span) {
			tail->prev->next = 0;
			(*head)->prev = tail->prev;
		} else {
			if (span->next)
				span->next->prev = span->prev;
			span->prev->next = span->next;
		}
	}
}

//! Check if span is fully used
static int
rpmalloc_span_is_fully_utilized(span_t* span) {
	return !span->free && (span->initialized_count >= span->block_count);
}

//! Get start of memory blocks for a span
static void*
rpmalloc_span_block_start(span_t* span) {
	return pointer_offset((uintptr_t)span, SPAN_HEADER_SIZE);
}

//! Split a large span
static span_t*
rpmalloc_span_large_split(span_t* span, uint32_t span_count) {
	span_t* remain = pointer_offset(span, span_count * SPAN_SIZE);
	remain->span_count = span->span_count - span_count;
	remain->chunk_index = span->chunk_index + (uint16_t)span_count;
	return remain;
}

//! Swap in the deferred free list from other thread deallocations
static void
rpmalloc_span_adopt_deferred_free(span_t* span) {
	// We need acquire semantics on the CAS operation since we are interested in the list size
	do {
		span->free = atomicptr_load(&span->free_defer);
	} while ((span->free == INVALID_POINTER) || !atomicptr_cas_acquire(&span->free_defer, INVALID_POINTER, span->free));
	span->used_count -= span->defer_size;
	span->defer_size = 0;
	atomicptr_store_release(&span->free_defer, 0);
}

//! Allocate a block from a partial span
static void*
rpmalloc_span_small_allocate(span_t* span, heap_t* heap) {
	void* block;
	if (!span->free && atomicptr_load(&span->free_defer))
		rpmalloc_span_adopt_deferred_free(span);
	if (span->free) {
		block = rpmalloc_free_list_pop(&span->free);
		heap->free[span->size_class].free = span->free;
		span->free = 0;
	} else {
		//If the span did not fully initialize free list, link up another page worth of blocks
		void* block_start = pointer_offset(rpmalloc_span_block_start(span), ((size_t)span->initialized_count * span->block_size));
		span->initialized_count += (uint16_t)rpmalloc_free_list_partial_init(
			&heap->free[span->size_class].free, &block, block_start,
			span->block_count - span->initialized_count, span->block_size);
	}
	span->used_count = span->initialized_count;

	//If the span is fully utilized, unlink it from partial list
	if (rpmalloc_span_is_fully_utilized(span))
		rpmalloc_span_double_link_list_pop_head(&heap->free[span->size_class].partial);
	return block;
}

//! Put the block in the deferred free list of the owning span
static void
rpmalloc_span_small_deallocate_defer(span_t* span, void* block) {
	// The memory ordering here is a bit tricky, to avoid having to ABA protect
	// the deferred free list to avoid desynchronization of list and list size
	// we need to have acquire semantics on successful CAS of the pointer to
	// guarantee the list_size variable validity + release semantics on pointer store
	void* free_list;
	do {
		free_list = atomicptr_load(&span->free_defer);
		*((void**)block) = free_list;
	} while ((free_list == INVALID_POINTER) || !atomicptr_cas_acquire(&span->free_defer, INVALID_POINTER, free_list));
	uint32_t free_count = ++span->defer_size;
	atomicptr_store_release(&span->free_defer, block);
	if (free_count == span->block_count) {
		// Span was completely freed by this block with everything in defer list.
		// Due to the INVALID_POINTER spin lock no other thread can reach this state
		// simultaneously on this span. Safe to move to owner heap deferred cache
		chunk_t* chunk = rpmalloc_chunk_from_span(span);
		rpmalloc_heap_defer_free_span(chunk->heap, span);
	}
	// TODO - devise a method to free this span in order to avoid it sitting
	//        completely free in partial list of owning chunk. Perhaps in
	//        conjunction with checking if heap is abandoned
	// else if (free_count == span->used_count) {
	//    ...
	// }
}

static void
rpmalloc_span_small_deallocate_direct(span_t* span, void* block) {
	//If span is fully utilized and free floating, add to list of partial spans for the size class
	if (rpmalloc_span_is_fully_utilized(span)) {
		chunk_t* chunk = rpmalloc_chunk_from_span(span);
		rpmalloc_span_double_link_list_add(&chunk->heap->free[span->size_class].partial, span);
	}
	//Add block to free list
	--span->used_count;
	*((void**)block) = span->free;
	span->free = block;
	//If span is completely free, remove from partial list for size class and add it to list of free spans
	if (span->used_count == span->defer_size) {
		chunk_t* chunk = rpmalloc_chunk_from_span(span);
		rpmalloc_span_double_link_list_remove(&chunk->heap->free[span->size_class].partial, span);
		rpmalloc_chunk_add_free_span(chunk, span);
	}
}

static void
rpmalloc_span_small_deallocate(span_t* span, void* block) {
	uintptr_t current_thread = rpmalloc_thread_id();
	int defer = (span->thread != current_thread);
	if (span->flags & SPAN_FLAG_ALIGNED_BLOCKS) {
		//Realign pointer to block start
		void* blocks_start = rpmalloc_span_block_start(span);
		uint32_t block_align_offset = (uint32_t)pointer_diff(block, blocks_start);
		block = pointer_offset(block, -(int32_t)(block_align_offset % span->block_size));
	}
	if (defer) {
		chunk_t* chunk = rpmalloc_chunk_from_span(span);
		if (chunk->heap->thread != current_thread) {
			rpmalloc_span_small_deallocate_defer(span, block);
			return;
		}
		span->thread = current_thread;
	}
	rpmalloc_span_small_deallocate_direct(span, block);
}

static void
rpmalloc_span_large_deallocate(span_t* span, void* block) {
	(void)sizeof(block);
	uintptr_t current_thread = rpmalloc_thread_id();
	int defer = (span->thread != current_thread);
	chunk_t* chunk = rpmalloc_chunk_from_span(span);
	if (defer) {
		if (chunk->heap->thread != current_thread) {
			rpmalloc_heap_defer_free_span(chunk->heap, span);
			return;
		}
		span->thread = current_thread;
	}
	rpmalloc_chunk_add_free_span(chunk, span);
}

static void
rpmalloc_span_huge_deallocate(span_t* span, void* block) {
	(void)sizeof(block);
	chunk_t* chunk = (chunk_t*)span;
	rpmalloc_unmap(chunk, chunk->mapped_size, chunk->mapped_offset, chunk->mapped_size);
}

///
/// Heap span control
///

//! Initialize a small span and allocate the first block
static void*
rpmalloc_heap_initialize_small_span(heap_t* heap, span_t* span, size_t chunk_index, uint32_t class_idx, uint32_t block_count, uint32_t block_size) {
	void* block;
	span->free = 0;
	span->used_count = (uint16_t)rpmalloc_free_list_partial_init(
		&heap->free[class_idx].free, &block, rpmalloc_span_block_start(span),
		block_count, block_size);
	span->defer_size = 0;
	span->block_count = (uint16_t)block_count;
	span->block_size = (uint16_t)block_size;
	span->initialized_count = span->used_count;
	span->chunk_index = (uint16_t)chunk_index;
	span->type = SPAN_TYPE_SMALL;
	span->size_class = class_idx;
	span->flags = 0;
	span->span_count = 1;
	span->thread = rpmalloc_thread_id();
	atomicptr_store(&span->free_defer, 0);
	span->prev = 0;
	span->next = 0;
	if (span->initialized_count < span->block_count)
		rpmalloc_span_double_link_list_add(&heap->free[class_idx].partial, span);
	return block;
}

//! Initialize a large span
static void
rpmalloc_heap_initialize_large_span(heap_t* heap, span_t* span, size_t chunk_index, uint32_t span_count) {
	(void)sizeof(heap);
	memset(span, 0, sizeof(span_t));
	span->chunk_index = (uint16_t)chunk_index;
	span->type = SPAN_TYPE_LARGE;
	span->span_count = span_count;
	span->thread = rpmalloc_thread_id();
}

//! Allocate a new small span (SPAN_TYPE_SMALL) from heap and allocate the first memory block from the span
static void*
rpmalloc_heap_allocate_small_span_and_block(heap_t* heap, uint32_t class_idx) {
retry:
	chunk_t* chunk = heap->partial_chunk;
	if (chunk) {
		span_t* span;
		uint32_t chunk_index;
		if (chunk->free) {
			//Utilize a free span before initializing more spans
			span = chunk->free;
			rpmalloc_span_double_link_list_with_tail_pop_head(&chunk->free);
			chunk->free_count -= span->span_count;
			if (span->span_count > 1) {
				//Split a large span
				span_t* remain = rpmalloc_span_large_split(span, 1);
				rpmalloc_chunk_add_free_span(chunk, remain);
			}
			chunk_index = span->chunk_index;
		} else {
			//Initialize a new span
			rpmalloc_assert(chunk->initialized_count < SPAN_COUNT);
			span = (span_t*)pointer_offset(chunk, SPAN_SIZE * chunk->initialized_count);
			chunk_index = chunk->initialized_count++;
		}
		rpmalloc_chunk_check_transition_partial_to_full(chunk);
		return rpmalloc_heap_initialize_small_span(heap, span, chunk_index, class_idx, size_class[class_idx].block_count, size_class[class_idx].block_size);
	}

	rpmalloc_heap_collect_free_span(heap);
	if (heap->partial_chunk)
		goto retry;

	chunk = rpmalloc_heap_allocate_chunk(heap);
	if (CHECK_NOT_NULL(chunk)) {
		span_t* span = (span_t*)chunk;
		chunk->initialized_count = 1;
		chunk->state = CHUNK_STATE_PARTIAL;
		rpmalloc_chunk_double_link_list_add(&heap->partial_chunk, chunk);
		return rpmalloc_heap_initialize_small_span(heap, span, 0, class_idx, size_class[class_idx].block_count, size_class[class_idx].block_size);
	}
	
	return 0;
}

//! Allocate a large span (SPAN_TYPE_LARGE) from heap
static span_t*
rpmalloc_heap_allocate_large_span_and_block(heap_t* heap, size_t size) {
	uint32_t span_count = (uint32_t)((size + SPAN_HEADER_SIZE + SPAN_SIZE - 1) >> SPAN_SHIFT);

	rpmalloc_heap_collect_free_span(heap);

	chunk_t* chunk = heap->partial_chunk;
	while (chunk) {
		if (chunk->free) {
			// Walk backwards from tail to find the best-fitting span, if any (or grab head if that is larger or equal in span count)
			span_t* best = chunk->free;
			if (best->span_count < span_count) {
				span_t* span = best->prev;
				while ((span->span_count > span_count) && (span != chunk->free)) {
					best = span;
					span = span->prev;
				}
			}
			if (best->span_count >= span_count) {
				span_t* span = best;
				rpmalloc_span_double_link_list_with_tail_remove(&chunk->free, span);
				chunk->free_count -= span->span_count;
				if (span->span_count > span_count) {
					//Split a large span
					span_t* remain = rpmalloc_span_large_split(span, span_count);
					rpmalloc_chunk_add_free_span(chunk, remain);
				}
				rpmalloc_heap_initialize_large_span(heap, span, span->chunk_index, span_count);
				rpmalloc_chunk_check_transition_partial_to_full(chunk);
				return rpmalloc_span_block_start(span);
			}
		}
		if ((SPAN_COUNT - chunk->initialized_count) >= span_count) {
			rpmalloc_assert((chunk->initialized_count + span_count) <= SPAN_COUNT);
			span_t* span = (span_t*)pointer_offset(chunk, SPAN_SIZE * chunk->initialized_count);
			rpmalloc_heap_initialize_large_span(heap, span, chunk->initialized_count, span_count);
			chunk->initialized_count += span_count;
			rpmalloc_chunk_check_transition_partial_to_full(chunk);
			return rpmalloc_span_block_start(span);
		}
		chunk = chunk->next;
	}

	chunk = rpmalloc_heap_allocate_chunk(heap);
	if (CHECK_NOT_NULL(chunk)) {
		span_t* span = (span_t*)chunk;
		// First span is truncated to accommodate chunk header
		span_count = (uint32_t)((size + CHUNK_HEADER_SIZE + SPAN_SIZE - 1) >> SPAN_SHIFT);
		rpmalloc_heap_initialize_large_span(heap, span, 0, span_count);
		chunk->initialized_count = span_count;
		if (chunk->initialized_count != SPAN_COUNT) {
			chunk->state = CHUNK_STATE_PARTIAL;
			rpmalloc_chunk_double_link_list_add(&heap->partial_chunk, chunk);
		} else {
			chunk->state = CHUNK_STATE_FULL;
#if RPMALLOC_FIRST_CLASS_HEAPS
			rpmalloc_chunk_double_link_list_add(&heap->full_chunk, chunk);
#endif
		}
		return rpmalloc_span_block_start(span);
	}

	return 0;
}

///
/// Heap control
///

//! Initialize a heap
static void
rpmalloc_heap_initialize(heap_t* heap) {
	memset(heap, 0, sizeof(heap_t));
	heap->id = 1 + atomicsize_incr(&heap_id);

	//Link in heap in heap ID map
	heap_t* next_heap;
	size_t list_idx = heap->id % HEAP_MAP_SIZE;
	do {
		next_heap = (heap_t*)atomicptr_load(&heap_map[list_idx]);
		heap->next = next_heap;
	} while (!atomicptr_cas(&heap_map[list_idx], heap, next_heap));
}

//! Orphan a heap
static void
rpmalloc_heap_orphan(heap_t* heap) {
	heap->thread = (uintptr_t)-1;
	heap_t* last_heap;
	void* raw_heap;
	do {
		last_heap = (heap_t*)atomicptr_load(&heap_orphan);
		heap->next_orphan = (heap_t*)((uintptr_t)last_heap & ~(uintptr_t)(ABA_SIZE - 1));
		uintptr_t orphan_counter = (uintptr_t)atomicsize_incr(&heap_orphan_counter);
		raw_heap = (void*)((uintptr_t)heap | (orphan_counter & (uintptr_t)(ABA_SIZE - 1)));
	} while (!atomicptr_cas(&heap_orphan, raw_heap, last_heap));
}

//! Allocate a new heap from newly mapped memory pages
static heap_t*
rpmalloc_mmap_heap(void) {
	size_t align_offset = 0;
	size_t block_size = ((sizeof(heap_t) + os_page_size - 1) / os_page_size) * os_page_size;
	heap_t* heap = (heap_t*)rpmalloc_mmap(block_size, &align_offset);
	if (!heap)
		return heap;

	rpmalloc_heap_initialize(heap);
	heap->align_offset = align_offset;

	//Put extra heaps as orphans
	size_t aligned_heap_size = ABA_SIZE * ((sizeof(heap_t) + ABA_SIZE - 1) / ABA_SIZE);
	size_t num_heaps = block_size / aligned_heap_size;
	atomicsize_store(&heap->child_count, num_heaps ? num_heaps - 1 : 0);
	heap_t* extra_heap = (heap_t*)pointer_offset(heap, aligned_heap_size);
	while (num_heaps > 1) {
		rpmalloc_heap_initialize(extra_heap);
		extra_heap->master = heap;
		rpmalloc_heap_orphan(extra_heap);
		extra_heap = (heap_t*)pointer_offset(extra_heap, aligned_heap_size);
		--num_heaps;
	}
	return heap;
}

//! Get a heap, either an orphan or a fresh mmapped heap
static heap_t*
rpmalloc_allocate_heap(void) {
	void* raw_heap;
	void* next_raw_heap;
	heap_t* heap;
	do {
		raw_heap = atomicptr_load(&heap_orphan);
		heap = (heap_t*)((uintptr_t)raw_heap & ~(uintptr_t)(ABA_SIZE - 1));
		if (!heap)
			return rpmalloc_mmap_heap();
		uintptr_t orphan_counter = (uintptr_t)atomicsize_incr(&heap_orphan_counter);
		next_raw_heap = (void*)((uintptr_t)heap->next_orphan | (orphan_counter & (uintptr_t)(ABA_SIZE - 1)));
	} while (!atomicptr_cas(&heap_orphan, next_raw_heap, raw_heap));
	return heap;
}

///
/// Main heap allocator entry points
///

//! Allocate a small or medium sized memory block from the given heap
static void*
rpmalloc_heap_allocate_small_medium(heap_t* heap, uint32_t class_idx) {
	free_t* free_data = heap->free + class_idx;
	if (free_data->free)
		return rpmalloc_free_list_pop(&free_data->free);
	if (free_data->partial)
		return rpmalloc_span_small_allocate(free_data->partial, heap);
	return rpmalloc_heap_allocate_small_span_and_block(heap, class_idx);
}

//! Allocate a large sized memory block from the given heap
static void*
rpmalloc_heap_allocate_large(heap_t* heap, size_t size) {
	return rpmalloc_heap_allocate_large_span_and_block(heap, size);
}

//! Allocate a huge sized memory block from the given heap
static void*
rpmalloc_heap_allocate_huge(heap_t* heap, size_t size) {
	(void)sizeof(heap);
	// Huge blocks are mmap:ed directly
	size_t offset = 0;
	size_t span_count = (size + CHUNK_HEADER_SIZE + SPAN_SIZE - 1) >> SPAN_SHIFT;
	size = SPAN_SIZE * span_count;
	span_t* span = rpmalloc_mmap(size, &offset);
	if (CHECK_NULL(span)) {
		errno = ENOMEM;
		return 0;
	}
	span->type = SPAN_TYPE_HUGE;
	chunk_t* chunk = (chunk_t*)span;
	chunk->mapped_offset = (uint32_t)offset;
	chunk->mapped_size = size;
	return rpmalloc_span_block_start(span);
}

//! Allocate any sized block from the given heap
static void*
rpmalloc_heap_allocate_block(heap_t* heap, size_t size) {
	rpmalloc_assert(heap);
	if (size <= SMALL_SIZE_LIMIT) {
		//Small sizes have unique size classes
		const uint32_t class_idx = (uint32_t)((size + (SMALL_GRANULARITY - 1)) >> SMALL_GRANULARITY_SHIFT);
		return rpmalloc_heap_allocate_small_medium(heap, class_idx);
	}
	if (size <= MEDIUM_SIZE_LIMIT) {
		//Calculate the size class index and do a dependent lookup of the final class index (in case of merged classes)
		const uint32_t class_idx = medium_class_map[(size - (SMALL_SIZE_LIMIT + 1)) >> MEDIUM_GRANULARITY_SHIFT];
		return rpmalloc_heap_allocate_small_medium(heap, class_idx);
	}
	if (size <= LARGE_SIZE_LIMIT)
		return rpmalloc_heap_allocate_large(heap, size);
	return rpmalloc_heap_allocate_huge(heap, size);
}

//! Deallocate the given block
static void
rpmalloc_deallocate_block(void* block) {
	span_t* span = rpmalloc_span_from_block(block);
	if (span) {
		if (span->type == SPAN_TYPE_SMALL)
			rpmalloc_span_small_deallocate(span, block);
		else if (span->type == SPAN_TYPE_LARGE)
			rpmalloc_span_large_deallocate(span, block);
		else
			rpmalloc_span_huge_deallocate(span, block);
	}
}

//! Collect free spans from the list of deferred free spans by other threads
static void
rpmalloc_heap_collect_free_span(heap_t* heap) {
	if (heap->free_span_deferred) {
		//This list does not need ABA protection, no mutable side state
		span_t* span = atomicptr_exchange(&heap->free_span_deferred, 0);
		while (span) {
			chunk_t* chunk = rpmalloc_chunk_from_span(span);
			span_t* next = span->next_deferred_span;	
			rpmalloc_chunk_add_free_span(chunk, span);
			span = next;
		}
	}
}

//! Put a free span owned by another thread on the list of deferred free spans for the heap
static void
rpmalloc_heap_defer_free_span(heap_t* heap, span_t* span) {
	//This list does not need ABA protection, no mutable side state
	do {
		span->next_deferred_span = atomicptr_load(&heap->free_span_deferred);
	} while (!atomicptr_cas(&heap->free_span_deferred, span, span->next_deferred_span));
}

//! Allocate block with alignment
static void*
rpmalloc_heap_aligned_allocate_block(heap_t* heap, size_t alignment, size_t size) {
	if (alignment <= SMALL_GRANULARITY)
		return rpmalloc_heap_allocate_block(heap, size);

	size_t total;
	rpmalloc_validate_alignment(alignment);
	rpmalloc_safe_add(size, alignment, total);
	rpmalloc_validate_size(size);
	if ((alignment <= SPAN_HEADER_SIZE) && (size < MEDIUM_SIZE_LIMIT)) {
		// If alignment is less or equal to span header size (which is power of two),
		// and size aligned to span header size multiples is less than size + alignment,
		// then use natural alignment of blocks to provide alignment
		size_t multiple_size = size ? (size + (SPAN_HEADER_SIZE - 1)) & ~(uintptr_t)(SPAN_HEADER_SIZE - 1) : SPAN_HEADER_SIZE;
		rpmalloc_assert(!(multiple_size % SPAN_HEADER_SIZE));
		if (multiple_size <= total)
			return rpmalloc_heap_allocate_block(heap, multiple_size);
	}

	void* block;
	size_t align_mask = alignment - 1;
	if (alignment <= os_page_size) {
		block = rpmalloc_heap_allocate_block(heap, total);
		if ((uintptr_t)block & align_mask) {
			//Mark as having aligned blocks
			span_t* span = rpmalloc_span_from_block(block);
			span->flags |= SPAN_FLAG_ALIGNED_BLOCKS;
			block = (void*)(((uintptr_t)block & ~(uintptr_t)align_mask) + alignment);
		}
		return block;
	}

	// Fallback to mapping new pages for this request. Since pointers passed
	// to rpfree must be able to reach the start of the span by bitmasking of
	// the address with the span size, the returned aligned pointer from this
	// function must be with a span size of the start of the mapped area.
	// In worst case this requires us to loop and map pages until we get a
	// suitable memory address. It also means we can never align to span size
	// or greater, since the span header will push alignment more than one
	// span size away from span start (thus causing pointer mask to give us
	// an invalid span start on free)
	if (alignment & align_mask) {
		errno = EINVAL;
		return 0;
	}
	if (alignment >= SPAN_SIZE) {
		errno = EINVAL;
		return 0;
	}

	// Since each span has a header, we will at least need one extra memory page
	size_t extra_pages = alignment / os_page_size;
	size_t num_pages = (size + CHUNK_HEADER_SIZE + os_page_size - 1) / os_page_size;
	if (extra_pages > num_pages)
		num_pages = 1 + extra_pages;

	size_t original_pages = num_pages;
	size_t limit_pages = (SPAN_SIZE / os_page_size) * 2;
	if (limit_pages < (original_pages * 2))
		limit_pages = original_pages * 2;

	size_t mapped_size, align_offset;
	span_t* span;

	do {
		align_offset = 0;
		mapped_size = num_pages * os_page_size;

		span = rpmalloc_mmap(mapped_size, &align_offset);
		if (CHECK_NULL(span)) {
			errno = ENOMEM;
			return 0;
		}
		block = pointer_offset(span, SPAN_HEADER_SIZE);

		if ((uintptr_t)block & align_mask)
			block = (void*)(((uintptr_t)block & ~(uintptr_t)align_mask) + alignment);

		if (((size_t)pointer_diff(block, span) >= SPAN_SIZE) ||
			(pointer_offset(block, size) > pointer_offset(span, mapped_size)) ||
			(((uintptr_t)block & SPAN_MASK) != (uintptr_t)span)) {
			rpmalloc_unmap(span, mapped_size, align_offset, mapped_size);
			++num_pages;
			if (num_pages > limit_pages) {
				errno = EINVAL;
				return 0;
			}
			span = 0;
		}
	} while (!span);

	span->type = SPAN_TYPE_HUGE;
	chunk_t* chunk = (chunk_t*)span;
	chunk->mapped_offset = (uint32_t)align_offset;
	chunk->mapped_size = mapped_size;

	return block;
}

//! Reallocate the given block to the given size
static void*
rpmalloc_heap_reallocate_block(heap_t* heap, void* block, size_t size, size_t oldsize, unsigned int flags) {
	if (block) {
		span_t* span = rpmalloc_span_from_block(block);
		if (span->type == SPAN_TYPE_SMALL) {
			//Small/medium sized block
			void* blocks_start = rpmalloc_span_block_start(span);
			uint32_t block_offset = (uint32_t)pointer_diff(block, blocks_start);
			uint32_t block_idx = block_offset / span->block_size;
			void* actual_block = pointer_offset(blocks_start, (size_t)block_idx * span->block_size);
			if (!oldsize)
				oldsize = (size_t)((ptrdiff_t)span->block_size - pointer_diff(block, actual_block));
			if ((size_t)span->block_size >= size) {
				//Still fits in block, never mind trying to save memory, preserve data if alignment changed
				if ((block != actual_block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(actual_block, block, oldsize);
				return actual_block;
			}
		} else if (span->type == SPAN_TYPE_LARGE) {
			//Large block
			size_t current_size = ((size_t)span->span_count * SPAN_SIZE) - SPAN_HEADER_SIZE;
			void* actual_block = rpmalloc_span_block_start(span);
			if (!oldsize)
				oldsize = current_size - (size_t)pointer_diff(block, actual_block);
			if ((current_size >= size) && (size >= (current_size >> 1))) {
				//Still fits in block and not wasting more than half the block, preserve data if alignment changed
				if ((block != actual_block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(actual_block, block, oldsize);
				return actual_block;
			}
		} else {
			//Huge block
			chunk_t* chunk = (chunk_t*)span;
			size_t current_size = chunk->mapped_size - CHUNK_HEADER_SIZE;
			void* actual_block = pointer_offset(span, CHUNK_HEADER_SIZE);
			if (!oldsize)
				oldsize = current_size - (size_t)pointer_diff(block, actual_block);
			if ((current_size >= size) && (size >= (current_size >> 1))) {
				//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((block != actual_block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(actual_block, block, oldsize);
				return actual_block;
			}
		}
	} else {
		oldsize = 0;
	}

	if (!!(flags & RPMALLOC_GROW_OR_FAIL))
	{
		if (oldsize >= size)
			return block;
		return 0;
	}

	//Size is greater than block size or small enough to warrant reallocation,
	//need to allocate a new block and deallocate the old.
	//Avoid hysteresis by overallocating if increase is small (below 37%)
	size_t lower_bound = oldsize + (oldsize >> 2) + (oldsize >> 3);
	size_t new_size = (size > lower_bound) ? size : ((size > oldsize) ? lower_bound : size);
	void* new_block = rpmalloc_heap_allocate_block(heap, new_size);
	if (block && new_block) {
		if (!(flags & RPMALLOC_NO_PRESERVE))
			memcpy(new_block, block, oldsize < new_size ? oldsize : new_size);
		rpmalloc_deallocate_block(block);
	}
	return new_block;
}

//! Reallocate a block with alignment
static void*
rpmalloc_heap_aligned_reallocate_block(heap_t* heap, void* block, size_t alignment, size_t size, size_t oldsize,
                                       unsigned int flags) {
	if (alignment <= SMALL_GRANULARITY)
		return rpmalloc_heap_reallocate_block(heap, block, size, oldsize, flags);

	int no_alloc = !!(flags & RPMALLOC_GROW_OR_FAIL);
	size_t usable_size = rpmalloc_usable_size(block);
	if ((usable_size >= size) && !((uintptr_t)block & (alignment - 1))) {
		if (no_alloc || (size >= (usable_size / 2)))
			return block;
	}
	// Aligned alloc marks span as having aligned blocks
	void* new_block = (!no_alloc ? rpmalloc_heap_aligned_allocate_block(heap, alignment, size) : 0);
	if (new_block) {
		if (!(flags & RPMALLOC_NO_PRESERVE) && block) {
			if (!oldsize)
				oldsize = usable_size;
			memcpy(new_block, block, oldsize < size ? oldsize : size);
		}
		rpfree(block);
	}
	return new_block;
}

//! Allocate a new chunk, either from free list or by mapping more virtual memory
static chunk_t*
rpmalloc_heap_allocate_chunk(heap_t* heap) {
	size_t offset;
	chunk_t* chunk;
#if ENABLE_THREAD_CACHE
	chunk = heap->free_chunk;
	if (chunk) {
		offset = chunk->mapped_offset;
		heap->free_chunk = chunk->next;
		--heap->free_chunk_count;
		rpmalloc_assert(heap->free_chunk || !heap->free_chunk_count);
	} else {
#endif
#if ENABLE_GLOBAL_CACHE
		chunk = rpmalloc_global_cache_pop();
		if (chunk) {
			offset = chunk->mapped_offset;
		} else {
#endif
			offset = 0;
			chunk = rpmalloc_mmap(CHUNK_SIZE, &offset);
#if ENABLE_GLOBAL_CACHE
		}
#endif
#if ENABLE_THREAD_CACHE
	}
#endif
	if (CHECK_NOT_NULL(chunk)) {
		chunk->heap = heap;
		chunk->free = 0;
		chunk->free_count = 0;
		chunk->initialized_count = 0;
		chunk->mapped_offset = (uint32_t)offset;
		chunk->mapped_size = CHUNK_SIZE;
	}
	return chunk;
}

//! Free a chunk
static void
rpmalloc_heap_free_chunk(heap_t* heap, chunk_t* chunk) {
	(void)sizeof(heap);
#if ENABLE_THREAD_CACHE
	rpmalloc_assert(heap->free_chunk || !heap->free_chunk_count);
	if (heap->free_chunk_count < THREAD_CACHE_MAX_CHUNKS) {
		chunk->state = CHUNK_STATE_FREE;
		chunk->next = heap->free_chunk;
		heap->free_chunk = chunk;
		++heap->free_chunk_count;
		return;
	}
#endif
#if ENABLE_GLOBAL_CACHE
	rpmalloc_global_cache_push(chunk);
#else
	rpmalloc_unmap(chunk, chunk->mapped_size, chunk->mapped_offset, chunk->mapped_size);
#endif
}

///
/// Other functions
///

//! Calculate size class properties for the given class
static void
rpmalloc_size_class_calc(size_t iclass) {
	size_class[iclass].block_count = (uint16_t)((SPAN_SIZE - SPAN_HEADER_SIZE) / (size_t)size_class[iclass].block_size);
}

//! Adjust and optimize the size class properties for the given class
static void
rpmalloc_medium_size_class_adjust(size_t iclass) {
	rpmalloc_size_class_calc(iclass);
	medium_class_map[iclass - SMALL_CLASS_COUNT] = (uint16_t)iclass;

	//Check if previous size classes can be merged
	size_t prevclass = iclass - 1;
	while (prevclass >= SMALL_CLASS_COUNT) {
		//A class can be merged if number of blocks are equal
		if (size_class[prevclass].block_count != size_class[iclass].block_count)
			break;
		size_class[prevclass].block_size = size_class[iclass].block_size;
		medium_class_map[prevclass - SMALL_CLASS_COUNT] = (uint16_t)iclass;
		--prevclass;
	}
}

///
/// Extern interface
///

extern void
rpmalloc_thread_initialize(void) {
	if (rpmalloc_thread_heap_raw())
		return;
	rpmalloc_thread_heap_set(rpmalloc_allocate_heap());
}

void
rpmalloc_thread_finalize(void) {
	heap_t* heap = rpmalloc_thread_heap_raw();
	if (!heap)
		return;

	rpmalloc_heap_collect_free_span(heap);
	rpmalloc_thread_collect();

	rpmalloc_heap_orphan(heap);
	rpmalloc_thread_heap_set(0);
}

extern void
rpmalloc_thread_collect() {
	heap_t* heap = rpmalloc_thread_heap_raw();
	if (!heap)
		return;

#if ENABLE_THREAD_CACHE
	chunk_t* chunk = heap->free_chunk;
	while (chunk) {
		chunk_t* next = chunk->next;
#if ENABLE_GLOBAL_CACHE
		rpmalloc_global_cache_push(chunk);
#else
		rpmalloc_unmap(chunk, chunk->mapped_size, chunk->mapped_offset, chunk->mapped_size);
#endif
		chunk = next;
	}
	heap->free_chunk = 0;
	heap->free_chunk_count = 0;
#endif
}

extern int
rpmalloc_initialize() {
#ifdef _WIN32
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	os_page_size = system_info.dwPageSize;
	os_mmap_granularity = system_info.dwAllocationGranularity;
#else
	os_page_size = (size_t)sysconf(_SC_PAGESIZE);
	os_mmap_granularity = os_page_size;
#endif
	if (os_page_size < ABA_SIZE)
		os_page_size = ABA_SIZE;
	if (os_mmap_granularity < os_page_size)
		os_mmap_granularity = os_page_size;

	int use_huge_pages = 0;
	if (use_huge_pages) {
#ifdef _WIN32
		HANDLE token = 0;
		size_t large_page_minimum = GetLargePageMinimum();
		if (large_page_minimum > os_page_size)
			OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
		if (token) {
			LUID luid;
			if (LookupPrivilegeValue(0, SE_LOCK_MEMORY_NAME, &luid)) {
				TOKEN_PRIVILEGES token_privileges;
				memset(&token_privileges, 0, sizeof(token_privileges));
				token_privileges.PrivilegeCount = 1;
				token_privileges.Privileges[0].Luid = luid;
				token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				if (AdjustTokenPrivileges(token, FALSE, &token_privileges, 0, 0, 0)) {
					DWORD err = GetLastError();
					if (err == ERROR_SUCCESS) {
						os_huge_pages = 1;
					 	os_huge_page_size = large_page_minimum;
					}
				}
			}
			CloseHandle(token);
		}
#endif
	} else {
		os_huge_page_size = 0;
		os_huge_pages = 0;
	}

	os_page_size_shift = 0;
	size_t page_size_bit = os_page_size;
	while (page_size_bit != 1) {
		++os_page_size_shift;
		page_size_bit >>= 1;
	}

	size_t iclass = 0;
	size_t size = SMALL_GRANULARITY;
	size_class[0].block_size = (uint16_t)size;
	rpmalloc_size_class_calc(0);
	for (iclass = 1; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_class[iclass].block_size = (uint16_t)size;
		rpmalloc_size_class_calc(iclass);
		size += SMALL_GRANULARITY;
	}
	size = SMALL_SIZE_LIMIT + MEDIUM_GRANULARITY;
	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_class[SMALL_CLASS_COUNT + iclass].block_size = (uint16_t)size;
		rpmalloc_medium_size_class_adjust(SMALL_CLASS_COUNT + iclass);
		size += MEDIUM_GRANULARITY;
	}

	rpmalloc_thread_initialize();
	return 0;
}

extern void
rpmalloc_finalize(void) {
	rpmalloc_thread_finalize();
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc(size_t size) {
	rpmalloc_validate_size(size);
	return rpmalloc_heap_allocate_block(rpmalloc_thread_heap(), size);
}

extern inline void
rpfree(void* ptr) {
	rpmalloc_deallocate_block(ptr);
}

extern inline RPMALLOC_ALLOCATOR void*
rpcalloc(size_t num, size_t size) {
	size_t total;
	rpmalloc_safe_mult(num, size, total);
	rpmalloc_validate_size(total);
	void* block = rpmalloc_heap_allocate_block(rpmalloc_thread_heap(), total);
	if (block)
		memset(block, 0, total);
	return block;
}

extern inline RPMALLOC_ALLOCATOR void*
rprealloc(void* ptr, size_t size) {
	rpmalloc_validate_size(size);
	return rpmalloc_heap_reallocate_block(rpmalloc_thread_heap(), ptr, size, 0, 0);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_realloc(void* ptr, size_t alignment, size_t size, size_t oldsize, unsigned int flags) {
	size_t total;
	rpmalloc_safe_add(size, alignment, total);
	rpmalloc_validate_size(total);
	rpmalloc_validate_alignment(alignment);
	return rpmalloc_heap_aligned_reallocate_block(rpmalloc_thread_heap(), ptr, alignment, size, oldsize, flags);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_alloc(size_t alignment, size_t size) {
	rpmalloc_validate_size(size);
	rpmalloc_validate_alignment(alignment);
	return rpmalloc_heap_aligned_allocate_block(rpmalloc_thread_heap(), alignment, size);
}

extern inline RPMALLOC_ALLOCATOR void*
rpaligned_calloc(size_t alignment, size_t num, size_t size) {
	size_t total;
	rpmalloc_safe_mult(num, size, total);
	rpmalloc_validate_size(total);
	rpmalloc_validate_alignment(alignment);
	void* block = rpmalloc_heap_aligned_allocate_block(rpmalloc_thread_heap(), alignment, total);
	if (block)
		memset(block, 0, total);
	return block;
}

extern inline RPMALLOC_ALLOCATOR void*
rpmemalign(size_t alignment, size_t size) {
	rpmalloc_validate_size(size);
	rpmalloc_validate_alignment(alignment);
	return rpmalloc_heap_aligned_allocate_block(rpmalloc_thread_heap(), alignment, size);
}

extern inline int
rpposix_memalign(void **memptr, size_t alignment, size_t size) {
	rpmalloc_validate_size(size);
	rpmalloc_validate_alignment(alignment);
	if (memptr)
		*memptr = rpmalloc_heap_aligned_allocate_block(rpmalloc_thread_heap(), alignment, size);
	else
		return EINVAL;
	return *memptr ? 0 : ENOMEM;
}

extern size_t
rpmalloc_usable_size(void* block) {
	if (!block)
		return 0;
	//Grab the chunk using guaranteed chunk alignment
	span_t* span = rpmalloc_span_from_block(block);
	if (span->type == SPAN_TYPE_SMALL) {
		//Small/medium block
		void* blocks_start = rpmalloc_span_block_start(span);
		return span->block_size - ((size_t)pointer_diff(block, blocks_start) % span->block_size);
	}
	if (span->type == SPAN_TYPE_LARGE) {
		//Large block
		void* blocks_start = rpmalloc_span_block_start(span);
		return (span->span_count * SPAN_SIZE) - (size_t)pointer_diff(block, blocks_start);
	}
	chunk_t* chunk = (chunk_t*)span;
	void* blocks_start = rpmalloc_span_block_start(span);
	return chunk->mapped_size - (size_t)pointer_diff(block, blocks_start);
}

#if ENABLE_PRELOAD || ENABLE_OVERRIDE
#include "malloc.c"
#endif



#if 0

/// Build time configurable limits
#ifndef HEAP_ARRAY_SIZE
//! Size of heap hashmap
#define HEAP_ARRAY_SIZE           47
#endif
#ifndef ENABLE_THREAD_CACHE
//! Enable per-thread cache
#define ENABLE_THREAD_CACHE       1
#endif
#ifndef ENABLE_GLOBAL_CACHE
//! Enable global cache shared between all threads, requires thread cache
#define ENABLE_GLOBAL_CACHE       1
#endif
#ifndef ENABLE_VALIDATE_ARGS
//! Enable validation of args to public entry points
#define ENABLE_VALIDATE_ARGS      0
#endif
#ifndef ENABLE_STATISTICS
//! Enable statistics collection
#define ENABLE_STATISTICS         0
#endif
#ifndef ENABLE_ASSERTS
//! Enable asserts
#define ENABLE_ASSERTS            0
#endif
#ifndef ENABLE_OVERRIDE
//! Override standard library malloc/free and new/delete entry points
#define ENABLE_OVERRIDE           0
#endif
#ifndef ENABLE_PRELOAD
//! Support preloading
#define ENABLE_PRELOAD            0
#endif
#ifndef DISABLE_UNMAP
//! Disable unmapping memory pages
#define DISABLE_UNMAP             0
#endif
#ifndef DEFAULT_SPAN_MAP_COUNT
//! Default number of spans to map in call to map more virtual memory (default values yield 4MiB here)
#define DEFAULT_SPAN_MAP_COUNT    64
#endif

#if ENABLE_THREAD_CACHE
#ifndef ENABLE_UNLIMITED_CACHE
//! Unlimited thread and global cache
#define ENABLE_UNLIMITED_CACHE    0
#endif
#ifndef ENABLE_UNLIMITED_THREAD_CACHE
//! Unlimited cache disables any thread cache limitations
#define ENABLE_UNLIMITED_THREAD_CACHE ENABLE_UNLIMITED_CACHE
#endif
#if !ENABLE_UNLIMITED_THREAD_CACHE
#ifndef THREAD_CACHE_MULTIPLIER
//! Multiplier for thread cache (cache limit will be span release count multiplied by this value)
#define THREAD_CACHE_MULTIPLIER 16
#endif
#ifndef ENABLE_ADAPTIVE_THREAD_CACHE
//! Enable adaptive size of per-thread cache (still bounded by THREAD_CACHE_MULTIPLIER hard limit)
#define ENABLE_ADAPTIVE_THREAD_CACHE  0
#endif
#endif
#endif

#if ENABLE_GLOBAL_CACHE && ENABLE_THREAD_CACHE
#if DISABLE_UNMAP
#undef ENABLE_UNLIMITED_GLOBAL_CACHE
#define ENABLE_UNLIMITED_GLOBAL_CACHE 1
#endif
#ifndef ENABLE_UNLIMITED_GLOBAL_CACHE
//! Unlimited cache disables any global cache limitations
#define ENABLE_UNLIMITED_GLOBAL_CACHE ENABLE_UNLIMITED_CACHE
#endif
#if !ENABLE_UNLIMITED_GLOBAL_CACHE
//! Multiplier for global cache (cache limit will be span release count multiplied by this value)
#define GLOBAL_CACHE_MULTIPLIER (THREAD_CACHE_MULTIPLIER * 6)
#endif
#else
#  undef ENABLE_GLOBAL_CACHE
#  define ENABLE_GLOBAL_CACHE 0
#endif

#if !ENABLE_THREAD_CACHE || ENABLE_UNLIMITED_THREAD_CACHE
#  undef ENABLE_ADAPTIVE_THREAD_CACHE
#  define ENABLE_ADAPTIVE_THREAD_CACHE 0
#endif

#if DISABLE_UNMAP && !ENABLE_GLOBAL_CACHE
#  error Must use global cache if unmap is disabled
#endif

#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( _WIN64 )
#  define PLATFORM_WINDOWS 1
#  define PLATFORM_POSIX 0
#else
#  define PLATFORM_WINDOWS 0
#  define PLATFORM_POSIX 1
#endif

/// Platform and arch specifics
#if defined(_MSC_VER) && !defined(__clang__)
#  ifndef FORCEINLINE
#    define FORCEINLINE inline __forceinline
#  endif
#  define _Static_assert static_assert
#else
#  ifndef FORCEINLINE
#    define FORCEINLINE inline __attribute__((__always_inline__))
#  endif
#endif
#if PLATFORM_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef __USE_MINGW_ANSI_STDIO
#  define __USE_MINGW_ANSI_STDIO 1
#  endif
#  include <windows.h>
#  if ENABLE_VALIDATE_ARGS
#    include <Intsafe.h>
#  endif
#else
#  include <unistd.h>
#  include <stdio.h>
#  include <stdlib.h>
#  if defined(__APPLE__)
#    include <mach/mach_vm.h>
#    include <mach/vm_statistics.h>
#    include <pthread.h>
#  endif
#  if defined(__HAIKU__)
#    include <OS.h>
#    include <pthread.h>
#  endif
#endif

#include <stdint.h>
#include <string.h>
#include <errno.h>

#if ENABLE_ASSERTS
#  undef NDEBUG
#  if defined(_MSC_VER) && !defined(_DEBUG)
#    define _DEBUG
#  endif
#  include <assert.h>
#else
#  undef  assert
#  define assert(x) do {} while(0)
#endif
#if ENABLE_STATISTICS
#  include <stdio.h>
#endif

/// Atomic access abstraction
#if defined(_MSC_VER) && !defined(__clang__)

typedef volatile long      atomic32_t;
typedef volatile long long atomic64_t;
typedef volatile void*     atomicptr_t;

static FORCEINLINE int32_t atomic_load32(atomic32_t* src) { return *src; }
static FORCEINLINE void    atomic_store32(atomic32_t* dst, int32_t val) { *dst = val; }
static FORCEINLINE int32_t atomic_incr32(atomic32_t* val) { return (int32_t)_InterlockedIncrement(val); }
static FORCEINLINE int32_t atomic_decr32(atomic32_t* val) { return (int32_t)_InterlockedDecrement(val); }
#if ENABLE_STATISTICS || ENABLE_ADAPTIVE_THREAD_CACHE
static FORCEINLINE int64_t atomic_load64(atomic64_t* src) { return *src; }
static FORCEINLINE int64_t atomic_add64(atomic64_t* val, int64_t add) { return (int64_t)_InterlockedExchangeAdd64(val, add) + add; }
#endif
static FORCEINLINE int32_t atomic_add32(atomic32_t* val, int32_t add) { return (int32_t)_InterlockedExchangeAdd(val, add) + add; }
static FORCEINLINE void*   atomic_load_ptr(atomicptr_t* src) { return (void*)*src; }
static FORCEINLINE void    atomic_store_ptr(atomicptr_t* dst, void* val) { *dst = val; }
static FORCEINLINE void    atomic_store_ptr_release(atomicptr_t* dst, void* val) { *dst = val; }
static FORCEINLINE int     atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) { return (_InterlockedCompareExchangePointer((void* volatile*)dst, val, ref) == ref) ? 1 : 0; }
static FORCEINLINE int     atomic_cas_ptr_acquire(atomicptr_t* dst, void* val, void* ref) { return atomic_cas_ptr(dst, val, ref); }

#define EXPECTED(x) (x)
#define UNEXPECTED(x) (x)

#else

#include <stdatomic.h>

typedef volatile _Atomic(int32_t) atomic32_t;
typedef volatile _Atomic(int64_t) atomic64_t;
typedef volatile _Atomic(void*) atomicptr_t;

static FORCEINLINE int32_t atomic_load32(atomic32_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static FORCEINLINE void    atomic_store32(atomic32_t* dst, int32_t val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static FORCEINLINE int32_t atomic_incr32(atomic32_t* val) { return atomic_fetch_add_explicit(val, 1, memory_order_relaxed) + 1; }
static FORCEINLINE int32_t atomic_decr32(atomic32_t* val) { return atomic_fetch_add_explicit(val, -1, memory_order_relaxed) - 1; }
#if ENABLE_STATISTICS || ENABLE_ADAPTIVE_THREAD_CACHE
static FORCEINLINE int64_t atomic_load64(atomic64_t* val) { return atomic_load_explicit(val, memory_order_relaxed); }
static FORCEINLINE int64_t atomic_add64(atomic64_t* val, int64_t add) { return atomic_fetch_add_explicit(val, add, memory_order_relaxed) + add; }
#endif
static FORCEINLINE int32_t atomic_add32(atomic32_t* val, int32_t add) { return atomic_fetch_add_explicit(val, add, memory_order_relaxed) + add; }
static FORCEINLINE void*   atomic_load_ptr(atomicptr_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static FORCEINLINE void    atomic_store_ptr(atomicptr_t* dst, void* val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static FORCEINLINE void    atomic_store_ptr_release(atomicptr_t* dst, void* val) { atomic_store_explicit(dst, val, memory_order_release); }
static FORCEINLINE int     atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_relaxed, memory_order_relaxed); }
static FORCEINLINE int     atomic_cas_ptr_acquire(atomicptr_t* dst, void* val, void* ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_acquire, memory_order_relaxed); }

#define EXPECTED(x) __builtin_expect((x), 1)
#define UNEXPECTED(x) __builtin_expect((x), 0)
    
#endif

/// Preconfigured limits and sizes
//! Granularity of a small allocation block (must be power of two)
#define SMALL_GRANULARITY         16
//! Small granularity shift count
#define SMALL_GRANULARITY_SHIFT   4
//! Number of small block size classes
#define SMALL_CLASS_COUNT         65
//! Maximum size of a small block
#define SMALL_SIZE_LIMIT          (SMALL_GRANULARITY * (SMALL_CLASS_COUNT - 1))
//! Granularity of a medium allocation block
#define MEDIUM_GRANULARITY        512
//! Medium granularity shift count
#define MEDIUM_GRANULARITY_SHIFT  9
//! Number of medium block size classes
#define MEDIUM_CLASS_COUNT        61
//! Total number of small + medium size classes
#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)
//! Number of large block size classes
#define LARGE_CLASS_COUNT         32
//! Maximum size of a medium block
#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY * MEDIUM_CLASS_COUNT))
//! Maximum size of a large block
#define LARGE_SIZE_LIMIT          ((LARGE_CLASS_COUNT * _memory_span_size) - SPAN_HEADER_SIZE)
//! ABA protection size in orhpan heap list (also becomes limit of smallest page size)
#define HEAP_ORPHAN_ABA_SIZE      512
//! Size of a span header (must be a multiple of SMALL_GRANULARITY and a power of two)
#define SPAN_HEADER_SIZE          128

_Static_assert((SMALL_GRANULARITY & (SMALL_GRANULARITY - 1)) == 0, "Small granularity must be power of two");
_Static_assert((SPAN_HEADER_SIZE & (SPAN_HEADER_SIZE - 1)) == 0, "Span header size must be power of two");

#if ENABLE_VALIDATE_ARGS
//! Maximum allocation size to avoid integer overflow
#undef  MAX_ALLOC_SIZE
#define MAX_ALLOC_SIZE            (((size_t)-1) - _memory_span_size)
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

#define INVALID_POINTER ((void*)((uintptr_t)-1))

#define SIZE_CLASS_LARGE SIZE_CLASS_COUNT
#define SIZE_CLASS_HUGE ((uint32_t)-1)

/// Data types
//! A memory heap, per thread
typedef struct heap_t heap_t;
//! Heap spans per size class
typedef struct heap_class_t heap_class_t;
//! Span of memory pages
typedef struct span_t span_t;
//! Span list
typedef struct span_list_t span_list_t;
//! Span active data
typedef struct span_active_t span_active_t;
//! Size class definition
typedef struct size_class_t size_class_t;
//! Global cache
typedef struct global_cache_t global_cache_t;

//! Flag indicating span is the first (master) span of a split superspan
#define SPAN_FLAG_MASTER 1U
//! Flag indicating span is a secondary (sub) span of a split superspan
#define SPAN_FLAG_SUBSPAN 2U
//! Flag indicating span has blocks with increased alignment
#define SPAN_FLAG_ALIGNED_BLOCKS 4U

#if ENABLE_ADAPTIVE_THREAD_CACHE || ENABLE_STATISTICS
struct span_use_t {
	//! Current number of spans used (actually used, not in cache)
	atomic32_t current;
	//! High water mark of spans used
	atomic32_t high;
#if ENABLE_STATISTICS
	//! Number of spans transitioned to global cache
	atomic32_t spans_to_global;
	//! Number of spans transitioned from global cache
	atomic32_t spans_from_global;
	//! Number of spans transitioned to thread cache
	atomic32_t spans_to_cache;
	//! Number of spans transitioned from thread cache
	atomic32_t spans_from_cache;
	//! Number of spans transitioned to reserved state
	atomic32_t spans_to_reserved;
	//! Number of spans transitioned from reserved state
	atomic32_t spans_from_reserved;
	//! Number of raw memory map calls
	atomic32_t spans_map_calls;
#endif
};
typedef struct span_use_t span_use_t;
#endif

#if ENABLE_STATISTICS
struct size_class_use_t {
	//! Current number of allocations
	atomic32_t alloc_current;
	//! Peak number of allocations
	int32_t alloc_peak;
	//! Total number of allocations
	atomic32_t alloc_total;
	//! Total number of frees
	atomic32_t free_total;
	//! Number of spans in use
	atomic32_t spans_current;
	//! Number of spans transitioned to cache
	int32_t spans_peak;
	//! Number of spans transitioned to cache
	atomic32_t spans_to_cache;
	//! Number of spans transitioned from cache
	atomic32_t spans_from_cache;
	//! Number of spans transitioned from reserved state
	atomic32_t spans_from_reserved;
	//! Number of spans mapped
	atomic32_t spans_map_calls;
};
typedef struct size_class_use_t size_class_use_t;
#endif

//A span can either represent a single span of memory pages with size declared by span_map_count configuration variable,
//or a set of spans in a continuous region, a super span. Any reference to the term "span" usually refers to both a single
//span or a super span. A super span can further be divided into multiple spans (or this, super spans), where the first
//(super)span is the master and subsequent (super)spans are subspans. The master span keeps track of how many subspans
//that are still alive and mapped in virtual memory, and once all subspans and master have been unmapped the entire
//superspan region is released and unmapped (on Windows for example, the entire superspan range has to be released
//in the same call to release the virtual memory range, but individual subranges can be decommitted individually
//to reduce physical memory use).
struct span_t {
	//! Free list
	void*       free_list;
	//! Total block count of size class
	uint32_t    block_count;
	//! Size class
	uint32_t    size_class;
	//! Index of last block initialized in free list
	uint32_t    free_list_limit;
	//! Number of used blocks remaining when in partial state
	uint32_t    used_count;
	//! Deferred free list
	atomicptr_t free_list_deferred;
	//! Size of deferred free list, or list of spans when part of a cache list
	uint32_t    list_size;
	//! Size of a block
	uint32_t    block_size;
	//! Flags and counters
	uint32_t    flags;
	//! Number of spans
	uint32_t    span_count;
	//! Total span counter for master spans
	uint32_t    total_spans;
	//! Offset from master span for subspans
	uint32_t    offset_from_master;
	//! Remaining span counter, for master spans
	atomic32_t  remaining_spans;
	//! Alignment offset
	uint32_t    align_offset;
	//! Owning heap
	heap_t*     heap;
	//! Next span
	span_t*     next;
	//! Previous span
	span_t*     prev;
};
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "span size mismatch");

struct heap_class_t {
	//! Free list of active span
	void*        free_list;
	//! Double linked list of partially used spans with free blocks for each size class.
	//  Previous span pointer in head points to tail span of list.
	span_t*      partial_span;
#if RPMALLOC_FIRST_CLASS_HEAPS
	//! Double linked list of fully utilized spans with free blocks for each size class.
	//  Previous span pointer in head points to tail span of list.
	span_t*      full_span;
#endif
};

struct heap_t {
	//! Owning thread ID
	uintptr_t    owner_thread;
	//! Partial span data per size class
	heap_class_t span_class[SIZE_CLASS_COUNT];
#if ENABLE_THREAD_CACHE
	//! List of free spans (single linked list)
	span_t*      span_cache[LARGE_CLASS_COUNT];
#endif
	//! List of deferred free spans (single linked list)
	atomicptr_t  span_free_deferred;
#if ENABLE_ADAPTIVE_THREAD_CACHE || ENABLE_STATISTICS
	//! Current and high water mark of spans used per span count
	span_use_t   span_use[LARGE_CLASS_COUNT];
#endif
#if RPMALLOC_FIRST_CLASS_HEAPS
	//! Double linked list of large and huge spans allocated by this heap
	span_t*      large_huge_span;
#endif
	//! Number of full spans
	size_t       full_span_count;
	//! Mapped but unused spans
	span_t*      span_reserve;
	//! Master span for mapped but unused spans
	span_t*      span_reserve_master;
	//! Number of mapped but unused spans
	size_t       spans_reserved;
	//! Next heap in id list
	heap_t*      next_heap;
	//! Next heap in orphan list
	heap_t*      next_orphan;
	//! Memory pages alignment offset
	size_t       align_offset;
	//! Heap ID
	int32_t      id;
	//! Finalization state flag
	int          finalize;
	//! Master heap owning the memory pages
	heap_t*      master_heap;
	//! Child count
	atomic32_t   child_count;
#if ENABLE_STATISTICS
	//! Number of bytes transitioned thread -> global
	atomic64_t   thread_to_global;
	//! Number of bytes transitioned global -> thread
	atomic64_t   global_to_thread;
	//! Allocation stats per size class
	size_class_use_t size_class_use[SIZE_CLASS_COUNT + 1];
#endif
};

struct size_class_t {
	//! Size of blocks in this class
	uint32_t block_size;
	//! Number of blocks in each chunk
	uint16_t block_count;
	//! Class index this class is merged with
	uint16_t class_idx;
};
_Static_assert(sizeof(size_class_t) == 8, "Size class size mismatch");

struct global_cache_t {
	//! Cache list pointer
	atomicptr_t cache;
	//! Cache size
	atomic32_t size;
	//! ABA counter
	atomic32_t counter;
};

/// Global data
//! Initialized flag
static int _rpmalloc_initialized;
//! Configuration
static rpmalloc_config_t _memory_config;
//! Memory page size
static size_t _memory_page_size;
//! Shift to divide by page size
static size_t _memory_page_size_shift;
//! Granularity at which memory pages are mapped by OS
static size_t _memory_map_granularity;
#if RPMALLOC_CONFIGURABLE
//! Size of a span of memory pages
static size_t _memory_span_size;
//! Shift to divide by span size
static size_t _memory_span_size_shift;
//! Mask to get to start of a memory span
static uintptr_t _memory_span_mask;
#else
//! Hardwired span size (64KiB)
#define _memory_span_size (64 * 1024)
#define _memory_span_size_shift 16
#define _memory_span_mask (~((uintptr_t)(_memory_span_size - 1)))
#endif
//! Number of spans to map in each map call
static size_t _memory_span_map_count;
//! Number of spans to release from thread cache to global cache (single spans)
static size_t _memory_span_release_count;
//! Number of spans to release from thread cache to global cache (large multiple spans)
static size_t _memory_span_release_count_large;
//! Global size classes
static size_class_t _memory_size_class[SIZE_CLASS_COUNT];
//! Run-time size limit of medium blocks
static size_t _memory_medium_size_limit;
//! Heap ID counter
static atomic32_t _memory_heap_id;
//! Huge page support
static int _memory_huge_pages;
#if ENABLE_GLOBAL_CACHE
//! Global span cache
static global_cache_t _memory_span_cache[LARGE_CLASS_COUNT];
#endif
//! All heaps
static atomicptr_t _memory_heaps[HEAP_ARRAY_SIZE];
//! Orphaned heaps
static atomicptr_t _memory_orphan_heaps;
#if RPMALLOC_FIRST_CLASS_HEAPS
//! Orphaned heaps (first class heaps)
static atomicptr_t _memory_first_class_orphan_heaps;
#endif
//! Running orphan counter to avoid ABA issues in linked list
static atomic32_t _memory_orphan_counter;
#if ENABLE_STATISTICS
//! Active heap count
static atomic32_t _memory_active_heaps;
//! Number of currently mapped memory pages
static atomic32_t _mapped_pages;
//! Peak number of concurrently mapped memory pages
static int32_t _mapped_pages_peak;
//! Number of mapped master spans
static atomic32_t _master_spans;
//! Number of currently unused spans
static atomic32_t _reserved_spans;
//! Running counter of total number of mapped memory pages since start
static atomic32_t _mapped_total;
//! Running counter of total number of unmapped memory pages since start
static atomic32_t _unmapped_total;
//! Number of currently mapped memory pages in OS calls
static atomic32_t _mapped_pages_os;
//! Number of currently allocated pages in huge allocations
static atomic32_t _huge_pages_current;
//! Peak number of currently allocated pages in huge allocations
static int32_t _huge_pages_peak;
#endif

//! Current thread heap
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
static pthread_key_t _memory_thread_heap;
#else
#  ifdef _MSC_VER
#    define _Thread_local __declspec(thread)
#    define TLS_MODEL
#  else
#    define TLS_MODEL __attribute__((tls_model("initial-exec")))
#    if !defined(__clang__) && defined(__GNUC__)
#      define _Thread_local __thread
#    endif
#  endif
static _Thread_local heap_t* _memory_thread_heap TLS_MODEL;
#endif

static inline heap_t*
get_thread_heap_raw(void) {
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	return pthread_getspecific(_memory_thread_heap);
#else
	return _memory_thread_heap;
#endif
}

//! Get the current thread heap
static inline heap_t*
get_thread_heap(void) {
	heap_t* heap = get_thread_heap_raw();
#if ENABLE_PRELOAD
	if (EXPECTED(heap != 0))
		return heap;
	rpmalloc_initialize();
	return get_thread_heap_raw();
#else
	return heap;
#endif
}

//! Fast thread ID
static inline uintptr_t 
get_thread_id(void) {
#if defined(_WIN32)
	return (uintptr_t)NtCurrentTeb();
#elif defined(__GNUC__) || defined(__clang__)
	uintptr_t tid;
#  if defined(__i386__)
	__asm__("movl %%gs:0, %0" : "=r" (tid) : : );
#  elif defined(__MACH__)
	__asm__("movq %%gs:0, %0" : "=r" (tid) : : );
#  elif defined(__x86_64__)
	__asm__("movq %%fs:0, %0" : "=r" (tid) : : );
#  elif defined(__arm__)
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r" (tid));
#  elif defined(__aarch64__)
	asm volatile ("mrs %0, tpidr_el0" : "=r" (tid));
#  else
	tid = (uintptr_t)get_thread_heap_raw();
#  endif
	return tid;
#else
	return (uintptr_t)get_thread_heap_raw();
#endif
}

//! Set the current thread heap
static void
set_thread_heap(heap_t* heap) {
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	pthread_setspecific(_memory_thread_heap, heap);
#else
	_memory_thread_heap = heap;
#endif
	if (heap)
		heap->owner_thread = get_thread_id();
}

//! Default implementation to map more virtual memory
static void*
_memory_map_os(size_t size, size_t* offset);

//! Default implementation to unmap virtual memory
static void
_memory_unmap_os(void* address, size_t size, size_t offset, size_t release);

#if ENABLE_STATISTICS
#  define _memory_statistics_inc(counter) atomic_incr32(counter)
#  define _memory_statistics_dec(counter) atomic_decr32(counter)
#  define _memory_statistics_add(counter, value) atomic_add32(counter, (int32_t)(value))
#  define _memory_statistics_add64(counter, value) atomic_add64(counter, (int64_t)(value))
#  define _memory_statistics_add_peak(counter, value, peak) do { int32_t _cur_count = atomic_add32(counter, (int32_t)(value)); if (_cur_count > (peak)) peak = _cur_count; } while (0)
#  define _memory_statistics_sub(counter, value) atomic_add32(counter, -(int32_t)(value))
#  define _memory_statistics_inc_alloc(heap, class_idx) do { \
	int32_t alloc_current = atomic_incr32(&heap->size_class_use[class_idx].alloc_current); \
	if (alloc_current > heap->size_class_use[class_idx].alloc_peak) \
		heap->size_class_use[class_idx].alloc_peak = alloc_current; \
	atomic_incr32(&heap->size_class_use[class_idx].alloc_total); \
} while(0)
#  define _memory_statistics_inc_free(heap, class_idx) do { \
	atomic_decr32(&heap->size_class_use[class_idx].alloc_current); \
	atomic_incr32(&heap->size_class_use[class_idx].free_total); \
} while(0)
#else
#  define _memory_statistics_inc(counter) do {} while(0)
#  define _memory_statistics_dec(counter) do {} while(0)
#  define _memory_statistics_add(counter, value) do {} while(0)
#  define _memory_statistics_add64(counter, value) do {} while(0)
#  define _memory_statistics_add_peak(counter, value, peak) do {} while (0)
#  define _memory_statistics_sub(counter, value) do {} while(0)
#  define _memory_statistics_inc_alloc(heap, class_idx) do {} while(0)
#  define _memory_statistics_inc_free(heap, class_idx) do {} while(0)
#endif

static void
_memory_heap_cache_insert(heap_t* heap, span_t* span);

static void
_memory_global_cache_insert(span_t* span);

static void
_memory_heap_finalize(heap_t* heap);

//! Map more virtual memory
static void*
_memory_map(size_t size, size_t* offset) {
	assert(!(size % _memory_page_size));
	assert(size >= _memory_page_size);
	_memory_statistics_add_peak(&_mapped_pages, (size >> _memory_page_size_shift), _mapped_pages_peak);
	_memory_statistics_add(&_mapped_total, (size >> _memory_page_size_shift));
	return _memory_config.memory_map(size, offset);
}

//! Unmap virtual memory
static void
_memory_unmap(void* address, size_t size, size_t offset, size_t release) {
	assert(!release || (release >= size));
	assert(!release || (release >= _memory_page_size));
	if (release) {
		assert(!(release % _memory_page_size));
		_memory_statistics_sub(&_mapped_pages, (release >> _memory_page_size_shift));
		_memory_statistics_add(&_unmapped_total, (release >> _memory_page_size_shift));
	}
	_memory_config.memory_unmap(address, size, offset, release);
}

//! Declare the span to be a subspan and store distance from master span and span count
static void
_memory_span_mark_as_subspan_unless_master(span_t* master, span_t* subspan, size_t span_count) {
	assert((subspan != master) || (subspan->flags & SPAN_FLAG_MASTER));
	if (subspan != master) {
		subspan->flags = SPAN_FLAG_SUBSPAN;
		subspan->offset_from_master = (uint32_t)((uintptr_t)pointer_diff(subspan, master) >> _memory_span_size_shift);
		subspan->align_offset = 0;
	}
	subspan->span_count = (uint32_t)span_count;
}

//! Use reserved spans to fulfill a memory map request (reserve size must be checked by caller)
static span_t*
_memory_map_from_reserve(heap_t* heap, size_t span_count) {
	//Update the heap span reserve
	span_t* span = heap->span_reserve;
	heap->span_reserve = (span_t*)pointer_offset(span, span_count * _memory_span_size);
	heap->spans_reserved -= span_count;

	_memory_span_mark_as_subspan_unless_master(heap->span_reserve_master, span, span_count);
	if (span_count <= LARGE_CLASS_COUNT)
		_memory_statistics_inc(&heap->span_use[span_count - 1].spans_from_reserved);

	return span;
}

//! Get the aligned number of spans to map in based on wanted count, configured mapping granularity and the page size
static size_t
_memory_map_align_span_count(size_t span_count) {
	size_t request_count = (span_count > _memory_span_map_count) ? span_count : _memory_span_map_count;
	if ((_memory_page_size > _memory_span_size) && ((request_count * _memory_span_size) % _memory_page_size))
		request_count += _memory_span_map_count - (request_count % _memory_span_map_count);	
	return request_count;
}

//! Store the given spans as reserve in the given heap
static void
_memory_heap_set_reserved_spans(heap_t* heap, span_t* master, span_t* reserve, size_t reserve_span_count) {
	heap->span_reserve_master = master;
	heap->span_reserve = reserve;
	heap->spans_reserved = reserve_span_count;
}

//! Setup a newly mapped span
static void
_memory_span_initialize(span_t* span, size_t total_span_count, size_t span_count, size_t align_offset) {
	span->total_spans = (uint32_t)total_span_count;
	span->span_count = (uint32_t)span_count;
	span->align_offset = (uint32_t)align_offset;
	span->flags = SPAN_FLAG_MASTER;
	atomic_store32(&span->remaining_spans, (int32_t)total_span_count);
}

//! Map an aligned set of spans, taking configured mapping granularity and the page size into account
static span_t*
_memory_map_aligned_span_count(heap_t* heap, size_t span_count) {
	//If we already have some, but not enough, reserved spans, release those to heap cache and map a new
	//full set of spans. Otherwise we would waste memory if page size > span size (huge pages)
	size_t aligned_span_count = _memory_map_align_span_count(span_count);
	size_t align_offset = 0;
	span_t* span = (span_t*)_memory_map(aligned_span_count * _memory_span_size, &align_offset);
	if (!span)
		return 0;
	_memory_span_initialize(span, aligned_span_count, span_count, align_offset);
	_memory_statistics_add(&_reserved_spans, aligned_span_count);
	_memory_statistics_inc(&_master_spans);
	if (span_count <= LARGE_CLASS_COUNT)
		_memory_statistics_inc(&heap->span_use[span_count - 1].spans_map_calls);
	if (aligned_span_count > span_count) {
		span_t* reserved_spans = (span_t*)pointer_offset(span, span_count * _memory_span_size);
		size_t reserved_count = aligned_span_count - span_count;
		if (heap->spans_reserved) {
			_memory_span_mark_as_subspan_unless_master(heap->span_reserve_master, heap->span_reserve, heap->spans_reserved);
			_memory_heap_cache_insert(heap, heap->span_reserve);
		}
		_memory_heap_set_reserved_spans(heap, span, reserved_spans, reserved_count);
	}
	return span;
}

//! Map in memory pages for the given number of spans (or use previously reserved pages)
static span_t*
_memory_map_spans(heap_t* heap, size_t span_count) {
	if (span_count <= heap->spans_reserved)
		return _memory_map_from_reserve(heap, span_count);
	return _memory_map_aligned_span_count(heap, span_count);
}

//! Unmap memory pages for the given number of spans (or mark as unused if no partial unmappings)
static void
_memory_unmap_span(span_t* span) {
	assert((span->flags & SPAN_FLAG_MASTER) || (span->flags & SPAN_FLAG_SUBSPAN));
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));

	int is_master = !!(span->flags & SPAN_FLAG_MASTER);
	span_t* master = is_master ? span : ((span_t*)pointer_offset(span, -(intptr_t)((uintptr_t)span->offset_from_master * _memory_span_size)));
	assert(is_master || (span->flags & SPAN_FLAG_SUBSPAN));
	assert(master->flags & SPAN_FLAG_MASTER);

	size_t span_count = span->span_count;
	if (!is_master) {
		//Directly unmap subspans (unless huge pages, in which case we defer and unmap entire page range with master)
		assert(span->align_offset == 0);
		if (_memory_span_size >= _memory_page_size) {
			_memory_unmap(span, span_count * _memory_span_size, 0, 0);
			_memory_statistics_sub(&_reserved_spans, span_count);
		}
	} else {
		//Special double flag to denote an unmapped master
		//It must be kept in memory since span header must be used
		span->flags |= SPAN_FLAG_MASTER | SPAN_FLAG_SUBSPAN;
	}

	if (atomic_add32(&master->remaining_spans, -(int32_t)span_count) <= 0) {
		//Everything unmapped, unmap the master span with release flag to unmap the entire range of the super span
		assert(!!(master->flags & SPAN_FLAG_MASTER) && !!(master->flags & SPAN_FLAG_SUBSPAN));
		size_t unmap_count = master->span_count;
		if (_memory_span_size < _memory_page_size)
			unmap_count = master->total_spans;
		_memory_statistics_sub(&_reserved_spans, unmap_count);
		_memory_statistics_sub(&_master_spans, 1);
		_memory_unmap(master, unmap_count * _memory_span_size, master->align_offset, (size_t)master->total_spans * _memory_span_size);
	}
}

#if ENABLE_THREAD_CACHE

//! Unmap a single linked list of spans
static void
_memory_unmap_span_list(span_t* span) {
	size_t list_size = span->list_size;
	for (size_t ispan = 0; ispan < list_size; ++ispan) {
		span_t* next_span = span->next;
		_memory_unmap_span(span);
		span = next_span;
	}
	assert(!span);
}

//! Add span to head of single linked span list
static size_t
_memory_span_list_push(span_t** head, span_t* span) {
	span->next = *head;
	if (*head)
		span->list_size = (*head)->list_size + 1;
	else
		span->list_size = 1;
	*head = span;
	return span->list_size;
}

//! Remove span from head of single linked span list, returns the new list head
static span_t*
_memory_span_list_pop(span_t** head) {
	span_t* span = *head;
	span_t* next_span = 0;
	if (span->list_size > 1) {
		assert(span->next);
		next_span = span->next;
		assert(next_span);
		next_span->list_size = span->list_size - 1;
	}
	*head = next_span;
	return span;
}

//! Split a single linked span list
static span_t*
_memory_span_list_split(span_t* span, size_t limit) {
	span_t* next = 0;
	if (limit < 2)
		limit = 2;
	if (span->list_size > limit) {
		uint32_t list_size = 1;
		span_t* last = span;
		next = span->next;
		while (list_size < limit) {
			last = next;
			next = next->next;
			++list_size;
		}
		last->next = 0;
		assert(next);
		next->list_size = span->list_size - list_size;
		span->list_size = list_size;
		span->prev = 0;
	}
	return next;
}

#endif

//! Add a span to double linked list at the head
static void
_memory_span_double_link_list_add(span_t** head, span_t* span) {
	if (*head) {
		span->next = *head;
		(*head)->prev = span;
	} else {
		span->next = 0;
	}
	*head = span;
}

//! Pop head span from double linked list
static void
_memory_span_double_link_list_pop_head(span_t** head, span_t* span) {
	assert(*head == span);
	span = *head;
	*head = span->next;
}

//! Remove a span from double linked list
static void
_memory_span_double_link_list_remove(span_t** head, span_t* span) {
	assert(*head);
	if (*head == span) {
		*head = span->next;
	} else {
		span_t* next_span = span->next;
		span_t* prev_span = span->prev;
		prev_span->next = next_span;
		if (EXPECTED(next_span != 0)) {
			next_span->prev = prev_span;
		}
	}
}

#if ENABLE_GLOBAL_CACHE

//! Insert the given list of memory page spans in the global cache
static void
_memory_cache_insert(global_cache_t* cache, span_t* span, size_t cache_limit) {
	assert((span->list_size == 1) || (span->next != 0));
	int32_t list_size = (int32_t)span->list_size;
	//Unmap if cache has reached the limit. Does not need stronger synchronization, the worst
	//case is that the span list is unmapped when it could have been cached (no real dependency
	//between the two variables)
	if (atomic_add32(&cache->size, list_size) > (int32_t)cache_limit) {
#if !ENABLE_UNLIMITED_GLOBAL_CACHE
		_memory_unmap_span_list(span);
		atomic_add32(&cache->size, -list_size);
		return;
#endif
	}
	void* current_cache, *new_cache;
	do {
		current_cache = atomic_load_ptr(&cache->cache);
		span->prev = (span_t*)((uintptr_t)current_cache & _memory_span_mask);
		new_cache = (void*)((uintptr_t)span | ((uintptr_t)atomic_incr32(&cache->counter) & ~_memory_span_mask));
	} while (!atomic_cas_ptr(&cache->cache, new_cache, current_cache));
}

//! Extract a number of memory page spans from the global cache
static span_t*
_memory_cache_extract(global_cache_t* cache) {
	uintptr_t span_ptr;
	do {
		void* global_span = atomic_load_ptr(&cache->cache);
		span_ptr = (uintptr_t)global_span & _memory_span_mask;
		if (span_ptr) {
			span_t* span = (span_t*)span_ptr;
			//By accessing the span ptr before it is swapped out of list we assume that a contending thread
			//does not manage to traverse the span to being unmapped before we access it
			void* new_cache = (void*)((uintptr_t)span->prev | ((uintptr_t)atomic_incr32(&cache->counter) & ~_memory_span_mask));
			if (atomic_cas_ptr(&cache->cache, new_cache, global_span)) {
				atomic_add32(&cache->size, -(int32_t)span->list_size);
				return span;
			}
		}
	} while (span_ptr);
	return 0;
}

//! Finalize a global cache, only valid from allocator finalization (not thread safe)
static void
_memory_cache_finalize(global_cache_t* cache) {
	void* current_cache = atomic_load_ptr(&cache->cache);
	span_t* span = (span_t*)((uintptr_t)current_cache & _memory_span_mask);
	while (span) {
		span_t* skip_span = (span_t*)((uintptr_t)span->prev & _memory_span_mask);
		atomic_add32(&cache->size, -(int32_t)span->list_size);
		_memory_unmap_span_list(span);
		span = skip_span;
	}
	assert(!atomic_load32(&cache->size));
	atomic_store_ptr(&cache->cache, 0);
	atomic_store32(&cache->size, 0);
}

//! Insert the given list of memory page spans in the global cache
static void
_memory_global_cache_insert(span_t* span) {
	size_t span_count = span->span_count;
#if ENABLE_UNLIMITED_GLOBAL_CACHE
	_memory_cache_insert(&_memory_span_cache[span_count - 1], span, 0);
#else
	const size_t cache_limit = (GLOBAL_CACHE_MULTIPLIER * ((span_count == 1) ? _memory_span_release_count : _memory_span_release_count_large));
	_memory_cache_insert(&_memory_span_cache[span_count - 1], span, cache_limit);
#endif
}

//! Extract a number of memory page spans from the global cache for large blocks
static span_t*
_memory_global_cache_extract(size_t span_count) {
	span_t* span = _memory_cache_extract(&_memory_span_cache[span_count - 1]);
	assert(!span || (span->span_count == span_count));
	return span;
}

#endif

static void _memory_deallocate_huge(span_t*);

//! Adopt the deferred span cache list, optionally extracting the first single span for immediate re-use
static void
_memory_heap_cache_adopt_deferred(heap_t* heap, span_t** single_span) {
	span_t* span = (span_t*)atomic_load_ptr(&heap->span_free_deferred);
	if (!span)
		return;
	while (!atomic_cas_ptr(&heap->span_free_deferred, 0, span))
		span = (span_t*)atomic_load_ptr(&heap->span_free_deferred);
	while (span) {
		span_t* next_span = (span_t*)span->free_list;
		assert(span->heap == heap);
		if (EXPECTED(span->size_class < SIZE_CLASS_COUNT)) {
			assert(heap->full_span_count);
			--heap->full_span_count;
#if RPMALLOC_FIRST_CLASS_HEAPS
			heap_class_t* heap_class = heap->span_class + span->size_class;
			_memory_span_double_link_list_remove(&heap_class->full_span, span);
#endif
			if (single_span && !*single_span) {
				*single_span = span;
			} else {
				_memory_statistics_dec(&heap->span_use[0].current);
				_memory_statistics_dec(&heap->size_class_use[span->size_class].spans_current);
				_memory_heap_cache_insert(heap, span);
			}
		} else {
			if (span->size_class == SIZE_CLASS_HUGE) {
				_memory_deallocate_huge(span);
			} else {
				assert(span->size_class == SIZE_CLASS_LARGE);
				assert(heap->full_span_count);
				--heap->full_span_count;
#if RPMALLOC_FIRST_CLASS_HEAPS
				_memory_span_double_link_list_remove(&heap->large_huge_span, span);
#endif
				uint32_t idx = span->span_count - 1;
				if (!idx && single_span && !*single_span) {
					*single_span = span;
				} else {
					_memory_statistics_dec(&heap->span_use[idx].current);
					_memory_heap_cache_insert(heap, span);
				}
			}
		}
		span = next_span;
	}
}

static void
_memory_heap_global_finalize(heap_t* heap);

static void
_memory_unlink_orphan_heap(atomicptr_t* list, heap_t* heap) {
	void* raworphan = atomic_load_ptr(list);
	heap_t* orphan = (heap_t*)((uintptr_t)raworphan & ~(uintptr_t)(HEAP_ORPHAN_ABA_SIZE - 1));
	if (orphan == heap) {
		//We're now in single-threaded finalization phase, no need to ABA protect or CAS
		atomic_store_ptr(list, heap->next_orphan);
	} else if (orphan) {
		heap_t* last = orphan;
		while (orphan && (orphan != heap)) {
			last = orphan;
			orphan = orphan->next_orphan;
		}
		if (orphan == heap)
			last->next_orphan = heap->next_orphan;
	}
}

static void
_memory_unmap_heap(heap_t* heap) {
	if (!heap->master_heap) {
		if (!atomic_load32(&heap->child_count)) {
			_memory_unlink_orphan_heap(&_memory_orphan_heaps, heap);
#if RPMALLOC_FIRST_CLASS_HEAPS
			_memory_unlink_orphan_heap(&_memory_first_class_orphan_heaps, heap);
#endif
			size_t block_size = (1 + (sizeof(heap_t) >> _memory_page_size_shift)) * _memory_page_size;
			_memory_unmap(heap, block_size, heap->align_offset, block_size);
		}
	} else {
		if (atomic_decr32(&heap->master_heap->child_count) == 0) {
			_memory_heap_global_finalize(heap->master_heap);
		}
	}
}

static void
_memory_heap_global_finalize(heap_t* heap) {
	if (heap->finalize++ > 1) {
		--heap->finalize;
		return;
	}

	_memory_heap_finalize(heap);

	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		span_t* span = heap->span_cache[iclass];
		heap->span_cache[iclass] = 0;
		if (span)
			_memory_unmap_span_list(span);
	}

	if (heap->full_span_count) {
		--heap->finalize;
		return;
	}

	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		heap_class_t* heap_class = heap->span_class + iclass;
		if (heap_class->free_list || heap_class->partial_span) {
			--heap->finalize;
			return;
		}
	}
	//Heap is now completely free, unmap and remove from heap list
	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	heap_t* list_heap = (heap_t*)atomic_load_ptr(&_memory_heaps[list_idx]);
	if (list_heap == heap) {
		atomic_store_ptr(&_memory_heaps[list_idx], heap->next_heap);
	} else {
		while (list_heap->next_heap != heap)
			list_heap = list_heap->next_heap;
		list_heap->next_heap = heap->next_heap;
	}

	_memory_unmap_heap( heap );
}

//! Insert a single span into thread heap cache, releasing to global cache if overflow
static void
_memory_heap_cache_insert(heap_t* heap, span_t* span) {
	if (UNEXPECTED(heap->finalize != 0)) {
		_memory_unmap_span(span);
		_memory_heap_global_finalize(heap);
		return;
	}
#if ENABLE_THREAD_CACHE
	size_t span_count = span->span_count;
	size_t idx = span_count - 1;
	_memory_statistics_inc(&heap->span_use[idx].spans_to_cache);
#if ENABLE_UNLIMITED_THREAD_CACHE
	_memory_span_list_push(&heap->span_cache[idx], span);
#else
	const size_t release_count = (!idx ? _memory_span_release_count : _memory_span_release_count_large);
	size_t current_cache_size = _memory_span_list_push(&heap->span_cache[idx], span);
	if (current_cache_size <= release_count)
		return;
	const size_t hard_limit = release_count * THREAD_CACHE_MULTIPLIER;
	if (current_cache_size <= hard_limit) {
#if ENABLE_ADAPTIVE_THREAD_CACHE
		//Require 25% of high water mark to remain in cache (and at least 1, if use is 0)
		const size_t high_mark = heap->span_use[idx].high;
		const size_t min_limit = (high_mark >> 2) + release_count + 1;
		if (current_cache_size < min_limit)
			return;
#else
		return;
#endif
	}
	heap->span_cache[idx] = _memory_span_list_split(span, release_count);
	assert(span->list_size == release_count);
#if ENABLE_GLOBAL_CACHE
	_memory_statistics_add64(&heap->thread_to_global, (size_t)span->list_size * span_count * _memory_span_size);
	_memory_statistics_add(&heap->span_use[idx].spans_to_global, span->list_size);
	_memory_global_cache_insert(span);
#else
	_memory_unmap_span_list(span);
#endif
#endif
#else
	(void)sizeof(heap);
	_memory_unmap_span(span);
#endif
}

//! Extract the given number of spans from the different cache levels
static span_t*
_memory_heap_thread_cache_extract(heap_t* heap, size_t span_count) {
	span_t* span = 0;
	size_t idx = span_count - 1;
	if (!idx)
		_memory_heap_cache_adopt_deferred(heap, &span);
#if ENABLE_THREAD_CACHE
	if (!span && heap->span_cache[idx]) {
		_memory_statistics_inc(&heap->span_use[idx].spans_from_cache);
		span = _memory_span_list_pop(&heap->span_cache[idx]);
	}
#endif
	return span;
}

static span_t*
_memory_heap_reserved_extract(heap_t* heap, size_t span_count) {
	if (heap->spans_reserved >= span_count)
		return _memory_map_spans(heap, span_count);
	return 0;
}

//! Extract a span from the global cache
static span_t*
_memory_heap_global_cache_extract(heap_t* heap, size_t span_count) {
#if ENABLE_GLOBAL_CACHE
	size_t idx = span_count - 1;
	heap->span_cache[idx] = _memory_global_cache_extract(span_count);
	if (heap->span_cache[idx]) {
		_memory_statistics_add64(&heap->global_to_thread, (size_t)heap->span_cache[idx]->list_size * span_count * _memory_span_size);
		_memory_statistics_add(&heap->span_use[idx].spans_from_global, heap->span_cache[idx]->list_size);
		return _memory_span_list_pop(&heap->span_cache[idx]);
	}
#endif
	(void)sizeof(heap);
	(void)sizeof(span_count);
	return 0;
}

//! Get a span from one of the cache levels (thread cache, reserved, global cache) or fallback to mapping more memory
static span_t*
_memory_heap_extract_new_span(heap_t* heap, size_t span_count, uint32_t class_idx) {
	(void)sizeof(class_idx);
#if ENABLE_ADAPTIVE_THREAD_CACHE || ENABLE_STATISTICS
	uint32_t idx = (uint32_t)span_count - 1;
	uint32_t current_count = (uint32_t)atomic_incr32(&heap->span_use[idx].current);
	if (current_count > (uint32_t)atomic_load32(&heap->span_use[idx].high))
		atomic_store32(&heap->span_use[idx].high, (int32_t)current_count);
	_memory_statistics_add_peak(&heap->size_class_use[class_idx].spans_current, 1, heap->size_class_use[class_idx].spans_peak);
#endif
	span_t* span = _memory_heap_thread_cache_extract(heap, span_count);
	if (EXPECTED(span != 0)) {
		_memory_statistics_inc(&heap->size_class_use[class_idx].spans_from_cache);
		return span;
	}
	span = _memory_heap_reserved_extract(heap, span_count);
	if (EXPECTED(span != 0)) {
		_memory_statistics_inc(&heap->size_class_use[class_idx].spans_from_reserved);
		return span;
	}
	span = _memory_heap_global_cache_extract(heap, span_count);
	if (EXPECTED(span != 0)) {
		_memory_statistics_inc(&heap->size_class_use[class_idx].spans_from_cache);
		return span;
	}
	//Final fallback, map in more virtual memory
	span = _memory_map_spans(heap, span_count);
	_memory_statistics_inc(&heap->size_class_use[class_idx].spans_map_calls);
	return span;
}

//! Move the span (used for small or medium allocations) to the heap thread cache
static void
_memory_span_release_to_cache(heap_t* heap, span_t* span) {
	assert(heap == span->heap);
	assert(span->size_class < SIZE_CLASS_COUNT);
#if ENABLE_ADAPTIVE_THREAD_CACHE || ENABLE_STATISTICS
	atomic_decr32(&heap->span_use[0].current);
#endif
	_memory_statistics_inc(&heap->span_use[0].spans_to_cache);
	_memory_statistics_inc(&heap->size_class_use[span->size_class].spans_to_cache);
	_memory_statistics_dec(&heap->size_class_use[span->size_class].spans_current);
	_memory_heap_cache_insert(heap, span);
}

//! Initialize a (partial) free list up to next system memory page, while reserving the first block
//! as allocated, returning number of blocks in list
static uint32_t
free_list_partial_init(void** list, void** first_block, void* page_start, void* block_start,
                       uint32_t block_count, uint32_t block_size) {
	assert(block_count);
	*first_block = block_start;
	if (block_count > 1) {
		void* free_block = pointer_offset(block_start, block_size);
		void* block_end = pointer_offset(block_start, (size_t)block_size * block_count);
		//If block size is less than half a memory page, bound init to next memory page boundary
		if (block_size < (_memory_page_size >> 1)) {
			void* page_end = pointer_offset(page_start, _memory_page_size);
			if (page_end < block_end)
				block_end = page_end;
		}
		*list = free_block;
		block_count = 2;
		void* next_block = pointer_offset(free_block, block_size);
		while (next_block < block_end) {
			*((void**)free_block) = next_block;
			free_block = next_block;
			++block_count;
			next_block = pointer_offset(next_block, block_size);
		}
		*((void**)free_block) = 0;
	} else {
		*list = 0;
	}
	return block_count;
}

//! Initialize an unused span (from cache or mapped) to be new active span, putting the initial free list in heap class free list
static void*
_memory_span_initialize_new(heap_t* heap, heap_class_t* heap_class, span_t* span, uint32_t class_idx) {
	assert(span->span_count == 1);
	size_class_t* size_class = _memory_size_class + class_idx;
	span->size_class = class_idx;
	span->heap = heap;
	span->flags &= ~SPAN_FLAG_ALIGNED_BLOCKS;
	span->block_size = size_class->block_size;
	span->block_count = size_class->block_count;
	span->free_list = 0;
	span->list_size = 0;
	atomic_store_ptr_release(&span->free_list_deferred, 0);

	//Setup free list. Only initialize one system page worth of free blocks in list
	void* block;
	span->free_list_limit = free_list_partial_init(&heap_class->free_list, &block, 
		span, pointer_offset(span, SPAN_HEADER_SIZE), size_class->block_count, size_class->block_size);
	//Link span as partial if there remains blocks to be initialized as free list, or full if fully initialized
	if (span->free_list_limit < span->block_count) {
		_memory_span_double_link_list_add(&heap_class->partial_span, span);
		span->used_count = span->free_list_limit;
	} else {
#if RPMALLOC_FIRST_CLASS_HEAPS
		_memory_span_double_link_list_add(&heap_class->full_span, span);
#endif
		++heap->full_span_count;
		span->used_count = span->block_count;
	}
	return block;
}

static void
_memory_span_extract_free_list_deferred(span_t* span) {
	// We need acquire semantics on the CAS operation since we are interested in the list size
	// Refer to _memory_deallocate_defer_small_or_medium for further comments on this dependency
	do {
		span->free_list = atomic_load_ptr(&span->free_list_deferred);
	} while ((span->free_list == INVALID_POINTER) || !atomic_cas_ptr_acquire(&span->free_list_deferred, INVALID_POINTER, span->free_list));
	span->used_count -= span->list_size;
	span->list_size = 0;
	atomic_store_ptr_release(&span->free_list_deferred, 0);
}

static int
_memory_span_is_fully_utilized(span_t* span) {
	assert(span->free_list_limit <= span->block_count);
	return !span->free_list && (span->free_list_limit >= span->block_count);
}

//! Pop first block from a free list
static void*
free_list_pop(void** list) {
	void* block = *list;
	*list = *((void**)block);
	return block;
}

//! Allocate a small/medium sized memory block from the given heap
static void*
_memory_allocate_from_heap_fallback(heap_t* heap, uint32_t class_idx) {
	heap_class_t* heap_class = &heap->span_class[class_idx];
	span_t* span = heap_class->partial_span;
	if (EXPECTED(span != 0)) {
		assert(span->block_count == _memory_size_class[span->size_class].block_count);
		assert(!_memory_span_is_fully_utilized(span));
		void* block;
		if (span->free_list) {
			//Swap in free list if not empty
			heap_class->free_list = span->free_list;
			span->free_list = 0;
			block = free_list_pop(&heap_class->free_list);
		} else {
			//If the span did not fully initialize free list, link up another page worth of blocks			
			void* block_start = pointer_offset(span, SPAN_HEADER_SIZE + ((size_t)span->free_list_limit * span->block_size));
			span->free_list_limit += free_list_partial_init(&heap_class->free_list, &block,
				(void*)((uintptr_t)block_start & ~(_memory_page_size - 1)), block_start,
				span->block_count - span->free_list_limit, span->block_size);
		}
		assert(span->free_list_limit <= span->block_count);
		span->used_count = span->free_list_limit;

		//Swap in deferred free list if present
		if (atomic_load_ptr(&span->free_list_deferred))
			_memory_span_extract_free_list_deferred(span);

		//If span is still not fully utilized keep it in partial list and early return block
		if (!_memory_span_is_fully_utilized(span))
			return block;

		//The span is fully utilized, unlink from partial list and add to fully utilized list
		_memory_span_double_link_list_pop_head(&heap_class->partial_span, span);
#if RPMALLOC_FIRST_CLASS_HEAPS
		_memory_span_double_link_list_add(&heap_class->full_span, span);
#endif
		++heap->full_span_count;
		return block;
	}

	//Find a span in one of the cache levels
	span = _memory_heap_extract_new_span(heap, 1, class_idx);
	if (EXPECTED(span != 0)) {
		//Mark span as owned by this heap and set base data, return first block
		return _memory_span_initialize_new(heap, heap_class, span, class_idx);
	}

	return 0;
}

//! Allocate a small sized memory block from the given heap
static void*
_memory_allocate_small(heap_t* heap, size_t size) {
	assert(heap);
	//Small sizes have unique size classes
	const uint32_t class_idx = (uint32_t)((size + (SMALL_GRANULARITY - 1)) >> SMALL_GRANULARITY_SHIFT);
	_memory_statistics_inc_alloc(heap, class_idx);
	if (EXPECTED(heap->span_class[class_idx].free_list != 0))
		return free_list_pop(&heap->span_class[class_idx].free_list);
	return _memory_allocate_from_heap_fallback(heap, class_idx);
}

//! Allocate a medium sized memory block from the given heap
static void*
_memory_allocate_medium(heap_t* heap, size_t size) {
	assert(heap);
	//Calculate the size class index and do a dependent lookup of the final class index (in case of merged classes)
	const uint32_t base_idx = (uint32_t)(SMALL_CLASS_COUNT + ((size - (SMALL_SIZE_LIMIT + 1)) >> MEDIUM_GRANULARITY_SHIFT));
	const uint32_t class_idx = _memory_size_class[base_idx].class_idx;
	_memory_statistics_inc_alloc(heap, class_idx);
	if (EXPECTED(heap->span_class[class_idx].free_list != 0))
		return free_list_pop(&heap->span_class[class_idx].free_list);
	return _memory_allocate_from_heap_fallback(heap, class_idx);
}

//! Allocate a large sized memory block from the given heap
static void*
_memory_allocate_large(heap_t* heap, size_t size) {
	assert(heap);
	//Calculate number of needed max sized spans (including header)
	//Since this function is never called if size > LARGE_SIZE_LIMIT
	//the span_count is guaranteed to be <= LARGE_CLASS_COUNT
	size += SPAN_HEADER_SIZE;
	size_t span_count = size >> _memory_span_size_shift;
	if (size & (_memory_span_size - 1))
		++span_count;

	//Find a span in one of the cache levels
	span_t* span = _memory_heap_extract_new_span(heap, span_count, SIZE_CLASS_LARGE);
	if (!span)
		return span;

	//Mark span as owned by this heap and set base data
	assert(span->span_count == span_count);
	span->size_class = SIZE_CLASS_LARGE;
	span->heap = heap;

#if RPMALLOC_FIRST_CLASS_HEAPS
	_memory_span_double_link_list_add(&heap->large_huge_span, span);
#endif
	++heap->full_span_count;

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

//! Allocate a huge block by mapping memory pages directly
static void*
_memory_allocate_huge(heap_t* heap, size_t size) {
	assert(heap);
	size += SPAN_HEADER_SIZE;
	size_t num_pages = size >> _memory_page_size_shift;
	if (size & (_memory_page_size - 1))
		++num_pages;
	size_t align_offset = 0;
	span_t* span = (span_t*)_memory_map(num_pages * _memory_page_size, &align_offset);
	if (!span)
		return span;

	//Store page count in span_count
	span->size_class = SIZE_CLASS_HUGE;
	span->span_count = (uint32_t)num_pages;
	span->align_offset = (uint32_t)align_offset;
	span->heap = heap;
	_memory_statistics_add_peak(&_huge_pages_current, num_pages, _huge_pages_peak);

#if RPMALLOC_FIRST_CLASS_HEAPS
	_memory_span_double_link_list_add(&heap->large_huge_span, span);
#endif
	++heap->full_span_count;

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

//! Allocate a block of the given size
static void*
_memory_allocate(heap_t* heap, size_t size) {
	if (EXPECTED(size <= SMALL_SIZE_LIMIT))
		return _memory_allocate_small(heap, size);
	else if (size <= _memory_medium_size_limit)
		return _memory_allocate_medium(heap, size);
	else if (size <= LARGE_SIZE_LIMIT)
		return _memory_allocate_large(heap, size);
	return _memory_allocate_huge(heap, size);
}

static void*
_memory_aligned_allocate(heap_t* heap, size_t alignment, size_t size) {
	if (alignment <= SMALL_GRANULARITY)
		return _memory_allocate(heap, size);

#if ENABLE_VALIDATE_ARGS
	if ((size + alignment) < size) {
		errno = EINVAL;
		return 0;
	}
	if (alignment & (alignment - 1)) {
		errno = EINVAL;
		return 0;
	}
#endif

	if ((alignment <= SPAN_HEADER_SIZE) && (size < _memory_medium_size_limit)) {
		// If alignment is less or equal to span header size (which is power of two),
		// and size aligned to span header size multiples is less than size + alignment,
		// then use natural alignment of blocks to provide alignment
		size_t multiple_size = size ? (size + (SPAN_HEADER_SIZE - 1)) & ~(uintptr_t)(SPAN_HEADER_SIZE - 1) : SPAN_HEADER_SIZE;
		assert(!(multiple_size % SPAN_HEADER_SIZE));
		if (multiple_size <= (size + alignment))
			return _memory_allocate(heap, multiple_size);
	}

	void* ptr = 0;
	size_t align_mask = alignment - 1;
	if (alignment <= _memory_page_size) {
		ptr = _memory_allocate(heap, size + alignment);
		if ((uintptr_t)ptr & align_mask) {
			ptr = (void*)(((uintptr_t)ptr & ~(uintptr_t)align_mask) + alignment);
			//Mark as having aligned blocks
			span_t* span = (span_t*)((uintptr_t)ptr & _memory_span_mask);
			span->flags |= SPAN_FLAG_ALIGNED_BLOCKS;
		}
		return ptr;
	}

	// Fallback to mapping new pages for this request. Since pointers passed
	// to rpfree must be able to reach the start of the span by bitmasking of
	// the address with the span size, the returned aligned pointer from this
	// function must be with a span size of the start of the mapped area.
	// In worst case this requires us to loop and map pages until we get a
	// suitable memory address. It also means we can never align to span size
	// or greater, since the span header will push alignment more than one
	// span size away from span start (thus causing pointer mask to give us
	// an invalid span start on free)
	if (alignment & align_mask) {
		errno = EINVAL;
		return 0;
	}
	if (alignment >= _memory_span_size) {
		errno = EINVAL;
		return 0;
	}

	size_t extra_pages = alignment / _memory_page_size;

	// Since each span has a header, we will at least need one extra memory page
	size_t num_pages = 1 + (size / _memory_page_size);
	if (size & (_memory_page_size - 1))
		++num_pages;

	if (extra_pages > num_pages)
		num_pages = 1 + extra_pages;

	size_t original_pages = num_pages;
	size_t limit_pages = (_memory_span_size / _memory_page_size) * 2;
	if (limit_pages < (original_pages * 2))
		limit_pages = original_pages * 2;

	size_t mapped_size, align_offset;
	span_t* span;

retry:
	align_offset = 0;
	mapped_size = num_pages * _memory_page_size;

	span = (span_t*)_memory_map(mapped_size, &align_offset);
	if (!span) {
		errno = ENOMEM;
		return 0;
	}
	ptr = pointer_offset(span, SPAN_HEADER_SIZE);

	if ((uintptr_t)ptr & align_mask)
		ptr = (void*)(((uintptr_t)ptr & ~(uintptr_t)align_mask) + alignment);

	if (((size_t)pointer_diff(ptr, span) >= _memory_span_size) ||
	    (pointer_offset(ptr, size) > pointer_offset(span, mapped_size)) ||
	    (((uintptr_t)ptr & _memory_span_mask) != (uintptr_t)span)) {
		_memory_unmap(span, mapped_size, align_offset, mapped_size);
		++num_pages;
		if (num_pages > limit_pages) {
			errno = EINVAL;
			return 0;
		}
		goto retry;
	}

	//Store page count in span_count
	span->size_class = SIZE_CLASS_HUGE;
	span->span_count = (uint32_t)num_pages;
	span->align_offset = (uint32_t)align_offset;
	span->heap = heap;
	_memory_statistics_add_peak(&_huge_pages_current, num_pages, _huge_pages_peak);

#if RPMALLOC_FIRST_CLASS_HEAPS
	_memory_span_double_link_list_add(&heap->large_huge_span, span);
#endif
	++heap->full_span_count;

	return ptr;
}

static void
_memory_heap_initialize(heap_t* heap) {
	memset(heap, 0, sizeof(heap_t));

	//Get a new heap ID
	heap->id = 1 + atomic_incr32(&_memory_heap_id);

	//Link in heap in heap ID map
	heap_t* next_heap;
	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	do {
		next_heap = (heap_t*)atomic_load_ptr(&_memory_heaps[list_idx]);
		heap->next_heap = next_heap;
	} while (!atomic_cas_ptr(&_memory_heaps[list_idx], heap, next_heap));
}

static void
_memory_heap_orphan(heap_t* heap, int first_class) {
	void* raw_heap;
	uintptr_t orphan_counter;
	heap_t* last_heap;
	heap->owner_thread = (uintptr_t)-1;
#if RPMALLOC_FIRST_CLASS_HEAPS
	atomicptr_t* heap_list = (first_class ? &_memory_first_class_orphan_heaps : &_memory_orphan_heaps);
#else
	(void)sizeof(first_class);
	atomicptr_t* heap_list = &_memory_orphan_heaps;
#endif
	do {
		last_heap = (heap_t*)atomic_load_ptr(heap_list);
		heap->next_orphan = (heap_t*)((uintptr_t)last_heap & ~(uintptr_t)(HEAP_ORPHAN_ABA_SIZE - 1));
		orphan_counter = (uintptr_t)atomic_incr32(&_memory_orphan_counter);
		raw_heap = (void*)((uintptr_t)heap | (orphan_counter & (uintptr_t)(HEAP_ORPHAN_ABA_SIZE - 1)));
	} while (!atomic_cas_ptr(heap_list, raw_heap, last_heap));
}

//! Allocate a new heap from newly mapped memory pages
static heap_t*
_memory_allocate_heap_new(void) {
	//Map in pages for a new heap
	size_t align_offset = 0;
	size_t block_size = (1 + (sizeof(heap_t) >> _memory_page_size_shift)) * _memory_page_size;
	heap_t* heap = (heap_t*)_memory_map(block_size, &align_offset);
	if (!heap)
		return heap;

	_memory_heap_initialize(heap);
	heap->align_offset = align_offset;

	//Put extra heaps as orphans, aligning to make sure ABA protection bits fit in pointer low bits
	size_t aligned_heap_size = sizeof(heap_t);
	if (aligned_heap_size % HEAP_ORPHAN_ABA_SIZE)
		aligned_heap_size += HEAP_ORPHAN_ABA_SIZE - (aligned_heap_size % HEAP_ORPHAN_ABA_SIZE);
	size_t num_heaps = block_size / aligned_heap_size;
	atomic_store32(&heap->child_count, (int32_t)num_heaps - 1);
	heap_t* extra_heap = (heap_t*)pointer_offset(heap, aligned_heap_size);
	while (num_heaps > 1) {
		_memory_heap_initialize(extra_heap);
		extra_heap->master_heap = heap;
		_memory_heap_orphan(extra_heap, 1);
		extra_heap = (heap_t*)pointer_offset(extra_heap, aligned_heap_size);
		--num_heaps;
	}
	return heap;
}

static heap_t*
_memory_heap_extract_orphan(atomicptr_t* heap_list) {
	void* raw_heap;
	void* next_raw_heap;
	uintptr_t orphan_counter;
	heap_t* heap;
	heap_t* next_heap;
	do {
		raw_heap = atomic_load_ptr(heap_list);
		heap = (heap_t*)((uintptr_t)raw_heap & ~(uintptr_t)(HEAP_ORPHAN_ABA_SIZE - 1));
		if (!heap)
			break;
		next_heap = heap->next_orphan;
		orphan_counter = (uintptr_t)atomic_incr32(&_memory_orphan_counter);
		next_raw_heap = (void*)((uintptr_t)next_heap | (orphan_counter & (uintptr_t)(HEAP_ORPHAN_ABA_SIZE - 1)));
	} while (!atomic_cas_ptr(heap_list, next_raw_heap, raw_heap));
	return heap;
}

//! Allocate a new heap, potentially reusing a previously orphaned heap
static heap_t*
_memory_allocate_heap(int first_class) {
	heap_t* heap = 0;
	if (first_class == 0)
		heap = _memory_heap_extract_orphan(&_memory_orphan_heaps);
#if RPMALLOC_FIRST_CLASS_HEAPS
	if (!heap)
		heap = _memory_heap_extract_orphan(&_memory_first_class_orphan_heaps);
#endif
	if (!heap)
		heap = _memory_allocate_heap_new();
	return heap;
}

//! Deallocate the given small/medium memory block in the current thread local heap
static void
_memory_deallocate_direct_small_or_medium(span_t* span, void* block) {
	heap_t* heap = span->heap;
	assert(heap->owner_thread == get_thread_id() || heap->finalize);
	//Add block to free list
	if (UNEXPECTED(_memory_span_is_fully_utilized(span))) {
		span->used_count = span->block_count;
		heap_class_t* heap_class = &heap->span_class[span->size_class];
#if RPMALLOC_FIRST_CLASS_HEAPS
		_memory_span_double_link_list_remove(&heap_class->full_span, span);
#endif
		_memory_span_double_link_list_add(&heap_class->partial_span, span);
		--heap->full_span_count;
	}
	--span->used_count;
	*((void**)block) = span->free_list;
	span->free_list = block;
	if (UNEXPECTED(span->used_count == span->list_size)) {
		heap_class_t* heap_class = &heap->span_class[span->size_class];
		_memory_span_double_link_list_remove(&heap_class->partial_span, span);
		_memory_span_release_to_cache(heap, span);
	}
}

static void
_memory_deallocate_defer_free_span(heap_t* heap, span_t* span) {
	//This list does not need ABA protection, no mutable side state
	do {
		span->free_list = atomic_load_ptr(&heap->span_free_deferred);
	} while (!atomic_cas_ptr(&heap->span_free_deferred, span, span->free_list));
}

//! Put the block in the deferred free list of the owning span
static void
_memory_deallocate_defer_small_or_medium(span_t* span, void* block) {
	// The memory ordering here is a bit tricky, to avoid having to ABA protect
	// the deferred free list to avoid desynchronization of list and list size
	// we need to have acquire semantics on successful CAS of the pointer to
	// guarantee the list_size variable validity + release semantics on pointer store
	void* free_list;
	do {
		free_list = atomic_load_ptr(&span->free_list_deferred);
		*((void**)block) = free_list;
	} while ((free_list == INVALID_POINTER) || !atomic_cas_ptr_acquire(&span->free_list_deferred, INVALID_POINTER, free_list));
	uint32_t free_count = ++span->list_size;
	atomic_store_ptr_release(&span->free_list_deferred, block);
	if (free_count == span->block_count) {
		// Span was completely freed by this block. Due to the INVALID_POINTER spin lock
		// no other thread can reach this state simultaneously on this span.
		// Safe to move to owner heap deferred cache
		_memory_deallocate_defer_free_span(span->heap, span);
	}
}

static void
_memory_deallocate_small_or_medium(span_t* span, void* p) {
	_memory_statistics_inc_free(span->heap, span->size_class);
	if (span->flags & SPAN_FLAG_ALIGNED_BLOCKS) {
		//Realign pointer to block start
		void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
		uint32_t block_offset = (uint32_t)pointer_diff(p, blocks_start);
		p = pointer_offset(p, -(int32_t)(block_offset % span->block_size));
	}
	//Check if block belongs to this heap or if deallocation should be deferred
	if ((span->heap->owner_thread == get_thread_id()) || span->heap->finalize)
		_memory_deallocate_direct_small_or_medium(span, p);
	else
		_memory_deallocate_defer_small_or_medium(span, p);
}

//! Deallocate the given large memory block to the current heap
static void
_memory_deallocate_large(span_t* span) {
	assert(span->size_class == SIZE_CLASS_LARGE);
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));
	assert((span->flags & SPAN_FLAG_MASTER) || (span->flags & SPAN_FLAG_SUBSPAN));
	//We must always defer (unless finalizing) if from another heap since we cannot touch the list or counters of another heap
	int defer = (span->heap->owner_thread != get_thread_id()) && !span->heap->finalize;
	if (defer) {
		_memory_deallocate_defer_free_span(span->heap, span);
		return;
	}
	assert(span->heap->full_span_count);
	--span->heap->full_span_count;
#if RPMALLOC_FIRST_CLASS_HEAPS
	_memory_span_double_link_list_remove(&span->heap->large_huge_span, span);
#endif
#if ENABLE_ADAPTIVE_THREAD_CACHE || ENABLE_STATISTICS
	//Decrease counter
	size_t idx = span->span_count - 1;
	atomic_decr32(&span->heap->span_use[idx].current);
#endif
	heap_t* heap = get_thread_heap();
	assert(heap);
	span->heap = heap;
	if ((span->span_count > 1) && !heap->finalize && !heap->spans_reserved) {
		heap->span_reserve = span;
		heap->spans_reserved = span->span_count;
		if (span->flags & SPAN_FLAG_MASTER) {
			heap->span_reserve_master = span;
		} else { //SPAN_FLAG_SUBSPAN
			span_t* master = (span_t*)pointer_offset(span, -(intptr_t)((size_t)span->offset_from_master * _memory_span_size));
			heap->span_reserve_master = master;
			assert(master->flags & SPAN_FLAG_MASTER);
			assert(atomic_load32(&master->remaining_spans) >= (int32_t)span->span_count);
		}
		_memory_statistics_inc(&heap->span_use[idx].spans_to_reserved);
	} else {
		//Insert into cache list
		_memory_heap_cache_insert(heap, span);
	}
}

//! Deallocate the given huge span
static void
_memory_deallocate_huge(span_t* span) {
	assert(span->heap);
	if ((span->heap->owner_thread != get_thread_id()) && !span->heap->finalize) {
		_memory_deallocate_defer_free_span(span->heap, span);
		return;
	}
	assert(span->heap->full_span_count);
	--span->heap->full_span_count;
#if RPMALLOC_FIRST_CLASS_HEAPS
	_memory_span_double_link_list_remove(&span->heap->large_huge_span, span);
#endif

	//Oversized allocation, page count is stored in span_count
	size_t num_pages = span->span_count;
	_memory_unmap(span, num_pages * _memory_page_size, span->align_offset, num_pages * _memory_page_size);
	_memory_statistics_sub(&_huge_pages_current, num_pages);
}

//! Deallocate the given block
static void
_memory_deallocate(void* p) {
	//Grab the span (always at start of span, using span alignment)
	span_t* span = (span_t*)((uintptr_t)p & _memory_span_mask);
	if (UNEXPECTED(!span))
		return;
	if (EXPECTED(span->size_class < SIZE_CLASS_COUNT))
		_memory_deallocate_small_or_medium(span, p);
	else if (span->size_class == SIZE_CLASS_LARGE)
		_memory_deallocate_large(span);
	else
		_memory_deallocate_huge(span);
}

//! Get the usable size of the given block
static size_t
_memory_usable_size(void* p) {
	//Grab the span using guaranteed span alignment
	span_t* span = (span_t*)((uintptr_t)p & _memory_span_mask);
	if (span->size_class < SIZE_CLASS_COUNT) {
		//Small/medium block
		void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
		return span->block_size - ((size_t)pointer_diff(p, blocks_start) % span->block_size);
	}
	if (span->size_class == SIZE_CLASS_LARGE) {
		//Large block
		size_t current_spans = span->span_count;
		return (current_spans * _memory_span_size) - (size_t)pointer_diff(p, span);
	}
	//Oversized block, page count is stored in span_count
	size_t current_pages = span->span_count;
	return (current_pages * _memory_page_size) - (size_t)pointer_diff(p, span);
}

//! Reallocate the given block to the given size
static void*
_memory_reallocate(heap_t* heap, void* p, size_t size, size_t oldsize, unsigned int flags) {
	if (p) {
		//Grab the span using guaranteed span alignment
		span_t* span = (span_t*)((uintptr_t)p & _memory_span_mask);
		if (EXPECTED(span->size_class < SIZE_CLASS_COUNT)) {
			//Small/medium sized block
			assert(span->span_count == 1);
			void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
			uint32_t block_offset = (uint32_t)pointer_diff(p, blocks_start);
			uint32_t block_idx = block_offset / span->block_size;
			void* block = pointer_offset(blocks_start, (size_t)block_idx * span->block_size);
			if (!oldsize)
				oldsize = (size_t)((ptrdiff_t)span->block_size - pointer_diff(p, block));
			if ((size_t)span->block_size >= size) {
				//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block, p, oldsize);
				return block;
			}
		} else if (span->size_class == SIZE_CLASS_LARGE) {
			//Large block
			size_t total_size = size + SPAN_HEADER_SIZE;
			size_t num_spans = total_size >> _memory_span_size_shift;
			if (total_size & (_memory_span_mask - 1))
				++num_spans;
			size_t current_spans = span->span_count;
			void* block = pointer_offset(span, SPAN_HEADER_SIZE);
			if (!oldsize)
				oldsize = (current_spans * _memory_span_size) - (size_t)pointer_diff(p, block) - SPAN_HEADER_SIZE;
			if ((current_spans >= num_spans) && (num_spans >= (current_spans / 2))) {
				//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block, p, oldsize);
				return block;
			}
		} else {
			//Oversized block
			size_t total_size = size + SPAN_HEADER_SIZE;
			size_t num_pages = total_size >> _memory_page_size_shift;
			if (total_size & (_memory_page_size - 1))
				++num_pages;
			//Page count is stored in span_count
			size_t current_pages = span->span_count;
			void* block = pointer_offset(span, SPAN_HEADER_SIZE);
			if (!oldsize)
				oldsize = (current_pages * _memory_page_size) - (size_t)pointer_diff(p, block) - SPAN_HEADER_SIZE;
			if ((current_pages >= num_pages) && (num_pages >= (current_pages / 2))) {
				//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block, p, oldsize);
				return block;
			}
		}
	} else {
		oldsize = 0;
	}

	if (!!(flags & RPMALLOC_GROW_OR_FAIL))
		return 0;

	//Size is greater than block size, need to allocate a new block and deallocate the old
	//Avoid hysteresis by overallocating if increase is small (below 37%)
	size_t lower_bound = oldsize + (oldsize >> 2) + (oldsize >> 3);
	size_t new_size = (size > lower_bound) ? size : ((size > oldsize) ? lower_bound : size);
	void* block = _memory_allocate(heap, new_size);
	if (p && block) {
		if (!(flags & RPMALLOC_NO_PRESERVE))
			memcpy(block, p, oldsize < new_size ? oldsize : new_size);
		_memory_deallocate(p);
	}

	return block;
}

static void*
_memory_aligned_reallocate(heap_t* heap, void* ptr, size_t alignment, size_t size, size_t oldsize,
                           unsigned int flags) {
	if (alignment <= SMALL_GRANULARITY)
		return _memory_reallocate(heap, ptr, size, oldsize, flags);

	int no_alloc = !!(flags & RPMALLOC_GROW_OR_FAIL);
	size_t usablesize = _memory_usable_size(ptr);
	if ((usablesize >= size) && !((uintptr_t)ptr & (alignment - 1))) {
		if (no_alloc || (size >= (usablesize / 2)))
			return ptr;
	}
	// Aligned alloc marks span as having aligned blocks
	void* block = (!no_alloc ? _memory_aligned_allocate(heap, alignment, size) : 0);
	if (EXPECTED(block != 0)) {
		if (!(flags & RPMALLOC_NO_PRESERVE) && ptr) {
			if (!oldsize)
				oldsize = usablesize;
			memcpy(block, ptr, oldsize < size ? oldsize : size);
		}
		rpfree(ptr);
	}
	return block;
}

//! Adjust and optimize the size class properties for the given class
static void
_memory_adjust_size_class(size_t iclass) {
	size_t block_size = _memory_size_class[iclass].block_size;
	size_t block_count = (_memory_span_size - SPAN_HEADER_SIZE) / block_size;

	_memory_size_class[iclass].block_count = (uint16_t)block_count;
	_memory_size_class[iclass].class_idx = (uint16_t)iclass;

	//Check if previous size classes can be merged
	size_t prevclass = iclass;
	while (prevclass > 0) {
		--prevclass;
		//A class can be merged if number of pages and number of blocks are equal
		if (_memory_size_class[prevclass].block_count == _memory_size_class[iclass].block_count)
			memcpy(_memory_size_class + prevclass, _memory_size_class + iclass, sizeof(_memory_size_class[iclass]));
		else
			break;
	}
}

static void
_memory_heap_release(void* heapptr, int first_class) {
	heap_t* heap = (heap_t*)heapptr;
	if (!heap)
		return;
	//Release thread cache spans back to global cache
	_memory_heap_cache_adopt_deferred(heap, 0);
#if ENABLE_THREAD_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		span_t* span = heap->span_cache[iclass];
		heap->span_cache[iclass] = 0;
		if (span && heap->finalize) {
			_memory_unmap_span_list(span);
			continue;
		}
#if ENABLE_GLOBAL_CACHE
		while (span) {
			assert(span->span_count == (iclass + 1));
			size_t release_count = (!iclass ? _memory_span_release_count : _memory_span_release_count_large);
			span_t* next = _memory_span_list_split(span, (uint32_t)release_count);
			_memory_statistics_add64(&heap->thread_to_global, (size_t)span->list_size * span->span_count * _memory_span_size);
			_memory_statistics_add(&heap->span_use[iclass].spans_to_global, span->list_size);
			_memory_global_cache_insert(span);
			span = next;
		}
#else
		if (span)
			_memory_unmap_span_list(span);
#endif
	}
#endif

	//Orphan the heap
	_memory_heap_orphan(heap, first_class);

	set_thread_heap(0);
#if ENABLE_STATISTICS
	atomic_decr32(&_memory_active_heaps);
	assert(atomic_load32(&_memory_active_heaps) >= 0);
#endif
}

static void
_memory_heap_release_raw(void* heapptr) {
	_memory_heap_release(heapptr, 0);
}

#if defined(_MSC_VER) && !defined(__clang__) && (!defined(BUILD_DYNAMIC_LINK) || !BUILD_DYNAMIC_LINK)
#include <fibersapi.h>
static DWORD fls_key;
static void NTAPI
rp_thread_destructor(void* value) {
	if (value)
		rpmalloc_thread_finalize();
}
#endif

#if PLATFORM_POSIX
#  include <sys/mman.h>
#  include <sched.h>
#  ifdef __FreeBSD__
#    include <sys/sysctl.h>
#    define MAP_HUGETLB MAP_ALIGNED_SUPER
#  endif
#  ifndef MAP_UNINITIALIZED
#    define MAP_UNINITIALIZED 0
#  endif
#endif
#include <errno.h>

//! Initialize the allocator and setup global data
extern inline int
rpmalloc_initialize(void) {
	if (_rpmalloc_initialized) {
		rpmalloc_thread_initialize();
		return 0;
	}
	return rpmalloc_initialize_config(0);
}

int
rpmalloc_initialize_config(const rpmalloc_config_t* config) {
	if (_rpmalloc_initialized) {
		rpmalloc_thread_initialize();
		return 0;
	}
	_rpmalloc_initialized = 1;

	if (config)
		memcpy(&_memory_config, config, sizeof(rpmalloc_config_t));
	else
		memset(&_memory_config, 0, sizeof(rpmalloc_config_t));

	if (!_memory_config.memory_map || !_memory_config.memory_unmap) {
		_memory_config.memory_map = _memory_map_os;
		_memory_config.memory_unmap = _memory_unmap_os;
	}

#if RPMALLOC_CONFIGURABLE
	_memory_page_size = _memory_config.page_size;
#else
	_memory_page_size = 0;
#endif
	_memory_huge_pages = 0;
	_memory_map_granularity = _memory_page_size;
	if (!_memory_page_size) {
#if PLATFORM_WINDOWS
		SYSTEM_INFO system_info;
		memset(&system_info, 0, sizeof(system_info));
		GetSystemInfo(&system_info);
		_memory_page_size = system_info.dwPageSize;
		_memory_map_granularity = system_info.dwAllocationGranularity;
#else
		_memory_page_size = (size_t)sysconf(_SC_PAGESIZE);
		_memory_map_granularity = _memory_page_size;
		if (_memory_config.enable_huge_pages) {
#if defined(__linux__)
			size_t huge_page_size = 0;
			FILE* meminfo = fopen("/proc/meminfo", "r");
			if (meminfo) {
				char line[128];
				while (!huge_page_size && fgets(line, sizeof(line) - 1, meminfo)) {
					line[sizeof(line) - 1] = 0;
					if (strstr(line, "Hugepagesize:"))
						huge_page_size = (size_t)strtol(line + 13, 0, 10) * 1024;
				}
				fclose(meminfo);
			}
			if (huge_page_size) {
				_memory_huge_pages = 1;
				_memory_page_size = huge_page_size;
				_memory_map_granularity = huge_page_size;
			}
#elif defined(__FreeBSD__)
			int rc;
			size_t sz = sizeof(rc);

			if (sysctlbyname("vm.pmap.pg_ps_enabled", &rc, &sz, NULL, 0) == 0 && rc == 1) {
				_memory_huge_pages = 1;
				_memory_page_size = 2 * 1024 * 1024;
				_memory_map_granularity = _memory_page_size;
			}
#elif defined(__APPLE__)
			_memory_huge_pages = 1;
			_memory_page_size = 2 * 1024 * 1024;
			_memory_map_granularity = _memory_page_size;
#endif
		}
#endif
	} else {
		if (_memory_config.enable_huge_pages)
			_memory_huge_pages = 1;
	}

#if PLATFORM_WINDOWS
	if (_memory_config.enable_huge_pages) {
		HANDLE token = 0;
		size_t large_page_minimum = GetLargePageMinimum();
		if (large_page_minimum)
			OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
		if (token) {
			LUID luid;
			if (LookupPrivilegeValue(0, SE_LOCK_MEMORY_NAME, &luid)) {
				TOKEN_PRIVILEGES token_privileges;
				memset(&token_privileges, 0, sizeof(token_privileges));
				token_privileges.PrivilegeCount = 1;
				token_privileges.Privileges[0].Luid = luid;
				token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				if (AdjustTokenPrivileges(token, FALSE, &token_privileges, 0, 0, 0)) {
					DWORD err = GetLastError();
					if (err == ERROR_SUCCESS) {
						_memory_huge_pages = 1;
						if (large_page_minimum > _memory_page_size)
						 	_memory_page_size = large_page_minimum;
						if (large_page_minimum > _memory_map_granularity)
							_memory_map_granularity = large_page_minimum;
					}
				}
			}
			CloseHandle(token);
		}
	}
#endif

	//The ABA counter in heap orphan list is tied to using HEAP_ORPHAN_ABA_SIZE
	size_t min_span_size = HEAP_ORPHAN_ABA_SIZE;
	size_t max_page_size;
#if UINTPTR_MAX > 0xFFFFFFFF
	max_page_size = 4096ULL * 1024ULL * 1024ULL;
#else
	max_page_size = 4 * 1024 * 1024;
#endif
	if (_memory_page_size < min_span_size)
		_memory_page_size = min_span_size;
	if (_memory_page_size > max_page_size)
		_memory_page_size = max_page_size;
	_memory_page_size_shift = 0;
	size_t page_size_bit = _memory_page_size;
	while (page_size_bit != 1) {
		++_memory_page_size_shift;
		page_size_bit >>= 1;
	}
	_memory_page_size = ((size_t)1 << _memory_page_size_shift);

#if RPMALLOC_CONFIGURABLE
	size_t span_size = _memory_config.span_size;
	if (!span_size)
		span_size = (64 * 1024);
	if (span_size > (256 * 1024))
		span_size = (256 * 1024);
	_memory_span_size = 4096;
	_memory_span_size_shift = 12;
	while (_memory_span_size < span_size) {
		_memory_span_size <<= 1;
		++_memory_span_size_shift;
	}
	_memory_span_mask = ~(uintptr_t)(_memory_span_size - 1);
#endif

	_memory_span_map_count = ( _memory_config.span_map_count ? _memory_config.span_map_count : DEFAULT_SPAN_MAP_COUNT);
	if ((_memory_span_size * _memory_span_map_count) < _memory_page_size)
		_memory_span_map_count = (_memory_page_size / _memory_span_size);
	if ((_memory_page_size >= _memory_span_size) && ((_memory_span_map_count * _memory_span_size) % _memory_page_size))
		_memory_span_map_count = (_memory_page_size / _memory_span_size);

	_memory_config.page_size = _memory_page_size;
	_memory_config.span_size = _memory_span_size;
	_memory_config.span_map_count = _memory_span_map_count;
	_memory_config.enable_huge_pages = _memory_huge_pages;

	_memory_span_release_count = (_memory_span_map_count > 4 ? ((_memory_span_map_count < 64) ? _memory_span_map_count : 64) : 4);
	_memory_span_release_count_large = (_memory_span_release_count > 8 ? (_memory_span_release_count / 4) : 2);

#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	if (pthread_key_create(&_memory_thread_heap, _memory_heap_release_raw))
		return -1;
#endif
#if defined(_MSC_VER) && !defined(__clang__) && (!defined(BUILD_DYNAMIC_LINK) || !BUILD_DYNAMIC_LINK)
    fls_key = FlsAlloc(&rp_thread_destructor);
#endif

	//Setup all small and medium size classes
	size_t iclass = 0;
	_memory_size_class[iclass].block_size = SMALL_GRANULARITY;
	_memory_adjust_size_class(iclass);
	for (iclass = 1; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = iclass * SMALL_GRANULARITY;
		_memory_size_class[iclass].block_size = (uint32_t)size;
		_memory_adjust_size_class(iclass);
	}
	//At least two blocks per span, then fall back to large allocations
	_memory_medium_size_limit = (_memory_span_size - SPAN_HEADER_SIZE) >> 1;
	if (_memory_medium_size_limit > MEDIUM_SIZE_LIMIT)
		_memory_medium_size_limit = MEDIUM_SIZE_LIMIT;
	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + ((iclass + 1) * MEDIUM_GRANULARITY);
		if (size > _memory_medium_size_limit)
			break;
		_memory_size_class[SMALL_CLASS_COUNT + iclass].block_size = (uint32_t)size;
		_memory_adjust_size_class(SMALL_CLASS_COUNT + iclass);
	}

	//Initialize this thread
	rpmalloc_thread_initialize();
	return 0;
}

static int
_memory_span_finalize(heap_t* heap, size_t iclass, span_t* span, span_t** list_head) {
	heap_class_t* heap_class = heap->span_class + iclass;
	span_t* class_span = (span_t*)((uintptr_t)heap_class->free_list & _memory_span_mask);
	if (span == class_span) {
		// Adopt the heap class free list back into the span free list
		void* block = span->free_list;
		void* last_block = 0;
		while (block) {
			last_block = block;
			block = *((void**)block);
		}
		uint32_t free_count = 0;
		block = heap_class->free_list;
		while (block) {
			++free_count;
			block = *((void**)block);
		}
		if (last_block) {
			*((void**)last_block) = heap_class->free_list;
		} else {
			span->free_list = heap_class->free_list;
		}
		heap_class->free_list = 0;
		span->used_count -= free_count;
	}
	//If this assert triggers you have memory leaks
	assert(span->list_size == span->used_count);
	if (span->list_size == span->used_count) {
		_memory_statistics_dec(&heap->span_use[0].current);
		_memory_statistics_dec(&heap->size_class_use[iclass].spans_current);
		// This function only used for spans in double linked lists
		if (list_head)
			_memory_span_double_link_list_remove(list_head, span);
		_memory_unmap_span(span);
		return 1;
	}
	return 0;
}

static void
_memory_heap_finalize(heap_t* heap) {
	if (heap->spans_reserved) {
		span_t* span = _memory_map_spans(heap, heap->spans_reserved);
		_memory_unmap_span(span);
		heap->spans_reserved = 0;
	}

	_memory_heap_cache_adopt_deferred(heap, 0);

	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		heap_class_t* heap_class = heap->span_class + iclass;				
		span_t* span = heap_class->partial_span;
		while (span) {
			span_t* next = span->next;
			_memory_span_finalize(heap, iclass, span, &heap_class->partial_span);
			span = next;
		}
		// If class still has a free list it must be a full span
		if (heap_class->free_list) {
			span_t* class_span = (span_t*)((uintptr_t)heap_class->free_list & _memory_span_mask);
			span_t** list = 0;
#if RPMALLOC_FIRST_CLASS_HEAPS
			list = &heap_class->full_span;
#endif
			--heap->full_span_count;
			if (!_memory_span_finalize(heap, iclass, class_span, list)) {
				if (list)
					_memory_span_double_link_list_remove(list, class_span);
				_memory_span_double_link_list_add(&heap_class->partial_span, class_span);
			}
		}
	}

#if ENABLE_THREAD_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		if (heap->span_cache[iclass]) {
			_memory_unmap_span_list(heap->span_cache[iclass]);
			heap->span_cache[iclass] = 0;
		}
	}
#endif
	assert(!atomic_load_ptr(&heap->span_free_deferred));
}

//! Finalize the allocator
void
rpmalloc_finalize(void) {
	rpmalloc_thread_finalize();
	//rpmalloc_dump_statistics(stderr);

	//Free all thread caches and fully free spans
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = (heap_t*)atomic_load_ptr(&_memory_heaps[list_idx]);
		while (heap) {
			heap_t* next_heap = heap->next_heap;
			heap->finalize = 1;
			_memory_heap_global_finalize(heap);
			heap = next_heap;
		}
	}

#if ENABLE_GLOBAL_CACHE
	//Free global caches
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass)
		_memory_cache_finalize(&_memory_span_cache[iclass]);
#endif

#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	pthread_key_delete(_memory_thread_heap);
#endif
#if defined(_MSC_VER) && !defined(__clang__) && (!defined(BUILD_DYNAMIC_LINK) || !BUILD_DYNAMIC_LINK)
	FlsFree(fls_key);
	fls_key = 0;
#endif
#if ENABLE_STATISTICS
	//If you hit these asserts you probably have memory leaks (perhaps global scope data doing dynamic allocations) or double frees in your code
	assert(!atomic_load32(&_mapped_pages));
	assert(!atomic_load32(&_reserved_spans));
	assert(!atomic_load32(&_mapped_pages_os));
#endif

	_rpmalloc_initialized = 0;
}

//! Initialize thread, assign heap
extern inline void
rpmalloc_thread_initialize(void) {
	if (!get_thread_heap_raw()) {
		heap_t* heap = _memory_allocate_heap(0);
		if (heap) {
			_memory_statistics_inc(&_memory_active_heaps);
			set_thread_heap(heap);
#if defined(_MSC_VER) && !defined(__clang__) && (!defined(BUILD_DYNAMIC_LINK) || !BUILD_DYNAMIC_LINK)
			FlsSetValue(fls_key, heap);
#endif
		}
	}
}

//! Finalize thread, orphan heap
void
rpmalloc_thread_finalize(void) {
	heap_t* heap = get_thread_heap_raw();
	if (heap)
		_memory_heap_release_raw(heap);
#if defined(_MSC_VER) && !defined(__clang__) && (!defined(BUILD_DYNAMIC_LINK) || !BUILD_DYNAMIC_LINK)
	FlsSetValue(fls_key, 0);
#endif
}

int
rpmalloc_is_thread_initialized(void) {
	return (get_thread_heap_raw() != 0) ? 1 : 0;
}

const rpmalloc_config_t*
rpmalloc_config(void) {
	return &_memory_config;
}

//! Map new pages to virtual memory
static void*
_memory_map_os(size_t size, size_t* offset) {
	//Either size is a heap (a single page) or a (multiple) span - we only need to align spans, and only if larger than map granularity
	size_t padding = ((size >= _memory_span_size) && (_memory_span_size > _memory_map_granularity)) ? _memory_span_size : 0;
	assert(size >= _memory_page_size);
#if PLATFORM_WINDOWS
	//Ok to MEM_COMMIT - according to MSDN, "actual physical pages are not allocated unless/until the virtual addresses are actually accessed"
	void* ptr = VirtualAlloc(0, size + padding, (_memory_huge_pages ? MEM_LARGE_PAGES : 0) | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!ptr) {
		assert(!"Failed to map virtual memory block");
		return 0;
	}
#else
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED;
#  if defined(__APPLE__)
	int fd = (int)VM_MAKE_TAG(240U);
	if (_memory_huge_pages)
		fd |= VM_FLAGS_SUPERPAGE_SIZE_2MB;
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, flags, fd, 0);
#  elif defined(MAP_HUGETLB)
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, (_memory_huge_pages ? MAP_HUGETLB : 0) | flags, -1, 0);
#  else
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, flags, -1, 0);
#  endif
	if ((ptr == MAP_FAILED) || !ptr) {
		assert("Failed to map virtual memory block" == 0);
		return 0;
	}
#endif
	_memory_statistics_add(&_mapped_pages_os, (int32_t)((size + padding) >> _memory_page_size_shift));
	if (padding) {
		size_t final_padding = padding - ((uintptr_t)ptr & ~_memory_span_mask);
		assert(final_padding <= _memory_span_size);
		assert(final_padding <= padding);
		assert(!(final_padding % 8));
		ptr = pointer_offset(ptr, final_padding);
		*offset = final_padding >> 3;
	}
	assert((size < _memory_span_size) || !((uintptr_t)ptr & ~_memory_span_mask));
	return ptr;
}

//! Unmap pages from virtual memory
static void
_memory_unmap_os(void* address, size_t size, size_t offset, size_t release) {
	assert(release || (offset == 0));
	assert(!release || (release >= _memory_page_size));
	assert(size >= _memory_page_size);
	if (release && offset) {
		offset <<= 3;
		address = pointer_offset(address, -(int32_t)offset);
#if PLATFORM_POSIX
		//Padding is always one span size
		release += _memory_span_size;
#endif
	}
#if !DISABLE_UNMAP
#if PLATFORM_WINDOWS
	if (!VirtualFree(address, release ? 0 : size, release ? MEM_RELEASE : MEM_DECOMMIT)) {
		assert(!"Failed to unmap virtual memory block");
	}
#else
	if (release) {
		if (munmap(address, release)) {
			assert("Failed to unmap virtual memory block" == 0);
		}
	}
	else {
#if defined(POSIX_MADV_FREE)
		if (posix_madvise(address, size, POSIX_MADV_FREE))
#endif
		if (posix_madvise(address, size, POSIX_MADV_DONTNEED)) {
			assert("Failed to madvise virtual memory block as free" == 0);
		}
	}
#endif
#endif
	if (release)
		_memory_statistics_sub(&_mapped_pages_os, release >> _memory_page_size_shift);
}

// Extern interface

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc(size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return 0;
	}
#endif
	heap_t* heap = get_thread_heap();
	return _memory_allocate(heap, size);
}

extern inline void
rpfree(void* ptr) {
	_memory_deallocate(ptr);
}

extern inline RPMALLOC_ALLOCATOR void*
rpcalloc(size_t num, size_t size) {
	size_t total;
#if ENABLE_VALIDATE_ARGS
#if PLATFORM_WINDOWS
	int err = SizeTMult(num, size, &total);
	if ((err != S_OK) || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#else
	int err = __builtin_umull_overflow(num, size, &total);
	if (err || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#endif
#else
	total = num * size;
#endif
	heap_t* heap = get_thread_heap();
	void* block = _memory_allocate(heap, total);
	if (block)
		memset(block, 0, total);
	return block;
}

extern inline RPMALLOC_ALLOCATOR void*
rprealloc(void* ptr, size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return ptr;
	}
#endif
	heap_t* heap = get_thread_heap();
	return _memory_reallocate(heap, ptr, size, 0, 0);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_realloc(void* ptr, size_t alignment, size_t size, size_t oldsize,
                  unsigned int flags) {
#if ENABLE_VALIDATE_ARGS
	if ((size + alignment < size) || (alignment > _memory_page_size)) {
		errno = EINVAL;
		return 0;
	}
#endif
	heap_t* heap = get_thread_heap();
	return _memory_aligned_reallocate(heap, ptr, alignment, size, oldsize, flags);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_alloc(size_t alignment, size_t size) {
	heap_t* heap = get_thread_heap();
	return _memory_aligned_allocate(heap, alignment, size);
}

extern inline RPMALLOC_ALLOCATOR void*
rpaligned_calloc(size_t alignment, size_t num, size_t size) {
	size_t total;
#if ENABLE_VALIDATE_ARGS
#if PLATFORM_WINDOWS
	int err = SizeTMult(num, size, &total);
	if ((err != S_OK) || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#else
	int err = __builtin_umull_overflow(num, size, &total);
	if (err || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#endif
#else
	total = num * size;
#endif
	void* block = rpaligned_alloc(alignment, total);
	if (block)
		memset(block, 0, total);
	return block;
}

extern inline RPMALLOC_ALLOCATOR void*
rpmemalign(size_t alignment, size_t size) {
	return rpaligned_alloc(alignment, size);
}

extern inline int
rpposix_memalign(void **memptr, size_t alignment, size_t size) {
	if (memptr)
		*memptr = rpaligned_alloc(alignment, size);
	else
		return EINVAL;
	return *memptr ? 0 : ENOMEM;
}

extern inline size_t
rpmalloc_usable_size(void* ptr) {
	return (ptr ? _memory_usable_size(ptr) : 0);
}

extern inline void
rpmalloc_thread_collect(void) {
}

void
rpmalloc_thread_statistics(rpmalloc_thread_statistics_t* stats) {
	memset(stats, 0, sizeof(rpmalloc_thread_statistics_t));
	heap_t* heap = get_thread_heap_raw();
	if (!heap)
		return;

	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		size_class_t* size_class = _memory_size_class + iclass;
		heap_class_t* heap_class = heap->span_class + iclass;
		span_t* span = heap_class->partial_span;
		while (span) {
			size_t free_count = span->list_size;
			size_t block_count = size_class->block_count;
			if (span->free_list_limit < block_count)
				block_count = span->free_list_limit;
			free_count += (block_count - span->used_count);
			stats->sizecache = free_count * size_class->block_size;
			span = span->next;
		}
	}

#if ENABLE_THREAD_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		if (heap->span_cache[iclass])
			stats->spancache = (size_t)heap->span_cache[iclass]->list_size * (iclass + 1) * _memory_span_size;
	}
#endif

	span_t* deferred = (span_t*)atomic_load_ptr(&heap->span_free_deferred);
	while (deferred) {
		if (deferred->size_class != SIZE_CLASS_HUGE)
			stats->spancache = (size_t)deferred->span_count * _memory_span_size;
		deferred = (span_t*)deferred->free_list;
	}

#if ENABLE_STATISTICS
	stats->thread_to_global = (size_t)atomic_load64(&heap->thread_to_global);
	stats->global_to_thread = (size_t)atomic_load64(&heap->global_to_thread);

	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		stats->span_use[iclass].current = (size_t)atomic_load32(&heap->span_use[iclass].current);
		stats->span_use[iclass].peak = (size_t)atomic_load32(&heap->span_use[iclass].high);
		stats->span_use[iclass].to_global = (size_t)atomic_load32(&heap->span_use[iclass].spans_to_global);
		stats->span_use[iclass].from_global = (size_t)atomic_load32(&heap->span_use[iclass].spans_from_global);
		stats->span_use[iclass].to_cache = (size_t)atomic_load32(&heap->span_use[iclass].spans_to_cache);
		stats->span_use[iclass].from_cache = (size_t)atomic_load32(&heap->span_use[iclass].spans_from_cache);
		stats->span_use[iclass].to_reserved = (size_t)atomic_load32(&heap->span_use[iclass].spans_to_reserved);
		stats->span_use[iclass].from_reserved = (size_t)atomic_load32(&heap->span_use[iclass].spans_from_reserved);
		stats->span_use[iclass].map_calls = (size_t)atomic_load32(&heap->span_use[iclass].spans_map_calls);
	}
	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		stats->size_use[iclass].alloc_current = (size_t)atomic_load32(&heap->size_class_use[iclass].alloc_current);
		stats->size_use[iclass].alloc_peak = (size_t)heap->size_class_use[iclass].alloc_peak;
		stats->size_use[iclass].alloc_total = (size_t)atomic_load32(&heap->size_class_use[iclass].alloc_total);
		stats->size_use[iclass].free_total = (size_t)atomic_load32(&heap->size_class_use[iclass].free_total);
		stats->size_use[iclass].spans_to_cache = (size_t)atomic_load32(&heap->size_class_use[iclass].spans_to_cache);
		stats->size_use[iclass].spans_from_cache = (size_t)atomic_load32(&heap->size_class_use[iclass].spans_from_cache);
		stats->size_use[iclass].spans_from_reserved = (size_t)atomic_load32(&heap->size_class_use[iclass].spans_from_reserved);
		stats->size_use[iclass].map_calls = (size_t)atomic_load32(&heap->size_class_use[iclass].spans_map_calls);
	}
#endif
}

void
rpmalloc_global_statistics(rpmalloc_global_statistics_t* stats) {
	memset(stats, 0, sizeof(rpmalloc_global_statistics_t));
#if ENABLE_STATISTICS
	stats->mapped = (size_t)atomic_load32(&_mapped_pages) * _memory_page_size;
	stats->mapped_peak = (size_t)_mapped_pages_peak * _memory_page_size;
	stats->mapped_total = (size_t)atomic_load32(&_mapped_total) * _memory_page_size;
	stats->unmapped_total = (size_t)atomic_load32(&_unmapped_total) * _memory_page_size;
	stats->huge_alloc = (size_t)atomic_load32(&_huge_pages_current) * _memory_page_size;
	stats->huge_alloc_peak = (size_t)_huge_pages_peak * _memory_page_size;
#endif
#if ENABLE_GLOBAL_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		stats->cached += (size_t)atomic_load32(&_memory_span_cache[iclass].size) * (iclass + 1) * _memory_span_size;
	}
#endif
}

#if ENABLE_STATISTICS

static void
_memory_heap_dump_statistics(heap_t* heap, void* file) {
	fprintf(file, "Heap %d stats:\n", heap->id);
	fprintf(file, "Class   CurAlloc  PeakAlloc   TotAlloc    TotFree  BlkSize BlkCount SpansCur SpansPeak  PeakAllocMiB  ToCacheMiB FromCacheMiB FromReserveMiB MmapCalls\n");
	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		if (!atomic_load32(&heap->size_class_use[iclass].alloc_total))
			continue;
		fprintf(file, "%3u:  %10u %10u %10u %10u %8u %8u %8d %9d %13zu %11zu %12zu %14zu %9u\n", (uint32_t)iclass,
			atomic_load32(&heap->size_class_use[iclass].alloc_current),
			heap->size_class_use[iclass].alloc_peak,
			atomic_load32(&heap->size_class_use[iclass].alloc_total),
			atomic_load32(&heap->size_class_use[iclass].free_total),
			_memory_size_class[iclass].block_size,
			_memory_size_class[iclass].block_count,
			atomic_load32(&heap->size_class_use[iclass].spans_current),
			heap->size_class_use[iclass].spans_peak,
			((size_t)heap->size_class_use[iclass].alloc_peak * (size_t)_memory_size_class[iclass].block_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->size_class_use[iclass].spans_to_cache) * _memory_span_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->size_class_use[iclass].spans_from_cache) * _memory_span_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->size_class_use[iclass].spans_from_reserved) * _memory_span_size) / (size_t)(1024 * 1024),
			atomic_load32(&heap->size_class_use[iclass].spans_map_calls));
	}
	fprintf(file, "Spans  Current     Peak  PeakMiB  Cached  ToCacheMiB FromCacheMiB ToReserveMiB FromReserveMiB ToGlobalMiB FromGlobalMiB  MmapCalls\n");
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		if (!atomic_load32(&heap->span_use[iclass].high) && !atomic_load32(&heap->span_use[iclass].spans_map_calls))
			continue;
		fprintf(file, "%4u: %8d %8u %8zu %7u %11zu %12zu %12zu %14zu %11zu %13zu %10u\n", (uint32_t)(iclass + 1),
			atomic_load32(&heap->span_use[iclass].current),
			atomic_load32(&heap->span_use[iclass].high),
			((size_t)atomic_load32(&heap->span_use[iclass].high) * (size_t)_memory_span_size * (iclass + 1)) / (size_t)(1024 * 1024),
#if ENABLE_THREAD_CACHE
			heap->span_cache[iclass] ? heap->span_cache[iclass]->list_size : 0,
			((size_t)atomic_load32(&heap->span_use[iclass].spans_to_cache) * (iclass + 1) * _memory_span_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->span_use[iclass].spans_from_cache) * (iclass + 1) * _memory_span_size) / (size_t)(1024 * 1024),
#else
			0, 0ULL, 0ULL,
#endif
			((size_t)atomic_load32(&heap->span_use[iclass].spans_to_reserved) * (iclass + 1) * _memory_span_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->span_use[iclass].spans_from_reserved) * (iclass + 1) * _memory_span_size) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->span_use[iclass].spans_to_global) * (size_t)_memory_span_size * (iclass + 1)) / (size_t)(1024 * 1024),
			((size_t)atomic_load32(&heap->span_use[iclass].spans_from_global) * (size_t)_memory_span_size * (iclass + 1)) / (size_t)(1024 * 1024),
			atomic_load32(&heap->span_use[iclass].spans_map_calls));
	}
	fprintf(file, "ThreadToGlobalMiB GlobalToThreadMiB\n");
	fprintf(file, "%17zu %17zu\n", (size_t)atomic_load64(&heap->thread_to_global) / (size_t)(1024 * 1024), (size_t)atomic_load64(&heap->global_to_thread) / (size_t)(1024 * 1024));
}

#endif

void
rpmalloc_dump_statistics(void* file) {
#if ENABLE_STATISTICS
	//If you hit this assert, you still have active threads or forgot to finalize some thread(s)
	assert(atomic_load32(&_memory_active_heaps) == 0);
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
		while (heap) {
			int need_dump = 0;
			for (size_t iclass = 0; !need_dump && (iclass < SIZE_CLASS_COUNT); ++iclass) {
				if (!atomic_load32(&heap->size_class_use[iclass].alloc_total)) {
					assert(!atomic_load32(&heap->size_class_use[iclass].free_total));
					assert(!atomic_load32(&heap->size_class_use[iclass].spans_map_calls));
					continue;
				}
				need_dump = 1;
			}
			for (size_t iclass = 0; !need_dump && (iclass < LARGE_CLASS_COUNT); ++iclass) {
				if (!atomic_load32(&heap->span_use[iclass].high) && !atomic_load32(&heap->span_use[iclass].spans_map_calls))
					continue;
				need_dump = 1;
			}
			if (need_dump)
				_memory_heap_dump_statistics(heap, file);
			heap = heap->next_heap;
		}
	}
	fprintf(file, "Global stats:\n");
	size_t huge_current = (size_t)atomic_load32(&_huge_pages_current) * _memory_page_size;
	size_t huge_peak = (size_t)_huge_pages_peak * _memory_page_size;
	fprintf(file, "HugeCurrentMiB HugePeakMiB\n");
	fprintf(file, "%14zu %11zu\n", huge_current / (size_t)(1024 * 1024), huge_peak / (size_t)(1024 * 1024));

	size_t mapped = (size_t)atomic_load32(&_mapped_pages) * _memory_page_size;
	size_t mapped_os = (size_t)atomic_load32(&_mapped_pages_os) * _memory_page_size;
	size_t mapped_peak = (size_t)_mapped_pages_peak * _memory_page_size;
	size_t mapped_total = (size_t)atomic_load32(&_mapped_total) * _memory_page_size;
	size_t unmapped_total = (size_t)atomic_load32(&_unmapped_total) * _memory_page_size;
	size_t reserved_total = (size_t)atomic_load32(&_reserved_spans) * _memory_span_size;
	fprintf(file, "MappedMiB MappedOSMiB MappedPeakMiB MappedTotalMiB UnmappedTotalMiB ReservedTotalMiB\n");
	fprintf(file, "%9zu %11zu %13zu %14zu %16zu %16zu\n",
		mapped / (size_t)(1024 * 1024),
		mapped_os / (size_t)(1024 * 1024),
		mapped_peak / (size_t)(1024 * 1024),
		mapped_total / (size_t)(1024 * 1024),
		unmapped_total / (size_t)(1024 * 1024),
		reserved_total / (size_t)(1024 * 1024));

	fprintf(file, "\n");
#else
	(void)sizeof(file);
#endif
}

#if RPMALLOC_FIRST_CLASS_HEAPS

extern inline rpmalloc_heap_t*
rpmalloc_heap_acquire(void) {
	// Must be a pristine heap from newly mapped memory pages, or else memory blocks
	// could already be allocated from the heap which would (wrongly) be released when
	// heap is cleared with rpmalloc_heap_free_all()
	heap_t* heap = _memory_allocate_heap(1);
	_memory_statistics_inc(&_memory_active_heaps);
	return heap;
}

extern inline void
rpmalloc_heap_release(rpmalloc_heap_t* heap) {
	if (heap)
		_memory_heap_release(heap, 1);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_alloc(rpmalloc_heap_t* heap, size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return ptr;
	}
#endif
	return _memory_allocate(heap, size);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_aligned_alloc(rpmalloc_heap_t* heap, size_t alignment, size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return ptr;
	}
#endif
	return _memory_aligned_allocate(heap, alignment, size);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_calloc(rpmalloc_heap_t* heap, size_t num, size_t size) {
	return rpmalloc_heap_aligned_calloc(heap, 0, num, size);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_aligned_calloc(rpmalloc_heap_t* heap, size_t alignment, size_t num, size_t size) {
	size_t total;
#if ENABLE_VALIDATE_ARGS
#if PLATFORM_WINDOWS
	int err = SizeTMult(num, size, &total);
	if ((err != S_OK) || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#else
	int err = __builtin_umull_overflow(num, size, &total);
	if (err || (total >= MAX_ALLOC_SIZE)) {
		errno = EINVAL;
		return 0;
	}
#endif
#else
	total = num * size;
#endif
	void* block = _memory_aligned_allocate(heap, alignment, total);
	if (block)
		memset(block, 0, total);
	return block;
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_realloc(rpmalloc_heap_t* heap, void* ptr, size_t size, unsigned int flags) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return ptr;
	}
#endif
	return _memory_reallocate(heap, ptr, size, 0, flags);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc_heap_aligned_realloc(rpmalloc_heap_t* heap, void* ptr, size_t alignment, size_t size, unsigned int flags) {
#if ENABLE_VALIDATE_ARGS
	if ((size + alignment < size) || (alignment > _memory_page_size)) {
		errno = EINVAL;
		return 0;
	}
#endif
	return _memory_aligned_reallocate(heap, ptr, alignment, size, 0, flags);	
}

extern inline void
rpmalloc_heap_free(rpmalloc_heap_t* heap, void* ptr) {
	(void)sizeof(heap);
	_memory_deallocate(ptr);
}

extern inline void
rpmalloc_heap_free_all(rpmalloc_heap_t* heap) {
	span_t* span;
	span_t* next_span;

	_memory_heap_cache_adopt_deferred(heap, 0);

	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		span = heap->span_class[iclass].partial_span;
		while (span) {
			next_span = span->next;
			_memory_heap_cache_insert(heap, span);
			span = next_span;
		}
		span = heap->span_class[iclass].full_span;
		while (span) {
			next_span = span->next;
			_memory_heap_cache_insert(heap, span);
			span = next_span;
		}
	}
	memset(heap->span_class, 0, sizeof(heap->span_class));

	span = heap->large_huge_span;
	while (span) {
		next_span = span->next;
		if (UNEXPECTED(span->size_class == SIZE_CLASS_HUGE))
			_memory_deallocate_huge(span);
		else
			_memory_heap_cache_insert(heap, span);
		span = next_span;
	}
	heap->large_huge_span = 0;
	heap->full_span_count = 0;

#if ENABLE_THREAD_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		span = heap->span_cache[iclass];
#if ENABLE_GLOBAL_CACHE
		while (span) {
			assert(span->span_count == (iclass + 1));
			size_t release_count = (!iclass ? _memory_span_release_count : _memory_span_release_count_large);
			next_span = _memory_span_list_split(span, (uint32_t)release_count);
			_memory_statistics_add64(&heap->thread_to_global, (size_t)span->list_size * span->span_count * _memory_span_size);
			_memory_statistics_add(&heap->span_use[iclass].spans_to_global, span->list_size);
			_memory_global_cache_insert(span);
			span = next_span;
		}
#else
		if (span)
			_memory_unmap_span_list(span);
#endif
		heap->span_cache[iclass] = 0;
	}
#endif

#if ENABLE_STATISTICS
	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		atomic_store32(&heap->size_class_use[iclass].alloc_current, 0);
		atomic_store32(&heap->size_class_use[iclass].spans_current, 0);
	}
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		atomic_store32(&heap->span_use[iclass].current, 0 );
	}
#endif
}

extern inline void
rpmalloc_heap_thread_set_current(rpmalloc_heap_t* heap) {
	heap_t* prev_heap = get_thread_heap_raw();
	if (prev_heap != heap) {
		set_thread_heap(heap);
		if (prev_heap)
			rpmalloc_heap_release(prev_heap);
	}
}

#endif

#if ENABLE_PRELOAD || ENABLE_OVERRIDE

#include "malloc.c"

#endif

#endif
