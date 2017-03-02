/* rpmalloc.c  -  Memory allocator  -  Public Domain  -  2016 Mattias Jansson / Rampant Pixels
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

// Build time configurable limits

//! Can be defined to 0 to reduce 16 byte overhead per memory page on 64 bit systems (will require total memory use of process to be less than 2^48)
#define USE_FULL_ADDRESS_RANGE    1
//! Limit of thread cache (total sum of thread cache for all page counts will be 16 * THREAD_SPAN_CACHE_LIMIT)
#define THREAD_SPAN_CACHE_LIMIT   (16*1024*1024)
//! Limit of global cache (total sum of global cache for all page counts will be 16 * GLOBAL_SPAN_CACHE_LIMIT)
#define GLOBAL_SPAN_CACHE_LIMIT   (128*1024*1024)
//! Size of heap hashmap
#define HEAP_ARRAY_SIZE           79


// Platform and arch specifics

#ifdef _MSC_VER
#  define ALIGNED_STRUCT(name, alignment) __declspec(align(alignment)) struct name
#  define FORCEINLINE __forceinline
#  define _Static_assert static_assert
#  define _Thread_local __declspec(thread)
#  define atomic_thread_fence_acquire() //_ReadWriteBarrier()
#  define atomic_thread_fence_release() //_ReadWriteBarrier()
#else
#  define ALIGNED_STRUCT(name, alignment) struct __attribute__((__aligned__(alignment))) name
#  define FORCEINLINE inline __attribute__((__always_inline__))
#  if !defined(__clang__) && defined(__GNUC__)
#    define _Thread_local __thread
#  endif
#  ifdef __arm__
#    define atomic_thread_fence_acquire() __asm volatile("dmb sy" ::: "memory")
#    define atomic_thread_fence_release() __asm volatile("dmb st" ::: "memory")
#  else
#    define atomic_thread_fence_acquire() //__asm volatile("" ::: "memory")
#    define atomic_thread_fence_release() //__asm volatile("" ::: "memory")
#  endif
#endif

#if defined( __x86_64__ ) || defined( _M_AMD64 ) || defined( _M_X64 ) || defined( _AMD64_ ) || defined( __arm64__ ) || defined( __aarch64__ )
#  define ARCH_64BIT 1
#else
#  define ARCH_64BIT 0
#endif

#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( _WIN64 )
#  define PLATFORM_WINDOWS 1
#else
#  define PLATFORM_POSIX 1
#endif

#include <stdint.h>
#include <string.h>

// Atomic access abstraction

ALIGNED_STRUCT(atomic32_t, 4) {
	int32_t nonatomic;
};
typedef struct atomic32_t atomic32_t;

ALIGNED_STRUCT(atomic64_t, 8) {
	int64_t nonatomic;
};
typedef struct atomic64_t atomic64_t;

ALIGNED_STRUCT(atomicptr_t, 8) {
	void* nonatomic;
};
typedef struct atomicptr_t atomicptr_t;

static FORCEINLINE int32_t
atomic_load32(atomic32_t* src) {
	return src->nonatomic;
}

static FORCEINLINE void
atomic_store32(atomic32_t* dst, int32_t val) {
	dst->nonatomic = val;
}

#if PLATFORM_POSIX

static FORCEINLINE void
atomic_store64(atomic64_t* dst, int64_t val) {
	dst->nonatomic = val;
}

static FORCEINLINE int64_t
atomic_exchange_and_add64(atomic64_t* dst, int64_t add) {
	return __sync_fetch_and_add(&dst->nonatomic, add);
}

#endif

static FORCEINLINE int32_t
atomic_incr32(atomic32_t* val) {
#ifdef _MSC_VER
	int32_t old = (int32_t)_InterlockedExchangeAdd((volatile long*)&val->nonatomic, 1);
	return (old + 1);
#else
	return __sync_add_and_fetch(&val->nonatomic, 1);
#endif
}

static FORCEINLINE void*
atomic_load_ptr(atomicptr_t* src) {
	return src->nonatomic;
}

static FORCEINLINE void
atomic_store_ptr(atomicptr_t* dst, void* val) {
	dst->nonatomic = val;
}

static FORCEINLINE int
atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref);

static void
thread_yield(void);

// Preconfigured limits and sizes

#define PAGE_SIZE                 4096

#define SPAN_ADDRESS_GRANULARITY  65536
#define SPAN_MAX_SIZE             (SPAN_ADDRESS_GRANULARITY)
#define SPAN_MASK                 (~(SPAN_MAX_SIZE - 1))
#define SPAN_MAX_PAGE_COUNT       (SPAN_MAX_SIZE / PAGE_SIZE)
#define SPAN_CLASS_COUNT          SPAN_MAX_PAGE_COUNT

#define SMALL_GRANULARITY         16
#define SMALL_GRANULARITY_SHIFT   4
#define SMALL_CLASS_COUNT         (((PAGE_SIZE - SPAN_HEADER_SIZE) / 2) / SMALL_GRANULARITY)
#define SMALL_SIZE_LIMIT          (SMALL_CLASS_COUNT * SMALL_GRANULARITY)

#define MEDIUM_GRANULARITY        2048
#define MEDIUM_GRANULARITY_SHIFT  11
#define MEDIUM_CLASS_COUNT        32
//#define MEDIUM_SIZE_INCR_UNALIGN  (((SPAN_MAX_SIZE - SPAN_HEADER_SIZE) - SMALL_SIZE_LIMIT) / MEDIUM_CLASS_COUNT)
//#define MEDIUM_SIZE_INCR          (MEDIUM_SIZE_INCR_UNALIGN - (MEDIUM_SIZE_INCR_UNALIGN % 16))
//#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_CLASS_COUNT * MEDIUM_SIZE_INCR))
#define MEDIUM_SIZE_LIMIT         (SPAN_ADDRESS_GRANULARITY - SPAN_HEADER_SIZE)

#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)

#define SPAN_LIST_LOCK_TOKEN      ((void*)1)

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

#define pointer_offset_span(ptr, offset) (pointer_offset((ptr), (intptr_t)(offset) * (intptr_t)SPAN_ADDRESS_GRANULARITY))
#define pointer_diff_span(a, b) ((offset_t)((intptr_t)pointer_diff((a), (b)) / (intptr_t)SPAN_ADDRESS_GRANULARITY))

#if ARCH_64BIT && USE_FULL_ADDRESS_RANGE
#define SPAN_HEADER_SIZE          32
typedef int64_t offset_t;
typedef uint32_t count_t;
typedef uint16_t half_count_t;
#else
#define SPAN_HEADER_SIZE          16
typedef int32_t offset_t;
typedef uint8_t count_t;
typedef uint8_t half_count_t;
#endif

// Data types

typedef struct span_t span_t;
typedef struct heap_t heap_t;
typedef struct size_class_t size_class_t;
typedef struct span_block_t span_block_t;
typedef union span_data_t span_data_t;

struct span_block_t {
	//! Free list
	half_count_t    free_list;
	//! Free count
	half_count_t    free_count;
	//! First autolinked block
	half_count_t    first_autolink;
};

union span_data_t {
	//! Span data
	span_block_t block;
	//! List size (used when span is part of a list)
	uint32_t list_size;
};

struct span_t {
	//!	Heap ID
	atomic32_t  heap_id;
	//! Size class
	count_t     size_class;
	//! Span data
	span_data_t data;
	//! Next span
	offset_t    next_span;
	//! Previous span
	offset_t    prev_span;
};
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "span size mismatch");

struct heap_t {
	//! Heap ID
	int32_t     id;
	//! Deferred deallocation
	atomicptr_t defer_deallocate;
	//! List of spans with free blocks for each size class
	span_t*     size_cache[SIZE_CLASS_COUNT];
	//! List of free spans for each page count
	span_t*     span_cache[SPAN_CLASS_COUNT];
	//! Next heap
	heap_t*     next_heap;
};
_Static_assert(sizeof(heap_t) <= PAGE_SIZE, "heap size mismatch");

struct size_class_t {
	//! Size of blocks in this class
	uint16_t size;
	//! Number of pages to allocate for a chunk
	uint8_t page_count;
	//! Number of blocks in each chunk
	uint8_t block_count;
};
_Static_assert(sizeof(size_class_t) == 4, "Size class size mismatch");

//! Global size classes
static size_class_t _memory_size_class[SIZE_CLASS_COUNT];

//! Heap ID counter
static atomic32_t _memory_heap_id;

#ifdef PLATFORM_POSIX
//! Virtual memory address counter
static atomic64_t _memory_addr;
#endif

//! Global span cache
static atomicptr_t _memory_span_cache[SPAN_CLASS_COUNT];

//! Current thread heap
static _Thread_local heap_t* _memory_thread_heap;

//! All heaps
static atomicptr_t _memory_heaps[HEAP_ARRAY_SIZE];

//! Orphaned heaps
static atomicptr_t _memory_orphan_heaps;

static void*
_memory_map(size_t page_count);

static void
_memory_unmap(void* ptr, size_t page_count);

static heap_t*
_memory_heap_lookup(int32_t id) {
	uint32_t list_idx = id % HEAP_ARRAY_SIZE;
	heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
	while (heap && (heap->id != id))
		heap = heap->next_heap;
	return heap;
}

static void
_memory_global_cache_insert(span_t* first_span, size_t page_count, size_t span_count) {
	while (1) {
		void* global_span_ptr = atomic_load_ptr(&_memory_span_cache[page_count-1]);
		if (global_span_ptr != SPAN_LIST_LOCK_TOKEN) {
			uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~(uintptr_t)SPAN_MASK;
			span_t* global_span = (span_t*)((void*)((uintptr_t)global_span_ptr & (uintptr_t)SPAN_MASK));

#if GLOBAL_SPAN_CACHE_LIMIT > 0
			size_t list_bytes = global_span_count * page_count * PAGE_SIZE;
			if (list_bytes >= GLOBAL_SPAN_CACHE_LIMIT)
				break;
#endif
			if (global_span_count + span_count > 0xFFFF)
				break;

			//Use prev_span as skip pointer over this range of spans
			first_span->data.list_size = (uint32_t)span_count;
			first_span->prev_span = global_span ? pointer_diff_span(global_span, first_span) : 0;

			global_span_count += span_count;
			void* first_span_ptr = (void*)((uintptr_t)first_span | global_span_count);

			if (atomic_cas_ptr(&_memory_span_cache[page_count-1], first_span_ptr, global_span_ptr)) {
				//Span list is inserted into global cache
				return;
			}
		}
		else {
			thread_yield();
			atomic_thread_fence_acquire();
		}
	}

	//Global cache full, release pages
	for (size_t ispan = 0; ispan < span_count; ++ispan) {
		span_t* next_span = pointer_offset_span(first_span, first_span->next_span);
		_memory_unmap(first_span, page_count);
		first_span = next_span;
	}
}

static span_t*
_memory_global_cache_extract(size_t page_count) {
	span_t* span = 0;
	atomicptr_t* cache = &_memory_span_cache[page_count-1];
	atomic_thread_fence_acquire();
	void* global_span_ptr = atomic_load_ptr(cache);
	while (global_span_ptr) {
		if ((global_span_ptr != SPAN_LIST_LOCK_TOKEN) &&
		        atomic_cas_ptr(cache, SPAN_LIST_LOCK_TOKEN, global_span_ptr)) {
			//Grab a number of thread cache spans, using the skip span pointer stored in prev_span to quickly
			//skip ahead in the list to get the new head
			uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~(uintptr_t)SPAN_MASK;
			span = (span_t*)((void*)((uintptr_t)global_span_ptr & (uintptr_t)SPAN_MASK));

			span_t* new_global_span = pointer_offset_span(span, span->prev_span);
			global_span_count -= span->data.list_size;

			void* new_cache_head = global_span_count ?
			                       ((void*)((uintptr_t)new_global_span | global_span_count)) :
			                       0;

			atomic_store_ptr(cache, new_cache_head);
			atomic_thread_fence_release();

			break;
		}

		thread_yield();
		atomic_thread_fence_acquire();

		global_span_ptr = atomic_load_ptr(cache);
	}

	return span;
}

static void*
_memory_allocate_from_heap(heap_t* heap, size_t size) {
	const size_t class_idx = (size <= SMALL_SIZE_LIMIT) ?
		((size + (SMALL_GRANULARITY - 1)) >> SMALL_GRANULARITY_SHIFT) - 1 :
		SMALL_CLASS_COUNT + ((size - SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY - 1)) >> MEDIUM_GRANULARITY_SHIFT) - 1;

	size_class_t* size_class = _memory_size_class + class_idx;
	span_t* span = heap->size_cache[class_idx];
	const count_t class_size = size_class->size;

	if (span) {
		//Happy path, we have a span with at least one free block
		count_t offset = class_size * span->data.block.free_list;
		uint32_t* block = pointer_offset(span, SPAN_HEADER_SIZE + offset);

		--span->data.block.free_count;

		if (!span->data.block.free_count) {
			span_t* next_span = span->next_span ? pointer_offset_span(span, span->next_span) : 0;
			heap->size_cache[class_idx] = next_span;
			span->data.block.first_autolink = size_class->block_count;
		}
		else {
			if (span->data.block.free_list < span->data.block.first_autolink) {
				span->data.block.free_list = (count_t)(*block);
			}
			else {
				++span->data.block.free_list;
				++span->data.block.first_autolink;
			}
		}

		return block;
	}

	//No span in use, grab a new span from heap cache
	span = heap->span_cache[size_class->page_count-1];
	if (!span)
		span = _memory_global_cache_extract(size_class->page_count);
	if (span) {
		if (span->data.list_size > 1) {
			span_t* next_span = pointer_offset_span(span, span->next_span);
			next_span->data.list_size = span->data.list_size - 1;
			heap->span_cache[size_class->page_count-1] = next_span;
		}
		else {
			heap->span_cache[size_class->page_count-1] = 0;
		}
	}
	else {
		span = _memory_map(size_class->page_count);
	}

	atomic_store32(&span->heap_id, heap->id);
	atomic_thread_fence_release();

	span->size_class = (count_t)class_idx;
	span->next_span = 0;

	span->data.block.free_count = (half_count_t)(size_class->block_count - 1);
	span->data.block.free_list = 1;
	span->data.block.first_autolink = 1;

	//If we only have one block we will grab it, otherwise
	//set span as new span to use for next allocation
	if (size_class->block_count > 1)
		heap->size_cache[class_idx] = span;

	//Return first block
	return pointer_offset(span, SPAN_HEADER_SIZE);
}

static heap_t*
_memory_allocate_heap(void) {
	heap_t* heap;
	heap_t* next_heap;
	atomic_thread_fence_acquire();
	do {
		heap = atomic_load_ptr(&_memory_orphan_heaps);
		if (!heap)
			break;
		next_heap = heap->next_heap;
	}
	while (!atomic_cas_ptr(&_memory_orphan_heaps, next_heap, heap));

	if (heap)
		return heap;

	heap = _memory_map(1);
	memset(heap, 0, sizeof(heap_t));

	do {
		heap->id = atomic_incr32(&_memory_heap_id);
		if (_memory_heap_lookup(heap->id))
			heap->id = 0;
	}
	while (!heap->id);

	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	do {
		next_heap = atomic_load_ptr(&_memory_heaps[list_idx]);
		heap->next_heap = next_heap;
	}
	while (!atomic_cas_ptr(&_memory_heaps[list_idx], heap, next_heap));

	return heap;
}

static void
_memory_sized_list_add(span_t** head, span_t* span) {
	if (*head) {
		offset_t next_offset = pointer_diff_span(*head, span);
		(*head)->prev_span = -next_offset;
		span->next_span = next_offset;
		span->data.list_size = (*head)->data.list_size + 1;
	}
	else {
		span->next_span = 0;
		span->data.list_size = 1;
	}
	*head = span;
}

static void
_memory_list_add(span_t** head, span_t* span) {
	if (*head) {
		offset_t next_offset = pointer_diff_span(*head, span);
		(*head)->prev_span = -next_offset;
		span->next_span = next_offset;
	}
	else {
		span->next_span = 0;
	}
	*head = span;
}

static void
_memory_list_remove(span_t** head, span_t* span) {
	if (*head == span) {
		if (span->next_span)
			*head = pointer_offset_span(span, span->next_span);
		else
			*head = 0;
	}
	else {
		span_t* prev_span = pointer_offset_span(span, span->prev_span);
		if (span->next_span) {
			span_t* next_span = pointer_offset_span(span, span->next_span);
			offset_t next_offset = pointer_diff_span(next_span, prev_span);
			prev_span->next_span = next_offset;
			next_span->prev_span = -next_offset;
		}
		else {
			prev_span->next_span = 0;
		}
	}
}

static void
_memory_deallocate_to_heap(heap_t* heap, span_t* span, void* p) {
	size_class_t* size_class = _memory_size_class + span->size_class;

	if (span->data.block.free_count == ((count_t)size_class->block_count - 1)) {
		//Remove from free list (present if we had a previous free block)
		if (span->data.block.free_count > 0)
			_memory_list_remove(&heap->size_cache[span->size_class], span);

		//Add to span cache
		span_t** cache = &heap->span_cache[size_class->page_count-1];
		_memory_sized_list_add(cache, span);
		size_t list_bytes = (size_t)(*cache)->data.list_size * (size_t)size_class->page_count * PAGE_SIZE;
		if (list_bytes > THREAD_SPAN_CACHE_LIMIT) {
			//Release half to global cache
			uint32_t span_count = (*cache)->data.list_size / 2;
			span_t* first_span = *cache;
			span_t* next_span = first_span;

			for (uint32_t ispan = 0; ispan < span_count; ++ispan)
				next_span = pointer_offset_span(next_span, next_span->next_span);
			next_span->data.list_size = first_span->data.list_size - span_count;
			*cache = next_span;

			_memory_global_cache_insert(first_span, size_class->page_count, span_count);
		}
	}
	else {
		if (span->data.block.free_count == 0) {
			//Add to free list
			_memory_list_add(&heap->size_cache[span->size_class], span);
		}
		uint32_t* block = p;
		*block = span->data.block.free_list;
		++span->data.block.free_count;
		count_t block_offset = (count_t)pointer_diff(block, span) - SPAN_HEADER_SIZE;
		count_t block_idx = block_offset / (count_t)size_class->size;
		span->data.block.free_list = block_idx;
	}
}

static void
_memory_deallocate_deferred(heap_t* heap) {
	atomic_thread_fence_acquire();
	void* p = atomic_load_ptr(&heap->defer_deallocate);
	if (!p)
		return;
	if (atomic_cas_ptr(&heap->defer_deallocate, 0, p)) {
		while (p) {
			void* next = *(void**)p;
			span_t* span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
			_memory_deallocate_to_heap(heap, span, p);
			p = next;
		}
	}
}

static void
_memory_deallocate_defer(int32_t heap_id, void* p) {
	//Delegate to heap
	heap_t* heap = _memory_heap_lookup(heap_id);
	void* last_ptr;
	do {
		last_ptr = atomic_load_ptr(&heap->defer_deallocate);
		*(void**)p = last_ptr;
	}
	while (!atomic_cas_ptr(&heap->defer_deallocate, p, last_ptr));
}

static void*
_memory_allocate(size_t size) {
	if (size <= MEDIUM_SIZE_LIMIT) {
		heap_t* heap = _memory_thread_heap;
		if (heap) {
			_memory_deallocate_deferred(heap);
			return _memory_allocate_from_heap(heap, size);
		}

		heap = _memory_allocate_heap();
		if (heap) {
			_memory_thread_heap = heap;
			return _memory_allocate_from_heap(heap, size);
		}

		return 0;
	}

	//Oversized, allocate pages directly
	size += SPAN_HEADER_SIZE;
	size_t num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE)
		++num_pages;
	span_t* span = _memory_map(num_pages);
	atomic_store32(&span->heap_id, 0);
	//Store page count in next_span
	span->next_span = (offset_t)num_pages;

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

static void
_memory_deallocate(void* p) {
	if (!p)
		return;

	span_t* span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
	int32_t heap_id = atomic_load32(&span->heap_id);
	heap_t* heap = _memory_thread_heap;
	if (heap && (heap_id == heap->id)) {
		_memory_deallocate_to_heap(heap, span, p);
	}
	else if (heap_id > 0) {
		_memory_deallocate_defer(heap_id, p);
	}
	else {
		//Large allocation, page count is stored in next_span
		size_t num_pages = (size_t)span->next_span;
		_memory_unmap(span, num_pages);
	}
}

static void*
_memory_reallocate(void* p, size_t size, size_t oldsize) {
	span_t* span = 0;
	if (p) {
		span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
		if (span->size_class != 0xFF) {
			size_class_t* size_class = _memory_size_class + span->size_class;
			if ((size_t)size_class->size >= size)
				return p; //Still fits in block
			if (!oldsize)
				oldsize = size_class->size;
		}
		else {
			size_t total_size = size + SPAN_HEADER_SIZE;
			size_t num_pages = total_size / PAGE_SIZE;
			if (total_size % PAGE_SIZE)
				++num_pages;
			//Page count is stored in next_span
			size_t current_pages = (size_t)span->next_span;
			if ((current_pages >= num_pages) && (num_pages >= (current_pages / 2)))
				return p; //Still fits and less than half of memory would be freed
			if (!oldsize)
				oldsize = current_pages * (size_t)PAGE_SIZE;
		}
	}

	void* block = _memory_allocate(size);
	if (p) {
		memcpy(block, p, oldsize < size ? oldsize : size);
		_memory_deallocate(p);
	}

	return block;
}

static void
_memory_adjust_size_class(size_t iclass) {
	size_t block_size = _memory_size_class[iclass].size;
	size_t header_size = SPAN_HEADER_SIZE;
	size_t remain_size = PAGE_SIZE - header_size;
	size_t block_count = remain_size / block_size;
	size_t wasted = remain_size - (block_size * block_count);

	size_t page_size_counter = 1;
	size_t overhead = wasted + header_size;

	float current_factor = (float)overhead / ((float)block_count * (float)block_size);
	float best_factor = current_factor;
	size_t best_page_count = page_size_counter;
	size_t best_block_count = block_count;

	while ((((float)wasted / (float)block_count) > ((float)block_size / 32.0f))) {
		size_t page_size = PAGE_SIZE * (++page_size_counter);
		if (page_size > (PAGE_SIZE * SPAN_MAX_PAGE_COUNT))
			break;
		remain_size = page_size - header_size;
		block_count = remain_size / block_size;
		if (block_count > 255)
			break;
		if (!block_count)
			++block_count;
		wasted = remain_size - (block_size * block_count);
		overhead = wasted + header_size;

		current_factor = (float)overhead / ((float)block_count * (float)block_size);
		if (current_factor < best_factor) {
			best_factor = current_factor;
			best_page_count = page_size_counter;
			best_block_count = block_count;
		}
	}

	_memory_size_class[iclass].page_count = (uint8_t)best_page_count;
	_memory_size_class[iclass].block_count = (uint8_t)best_block_count;

	//Check if previous size class can be merged
	if (iclass > 0) {
		size_t prevclass = iclass - 1;
		if ((_memory_size_class[prevclass].page_count == _memory_size_class[iclass].page_count) &&
		        (_memory_size_class[prevclass].block_count == _memory_size_class[iclass].block_count)) {
			memcpy(_memory_size_class + prevclass, _memory_size_class + iclass, sizeof(_memory_size_class[iclass]));
		}
	}
}

#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( _WIN64 )
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sched.h>
#  include <errno.h>
#  ifndef MAP_UNINITIALIZED
#    define MAP_UNINITIALIZED 0
#  endif
#endif

int
rpmalloc_initialize(void) {
#ifdef PLATFORM_WINDOWS
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	if (system_info.dwAllocationGranularity < SPAN_ADDRESS_GRANULARITY)
		return -1;
#else
	atomic_store64(&_memory_addr, 0x1000000000ULL);
#endif

	atomic_store32(&_memory_heap_id, 0);

	size_t iclass;
	for (iclass = 0; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = (iclass + 1) * SMALL_GRANULARITY;
		_memory_size_class[iclass].size = (uint16_t)size;
		_memory_adjust_size_class(iclass);
	}
	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + ((iclass + 1) * MEDIUM_GRANULARITY);
		if (size > MEDIUM_SIZE_LIMIT)
			size = MEDIUM_SIZE_LIMIT;
		_memory_size_class[SMALL_CLASS_COUNT + iclass].size = (uint16_t)size;
		_memory_adjust_size_class(SMALL_CLASS_COUNT + iclass);
	}

	return 0;
}

void
rpmalloc_finalize(void) {
	atomic_thread_fence_acquire();

	//Free all thread caches
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
		while (heap) {
			_memory_deallocate_deferred(heap);

			for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
				size_t page_count = iclass + 1;
				span_t* span = heap->span_cache[iclass];
				unsigned int span_count = span ? span->data.list_size : 0;
				for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
					span_t* next_span = pointer_offset_span(span, span->next_span);
					_memory_unmap(span, page_count);
					span = next_span;
				}
				heap->span_cache[iclass] = 0;
			}
			heap = heap->next_heap;
		}
	}

	//Free global cache
	for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
		void* span_ptr = atomic_load_ptr(&_memory_span_cache[iclass]);
		size_t cache_count = (uintptr_t)span_ptr & ~(uintptr_t)SPAN_MASK;
		span_t* span = (span_t*)((void*)((uintptr_t)span_ptr & (uintptr_t)SPAN_MASK));
		while (cache_count) {
			span_t* skip_span = pointer_offset_span(span, span->prev_span);
			unsigned int span_count = span->data.list_size;
			for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
				span_t* next_span = pointer_offset_span(span, span->next_span);
				_memory_unmap(span, iclass+1);
				span = next_span;
			}
			span = skip_span;
			cache_count -= span_count;
		}
		atomic_store_ptr(&_memory_span_cache[iclass], 0);
	}
}

void
rpmalloc_thread_finalize(void) {
	heap_t* heap = _memory_thread_heap;
	if (heap) {
		//Release thread cache spans back to global cache
		for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
			size_t page_count = iclass + 1;
			span_t* span = heap->span_cache[iclass];
			unsigned int span_count = span ? span->data.list_size : 0;
			while (span_count) {
				//Release to global cache
				unsigned int release_count = span_count;
				unsigned int release_limit = (unsigned int)(THREAD_SPAN_CACHE_LIMIT / (page_count * PAGE_SIZE)) / 2;
				if (release_count > release_limit) {
					if (release_count > (release_limit * 2))
						release_count = release_limit;
					else
						release_count /= 2;
				}

				span_t* first_span = heap->span_cache[iclass];
				span_t* next_span = first_span;

				for (uint32_t ispan = 0; ispan < release_count; ++ispan) {
					next_span = pointer_offset_span(next_span, next_span->next_span);
				}

				heap->span_cache[iclass] = next_span;

				_memory_global_cache_insert(first_span, page_count, release_count);

				span_count -= release_count;
			}
			heap->span_cache[iclass] = 0;
		}

		//Orphan the heap
		heap_t* last_heap;
		do {
			last_heap = atomic_load_ptr(&_memory_orphan_heaps);
			heap->next_heap = last_heap;
		}
		while (!atomic_cas_ptr(&_memory_orphan_heaps, heap, last_heap));
	}
	
	_memory_thread_heap = 0;
}

static void*
_memory_map(size_t page_count) {
	size_t total_size = page_count * PAGE_SIZE;
	void* pages_ptr = 0;

#ifdef PLATFORM_WINDOWS
	pages_ptr = VirtualAlloc(0, total_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	intptr_t incr = (intptr_t)total_size / (intptr_t)SPAN_ADDRESS_GRANULARITY;
	if (total_size % SPAN_ADDRESS_GRANULARITY)
		++incr;
	do {
		void* base_addr = (void*)(uintptr_t)atomic_exchange_and_add64(&_memory_addr,
		                  (incr * (intptr_t)SPAN_ADDRESS_GRANULARITY));
		pages_ptr = mmap(base_addr, total_size, PROT_READ | PROT_WRITE,
		                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, -1, 0);
		if (!((uintptr_t)pages_ptr & ~(uintptr_t)SPAN_MASK)) {
			if (pages_ptr != base_addr) {
				pages_ptr = (void*)((uintptr_t)pages_ptr & (uintptr_t)SPAN_MASK);
				atomic_store64(&_memory_addr, (int64_t)((uintptr_t)pages_ptr) +
							   (incr * (intptr_t)SPAN_ADDRESS_GRANULARITY));
				atomic_thread_fence_release();
			}
			break;
		}
		munmap(pages_ptr, total_size);
	}
	while (1);
#endif

	return pages_ptr;
}

static void
_memory_unmap(void* ptr, size_t page_count) {
#ifdef PLATFORM_WINDOWS
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	munmap(ptr, PAGE_SIZE * page_count);
#endif
}

static FORCEINLINE int
atomic_cas_ptr(atomicptr_t* dst, void* val, void* ref) {
#ifdef _MSC_VER
#  if ARCH_64BIT
	return (_InterlockedCompareExchange64((volatile long long*)&dst->nonatomic,
	                                      (long long)val, (long long)ref) == (long long)ref) ? 1 : 0;
#  else
	return (_InterlockedCompareExchange((volatile long*)&dst->nonatomic,
	                                      (long)val, (long)ref) == (long)ref) ? 1 : 0;
#  endif
#else
	return __sync_bool_compare_and_swap(&dst->nonatomic, ref, val);
#endif
}

static void
thread_yield(void) {
#ifdef PLATFORM_WINDOWS
	Sleep(0);
#else
	sched_yield();
#endif
}

// Extern interface

void* 
rpmalloc(size_t size) {
	return _memory_allocate(size);
}

void
rpfree(void* ptr) {
	_memory_deallocate(ptr);
}

void*
rpcalloc(size_t num, size_t size) {
	size_t total = num * size;
	void* ptr = _memory_allocate(total);
	memset(ptr, 0, total);
	return ptr;
}

void*
rprealloc(void* ptr, size_t size) {
	return _memory_reallocate(ptr, size, 0);
}

void*
rpaligned_alloc(size_t alignment, size_t size) {
	if (alignment > 16)
		return 0;
	return _memory_allocate(size);
}

void*
rpmemalign(size_t alignment, size_t size) {
	if (alignment > 16)
		return 0;
	return _memory_allocate(size);
}

int
rpposix_memalign(void **memptr, size_t alignment, size_t size) {
	if (!memptr || (alignment > 16))
		return EINVAL;
	*memptr = _memory_allocate(size);
	if (*memptr)
		return 0;
	return ENOMEM;
}

