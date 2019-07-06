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

/// Build time configurable limits
#ifndef HEAP_ARRAY_SIZE
//! Size of heap hashmap
#define HEAP_ARRAY_SIZE           47
#endif
#ifndef ENABLE_THREAD_CACHE
//! Enable per-thread cache
#define ENABLE_THREAD_CACHE       1
#endif
#ifndef ENABLE_ADAPTIVE_THREAD_CACHE
//! Enable adaptive size of per-thread cache (still bounded by THREAD_CACHE_MULTIPLIER hard limit)
#define ENABLE_ADAPTIVE_THREAD_CACHE  1
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
#ifndef ENABLE_PRELOAD
//! Support preloading
#define ENABLE_PRELOAD            0
#endif
#ifndef DISABLE_UNMAP
//! Disable unmapping memory pages
#define DISABLE_UNMAP             0
#endif
#ifndef DEFAULT_SPAN_MAP_COUNT
//! Default number of spans to map in call to map more virtual memory
#define DEFAULT_SPAN_MAP_COUNT    32
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
//! Multiplier for thread cache (cache limit will be span release count multiplied by this value)
#define THREAD_CACHE_MULTIPLIER 16
#endif
#endif

#if ENABLE_GLOBAL_CACHE && ENABLE_THREAD_CACHE
#ifndef ENABLE_UNLIMITED_GLOBAL_CACHE
//! Unlimited cache disables any global cache limitations
#define ENABLE_UNLIMITED_GLOBAL_CACHE ENABLE_UNLIMITED_CACHE
#endif
#if !ENABLE_UNLIMITED_GLOBAL_CACHE
//! Multiplier for global cache (cache limit will be span release count multiplied by this value)
#define GLOBAL_CACHE_MULTIPLIER 64
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
#  define FORCEINLINE __forceinline
#  define _Static_assert static_assert
#else
#  define FORCEINLINE inline __attribute__((__always_inline__))
#endif
#if PLATFORM_WINDOWS
#  define WIN32_LEAN_AND_MEAN
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
#    include <pthread.h>
#  endif
#  if defined(__HAIKU__)
#    include <OS.h>
#    include <pthread.h>
#  endif
#endif

#include <stdint.h>
#include <string.h>

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

/// Atomic access abstraction
#if defined(_MSC_VER) && !defined(__clang__)

typedef volatile long      atomic32_t;
typedef volatile long long atomic64_t;
typedef volatile void*     atomicptr_t;

#define atomic_thread_fence_acquire()
#define atomic_thread_fence_release()

static FORCEINLINE int32_t atomic_load32(atomic32_t* src) { return *src; }
static FORCEINLINE void    atomic_store32(atomic32_t* dst, int32_t val) { *dst = val; }
static FORCEINLINE int32_t atomic_incr32(atomic32_t* val) { return (int32_t)_InterlockedExchangeAdd(val, 1) + 1; }
static FORCEINLINE int32_t atomic_add32(atomic32_t* val, int32_t add) { return (int32_t)_InterlockedExchangeAdd(val, add) + add; }
static FORCEINLINE int64_t atomic_load64(atomic64_t* src) { return *src; }
static FORCEINLINE void    atomic_store64(atomic64_t* dst, int64_t val) { *dst = val; }
static FORCEINLINE int     atomic_cas64(atomic64_t* dst, int64_t val, int64_t ref) { return (_InterlockedCompareExchange64((volatile long long*)dst, (long long)val, (long long)ref) == (long long)ref) ? 1 : 0; }
static FORCEINLINE void*   atomic_load_ptr(atomicptr_t* src) { return (void*)*src; }
static FORCEINLINE void    atomic_store_ptr(atomicptr_t* dst, void* val) { *dst = val; }
#  if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
static FORCEINLINE int     atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) { return (_InterlockedCompareExchange64((volatile long long*)dst, (long long)val, (long long)ref) == (long long)ref) ? 1 : 0; }
#else
static FORCEINLINE int     atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) { return (_InterlockedCompareExchange((volatile long*)dst, (long)val, (long)ref) == (long)ref) ? 1 : 0; }
#endif

#else

#include <stdatomic.h>

typedef volatile _Atomic(int32_t) atomic32_t;
typedef volatile _Atomic(int64_t) atomic64_t;
typedef volatile _Atomic(void*) atomicptr_t;

#define atomic_thread_fence_acquire() atomic_thread_fence(memory_order_acquire)
#define atomic_thread_fence_release() atomic_thread_fence(memory_order_release)

static FORCEINLINE int32_t atomic_load32(atomic32_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static FORCEINLINE void    atomic_store32(atomic32_t* dst, int32_t val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static FORCEINLINE int32_t atomic_incr32(atomic32_t* val) { return atomic_fetch_add_explicit(val, 1, memory_order_relaxed) + 1; }
static FORCEINLINE int32_t atomic_add32(atomic32_t* val, int32_t add) { return atomic_fetch_add_explicit(val, add, memory_order_relaxed) + add; }
static FORCEINLINE int64_t atomic_load64(atomic64_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static FORCEINLINE void    atomic_store64(atomic64_t* dst, int64_t val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static FORCEINLINE int     atomic_cas64(atomic64_t* dst, int64_t val, int64_t ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_release, memory_order_acquire); }
static FORCEINLINE void*   atomic_load_ptr(atomicptr_t* src) { return atomic_load_explicit(src, memory_order_relaxed); }
static FORCEINLINE void    atomic_store_ptr(atomicptr_t* dst, void* val) { atomic_store_explicit(dst, val, memory_order_relaxed); }
static FORCEINLINE int     atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) { return atomic_compare_exchange_weak_explicit(dst, &ref, val, memory_order_release, memory_order_acquire); }

#endif

/// Preconfigured limits and sizes
//! Granularity of a small allocation block
#define SMALL_GRANULARITY         32
//! Small granularity shift count
#define SMALL_GRANULARITY_SHIFT   5
//! Number of small block size classes
#define SMALL_CLASS_COUNT         64
//! Maximum size of a small block
#define SMALL_SIZE_LIMIT          (SMALL_GRANULARITY * SMALL_CLASS_COUNT)
//! Granularity of a medium allocation block
#define MEDIUM_GRANULARITY        512
//! Medium granularity shift count
#define MEDIUM_GRANULARITY_SHIFT  9
//! Number of medium block size classes
#define MEDIUM_CLASS_COUNT        59
//! Total number of small + medium size classes
#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)
//! Number of large block size classes
#define LARGE_CLASS_COUNT         32
//! Maximum size of a medium block
#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY * MEDIUM_CLASS_COUNT))
//! Maximum size of a large block
#define LARGE_SIZE_LIMIT          ((LARGE_CLASS_COUNT * _memory_span_size) - SPAN_HEADER_SIZE)
//! Size of a span header
#define SPAN_HEADER_SIZE          96

#if ENABLE_VALIDATE_ARGS
//! Maximum allocation size to avoid integer overflow
#undef  MAX_ALLOC_SIZE
#define MAX_ALLOC_SIZE            (((size_t)-1) - _memory_span_size)
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

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
//! Free list flag indicating span is active
#define FREE_LIST_FLAG_ACTIVE 0x8000000000000000ULL

#if ENABLE_ADAPTIVE_THREAD_CACHE
struct span_use_t {
	//! Current number of spans used (actually used, not in cache)
	unsigned int current;
	//! High water mark of spans used
	unsigned int high;
};
typedef struct span_use_t span_use_t;
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
	//!	Owning thread ID
	atomicptr_t thread_id;
	//! Free list
	void* free_list;
	//! Size class
	uint32_t    size_class;
	//! Free count when not active
	uint32_t    free_count;
	//! Deferred free list (count in 32 high bits, block index in 32 low bits)
	atomic64_t  free_list_deferred;
	//! Index of last block initialized in free list
	uint32_t    free_list_limit;
	//! Remaining span counter, for master spans
	atomic32_t  remaining_spans;
	//! Owning heap
	heap_t*     heap;
	//! Flags and counters
	uint32_t    flags;
	//! Total span counter for master spans, distance for subspans
	uint32_t    total_spans_or_distance;
	//! Number of spans
	uint32_t    span_count;
	//! Alignment offset
	uint32_t    align_offset;
	//! Next span
	span_t*     next;
	//! Previous span
	span_t*     prev;
	//! Span list size when part of a list
	uint32_t    list_size;
};
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "span size mismatch");

struct heap_class_t {
	//! Free list of active span
	void*        free_list;
	//! Active span
	span_t*      active_span;
	//! List of semi-used spans with free blocks for each size class (double linked list)
	span_t*      used_span;
};

struct heap_t {
	//! Active and semi-used span data per size class
	heap_class_t span_class[SIZE_CLASS_COUNT];
#if ENABLE_THREAD_CACHE
	//! List of free spans (single linked list)
	span_t*      span_cache[LARGE_CLASS_COUNT];
	//! List of deferred free spans of class 0 (single linked list)
	atomicptr_t  span_cache_deferred;
#endif
#if ENABLE_ADAPTIVE_THREAD_CACHE
	//! Current and high water mark of spans used per span count
	span_use_t   span_use[LARGE_CLASS_COUNT];
#endif
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
	//! Owning thread
	void*        owner_thread;
#if ENABLE_STATISTICS
	//! Number of bytes transitioned thread -> global
	size_t       thread_to_global;
	//! Number of bytes transitioned global -> thread
	size_t       global_to_thread;
#endif
};

struct size_class_t {
	//! Size of blocks in this class
	uint32_t size;
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
//! Configuration
static rpmalloc_config_t _memory_config;
//! Memory page size
static size_t _memory_page_size;
//! Shift to divide by page size
static size_t _memory_page_size_shift;
//! Granularity at which memory pages are mapped by OS
static size_t _memory_map_granularity;
//! Size of a span of memory pages
static size_t _memory_span_size;
//! Shift to divide by span size
static size_t _memory_span_size_shift;
//! Mask to get to start of a memory span
static uintptr_t _memory_span_mask;
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
//! Running orphan counter to avoid ABA issues in linked list
static atomic32_t _memory_orphan_counter;
#if ENABLE_STATISTICS
//! Active heap count
static atomic32_t _memory_active_heaps;
//! Total number of currently mapped memory pages
static atomic32_t _mapped_pages;
//! Total number of currently lost spans
static atomic32_t _reserved_spans;
//! Running counter of total number of mapped memory pages since start
static atomic32_t _mapped_total;
//! Running counter of total number of unmapped memory pages since start
static atomic32_t _unmapped_total;
//! Total number of currently mapped memory pages in OS calls
static atomic32_t _mapped_pages_os;
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

//! Get the current thread heap
static FORCEINLINE heap_t*
get_thread_heap(void) {
#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	return pthread_getspecific(_memory_thread_heap);
#else
	return _memory_thread_heap;
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
}

//! Fast thread ID
static inline void*
get_thread_id(void) {
#if PLATFORM_WINDOWS
	return (void*)NtCurrentTeb();
#else
	uintptr_t tid;
#  if defined(__i386__) || defined(__MACH__)
	// 32-bit always uses GS, x86_64 macOS uses GS
	__asm__("movl %%gs:0, %0" : "=r" (tid) ::);
#  elif defined(__x86_64__)
	// x86_64 Linux, BSD uses FS
	__asm__("movq %%fs:0, %0" : "=r" (tid) ::);
#  elif defined(__arm__)
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r" (tid));
#  elif defined(__aarch64__)
	asm volatile ("mrs %0, tpidr_el0" : "=r" (tid));
#  else
	void* thread_heap = get_thread_heap();
	tid = (uintptr_t)thread_heap;
#  endif
	return (void*)tid;
#endif
}

//! Default implementation to map more virtual memory
static void*
_memory_map_os(size_t size, size_t* offset);

//! Default implementation to unmap virtual memory
static void
_memory_unmap_os(void* address, size_t size, size_t offset, size_t release);

//! Lookup a memory heap from heap ID
static heap_t*
_memory_heap_lookup(int32_t id) {
	uint32_t list_idx = id % HEAP_ARRAY_SIZE;
	heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
	while (heap && (heap->id != id))
		heap = heap->next_heap;
	return heap;
}

#if ENABLE_STATISTICS
#  define _memory_statistics_add(atomic_counter, value) atomic_add32(atomic_counter, (int32_t)(value))
#  define _memory_statistics_sub(atomic_counter, value) atomic_add32(atomic_counter, -(int32_t)(value))
#else
#  define _memory_statistics_add(atomic_counter, value) do {} while(0)
#  define _memory_statistics_sub(atomic_counter, value) do {} while(0)
#endif

static void
_memory_heap_cache_insert(heap_t* heap, span_t* span);

//! Map more virtual memory
static void*
_memory_map(size_t size, size_t* offset) {
	assert(!(size % _memory_page_size));
	assert(size >= _memory_page_size);
	_memory_statistics_add(&_mapped_pages, (size >> _memory_page_size_shift));
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

//! Map in memory pages for the given number of spans (or use previously reserved pages)
static span_t*
_memory_map_spans(heap_t* heap, size_t span_count) {
	if (span_count <= heap->spans_reserved) {
		span_t* span = heap->span_reserve;
		heap->span_reserve = pointer_offset(span, span_count * _memory_span_size);
		heap->spans_reserved -= span_count;
		if (span == heap->span_reserve_master) {
			assert(span->flags & SPAN_FLAG_MASTER);
		} else {
			//Declare the span to be a subspan with given distance from master span
			uint32_t distance = (uint32_t)((uintptr_t)pointer_diff(span, heap->span_reserve_master) >> _memory_span_size_shift);
			span->flags = SPAN_FLAG_SUBSPAN;
			span->total_spans_or_distance = distance;
			span->align_offset = 0;
		}
		span->span_count = (uint32_t)span_count;
		return span;
	}

	//If we already have some, but not enough, reserved spans, release those to heap cache and map a new
	//full set of spans. Otherwise we would waste memory if page size > span size (huge pages)
	size_t request_spans = (span_count > _memory_span_map_count) ? span_count : _memory_span_map_count;
	if ((_memory_page_size > _memory_span_size) && ((request_spans * _memory_span_size) % _memory_page_size))
		request_spans += _memory_span_map_count - (request_spans % _memory_span_map_count);
	size_t align_offset = 0;
	span_t* span = _memory_map(request_spans * _memory_span_size, &align_offset);
	if (!span)
		return span;
	span->align_offset = (uint32_t)align_offset;
	span->total_spans_or_distance = (uint32_t)request_spans;
	span->span_count = (uint32_t)span_count;
	span->flags = SPAN_FLAG_MASTER;
	atomic_store32(&span->remaining_spans, (int32_t)request_spans);
	_memory_statistics_add(&_reserved_spans, request_spans);
	if (request_spans > span_count) {
		if (heap->spans_reserved) {
			span_t* prev_span = heap->span_reserve;
			if (prev_span == heap->span_reserve_master) {
				assert(prev_span->flags & SPAN_FLAG_MASTER);
			} else {
				uint32_t distance = (uint32_t)((uintptr_t)pointer_diff(prev_span, heap->span_reserve_master) >> _memory_span_size_shift);
				prev_span->flags = SPAN_FLAG_SUBSPAN;
				prev_span->total_spans_or_distance = distance;
				prev_span->align_offset = 0;
			}
			prev_span->span_count = (uint32_t)heap->spans_reserved;
			_memory_heap_cache_insert(heap, prev_span);
		}
		heap->span_reserve_master = span;
		heap->span_reserve = pointer_offset(span, span_count * _memory_span_size);
		heap->spans_reserved = request_spans - span_count;
	}
	return span;
}

//! Unmap memory pages for the given number of spans (or mark as unused if no partial unmappings)
static void
_memory_unmap_span(span_t* span) {
	size_t span_count = span->span_count;
	assert((span->flags & SPAN_FLAG_MASTER) || (span->flags & SPAN_FLAG_SUBSPAN));
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));

	int is_master = !!(span->flags & SPAN_FLAG_MASTER);
	span_t* master = is_master ? span : (pointer_offset(span, -(int32_t)(span->total_spans_or_distance * _memory_span_size)));

	assert(is_master || (span->flags & SPAN_FLAG_SUBSPAN));
	assert(master->flags & SPAN_FLAG_MASTER);

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
			unmap_count = master->total_spans_or_distance;
		_memory_statistics_sub(&_reserved_spans, unmap_count);
		_memory_unmap(master, unmap_count * _memory_span_size, master->align_offset, master->total_spans_or_distance * _memory_span_size);
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

//! Split a super span in two
static span_t*
_memory_span_split(span_t* span, size_t use_count) {
	size_t current_count = span->span_count;
	uint32_t distance = 0;
	assert(current_count > use_count);
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));

	span->span_count = (uint32_t)use_count;
	if (span->flags & SPAN_FLAG_SUBSPAN)
		distance = span->total_spans_or_distance;

	//Setup remainder as a subspan
	span_t* subspan = pointer_offset(span, use_count * _memory_span_size);
	subspan->flags = SPAN_FLAG_SUBSPAN;
	subspan->total_spans_or_distance = (uint32_t)(distance + use_count);
	subspan->span_count = (uint32_t)(current_count - use_count);
	subspan->align_offset = 0;
	return subspan;
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

#endif
#if ENABLE_THREAD_CACHE

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

//! Add a span to a double linked list
static void
_memory_span_list_doublelink_add(span_t** head, span_t* span) {
	if (*head) {
		(*head)->prev = span;
		span->next = *head;
	} else {
		span->next = 0;
	}
	*head = span;
}

//! Remove a span from a double linked list
static void
_memory_span_list_doublelink_remove(span_t** head, span_t* span) {
	if (*head == span) {
		*head = span->next;
	} else {
		span_t* next_span = span->next;
		span_t* prev_span = span->prev;
		if (next_span)
			next_span->prev = prev_span;
		prev_span->next = next_span;
	}
}

#if ENABLE_GLOBAL_CACHE

//! Insert the given list of memory page spans in the global cache
static void
_memory_cache_insert(global_cache_t* cache, span_t* span, size_t cache_limit) {
	assert((span->list_size == 1) || (span->next != 0));
	int32_t list_size = (int32_t)span->list_size;
	//Unmap if cache has reached the limit
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
		span->prev = (void*)((uintptr_t)current_cache & _memory_span_mask);
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
			span_t* span = (void*)span_ptr;
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
	span_t* span = (void*)((uintptr_t)current_cache & _memory_span_mask);
	while (span) {
		span_t* skip_span = (void*)((uintptr_t)span->prev & _memory_span_mask);
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

#if ENABLE_THREAD_CACHE

//! Adopt the deferred span cache list
static void
_memory_heap_cache_adopt_deferred(heap_t* heap) {
	atomic_thread_fence_acquire();
	span_t* span = atomic_load_ptr(&heap->span_cache_deferred);
	if (!span)
		return;
	do {
		span = atomic_load_ptr(&heap->span_cache_deferred);
	} while (!atomic_cas_ptr(&heap->span_cache_deferred, 0, span));
	while (span) {
		span_t* next_span = span->next;
		assert(atomic_load_ptr(&span->thread_id) == get_thread_id());
		_memory_span_list_push(&heap->span_cache[0], span);
		span = next_span;
	}
}

#endif

//! Insert a single span into thread heap cache, releasing to global cache if overflow
static void
_memory_heap_cache_insert(heap_t* heap, span_t* span) {
#if ENABLE_THREAD_CACHE
	size_t span_count = span->span_count;
	size_t idx = span_count - 1;
	if (!idx)
		_memory_heap_cache_adopt_deferred(heap);
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
		size_t high_mark = heap->span_use[idx].high;
		const size_t min_limit = (high_mark >> 2) + release_count + 1;
		if (current_cache_size < min_limit)
			return;
#else
		return;
#endif
	}
	heap->span_cache[idx] = _memory_span_list_split(span, release_count);
	assert(span->list_size == release_count);
#if ENABLE_STATISTICS
	heap->thread_to_global += (size_t)span->list_size * span_count * _memory_span_size;
#endif
#if ENABLE_GLOBAL_CACHE
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
_memory_heap_cache_extract(heap_t* heap, size_t span_count) {
#if ENABLE_THREAD_CACHE
	size_t idx = span_count - 1;
	if (!idx)
		_memory_heap_cache_adopt_deferred(heap);
	//Step 1: check thread cache
	if (heap->span_cache[idx])
		return _memory_span_list_pop(&heap->span_cache[idx]);
#endif
	//Step 2: Check reserved spans
	if (heap->spans_reserved >= span_count)
		return _memory_map_spans(heap, span_count);
#if ENABLE_THREAD_CACHE
	//Step 3: Check larger super spans and split if we find one
	span_t* span = 0;
	for (++idx; idx < LARGE_CLASS_COUNT; ++idx) {
		if (heap->span_cache[idx]) {
			span = _memory_span_list_pop(&heap->span_cache[idx]);
			break;
		}
	}
	if (span) {
		//Split the span and store as reserved if no previously reserved spans, or in thread cache otherwise
		size_t got_count = span->span_count;
		assert(got_count > span_count);
		span_t* subspan = _memory_span_split(span, span_count);
		assert((span->span_count + subspan->span_count) == got_count);
		assert(span->span_count == span_count);
		if (!heap->spans_reserved) {
			heap->spans_reserved = got_count - span_count;
			heap->span_reserve = subspan;
			heap->span_reserve_master = pointer_offset(subspan, -(int32_t)((uint32_t)subspan->total_spans_or_distance * _memory_span_size));
		} else {
			_memory_heap_cache_insert(heap, subspan);
		}
		return span;
	}
#if ENABLE_GLOBAL_CACHE
	//Step 4: Extract from global cache
	idx = span_count - 1;
	heap->span_cache[idx] = _memory_global_cache_extract(span_count);
	if (heap->span_cache[idx]) {
#if ENABLE_STATISTICS
		heap->global_to_thread += (size_t)heap->span_cache[idx]->list_size * span_count * _memory_span_size;
#endif
		return _memory_span_list_pop(&heap->span_cache[idx]);
	}
#endif
#endif
	return 0;
}

static inline void*
free_list_pop(void** list) {
	void* block = *list;
	*list = *((void**)block);
	return block;
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
		void* block_end = pointer_offset(block_start, block_size * block_count);
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

//! Allocate a small/medium sized memory block from the given heap
static void*
_memory_allocate_from_heap_fallback(heap_t* heap, uint32_t class_idx) {
	size_class_t* size_class = _memory_size_class + class_idx;
	heap_class_t* heap_class = heap->span_class + class_idx;
	void* block;

	span_t* active_span = heap_class->active_span;
	if (active_span) {
		atomic_store_ptr(&active_span->thread_id, get_thread_id());
		//Swap in free list if not empty
		if (active_span->free_list) {
			heap_class->free_list = active_span->free_list;
			active_span->free_list = 0;
			return free_list_pop(&heap_class->free_list);
		}
		//If the span did not fully initialize free list, link up another page worth of blocks
		if (active_span->free_list_limit < size_class->block_count) {
			void* block_start = pointer_offset(active_span, SPAN_HEADER_SIZE + (active_span->free_list_limit * size_class->size));
			active_span->free_list_limit += free_list_partial_init(&heap_class->free_list, &block,
				(void*)((uintptr_t)block_start & ~(_memory_page_size - 1)), block_start,
				size_class->block_count - active_span->free_list_limit, size_class->size);
			return block;
		}
		//Swap in deferred free list (if list has at least one element the compound 64-bit value will not be set to raw flag)
retry_deferred_free_list:
		atomic_store_ptr(&active_span->thread_id, get_thread_id());
		atomic_thread_fence_acquire();
		if ((uint64_t)atomic_load64(&active_span->free_list_deferred) != FREE_LIST_FLAG_ACTIVE) {
			uint64_t free_list_deferred;
			do {
				// Safe to assume nothing else can reset to a null list here, only owning thread can do this
				free_list_deferred = (uint64_t)atomic_load64(&active_span->free_list_deferred);
			} while (!atomic_cas64(&active_span->free_list_deferred, (int64_t)FREE_LIST_FLAG_ACTIVE, (int64_t)free_list_deferred));

			heap_class->free_list = pointer_offset(active_span, SPAN_HEADER_SIZE + (size_class->size * (uint32_t)free_list_deferred));
			assert(heap_class->free_list);
			return free_list_pop(&heap_class->free_list);
		}

		//If the active span is fully allocated, mark span as free floating (fully allocated and not part of any list)
		assert(!heap_class->free_list);
		assert(active_span->free_list_limit == size_class->block_count);
		int free_floating = atomic_cas64(&active_span->free_list_deferred, 0, (int64_t)FREE_LIST_FLAG_ACTIVE);
		active_span->free_list = 0;
		active_span->free_count = 0;
		//If CAS fails some other thread freed up additional blocks, then loop around and try again
		if (!free_floating)
			goto retry_deferred_free_list;
		active_span = 0;
		heap_class->active_span = 0;
	}
	assert(!heap_class->free_list);
	assert(!heap_class->active_span);
	assert(!active_span);

	//Try promoting a semi-used span
	active_span = heap_class->used_span;
	if (active_span) {
		atomic_store_ptr(&active_span->thread_id, get_thread_id());
		//Mark span as active
		uint64_t free_list_deferred, new_list_deferred;
		do {
			free_list_deferred = (uint64_t)atomic_load64(&active_span->free_list_deferred);
			new_list_deferred = free_list_deferred | FREE_LIST_FLAG_ACTIVE;
		} while (!atomic_cas64(&active_span->free_list_deferred, (int64_t)new_list_deferred, (int64_t)free_list_deferred));
		//Move data to heap size class, set span as active and remove the span from used list
		heap_class->free_list = active_span->free_list;
		heap_class->active_span = active_span;
		heap_class->used_span = active_span->next;
		//A span which has been put in the used list has always been fully initialized
		active_span->free_list_limit = size_class->block_count;
		active_span->free_list = 0;
		//Grab either from the local free list or go back and try the deferred free list
		if (heap_class->free_list)
			return free_list_pop(&heap_class->free_list);
		assert(atomic_load64(&active_span->free_list_deferred) != (int64_t)FREE_LIST_FLAG_ACTIVE);
		goto retry_deferred_free_list;
	}

	assert(!heap_class->free_list);
	assert(!heap_class->active_span);
	assert(!heap_class->used_span);

	//Find a span in one of the cache levels
	active_span = _memory_heap_cache_extract(heap, 1);
	if (!active_span) {
		//Final fallback, map in more virtual memory (potentially using a previously reserved span)
		active_span = _memory_map_spans(heap, 1);
		if (!active_span)
			return 0;
	}

#if ENABLE_ADAPTIVE_THREAD_CACHE
	++heap->span_use[0].current;
	if (heap->span_use[0].current > heap->span_use[0].high)
		heap->span_use[0].high = heap->span_use[0].current;
#endif

	//Mark span as owned by this heap and set base data
	assert(active_span->span_count == 1);
	active_span->size_class = (uint16_t)class_idx;
	active_span->heap = heap;
	active_span->flags &= ~SPAN_FLAG_ALIGNED_BLOCKS;
	atomic_store_ptr(&active_span->thread_id, get_thread_id());

	if (size_class->block_count > 1) {
		//Setup free list. Only initialize one system page worth of free blocks in list
		heap_class->active_span = active_span;
		active_span->free_list_limit = free_list_partial_init(&heap_class->free_list, &block, 
			active_span, pointer_offset(active_span, SPAN_HEADER_SIZE),
			size_class->block_count, size_class->size);
		active_span->free_list = 0;
		atomic_store64(&active_span->free_list_deferred, (int64_t)FREE_LIST_FLAG_ACTIVE);
	} else {
		//Single block span (should not happen with default size configurations)
		block = pointer_offset(active_span, SPAN_HEADER_SIZE);
		active_span->free_list = 0;
		active_span->free_list_limit = 1;
		atomic_store64(&active_span->free_list_deferred, 0);
	}
	atomic_thread_fence_release();

	//Return first block in span
	return block;
}

//! Allocate a large sized memory block from the given heap
static void*
_memory_allocate_large_from_heap(heap_t* heap, size_t size) {
	//Calculate number of needed max sized spans (including header)
	//Since this function is never called if size > LARGE_SIZE_LIMIT
	//the span_count is guaranteed to be <= LARGE_CLASS_COUNT
	size += SPAN_HEADER_SIZE;
	size_t span_count = size >> _memory_span_size_shift;
	if (size & (_memory_span_size - 1))
		++span_count;
	size_t idx = span_count - 1;
#if ENABLE_ADAPTIVE_THREAD_CACHE
	++heap->span_use[idx].current;
	if (heap->span_use[idx].current > heap->span_use[idx].high)
		heap->span_use[idx].high = heap->span_use[idx].current;
#endif

	//Step 1: Find span in one of the cache levels
	span_t* span = _memory_heap_cache_extract(heap, span_count);
	if (!span) {
		//Step 2: Map in more virtual memory
		span = _memory_map_spans(heap, span_count);
		if (!span)
			return span;
	}

	//Mark span as owned by this heap and set base data
	assert(span->span_count == span_count);
	span->size_class = (uint16_t)(SIZE_CLASS_COUNT + idx);
	span->heap = heap;
	atomic_store_ptr(&span->thread_id, get_thread_id());
	atomic_thread_fence_release();

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

//! Allocate a new heap
static heap_t*
_memory_allocate_heap(void) {
	void* raw_heap;
	void* next_raw_heap;
	uintptr_t orphan_counter;
	heap_t* heap;
	heap_t* next_heap;
	//Try getting an orphaned heap
	atomic_thread_fence_acquire();
	do {
		raw_heap = atomic_load_ptr(&_memory_orphan_heaps);
		heap = (void*)((uintptr_t)raw_heap & ~(uintptr_t)0x1FF);
		if (!heap)
			break;
		next_heap = heap->next_orphan;
		orphan_counter = (uintptr_t)atomic_incr32(&_memory_orphan_counter);
		next_raw_heap = (void*)((uintptr_t)next_heap | (orphan_counter & (uintptr_t)0x1FF));
	} while (!atomic_cas_ptr(&_memory_orphan_heaps, next_raw_heap, raw_heap));

	if (!heap) {
		//Map in pages for a new heap
		size_t align_offset = 0;
		heap = _memory_map((1 + (sizeof(heap_t) >> _memory_page_size_shift)) * _memory_page_size, &align_offset);
		if (!heap)
			return heap;
		memset(heap, 0, sizeof(heap_t));
		heap->align_offset = align_offset;

		//Get a new heap ID
		do {
			heap->id = atomic_incr32(&_memory_heap_id);
			if (_memory_heap_lookup(heap->id))
				heap->id = 0;
		} while (!heap->id);

		//Link in heap in heap ID map
		size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
		do {
			next_heap = atomic_load_ptr(&_memory_heaps[list_idx]);
			heap->next_heap = next_heap;
		} while (!atomic_cas_ptr(&_memory_heaps[list_idx], heap, next_heap));
	}

	return heap;
}

//! Deallocate the given small/medium memory block in the current thread local heap
static void
_memory_deallocate_direct(span_t* span, void* p) {
	const uint32_t class_idx = span->size_class;
	size_class_t* size_class = _memory_size_class + class_idx;
	void* block = p;

	atomic_thread_fence_acquire();
	uint64_t free_list_deferred = (uint64_t)atomic_load64(&span->free_list_deferred);
	uint64_t is_active = (free_list_deferred & FREE_LIST_FLAG_ACTIVE);

	assert(atomic_load_ptr(&span->thread_id) == get_thread_id());
	*((void**)block) = span->free_list;
	span->free_list = block;

	if (is_active)
		return;

	++span->free_count;
	assert(span->free_count <= size_class->block_count);

	//Not active span, check if the span will become completely free
	uint32_t free_count = span->free_count;
	if (free_count < size_class->block_count) {
		uint32_t list_size = (uint32_t)((free_list_deferred & ~FREE_LIST_FLAG_ACTIVE) >> 32ULL);
		free_count += list_size;
	}
	assert(span->free_count <= size_class->block_count);
	if (free_count == size_class->block_count) {
		heap_t* heap = get_thread_heap();
		heap_class_t* heap_class = heap->span_class + class_idx;
		assert(heap_class->active_span != span);
		//Remove from partial free list if we had a previous locally free block and add to heap cache
		if (span->free_count > 1)
			_memory_span_list_doublelink_remove(&heap_class->used_span, span);
#if ENABLE_ADAPTIVE_THREAD_CACHE
		if (heap->span_use[0].current)
			--heap->span_use[0].current;
#endif
		_memory_heap_cache_insert(heap, span);
		return;
	}
	if (span->free_count == 1) {
		heap_t* heap = get_thread_heap();
		heap_class_t* heap_class = heap->span_class + class_idx;
		assert(heap_class->active_span != span);
		_memory_span_list_doublelink_add(&heap_class->used_span, span);
	}
}

//! Deallocate the given large memory block to the given heap
static void
_memory_deallocate_large_direct(heap_t* heap, span_t* span) {
	//Decrease counter
	assert(span->span_count == ((size_t)span->size_class - SIZE_CLASS_COUNT + 1));
	assert(span->size_class >= SIZE_CLASS_COUNT);
	assert(span->size_class - SIZE_CLASS_COUNT < LARGE_CLASS_COUNT);
	assert(!(span->flags & SPAN_FLAG_MASTER) || !(span->flags & SPAN_FLAG_SUBSPAN));
	assert((span->flags & SPAN_FLAG_MASTER) || (span->flags & SPAN_FLAG_SUBSPAN));
#if ENABLE_ADAPTIVE_THREAD_CACHE
	size_t idx = span->span_count - 1;
	if (heap->span_use[idx].current)
		--heap->span_use[idx].current;
#endif
	if ((span->span_count > 1) && !heap->spans_reserved) {
		heap->span_reserve = span;
		heap->spans_reserved = span->span_count;
		if (span->flags & SPAN_FLAG_MASTER) {
			heap->span_reserve_master = span;
		} else { //SPAN_FLAG_SUBSPAN
			uint32_t distance = span->total_spans_or_distance;
			span_t* master = pointer_offset(span, -(int32_t)(distance * _memory_span_size));
			heap->span_reserve_master = master;
			assert(master->flags & SPAN_FLAG_MASTER);
			assert(atomic_load32(&master->remaining_spans) >= (int32_t)span->span_count);
		}
	} else {
		//Insert into cache list
		_memory_heap_cache_insert(heap, span);
	}
}

//! Put the block in the deferred free list of the owning span
static void
_memory_deallocate_defer(span_t* span, void* p) {
	size_class_t* size_class = _memory_size_class + span->size_class;
	void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
	uint32_t block_offset = (uint32_t)pointer_diff(p, blocks_start);
	uint32_t block_idx = block_offset / (uint32_t)size_class->size;
	void* block = p;
	
	uint64_t free_list, new_free_list;
	atomic_thread_fence_acquire();
	do {
		free_list = (uint64_t)atomic_load64(&span->free_list_deferred);
		uint32_t list_size = (uint32_t)(free_list >> 32ULL) & 0xFFFF;
		uint32_t new_list_size = list_size + 1;
		new_free_list = (free_list & FREE_LIST_FLAG_ACTIVE) | ((uint64_t)new_list_size << 32ULL) | (uint64_t)block_idx;
		if (new_list_size == size_class->block_count) {
			//Span will be completely freed by deferred deallocations
			if (free_list & FREE_LIST_FLAG_ACTIVE) {
				//Active span, just leave it
			} else {
				//Free floating span, so no other thread can currently touch it
				span_t* last_head;
				heap_t* heap = span->heap;
				do {
					last_head = atomic_load_ptr(&heap->span_cache_deferred);
					span->next = last_head;
				} while (!atomic_cas_ptr(&heap->span_cache_deferred, span, last_head));
				return;
			}
		}
		*((void**)block) = list_size ? pointer_offset(blocks_start, size_class->size * (uint32_t)free_list) : 0;
	} while (!atomic_cas64(&span->free_list_deferred, (int64_t)new_free_list, (int64_t)free_list));
}

//! Allocate a block of the given size
static void*
_memory_allocate(size_t size) {
	heap_t* heap = get_thread_heap();
	if (size <= SMALL_SIZE_LIMIT) {
		//Small sizes have unique size classes
		const uint32_t class_idx = (uint32_t)(size >> SMALL_GRANULARITY_SHIFT);
		if (heap->span_class[class_idx].free_list)
			return free_list_pop(&heap->span_class[class_idx].free_list);
		return _memory_allocate_from_heap_fallback(heap, class_idx);
	} else if (size <= _memory_medium_size_limit) {
		//Calculate the size class index and do a dependent lookup of the final class index (in case of merged classes)
		const uint32_t base_idx = (uint32_t)(SMALL_CLASS_COUNT + ((size - SMALL_SIZE_LIMIT) >> MEDIUM_GRANULARITY_SHIFT));
		const uint32_t class_idx = _memory_size_class[base_idx].class_idx;
		if (heap->span_class[class_idx].free_list)
			return free_list_pop(&heap->span_class[class_idx].free_list);
		return _memory_allocate_from_heap_fallback(heap, class_idx);
	} else if (size <= LARGE_SIZE_LIMIT) {
		return _memory_allocate_large_from_heap(heap, size);
	}

	//Oversized, allocate pages directly
	size += SPAN_HEADER_SIZE;
	size_t num_pages = size >> _memory_page_size_shift;
	if (size & (_memory_page_size - 1))
		++num_pages;
	size_t align_offset = 0;
	span_t* span = _memory_map(num_pages * _memory_page_size, &align_offset);
	if (!span)
		return span;
	atomic_store_ptr(&span->thread_id, 0);
	//Store page count in span_count
	span->span_count = (uint32_t)num_pages;
	span->align_offset = (uint32_t)align_offset;

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

//! Deallocate the given block
static void
_memory_deallocate(void* p) {
	if (!p)
		return;

	//Grab the span (always at start of span, using span alignment)
	span_t* span = (void*)((uintptr_t)p & _memory_span_mask);
	void* thread_id = atomic_load_ptr(&span->thread_id);
	if (thread_id) {
		if (span->size_class < SIZE_CLASS_COUNT) {
			if (span->flags & SPAN_FLAG_ALIGNED_BLOCKS) {
				//Realign pointer to block start
				void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
				uint32_t block_offset = (uint32_t)pointer_diff(p, blocks_start);
				uint32_t block_size = _memory_size_class[span->size_class].size;
				p = pointer_offset(p, -(int32_t)(block_offset % block_size));
			}
			//Check if block belongs to this heap or if deallocation should be deferred
			if(thread_id == get_thread_id()) {
				_memory_deallocate_direct(span, p);
			} else {
				// Check if span heap was transitioned across threads
				heap_t* heap = get_thread_heap();
				if (span->heap == heap) {
					atomic_store_ptr(&span->thread_id, get_thread_id());
					_memory_deallocate_direct(span, p);
				} else {
					_memory_deallocate_defer(span, p);
				}
			}
		} else {
			//Large blocks can always be deallocated and transferred between heaps
			//Investigate if it is better to defer large spans as well through span_cache_deferred,
			//possibly with some heuristics to pick either scheme at runtime per deallocation
			_memory_deallocate_large_direct(get_thread_heap(), span);
		}
	} else {
		//Oversized allocation, page count is stored in span_count
		size_t num_pages = span->span_count;
		_memory_unmap(span, num_pages * _memory_page_size, span->align_offset, num_pages * _memory_page_size);
	}
}

//! Reallocate the given block to the given size
static void*
_memory_reallocate(void* p, size_t size, size_t oldsize, unsigned int flags) {
	if (p) {
		//Grab the span using guaranteed span alignment
		span_t* span = (void*)((uintptr_t)p & _memory_span_mask);
		void* thread_id = atomic_load_ptr(&span->thread_id);
		if (thread_id) {
			if (span->size_class < SIZE_CLASS_COUNT) {
				//Small/medium sized block
				assert(span->span_count == 1);
				size_class_t* size_class = _memory_size_class + span->size_class;
				void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
				uint32_t block_offset = (uint32_t)pointer_diff(p, blocks_start);
				uint32_t block_idx = block_offset / (uint32_t)size_class->size;
				void* block = pointer_offset(blocks_start, block_idx * size_class->size);
				if (!oldsize)
					oldsize = size_class->size - (uint32_t)pointer_diff(p, block);
				if ((size_t)size_class->size >= size) {
					//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
					if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
						memmove(block, p, oldsize);
					return block;
				}
			} else {
				//Large block
				size_t total_size = size + SPAN_HEADER_SIZE;
				size_t num_spans = total_size >> _memory_span_size_shift;
				if (total_size & (_memory_span_mask - 1))
					++num_spans;
				size_t current_spans = (span->size_class - SIZE_CLASS_COUNT) + 1;
				assert(current_spans == span->span_count);
				void* block = pointer_offset(span, SPAN_HEADER_SIZE);
				if (!oldsize)
					oldsize = (current_spans * _memory_span_size) - (size_t)pointer_diff(p, block);
				if ((current_spans >= num_spans) && (num_spans >= (current_spans / 2))) {
					//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
					if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
						memmove(block, p, oldsize);
					return block;
				}
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
				oldsize = (current_pages * _memory_page_size) - (size_t)pointer_diff(p, block);
			if ((current_pages >= num_pages) && (num_pages >= (current_pages / 2))) {
				//Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block, p, oldsize);
				return block;
			}
		}
	}

	//Size is greater than block size, need to allocate a new block and deallocate the old
	//Avoid hysteresis by overallocating if increase is small (below 37%)
	size_t lower_bound = oldsize + (oldsize >> 2) + (oldsize >> 3);
	void* block = _memory_allocate((size > lower_bound) ? size : ((size > oldsize) ? lower_bound : size));
	if (p) {
		if (!(flags & RPMALLOC_NO_PRESERVE))
			memcpy(block, p, oldsize < size ? oldsize : size);
		_memory_deallocate(p);
	}

	return block;
}

//! Get the usable size of the given block
static size_t
_memory_usable_size(void* p) {
	//Grab the span using guaranteed span alignment
	span_t* span = (void*)((uintptr_t)p & _memory_span_mask);
	void* thread_id = atomic_load_ptr(&span->thread_id);
	if (thread_id) {
		//Small/medium block
		if (span->size_class < SIZE_CLASS_COUNT) {
			size_class_t* size_class = _memory_size_class + span->size_class;
			void* blocks_start = pointer_offset(span, SPAN_HEADER_SIZE);
			return size_class->size - ((size_t)pointer_diff(p, blocks_start) % size_class->size);
		}

		//Large block
		size_t current_spans = (span->size_class - SIZE_CLASS_COUNT) + 1;
		return (current_spans * _memory_span_size) - (size_t)pointer_diff(p, span);
	}

	//Oversized block, page count is stored in span_count
	size_t current_pages = span->span_count;
	return (current_pages * _memory_page_size) - (size_t)pointer_diff(p, span);
}

//! Adjust and optimize the size class properties for the given class
static void
_memory_adjust_size_class(size_t iclass) {
	size_t block_size = _memory_size_class[iclass].size;
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
int
rpmalloc_initialize(void) {
	memset(&_memory_config, 0, sizeof(rpmalloc_config_t));
	return rpmalloc_initialize_config(0);
}

int
rpmalloc_initialize_config(const rpmalloc_config_t* config) {
	if (config)
		memcpy(&_memory_config, config, sizeof(rpmalloc_config_t));

	if (!_memory_config.memory_map || !_memory_config.memory_unmap) {
		_memory_config.memory_map = _memory_map_os;
		_memory_config.memory_unmap = _memory_unmap_os;
	}

	_memory_huge_pages = 0;
	_memory_page_size = _memory_config.page_size;
	_memory_map_granularity = _memory_page_size;
	if (!_memory_page_size) {
#if PLATFORM_WINDOWS
		SYSTEM_INFO system_info;
		memset(&system_info, 0, sizeof(system_info));
		GetSystemInfo(&system_info);
		_memory_page_size = system_info.dwPageSize;
		_memory_map_granularity = system_info.dwAllocationGranularity;
		if (config && config->enable_huge_pages) {
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
							_memory_page_size = large_page_minimum;
							_memory_map_granularity = large_page_minimum;
						}
					}
				}
				CloseHandle(token);
			}
		}
#else
		_memory_page_size = (size_t)sysconf(_SC_PAGESIZE);
		_memory_map_granularity = _memory_page_size;
		if (config && config->enable_huge_pages) {
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
		if (config && config->enable_huge_pages)
			_memory_huge_pages = 1;
	}

	//The ABA counter in heap orphan list is tied to using 512 (bitmask 0x1FF)
	if (_memory_page_size < 512)
		_memory_page_size = 512;
	if (_memory_page_size > (64 * 1024 * 1024))
		_memory_page_size = (64 * 1024 * 1024);
	_memory_page_size_shift = 0;
	size_t page_size_bit = _memory_page_size;
	while (page_size_bit != 1) {
		++_memory_page_size_shift;
		page_size_bit >>= 1;
	}
	_memory_page_size = ((size_t)1 << _memory_page_size_shift);

	size_t span_size = _memory_config.span_size;
	if (!span_size)
		span_size = (64 * 1024);
	if (span_size > (256 * 1024))
		span_size = (256 * 1024);
	_memory_span_size = 4096;
	_memory_span_size = 4096;
	_memory_span_size_shift = 12;
	while (_memory_span_size < span_size) {
		_memory_span_size <<= 1;
		++_memory_span_size_shift;
	}
	_memory_span_mask = ~(uintptr_t)(_memory_span_size - 1);

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
	if (pthread_key_create(&_memory_thread_heap, 0))
		return -1;
#endif

	atomic_store32(&_memory_heap_id, 0);
	atomic_store32(&_memory_orphan_counter, 0);
#if ENABLE_STATISTICS
	atomic_store32(&_memory_active_heaps, 0);
	atomic_store32(&_reserved_spans, 0);
	atomic_store32(&_mapped_pages, 0);
	atomic_store32(&_mapped_total, 0);
	atomic_store32(&_unmapped_total, 0);
	atomic_store32(&_mapped_pages_os, 0);
#endif

	//Setup all small and medium size classes
	size_t iclass;
	for (iclass = 0; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = (iclass + 1) * SMALL_GRANULARITY;
		_memory_size_class[iclass].size = (uint16_t)size;
		_memory_adjust_size_class(iclass);
	}

	_memory_medium_size_limit = _memory_span_size - SPAN_HEADER_SIZE;
	if (_memory_medium_size_limit > MEDIUM_SIZE_LIMIT)
		_memory_medium_size_limit = MEDIUM_SIZE_LIMIT;
	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + ((iclass + 1) * MEDIUM_GRANULARITY);
		if (size > _memory_medium_size_limit)
			size = _memory_medium_size_limit;
		_memory_size_class[SMALL_CLASS_COUNT + iclass].size = (uint16_t)size;
		_memory_adjust_size_class(SMALL_CLASS_COUNT + iclass);
	}

	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx)
		atomic_store_ptr(&_memory_heaps[list_idx], 0);

	//Initialize this thread
	rpmalloc_thread_initialize();
	return 0;
}

//! Finalize the allocator
void
rpmalloc_finalize(void) {
	atomic_thread_fence_acquire();

	rpmalloc_thread_finalize();

#if ENABLE_STATISTICS
	//If you hit this assert, you still have active threads or forgot to finalize some thread(s)
	assert(atomic_load32(&_memory_active_heaps) == 0);
#endif

	//Free all thread caches
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
		while (heap) {
			if (heap->spans_reserved) {
				span_t* span = _memory_map_spans(heap, heap->spans_reserved);
				_memory_unmap_span(span);
			}

			for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
				heap_class_t* heap_class = heap->span_class + iclass;
				if (heap_class->active_span)
					_memory_heap_cache_insert(heap, heap_class->active_span);
				span_t* span = heap_class->used_span;
				while (span) {
					span_t* next = span->next;
					_memory_heap_cache_insert(heap, span);
					span = next;
				}
			}

			//Free span caches (other thread might have deferred after the thread using this heap finalized)
#if ENABLE_THREAD_CACHE
			_memory_heap_cache_adopt_deferred(heap);
			for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
				if (heap->span_cache[iclass])
					_memory_unmap_span_list(heap->span_cache[iclass]);
			}
#endif
			heap_t* next_heap = heap->next_heap;
			size_t heap_size = (1 + (sizeof(heap_t) >> _memory_page_size_shift)) * _memory_page_size;
			_memory_unmap(heap, heap_size, heap->align_offset, heap_size);
			heap = next_heap;
		}
	}

#if ENABLE_GLOBAL_CACHE
	//Free global caches
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass)
		_memory_cache_finalize(&_memory_span_cache[iclass]);
#endif

	atomic_store_ptr(&_memory_orphan_heaps, 0);
	atomic_thread_fence_release();

#if ENABLE_STATISTICS
	//If you hit these asserts you probably have memory leaks or double frees in your code
	assert(!atomic_load32(&_mapped_pages));
	assert(!atomic_load32(&_reserved_spans));
	assert(!atomic_load32(&_mapped_pages_os));
#endif

#if (defined(__APPLE__) || defined(__HAIKU__)) && ENABLE_PRELOAD
	pthread_key_delete(_memory_thread_heap);
#endif
}

//! Initialize thread, assign heap
void
rpmalloc_thread_initialize(void) {
	if (!get_thread_heap()) {
		heap_t* heap = _memory_allocate_heap();
		if (heap) {
			atomic_thread_fence_acquire();
			assert(!heap->owner_thread);
			heap->owner_thread = get_thread_id();
#if ENABLE_STATISTICS
			atomic_incr32(&_memory_active_heaps);
			heap->thread_to_global = 0;
			heap->global_to_thread = 0;
#endif
			set_thread_heap(heap);
		}
	}
}

//! Finalize thread, orphan heap
void
rpmalloc_thread_finalize(void) {
	heap_t* heap = get_thread_heap();
	if (!heap)
		return;

	assert(heap->owner_thread == get_thread_id());
	heap->owner_thread = 0;
	atomic_thread_fence_release();

	//Release thread cache spans back to global cache
#if ENABLE_THREAD_CACHE
	_memory_heap_cache_adopt_deferred(heap);
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		span_t* span = heap->span_cache[iclass];
#if ENABLE_GLOBAL_CACHE
		while (span) {
			assert(span->span_count == (iclass + 1));
			size_t release_count = (!iclass ? _memory_span_release_count : _memory_span_release_count_large);
			span_t* next = _memory_span_list_split(span, (uint32_t)release_count);
			_memory_global_cache_insert(span);
			span = next;
		}
#else
		if (span)
			_memory_unmap_span_list(span);
#endif
		heap->span_cache[iclass] = 0;
	}
#endif

	//Orphan the heap
	void* raw_heap;
	uintptr_t orphan_counter;
	heap_t* last_heap;
	do {
		last_heap = atomic_load_ptr(&_memory_orphan_heaps);
		heap->next_orphan = (void*)((uintptr_t)last_heap & ~(uintptr_t)0x1FF);
		orphan_counter = (uintptr_t)atomic_incr32(&_memory_orphan_counter);
		raw_heap = (void*)((uintptr_t)heap | (orphan_counter & (uintptr_t)0x1FF));
	} while (!atomic_cas_ptr(&_memory_orphan_heaps, raw_heap, last_heap));

	set_thread_heap(0);

#if ENABLE_STATISTICS
	atomic_add32(&_memory_active_heaps, -1);
	assert(atomic_load32(&_memory_active_heaps) >= 0);
#endif
}

int
rpmalloc_is_thread_initialized(void) {
	return (get_thread_heap() != 0) ? 1 : 0;
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
#  if defined(__APPLE__)
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, (_memory_huge_pages ? VM_FLAGS_SUPERPAGE_SIZE_2MB : -1), 0);
#  elif defined(MAP_HUGETLB)
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, (_memory_huge_pages ? MAP_HUGETLB : 0) | MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, -1, 0);
#  else
	void* ptr = mmap(0, size + padding, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, -1, 0);
#  endif
	if ((ptr == MAP_FAILED) || !ptr) {
		assert("Failed to map virtual memory block" == 0);
		return 0;
	}
#endif
#if ENABLE_STATISTICS
	atomic_add32(&_mapped_pages_os, (int32_t)((size + padding) >> _memory_page_size_shift));
#endif
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
#if ENABLE_STATISTICS
	if (release)
		atomic_add32(&_mapped_pages_os, -(int32_t)(release >> _memory_page_size_shift));
#endif
}

// Extern interface

RPMALLOC_RESTRICT void*
rpmalloc(size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return 0;
	}
#endif
	return _memory_allocate(size);
}

void
rpfree(void* ptr) {
	_memory_deallocate(ptr);
}

RPMALLOC_RESTRICT void*
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
	void* block = _memory_allocate(total);
	memset(block, 0, total);
	return block;
}

void*
rprealloc(void* ptr, size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return ptr;
	}
#endif
	return _memory_reallocate(ptr, size, 0, 0);
}

void*
rpaligned_realloc(void* ptr, size_t alignment, size_t size, size_t oldsize,
                  unsigned int flags) {
#if ENABLE_VALIDATE_ARGS
	if ((size + alignment < size) || (alignment > _memory_page_size)) {
		errno = EINVAL;
		return 0;
	}
#endif
	void* block;
	if (alignment > 32) {
		size_t usablesize = _memory_usable_size(ptr);
		if ((usablesize >= size) && (size >= (usablesize / 2)) && !((uintptr_t)ptr & (alignment - 1)))
			return ptr;

		block = rpaligned_alloc(alignment, size);
		if (ptr) {
			if (!oldsize)
				oldsize = usablesize;
			if (!(flags & RPMALLOC_NO_PRESERVE))
				memcpy(block, ptr, oldsize < size ? oldsize : size);
			rpfree(ptr);
		}
		//Mark as having aligned blocks
		span_t* span = (span_t*)((uintptr_t)block & _memory_span_mask);
		span->flags |= SPAN_FLAG_ALIGNED_BLOCKS;
	} else {
		block = _memory_reallocate(ptr, size, oldsize, flags);
	}
	return block;
}

RPMALLOC_RESTRICT void*
rpaligned_alloc(size_t alignment, size_t size) {
	if (alignment <= 32)
		return rpmalloc(size);

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

	void* ptr = 0;
	size_t align_mask = alignment - 1;
	if (alignment < _memory_page_size) {
		ptr = rpmalloc(size + alignment);
		if ((uintptr_t)ptr & align_mask)
			ptr = (void*)(((uintptr_t)ptr & ~(uintptr_t)align_mask) + alignment);
		//Mark as having aligned blocks
		span_t* span = (span_t*)((uintptr_t)ptr & _memory_span_mask);
		span->flags |= SPAN_FLAG_ALIGNED_BLOCKS;
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

	span = _memory_map(mapped_size, &align_offset);
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

	atomic_store_ptr(&span->thread_id, 0);
	//Store page count in span_count
	span->span_count = (uint32_t)num_pages;
	span->align_offset = (uint32_t)align_offset;

	return ptr;
}

RPMALLOC_RESTRICT void*
rpmemalign(size_t alignment, size_t size) {
	return rpaligned_alloc(alignment, size);
}

int
rpposix_memalign(void **memptr, size_t alignment, size_t size) {
	if (memptr)
		*memptr = rpaligned_alloc(alignment, size);
	else
		return EINVAL;
	return *memptr ? 0 : ENOMEM;
}

size_t
rpmalloc_usable_size(void* ptr) {
	return (ptr ? _memory_usable_size(ptr) : 0);
}

void
rpmalloc_thread_collect(void) {
}

void
rpmalloc_thread_statistics(rpmalloc_thread_statistics_t* stats) {
	memset(stats, 0, sizeof(rpmalloc_thread_statistics_t));
	heap_t* heap = get_thread_heap();

	for (size_t iclass = 0; iclass < SIZE_CLASS_COUNT; ++iclass) {
		heap_class_t* heap_class = heap->span_class + iclass;
		span_t* span = heap_class->used_span;
		while (span) {
			atomic_thread_fence_acquire();
			uint64_t free_list_deferred = (uint64_t)atomic_load64(&span->free_list_deferred);
			stats->sizecache = (span->free_count + (uint32_t)free_list_deferred) * _memory_size_class[iclass].size;
			span = span->next;
		}
	}

#if ENABLE_THREAD_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		if (heap->span_cache[iclass])
			stats->spancache = (size_t)heap->span_cache[iclass]->list_size * (iclass + 1) * _memory_span_size;
		span_t* deferred_list = !iclass ? atomic_load_ptr(&heap->span_cache_deferred) : 0;
		if (deferred_list)
			stats->spancache = (size_t)deferred_list->list_size * (iclass + 1) * _memory_span_size;
	}
#endif
}

void
rpmalloc_global_statistics(rpmalloc_global_statistics_t* stats) {
	memset(stats, 0, sizeof(rpmalloc_global_statistics_t));
#if ENABLE_STATISTICS
	stats->mapped = (size_t)atomic_load32(&_mapped_pages) * _memory_page_size;
	stats->mapped_total = (size_t)atomic_load32(&_mapped_total) * _memory_page_size;
	stats->unmapped_total = (size_t)atomic_load32(&_unmapped_total) * _memory_page_size;
#endif
#if ENABLE_GLOBAL_CACHE
	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		stats->cached += (size_t)atomic_load32(&_memory_span_cache[iclass].size) * (iclass + 1) * _memory_span_size;
	}
#endif
}
