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

//! Limit of thread cache for each page count class (undefine for unlimited cache - i.e never release spans to global cache unless thread finishes)
#define THREAD_SPAN_CACHE_LIMIT(page_count)   ((128*1024) * page_count)
//! Limit of global cache for each page count class (undefine for unlimited cache - i.e never free mapped pages)
#define GLOBAL_SPAN_CACHE_LIMIT(page_count)   ((2*1024*1024) * page_count)
//! Limit of thread cache for each large span count class (undefine for unlimited cache - i.e never release spans to global cache unless thread finishes)
#define THREAD_LARGE_CACHE_LIMIT(span_count)  (70 - (span_count * 2))
//! Limit of global cache for each large span count class (undefine for unlimited cache - i.e never free mapped pages)
#define GLOBAL_LARGE_CACHE_LIMIT(span_count)  (256 - (span_count * 3))
//! Size of heap hashmap
#define HEAP_ARRAY_SIZE           79
//! Enable statistics collection
#define ENABLE_STATISTICS         0

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

#if ENABLE_STATISTICS

static FORCEINLINE int32_t
atomic_add32(atomic32_t* val, int32_t add) {
#ifdef _MSC_VER
	int32_t old = (int32_t)_InterlockedExchangeAdd((volatile long*)&val->nonatomic, add);
	return (old + add);
#else
	return __sync_add_and_fetch(&val->nonatomic, add);
#endif
}

#endif

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
#define SPAN_MASK                 (~((uintptr_t)SPAN_MAX_SIZE - 1))
#define SPAN_MAX_PAGE_COUNT       (SPAN_MAX_SIZE / PAGE_SIZE)
#define SPAN_CLASS_COUNT          SPAN_MAX_PAGE_COUNT

#define SMALL_GRANULARITY         16
#define SMALL_GRANULARITY_SHIFT   4
#define SMALL_CLASS_COUNT         (((PAGE_SIZE - SPAN_HEADER_SIZE) / 2) / SMALL_GRANULARITY)
#define SMALL_SIZE_LIMIT          (SMALL_CLASS_COUNT * SMALL_GRANULARITY)

#define MEDIUM_GRANULARITY        512
#define MEDIUM_GRANULARITY_SHIFT  9
#define MEDIUM_CLASS_COUNT        60
#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY * MEDIUM_CLASS_COUNT) - SPAN_HEADER_SIZE)

#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)

#define SPAN_LIST_LOCK_TOKEN      ((void*)1)

#define LARGE_CLASS_COUNT         32
#define LARGE_MAX_PAGES           (SPAN_MAX_PAGE_COUNT * LARGE_CLASS_COUNT)
#define LARGE_SIZE_LIMIT          ((LARGE_MAX_PAGES * PAGE_SIZE) - SPAN_HEADER_SIZE)

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

#define SPAN_HEADER_SIZE          32
#if ARCH_64BIT
typedef int64_t offset_t;
#else
typedef int32_t offset_t;
#endif
typedef uint32_t count_t;

// Data types

typedef struct span_t span_t;
typedef struct heap_t heap_t;
typedef struct size_class_t size_class_t;
typedef struct span_block_t span_block_t;
typedef union span_data_t span_data_t;

struct span_block_t {
	//! Free list
	uint8_t    free_list;
	//! First autolinked block
	uint8_t    first_autolink;
	//! Free count
	uint8_t    free_count;
	//! Padding
	uint8_t    padding;
};

union span_data_t {
	//! Span data
	span_block_t block;
	//! Span count (for large spans)
	uint32_t span_count;
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
	span_t*     next_span;
	//! Previous span
	span_t*     prev_span;
};
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "span size mismatch");

struct heap_t {
	//! Heap ID
	int32_t      id;
	//! Deferred deallocation
	atomicptr_t  defer_deallocate;
	//! Free count for each size class active span
	span_block_t active_block[SIZE_CLASS_COUNT];
	//! Active span for each size class
	span_t*      active_span[SIZE_CLASS_COUNT];
	//! List of spans with free blocks for each size class
	span_t*      size_cache[SIZE_CLASS_COUNT];
	//! List of free spans for each page count
	span_t*      span_cache[SPAN_CLASS_COUNT];
	//! List of free spans for each large class count
	span_t*      large_cache[LARGE_CLASS_COUNT];
	//! Next heap in id list
	heap_t*      next_heap;
	//! Next heap in orphan list
	heap_t*      next_orphan;
#if ENABLE_STATISTICS
	size_t       requested;
	size_t       allocated;
#endif
};
_Static_assert(sizeof(heap_t) <= PAGE_SIZE*2, "heap size mismatch");

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

//! Global large cache
static atomicptr_t _memory_large_cache[LARGE_CLASS_COUNT];

//! Current thread heap
static _Thread_local heap_t* _memory_thread_heap;

//! All heaps
static atomicptr_t _memory_heaps[HEAP_ARRAY_SIZE];

//! Orphaned heaps
static atomicptr_t _memory_orphan_heaps;

#if ENABLE_STATISTICS
static atomic32_t _mapped_pages;
#endif

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
_memory_global_cache_insert(span_t* first_span, size_t list_size, size_t page_count) {
	while (1) {
		void* global_span_ptr = atomic_load_ptr(&_memory_span_cache[page_count-1]);
		if (global_span_ptr != SPAN_LIST_LOCK_TOKEN) {
			uintptr_t global_list_size = (uintptr_t)global_span_ptr & ~SPAN_MASK;
			span_t* global_span = (span_t*)((void*)((uintptr_t)global_span_ptr & SPAN_MASK));

#ifdef GLOBAL_SPAN_CACHE_LIMIT
			size_t list_bytes = global_list_size * page_count * PAGE_SIZE;
			if (list_bytes >= GLOBAL_SPAN_CACHE_LIMIT(page_count))
				break;
#endif
			if ((global_list_size + list_size) > 0xFFFF)
				break;

			//Use prev_span as skip pointer over this range of spans
			first_span->data.list_size = (uint32_t)list_size;
			first_span->prev_span = global_span;

			global_list_size += list_size;
			void* first_span_ptr = (void*)((uintptr_t)first_span | global_list_size);

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
	for (size_t ispan = 0; ispan < list_size; ++ispan) {
		span_t* next_span = first_span->next_span;
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
			uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~SPAN_MASK;
			span = (span_t*)((void*)((uintptr_t)global_span_ptr & SPAN_MASK));

			span_t* new_global_span = span->prev_span;
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

static void
_memory_global_cache_large_insert(span_t* span_list, size_t list_size, size_t span_count) {
	atomicptr_t* cache = &_memory_large_cache[span_count - 1];
	while (1) {
		void* global_span_ptr = atomic_load_ptr(cache);
		if (global_span_ptr != SPAN_LIST_LOCK_TOKEN) {
			uintptr_t global_list_size = (uintptr_t)global_span_ptr & ~SPAN_MASK;
			span_t* global_span = (span_t*)((void*)((uintptr_t)global_span_ptr & SPAN_MASK));

#ifdef GLOBAL_LARGE_CACHE_LIMIT
			if ((global_list_size + list_size) >= GLOBAL_LARGE_CACHE_LIMIT(span_count))
				break;
#endif
			if ((global_list_size + list_size) > 0xFFFF)
				break;

			//Use prev_span as skip pointer over this range of spans
			span_list->data.list_size = (uint32_t)list_size;
			span_list->prev_span = global_span;

			global_list_size += list_size;
			void* new_global_span_ptr = (void*)((uintptr_t)span_list | global_list_size);

			if (atomic_cas_ptr(cache, new_global_span_ptr, global_span_ptr)) {
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
	for (size_t ispan = 0; ispan < list_size; ++ispan) {
		span_t* next_span = first_span->next_span;
		_memory_unmap(first_span, span_count * SPAN_MAX_PAGE_COUNT);
		first_span = next_span;
	}
}

static span_t*
_memory_global_cache_large_extract(size_t span_count) {
	span_t* span = 0;
	atomicptr_t* cache = &_memory_large_cache[span_count - 1];
	atomic_thread_fence_acquire();
	void* global_span_ptr = atomic_load_ptr(cache);
	while (global_span_ptr) {
		if ((global_span_ptr != SPAN_LIST_LOCK_TOKEN) &&
			atomic_cas_ptr(cache, SPAN_LIST_LOCK_TOKEN, global_span_ptr)) {
			//Grab a number of thread cache spans, using the skip span pointer stored in prev_span to quickly
			//skip ahead in the list to get the new head
			uintptr_t global_list_size = (uintptr_t)global_span_ptr & ~SPAN_MASK;
			span = (span_t*)((void*)((uintptr_t)global_span_ptr & SPAN_MASK));

			span_t* new_global_span = span->prev_span;
			global_list_size -= span->data.list_size;

			void* new_global_span_ptr = global_list_size ?
				((void*)((uintptr_t)new_global_span | global_list_size)) :
				0;

			atomic_store_ptr(cache, new_global_span_ptr);
			atomic_thread_fence_release();

			break;
		}

		thread_yield();
		atomic_thread_fence_acquire();

		global_span_ptr = atomic_load_ptr(cache);
	}

	return span;
}

static int
_memory_deallocate_deferred(heap_t* heap, size_t size_class);

static void*
_memory_allocate_from_heap(heap_t* heap, size_t size) {
#if ENABLE_STATISTICS
	size += sizeof(size_t);
#endif

	const size_t class_idx = (size <= SMALL_SIZE_LIMIT) ?
		((size + (SMALL_GRANULARITY - 1)) >> SMALL_GRANULARITY_SHIFT) - 1 :
		SMALL_CLASS_COUNT + ((size - SMALL_SIZE_LIMIT + (MEDIUM_GRANULARITY - 1)) >> MEDIUM_GRANULARITY_SHIFT) - 1;

	span_block_t* active_block = heap->active_block + class_idx;
	size_class_t* size_class = _memory_size_class + class_idx;
	const count_t class_size = size_class->size;

#if ENABLE_STATISTICS
	heap->allocated += class_size;
	heap->requested += size;
#endif

use_active:
	if (active_block->free_count) {
		//Happy path, we have a span with at least one free block
		span_t* span = heap->active_span[class_idx];
		count_t offset = class_size * active_block->free_list;
		uint32_t* block = pointer_offset(span, SPAN_HEADER_SIZE + offset);

		--active_block->free_count;
		if (!active_block->free_count) {
			span->data.block.free_count = 0;
			span->data.block.first_autolink = size_class->block_count;
			heap->active_span[class_idx] = 0;
		}
		else {
			if (active_block->free_list < active_block->first_autolink) {
				active_block->free_list = (uint8_t)(*block);
			}
			else {
				++active_block->free_list;
				++active_block->first_autolink;
			}
		}

#if ENABLE_STATISTICS
		*(size_t*)pointer_offset(block, class_size - sizeof(size_t)) = size;
#endif

		return block;
	}

	if (_memory_deallocate_deferred(heap, class_idx)) {
		if (active_block->free_count)
			goto use_active;
	}

	if (heap->size_cache[class_idx]) {
		//Promote a pending semi-used span
		span_t* span = heap->size_cache[class_idx];
		*active_block = span->data.block;
		span_t* next_span = span->next_span;
		heap->size_cache[class_idx] = next_span;
		heap->active_span[class_idx] = span;
		goto use_active;
	}

	//No span in use, grab a new span from heap cache
	span_t* span = heap->span_cache[size_class->page_count-1];
	if (!span)
		span = _memory_global_cache_extract(size_class->page_count);
	if (span) {
		if (span->data.list_size > 1) {
			span_t* next_span = span->next_span;
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

	//If we only have one block we will grab it, otherwise
	//set span as new span to use for next allocation
	if (size_class->block_count > 1) {
		active_block->free_count = (uint8_t)(size_class->block_count - 1);
		active_block->free_list = 1;
		active_block->first_autolink = 1;
		heap->active_span[class_idx] = span;
	}
	else {
		span->data.block.free_count = 0;
		span->data.block.first_autolink = size_class->block_count;
	}

#if ENABLE_STATISTICS
	*(size_t*)pointer_offset(span, SPAN_HEADER_SIZE + class_size - sizeof(size_t)) = size;
#endif

	//Return first block
	return pointer_offset(span, SPAN_HEADER_SIZE);
}

static void*
_memory_allocate_large_from_heap(heap_t* heap, size_t size) {
	size += SPAN_HEADER_SIZE;

	size_t num_spans = size / SPAN_MAX_SIZE;
	if (size % SPAN_MAX_SIZE)
		++num_spans;

	size_t idx = num_spans - 1;
	while (!heap->large_cache[idx] && (idx < LARGE_CLASS_COUNT) && (idx < num_spans + 1))
		++idx;
	span_t* span = heap->large_cache[idx];
	if (span) {
		//Happy path, use from cache
		if (span->data.list_size > 1) {
			span->next_span->data.list_size = span->data.list_size - 1;
			heap->large_cache[idx] = span->next_span;
		}
		else {
			heap->large_cache[idx] = 0;
		}
		span->data.span_count = (uint32_t)(idx + 1);
	}
	else {
		span = _memory_global_cache_large_extract(num_spans);
		if (span) {
			if (span->data.list_size > 1) {
				heap->large_cache[idx] = span->next_span;
				heap->large_cache[idx]->prev_span = 0;
				heap->large_cache[idx]->data.list_size = span->data.list_size - 1;
			}
		}
		else {
			span = _memory_map(num_spans * SPAN_MAX_PAGE_COUNT);
			span->size_class = SIZE_CLASS_COUNT;
		}
		span->data.span_count = (uint32_t)num_spans;
		atomic_store32(&span->heap_id, heap->id);
		atomic_thread_fence_release();
	}
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
		next_heap = heap->next_orphan;
	}
	while (!atomic_cas_ptr(&_memory_orphan_heaps, next_heap, heap));

	if (heap) {
		heap->next_orphan = 0;
		return heap;
	}

	size_t page_count = sizeof(heap_t) / PAGE_SIZE;
	if (sizeof(heap_t) % PAGE_SIZE)
		++page_count;
	heap = _memory_map(page_count);
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
_memory_list_add(span_t** head, span_t* span) {
	if (*head) {
		(*head)->prev_span = span;
		span->next_span = *head;
	}
	else {
		span->next_span = 0;
	}
	*head = span;
}

static void
_memory_list_remove(span_t** head, span_t* span) {
	if (*head == span) {
		*head = span->next_span;
	}
	else {
		if (span->next_span)
			span->next_span->prev_span = span->prev_span;
		span->prev_span->next_span = span->next_span;
	}
}

static void
_memory_deallocate_to_heap(heap_t* heap, span_t* span, void* p) {
	const count_t class_idx = span->size_class;
	size_class_t* size_class = _memory_size_class + class_idx;
	int is_active = (heap->active_span[class_idx] == span);
	span_block_t* block_data = is_active ?
		heap->active_block + class_idx :
		&span->data.block;

#if ENABLE_STATISTICS
	heap->allocated -= size_class->size;
	heap->requested -= *(size_t*)pointer_offset(p, size_class->size - sizeof(size_t));
#endif

	if (!is_active && (block_data->free_count == ((count_t)size_class->block_count - 1))) {
		//Remove from free list (present if we had a previous free block)
		if (block_data->free_count > 0)
			_memory_list_remove(&heap->size_cache[class_idx], span);

		//Add to span cache
		span_t** cache = &heap->span_cache[size_class->page_count-1];
		if (*cache) {
			span->next_span = *cache;
			span->prev_span = (*cache)->prev_span; //Propagate skip span
			span->data.list_size = (*cache)->data.list_size + 1;
		}
		else {
			span->next_span = 0;
			span->data.list_size = 1;
		}
		*cache = span;
#if defined(THREAD_SPAN_CACHE_LIMIT)
		const size_t cache_limit = THREAD_SPAN_CACHE_LIMIT(size_class->page_count) / ((size_t)size_class->page_count * PAGE_SIZE);
		if (span->data.list_size >= cache_limit) {
			//Release to global cache
			*cache = span->prev_span->next_span;
			span->prev_span->next_span = 0; //Terminate list
			_memory_global_cache_insert(span, span->data.list_size - (*cache)->data.list_size, size_class->page_count);
		}
		else if (span->data.list_size == ((cache_limit / 2) + 1)) {
			span->prev_span = span; //Set last span to release as skip span
		}
#endif
	}
	else {
		if (block_data->free_count == 0) {
			//Add to free list
			_memory_list_add(&heap->size_cache[class_idx], span);
			block_data->first_autolink = size_class->block_count;
			block_data->free_count = 0;
		}
		++block_data->free_count;
		if (block_data->free_count < size_class->block_count) {
			uint32_t* block = p;
			*block = block_data->free_list;
			count_t block_offset = (count_t)pointer_diff(block, span) - SPAN_HEADER_SIZE;
			count_t block_idx = block_offset / (count_t)size_class->size;
			block_data->free_list = (uint8_t)block_idx;
		}
		else {
			block_data->free_list = 0;
			block_data->first_autolink = 0;
		}
	}
}

static void
_memory_deallocate_large_to_heap(heap_t* heap, span_t* span) {
	size_t idx = span->data.span_count - 1;
	span_t* head = heap->large_cache[idx];
	span->next_span = head;
	//Use prev_span pointer for quick skip access to next head element after release
	if (head) {
		span->data.list_size = head->data.list_size + 1;
#if defined(THREAD_LARGE_CACHE_LIMIT)
		const size_t cache_limit = THREAD_LARGE_CACHE_LIMIT(span->data.span_count);
		span->prev_span = head->prev_span; //Propagate skip span pointer
		if (span->data.list_size >= cache_limit) {
			heap->large_cache[idx] = span->prev_span->next_span;
			span->prev_span->next_span = 0; //Terminate list
			_memory_global_cache_large_insert(span, span->data.list_size - heap->large_cache[idx]->data.list_size, idx + 1);
			return;
		}
		else if (span->data.list_size == ((cache_limit / 2) + 1)) {
			span->prev_span = span; //Set last span to release as skip span
		}
#endif
	}
	else {
		span->data.list_size = 1;
	}
	heap->large_cache[idx] = span;
}

static int
_memory_deallocate_deferred(heap_t* heap, size_t size_class) {
	atomic_thread_fence_acquire();
	void* p = atomic_load_ptr(&heap->defer_deallocate);
	if (!p)
		return 0;
	if (!atomic_cas_ptr(&heap->defer_deallocate, 0, p))
		return 0;
	int got_class = 0;
	do {
		void* next = *(void**)p;
		span_t* span = (void*)((uintptr_t)p & SPAN_MASK);
		if (span->size_class < SIZE_CLASS_COUNT) {
			got_class |= (span->size_class == size_class);
			_memory_deallocate_to_heap(heap, span, p);
		}
		else {
			_memory_deallocate_large_to_heap(heap, span);
		}
		p = next;
	} while (p);
	return got_class;
}

static void
_memory_deallocate_defer(int32_t heap_id, void* p) {
	//Delegate to heap
	heap_t* heap = _memory_heap_lookup(heap_id);
	void* last_ptr;
	do {
		last_ptr = atomic_load_ptr(&heap->defer_deallocate);
		*(void**)p = last_ptr;
	} while (!atomic_cas_ptr(&heap->defer_deallocate, p, last_ptr));
}

static void*
_memory_allocate(size_t size) {
	if (size <= MEDIUM_SIZE_LIMIT) {
		heap_t* heap = _memory_thread_heap;
		if (heap)
			return _memory_allocate_from_heap(heap, size);

		heap = _memory_allocate_heap();
		if (heap) {
			_memory_thread_heap = heap;
			return _memory_allocate_from_heap(heap, size);
		}
	}
	else if (size <= LARGE_SIZE_LIMIT) {
		heap_t* heap = _memory_thread_heap;
		if (heap)
			return _memory_allocate_large_from_heap(heap, size);

		heap = _memory_allocate_heap();
		if (heap) {
			_memory_thread_heap = heap;
			return _memory_allocate_large_from_heap(heap, size);
		}
	}

	//Oversized, allocate pages directly
	size += SPAN_HEADER_SIZE;
	size_t num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE)
		++num_pages;
	span_t* span = _memory_map(num_pages);
	atomic_store32(&span->heap_id, 0);
	//Store page count in next_span
	span->next_span = (span_t*)((uintptr_t)num_pages);

	return pointer_offset(span, SPAN_HEADER_SIZE);
}

static void
_memory_deallocate(void* p) {
	if (!p)
		return;

	span_t* span = (void*)((uintptr_t)p & SPAN_MASK);
	int32_t heap_id = atomic_load32(&span->heap_id);
	heap_t* heap = _memory_thread_heap;
	if (heap && (heap_id == heap->id)) {
		if (span->size_class < SIZE_CLASS_COUNT)
			_memory_deallocate_to_heap(heap, span, p);
		else
			_memory_deallocate_large_to_heap(heap, span);
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
		span = (void*)((uintptr_t)p & SPAN_MASK);
		int32_t heap_id = atomic_load32(&span->heap_id);
		if (heap_id) {
			if (span->size_class < SIZE_CLASS_COUNT) {
				size_class_t* size_class = _memory_size_class + span->size_class;
				if ((size_t)size_class->size >= size)
					return p; //Still fits in block
				if (!oldsize)
					oldsize = size_class->size;
			}
			else {
				//Large span
				size_t total_size = size + SPAN_HEADER_SIZE;
				size_t num_spans = total_size / SPAN_MAX_SIZE;
				if (total_size % SPAN_MAX_SIZE)
					++num_spans;
				size_t current_spans = span->data.span_count;
				if ((current_spans >= num_spans) && (num_spans >= (current_spans / 2)))
					return p; //Still fits and less than half of memory would be freed
				if (!oldsize)
					oldsize = current_spans * (size_t)SPAN_MAX_SIZE;
			}
		}
		else {
			//Oversized block
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
	size_t remain_size, block_count, wasted, overhead;
	float current_factor;
	float best_factor = 0;
	size_t best_page_count = 0;
	size_t best_block_count = 0;
	size_t page_size_counter = 0;

	while (1) {
		size_t page_size = PAGE_SIZE * (++page_size_counter);
		if (page_size > (PAGE_SIZE * SPAN_MAX_PAGE_COUNT))
			break;
		remain_size = page_size - header_size;
		block_count = remain_size / block_size;
		if (block_count > 255)
			break;
		if (!block_count)
			continue;
		wasted = remain_size - (block_size * block_count);
		overhead = wasted + header_size;
		current_factor = (float)overhead / ((float)block_count * (float)block_size);
		if (block_count > best_block_count) {
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

	size_t heap_page_count = sizeof(heap_t) / PAGE_SIZE;
	if (sizeof(heap_t) % PAGE_SIZE)
		++heap_page_count;

	//Free all thread caches
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = atomic_load_ptr(&_memory_heaps[list_idx]);
		while (heap) {
			_memory_deallocate_deferred(heap, 0);

			for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
				const size_t page_count = iclass + 1;
				span_t* span = heap->span_cache[iclass];
				unsigned int span_count = span ? span->data.list_size : 0;
				for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
					span_t* next_span = span->next_span;
					_memory_unmap(span, page_count);
					span = next_span;
				}
			}

			//Free large spans
			for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
				const size_t span_count = iclass + 1;
				span_t* span = heap->large_cache[iclass];
				while (span) {
					span_t* next_span = span->next_span;
					_memory_unmap(span, span_count * SPAN_MAX_PAGE_COUNT);
					span = next_span;
				}
			}

			heap_t* next_heap = heap->next_heap;
			_memory_unmap(heap, heap_page_count);
			heap = next_heap;
		}

		atomic_store_ptr(&_memory_heaps[list_idx], 0);
	}

	//Free global caches
	for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
		void* span_ptr = atomic_load_ptr(&_memory_span_cache[iclass]);
		size_t cache_count = (uintptr_t)span_ptr & ~SPAN_MASK;
		span_t* span = (span_t*)((void*)((uintptr_t)span_ptr & SPAN_MASK));
		while (cache_count) {
			span_t* skip_span = span->prev_span;
			unsigned int span_count = span->data.list_size;
			for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
				span_t* next_span = span->next_span;
				_memory_unmap(span, iclass+1);
				span = next_span;
			}
			span = skip_span;
			cache_count -= span_count;
		}
		atomic_store_ptr(&_memory_span_cache[iclass], 0);
	}

	for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
		void* span_ptr = atomic_load_ptr(&_memory_large_cache[iclass]);
		size_t cache_count = (uintptr_t)span_ptr & ~SPAN_MASK;
		span_t* span = (span_t*)((void*)((uintptr_t)span_ptr & SPAN_MASK));
		while (cache_count) {
			span_t* skip_span = span->prev_span;
			unsigned int span_count = span->data.list_size;
			for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
				span_t* next_span = span->next_span;
				_memory_unmap(span, (iclass + 1) * SPAN_MAX_PAGE_COUNT);
				span = next_span;
			}
			span = skip_span;
			cache_count -= span_count;
		}
		atomic_store_ptr(&_memory_large_cache[iclass], 0);
	}

	atomic_thread_fence_release();
}

void
rpmalloc_thread_finalize(void) {
	heap_t* heap = _memory_thread_heap;
	if (heap) {
		//Release thread cache spans back to global cache
		for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
			const size_t page_count = iclass + 1;
			const size_t cache_limit = THREAD_SPAN_CACHE_LIMIT(page_count);
			span_t* span = heap->span_cache[iclass];
			while (span) {
				if (span->data.list_size >(cache_limit / 2)) {
					span_t* new_head = span->prev_span->next_span;
					span->prev_span->next_span = 0; //Terminate list
					_memory_global_cache_insert(span, span->data.list_size - new_head->data.list_size, page_count);
					span = new_head;
				}
				else {
					_memory_global_cache_insert(span, span->data.list_size, page_count);
					span = 0;
				}
			}
			heap->span_cache[iclass] = 0;
		}

		for (size_t iclass = 0; iclass < LARGE_CLASS_COUNT; ++iclass) {
			const size_t span_count = iclass + 1;
			const size_t cache_limit = THREAD_LARGE_CACHE_LIMIT(span_count);
			span_t* span = heap->large_cache[iclass];
			while (span) {
				if (span->data.list_size > (cache_limit / 2)) {
					span_t* new_head = span->prev_span->next_span;
					span->prev_span->next_span = 0; //Terminate list
					_memory_global_cache_large_insert(span, span->data.list_size - new_head->data.list_size, span_count);
					span = new_head;
				}
				else  {
					_memory_global_cache_large_insert(span, span->data.list_size, span_count);
					span = 0;
				}
			}
			heap->large_cache[iclass] = 0;
		}

		//Orphan the heap
		heap_t* last_heap;
		do {
			last_heap = atomic_load_ptr(&_memory_orphan_heaps);
			heap->next_orphan = last_heap;
		}
		while (!atomic_cas_ptr(&_memory_orphan_heaps, heap, last_heap));
	}
	
	_memory_thread_heap = 0;
}

static void*
_memory_map(size_t page_count) {
	size_t total_size = page_count * PAGE_SIZE;
	void* pages_ptr = 0;

#if ENABLE_STATISTICS
	atomic_add32(&_mapped_pages, (int32_t)page_count);
#endif

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
		if (!((uintptr_t)pages_ptr & ~SPAN_MASK)) {
			if (pages_ptr != base_addr) {
				pages_ptr = (void*)((uintptr_t)pages_ptr & SPAN_MASK);
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
#if ENABLE_STATISTICS
	atomic_add32(&_mapped_pages, -(int32_t)page_count);
#endif

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
	//Sleep(0);
	YieldProcessor();
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

void
rpmalloc_thread_collect(void) {
	heap_t* heap = _memory_thread_heap;
	if (heap)
		_memory_deallocate_deferred(heap, 0);
}

rpmalloc_thread_statistics_t
rpmalloc_thread_statistics(void) {
	rpmalloc_thread_statistics_t stats;
	memset(&stats, 0, sizeof(stats));
	heap_t* heap = _memory_thread_heap;
	if (heap) {
#if ENABLE_STATISTICS
		stats.allocated = heap->allocated;
#endif
		void* p = atomic_load_ptr(&heap->defer_deallocate);
		while (p) {
			void* next = *(void**)p;
			span_t* span = (void*)((uintptr_t)p & SPAN_MASK);
			stats.deferred += _memory_size_class[span->size_class].size;
			p = next;
		}

		for (size_t isize = 0; isize < SIZE_CLASS_COUNT; ++isize) {
			if (heap->active_block[isize].free_count)
				stats.active += heap->active_block[isize].free_count * _memory_size_class[heap->active_span[isize]->size_class].size;

			span_t* cache = heap->size_cache[isize];
			while (cache) {
				stats.sizecache = cache->data.block.free_count * _memory_size_class[cache->size_class].size;
				cache = cache->next_span;
			}
		}

		for (size_t isize = 0; isize < SPAN_CLASS_COUNT; ++isize) {
			if (heap->span_cache[isize])
				stats.spancache = (size_t)heap->span_cache[isize]->data.list_size * (isize + 1) * PAGE_SIZE;
		}
	}
	return stats;
}

rpmalloc_global_statistics_t
rpmalloc_global_statistics(void) {
	rpmalloc_global_statistics_t stats;
	memset(&stats, 0, sizeof(stats));
#if ENABLE_STATISTICS
	stats.mapped = atomic_load32(&_mapped_pages) * PAGE_SIZE;
#endif
	for (size_t page_count = 0; page_count < SPAN_CLASS_COUNT; ++page_count) {
		void* global_span_ptr = atomic_load_ptr(&_memory_span_cache[page_count]);
		while (global_span_ptr == SPAN_LIST_LOCK_TOKEN) {
			thread_yield();
			global_span_ptr = atomic_load_ptr(&_memory_span_cache[page_count]);
		}
		uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~SPAN_MASK;
		size_t list_bytes = global_span_count * (page_count + 1) * PAGE_SIZE;
		stats.cache += list_bytes;
	}
	return stats;
}
