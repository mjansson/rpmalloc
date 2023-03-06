/* rpmalloc.c  -  Memory allocator  -  Public Domain  -  2016-2020 Mattias
 * Jansson
 *
 * This library provides a cross-platform lock free thread caching malloc
 * implementation in C11. The latest source code is always available at
 *
 * https://github.com/mjansson/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or
 * modify it without any restrictions.
 *
 */

#include "rpmalloc.h"

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-macros"
#pragma clang diagnostic ignored "-Wunused-function"
#if __has_warning("-Wreserved-identifier")
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
#if __has_warning("-Wstatic-in-inline")
#pragma clang diagnostic ignored "-Wstatic-in-inline"
#endif
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-macros"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#define PLATFORM_POSIX 0
#else
#define PLATFORM_WINDOWS 0
#define PLATFORM_POSIX 1
#endif

#if PLATFORM_WINDOWS
#include <windows.h>
#endif
#if PLATFORM_POSIX
#include <sys/mman.h>
#include <sched.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#define MAP_HUGETLB MAP_ALIGNED_SUPER
#ifndef PROT_MAX
#define PROT_MAX(f) 0
#endif
#else
#define PROT_MAX(f) 0
#endif
#ifdef __sun
extern int
madvise(caddr_t, size_t, int);
#endif
#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0
#endif
#endif

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/prctl.h>
#if !defined(PR_SET_VMA)
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
#include <mach/mach_vm.h>
#include <mach/vm_statistics.h>
#endif
#include <pthread.h>
#endif
#if defined(__HAIKU__) || defined(__TINYC__)
#include <pthread.h>
#endif

#include <limits.h>
#if (INTPTR_MAX == LONG_MAX)
#define ARCH_64BIT 1
#define ARCH_32BIT 0
#else
#define ARCH_64BIT 0
#define ARCH_32BIT 1
#endif

#if !defined(__has_builtin)
#define __has_builtin(b) 0
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

////////////
///
/// Build time configurable limits
///
//////

#ifndef ENABLE_VALIDATE_ARGS
//! Enable validation of args to public entry points
#define ENABLE_VALIDATE_ARGS 0
#endif
#ifndef ENABLE_ASSERTS
//! Enable asserts
#define ENABLE_ASSERTS 0
#endif
#ifndef ENABLE_UNMAP
//! Enable unmapping memory pages
#define ENABLE_UNMAP 1
#endif

////////////
///
/// Built in size configurations
///
//////

#define PAGE_HEADER_SIZE 128
#define SPAN_HEADER_SIZE PAGE_HEADER_SIZE

#define SMALL_GRANULARITY 16

#define SMALL_BLOCK_SIZE_LIMIT (4 * 1024)
#define MEDIUM_BLOCK_SIZE_LIMIT (256 * 1024)
#define LARGE_BLOCK_SIZE_LIMIT (8 * 1024 * 1024)

#define SMALL_SIZE_CLASS_COUNT 29
#define MEDIUM_SIZE_CLASS_COUNT 24
#define LARGE_SIZE_CLASS_COUNT 20
#define SIZE_CLASS_COUNT (SMALL_SIZE_CLASS_COUNT + MEDIUM_SIZE_CLASS_COUNT + LARGE_SIZE_CLASS_COUNT)

#define SMALL_PAGE_SIZE (64 * 1024)
#define MEDIUM_PAGE_SIZE (4 * 1024 * 1024)
#define LARGE_PAGE_SIZE (64 * 1024 * 1024)

#define SPAN_SIZE (256 * 1024 * 1024)
#define SPAN_MASK (~((uintptr_t)(SPAN_SIZE - 1)))

#define MAX_ALIGNMENT (256 * 1024)

////////////
///
/// Utility macros
///
//////

#if ENABLE_ASSERTS
#undef NDEBUG
#if defined(_MSC_VER) && !defined(_DEBUG)
#define _DEBUG
#endif
#include <assert.h>
#define RPMALLOC_TOSTRING_M(x) #x
#define RPMALLOC_TOSTRING(x) RPMALLOC_TOSTRING_M(x)
#define rpmalloc_assert(truth, message) \
	do {                                \
		if (!(truth)) {                 \
			assert((truth) && message); \
		}                               \
	} while (0)
#else
#define rpmalloc_assert(truth, message) \
	do {                                \
	} while (0)
#endif

////////////
///
/// Low level abstractions
///
//////

#if defined(__GNUC__) || defined(__clang__)

#include <limits.h>
static inline size_t
rpmalloc_clz(uintptr_t x) {
#if (INTPTR_MAX == LONG_MAX)
	return (size_t)__builtin_clzl(x);
#else
	return (size_t)__builtin_clzll(x);
#endif
}

#elif defined(_MSC_VER)

#include <limits.h>
static inline size_t
rpmalloc_clz(uintptr_t x) {
#if (INTPTR_MAX == LONG_MAX)
	return (size_t)__builtin_clzl(x);
#else
	return (size_t)__builtin_clzll(x);
#endif
}

#else
#error Not implemented
#endif

static void
wait_spin(void) {
#if defined(_MSC_VER)
	_mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || (defined(__arm__) && __ARM_ARCH >= 7)
	__asm__ volatile("yield" ::: "memory");
#elif defined(__powerpc__) || defined(__powerpc64__)
	// No idea if ever been compiled in such archs but ... as precaution
	__asm__ volatile("or 27,27,27");
#elif defined(__sparc__)
	__asm__ volatile("rd %ccr, %g0 \n\trd %ccr, %g0 \n\trd %ccr, %g0");
#else
	struct timespec ts = {0};
	nanosleep(&ts, 0);
#endif
}

#include <stdatomic.h>

typedef volatile _Atomic(uint32_t) atomic32_t;
typedef volatile _Atomic(uint64_t) atomic64_t;
typedef volatile _Atomic(void*) atomicptr_t;

#if defined(__GNUC__) || defined(__clang__)

#define EXPECTED(x) __builtin_expect((x), 1)
#define UNEXPECTED(x) __builtin_expect((x), 0)

#else

#define EXPECTED(x)
#define UNEXPECTED(x)

#endif
#if defined(__GNUC__) || defined(__clang__)

#if __has_builtin(__builtin_memcpy_inline)
#define memcpy_const(x, y, s) __builtin_memcpy_inline(x, y, s)
#else
#define memcpy_const(x, y, s)                                                                                   \
	do {                                                                                                        \
		_Static_assert(__builtin_choose_expr(__builtin_constant_p(s), 1, 0), "len must be a constant integer"); \
		memcpy(x, y, s);                                                                                        \
	} while (0)
#endif

#if __has_builtin(__builtin_memset_inline)
#define memset_const(x, y, s) __builtin_memset_inline(x, y, s)
#else
#define memset_const(x, y, s)                                                                                   \
	do {                                                                                                        \
		_Static_assert(__builtin_choose_expr(__builtin_constant_p(s), 1, 0), "len must be a constant integer"); \
		memset(x, y, s);                                                                                        \
	} while (0)
#endif
#else
#define memcpy_const(x, y, s) memcpy(x, y, s)
#define memset_const(x, y, s) memset(x, y, s)
#endif

////////////
///
/// Data types
///
//////

//! A memory heap, per thread
typedef struct heap_t heap_t;
//! Span of memory pages
typedef struct span_t span_t;
//! Memory page
typedef struct page_t page_t;
//! Memory block
typedef struct block_t block_t;
//! Size class for a memory block
typedef struct size_class_t size_class_t;

//! Memory page type
typedef enum page_type_t {
	PAGE_SMALL,   // 64KiB
	PAGE_MEDIUM,  // 4MiB
	PAGE_LARGE,   // 64MiB
	PAGE_HUGE
} page_type_t;

//! Block size class
struct size_class_t {
	//! Size of blocks in this class
	uint32_t block_size;
	//! Number of blocks in each chunk
	uint32_t block_count;
};

//! A memory block
struct block_t {
	//! Next block in list
	block_t* next;
};

//! A page contains blocks of a given size
struct page_t {
	//! Local free list
	block_t* local_free;
	//! Local free list count
	uint32_t local_free_count;
	//! Multithreaded free list, block index is in low 32 bit, list count is high 32 bit
	atomic64_t thread_free;
	//! Size class of blocks
	uint32_t size_class;
	//! Block size
	uint32_t block_size;
	//! Block count
	uint32_t block_count;
	//! Block initialized count
	uint32_t block_initialized;
	//! Block used count
	uint32_t block_used;
	//! Page type
	page_type_t page_type;
	//! Flag set if part of heap full list
	uint32_t is_full : 1;
	//! Flag set if part of heap available list
	uint32_t is_available : 1;
	//! Flag set if part of heap free list
	uint32_t is_free : 1;
	//! Flag set if blocks are zero initialied
	uint32_t is_zero : 1;
	//! Flag set if containing aligned blocks
	uint32_t has_aligned_block : 1;
	//! Unused flags
	uint32_t unused : 30;
	//! Owning heap
	heap_t* heap;
	//! Next page in list
	page_t* next;
	//! Previous page in list
	page_t* prev;
};

//! A span contains pages of a given type
struct span_t {
	//! Page header
	page_t page;
	//! Number of pages initialized
	uint32_t page_initialized;
	//! Number of pages in use
	uint32_t page_count;
	//! Number of bytes per page
	uint32_t page_size;
	//! Offset to start of mapped memory region
	uint32_t offset;
	//! Mapped size
	uint64_t mapped_size;
	//! Next span in list
	span_t* next;
	//! Previous span in list
	span_t* prev;
};

// Control structure for a heap, either a thread heap or a first class heap if enabled
struct heap_t {
	//! Owning thread ID
	uintptr_t owner_thread;
	//! Heap ID
	uint32_t id;
	//! Finalization state flag
	int finalize;
	//! Heap local free list for small size classes
	block_t* small_free[SMALL_SIZE_CLASS_COUNT];
	//! Available non-full pages for each size class
	page_t* page_available[SIZE_CLASS_COUNT];
	//! Full pages
	page_t* page_full;
	//! Free pages for each page type
	page_t* page_free[3];
	//! Multithreaded free pages for each page type
	atomicptr_t page_free_thread[3];
	//! Available partially initialized spans for each page type
	span_t* span_partial[3];
	//! Spans in full use for each page type
	span_t* span_used[3];
	//! Next heap in queue
	heap_t* next;
	//! Memory map region offset
	uint32_t offset;
	//! Memory map size
	size_t mapped_size;
};

_Static_assert(sizeof(page_t) <= PAGE_HEADER_SIZE, "Invalid page header size");
_Static_assert(sizeof(span_t) <= SPAN_HEADER_SIZE, "Invalid span header size");

////////////
///
/// Global data
///
//////

//! Fallback heap
static RPMALLOC_CACHE_ALIGNED heap_t global_heap_fallback;
//! Default heap
static atomicptr_t global_heap_default = &global_heap_fallback;
//! Available heaps
static heap_t* global_heap_queue;
//! Lock for heap queue
static atomicptr_t global_heap_lock;
//! Heap ID counter
static atomic32_t global_heap_id = 1;
//! Memory interface
static rpmalloc_interface_t* global_memory_interface;
//! Default memory interface
static rpmalloc_interface_t global_memory_interface_default;

//! Size classes
#define SCLASS(n) \
	{ (n * SMALL_GRANULARITY), (SMALL_PAGE_SIZE - PAGE_HEADER_SIZE) / (n * SMALL_GRANULARITY) }
#define MCLASS(n) \
	{ (n * SMALL_GRANULARITY), (MEDIUM_PAGE_SIZE - PAGE_HEADER_SIZE) / (n * SMALL_GRANULARITY) }
#define LCLASS(n) \
	{ (n * SMALL_GRANULARITY), (LARGE_PAGE_SIZE - PAGE_HEADER_SIZE) / (n * SMALL_GRANULARITY) }
static const size_class_t global_size_class[SIZE_CLASS_COUNT] = {
    SCLASS(1),      SCLASS(1),      SCLASS(2),      SCLASS(3),      SCLASS(4),      SCLASS(5),      SCLASS(6),
    SCLASS(7),      SCLASS(8),      SCLASS(10),     SCLASS(12),     SCLASS(14),     SCLASS(16),     SCLASS(20),
    SCLASS(24),     SCLASS(28),     SCLASS(32),     SCLASS(40),     SCLASS(48),     SCLASS(56),     SCLASS(64),
    SCLASS(80),     SCLASS(96),     SCLASS(112),    SCLASS(128),    SCLASS(160),    SCLASS(192),    SCLASS(224),
    SCLASS(256),    MCLASS(320),    MCLASS(384),    MCLASS(448),    MCLASS(512),    MCLASS(640),    MCLASS(768),
    MCLASS(896),    MCLASS(1024),   MCLASS(1280),   MCLASS(1536),   MCLASS(1792),   MCLASS(2048),   MCLASS(2560),
    MCLASS(3072),   MCLASS(3584),   MCLASS(4096),   MCLASS(5120),   MCLASS(6144),   MCLASS(7168),   MCLASS(8192),
    MCLASS(10240),  MCLASS(12288),  MCLASS(14336),  MCLASS(16384),  LCLASS(20480),  LCLASS(24576),  LCLASS(28672),
    LCLASS(32768),  LCLASS(40960),  LCLASS(49152),  LCLASS(57344),  LCLASS(65536),  LCLASS(81920),  LCLASS(98304),
    LCLASS(114688), LCLASS(131072), LCLASS(163840), LCLASS(196608), LCLASS(229376), LCLASS(262144), LCLASS(327680),
    LCLASS(393216), LCLASS(458752), LCLASS(524288)};

//! Flag indicating huge pages are used
static int global_use_huge_pages;
//! Name to tag mapped huge pages
static const char* global_huge_page_name = "rpmalloc-huge";
//! Name to tag mapped huge pages
static const char* global_page_name = "rpmalloc";
//! OS huge page support
static int os_huge_pages;
//! OS memory map granularity
static size_t os_map_granularity;
//! OS memory page size
static size_t os_page_size;

////////////
///
/// Thread local heap and ID
///
//////

//! Current thread heap
#if defined(_MSC_VER) && !defined(__clang__)
#define TLS_MODEL
#else
#define TLS_MODEL __attribute__((tls_model("initial-exec")))
#endif
static _Thread_local heap_t* global_thread_heap TLS_MODEL;

static inline heap_t*
heap_allocate(int first_class);

//! Fast thread ID
static inline uintptr_t
get_thread_id(void) {
#if defined(_WIN32)
	return (uintptr_t)((void*)NtCurrentTeb());
#elif (defined(__GNUC__) || defined(__clang__)) && !defined(__CYGWIN__)
	uintptr_t tid;
#if defined(__i386__)
	__asm__("movl %%gs:0, %0" : "=r"(tid) : :);
#elif defined(__x86_64__)
#if defined(__MACH__)
	__asm__("movq %%gs:0, %0" : "=r"(tid) : :);
#else
	__asm__("movq %%fs:0, %0" : "=r"(tid) : :);
#endif
#elif defined(__arm__)
	__asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tid));
#elif defined(__aarch64__)
#if defined(__MACH__)
	// tpidr_el0 likely unused, always return 0 on iOS
	__asm__ volatile("mrs %0, tpidrro_el0" : "=r"(tid));
#else
	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(tid));
#endif
#else
#error This platform needs implementation of get_thread_id()
#endif
	return tid;
#else
#error This platform needs implementation of get_thread_id()
#endif
}

//! Set the current thread heap
static void
set_thread_heap(heap_t* heap) {
	global_thread_heap = heap;
	if (heap)
		heap->owner_thread = get_thread_id();
#if defined(_WIN32)
	FlsSetValue(fls_key, heap);
#endif
}

//! Get the current thread heap without automatically initializing thread
static inline heap_t*
get_thread_heap_raw(void) {
	return global_thread_heap;
}

//! Get the current thread heap
static inline heap_t*
get_thread_heap(void) {
	heap_t* heap = get_thread_heap_raw();
	if (EXPECTED(heap != 0))
		return heap;
	heap = heap_allocate(0);
	set_thread_heap(heap);
	return heap;
}

//! Get the size class from given size in bytes (must not be zero)
static inline uint32_t
get_size_class(size_t size) {
	uintptr_t minblock_count = (size + (SMALL_GRANULARITY - 1)) / SMALL_GRANULARITY;
	// For sizes up to 8 times the minimum granularity the size class is equal to number of such blocks
	if (minblock_count <= 8) {
		rpmalloc_assert(global_size_class[minblock_count].block_size >= size, "Size class misconfiguration");
		return (uint32_t)(minblock_count ? minblock_count : 1);
	}
	--minblock_count;
	// Calculate position of most significant bit, since minblock_count now guaranteed to be > 8 this position is
	// guaranteed to be >= 2
#if ARCH_64BIT
	const uint32_t most_significant_bit = (uint32_t)(63 - (int)rpmalloc_clz(minblock_count));
#else
	const uint32_t most_significant_bit = (uint32_t)(31 - (int)rpmalloc_clz(minblock_count));
#endif
	// Class sizes are of the bit format [..]000xxx000[..] where we already have the position of the most significant
	// bit, now calculate the subclass from the remaining two bits
	const uint32_t subclass_bits = (minblock_count >> (most_significant_bit - 2)) & 0x03;
	const uint32_t class_idx = (uint32_t)((most_significant_bit << 2) + subclass_bits) - 3;
	rpmalloc_assert((class_idx >= SIZE_CLASS_COUNT) || (global_size_class[class_idx].block_size >= size),
	                "Size class misconfiguration");
	return class_idx;
}

static inline page_type_t
get_page_type(uint32_t size_class) {
	if (size_class < SMALL_SIZE_CLASS_COUNT)
		return PAGE_SMALL;
	if (size_class < (SMALL_SIZE_CLASS_COUNT + MEDIUM_SIZE_CLASS_COUNT))
		return PAGE_MEDIUM;
	if (size_class < SIZE_CLASS_COUNT)
		return PAGE_LARGE;
	return PAGE_HUGE;
}

static inline size_t
get_page_aligned_size(size_t size) {
	size_t unalign = size % os_page_size;
	if (unalign)
		size += os_page_size - unalign;
	return size;
}

////////////
///
/// OS entry points
///
//////

static void
os_set_page_name(void* address, size_t size) {
#if defined(__linux__) || defined(__ANDROID__)
	const char *name = global_use_huge_pages ? global_huge_page_name : global_page_name;
	if ((address == MAP_FAILED) || !name)
		return;
	// If the kernel does not support CONFIG_ANON_VMA_NAME or if the call fails
	// (e.g. invalid name) it is a no-op basically.
	(void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (uintptr_t)address, size, (uintptr_t)name);
#else
	(void)sizeof(size);
	(void)sizeof(address);
#endif
}

static void*
os_mmap(size_t size, size_t alignment, size_t* offset, size_t* mapped_size) {
	size_t map_size = size + alignment;
#if PLATFORM_WINDOWS
	// Ok to MEM_COMMIT - according to MSDN, "actual physical pages are not allocated unless/until the virtual addresses
	// are actually accessed"
	void* ptr = VirtualAlloc(0, map_size, (global_use_huge_pages ? MEM_LARGE_PAGES : 0) | MEM_RESERVE | MEM_COMMIT,
	                         PAGE_READWRITE);
#else
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED;
#if defined(__APPLE__) && !TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
	int fd = (int)VM_MAKE_TAG(240U);
	if (global_use_huge_pages)
		fd |= VM_FLAGS_SUPERPAGE_SIZE_2MB;
	void* ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, flags, fd, 0);
#elif defined(MAP_HUGETLB)
	void* ptr = mmap(0, map_size, PROT_READ | PROT_WRITE | PROT_MAX(PROT_READ | PROT_WRITE),
	                 (global_use_huge_pages ? MAP_HUGETLB : 0) | flags, -1, 0);
#if defined(MADV_HUGEPAGE)
	// In some configurations, huge pages allocations might fail thus
	// we fallback to normal allocations and promote the region as transparent huge page
	if ((ptr == MAP_FAILED || !ptr) && global_use_huge_pages) {
		ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (ptr && ptr != MAP_FAILED) {
			int prm = madvise(ptr, size, MADV_HUGEPAGE);
			(void)prm;
			rpmalloc_assert((prm == 0), "Failed to promote the page to transparent huge page");
		}
	}
#endif
	os_set_page_name(ptr, map_size);
#elif defined(MAP_ALIGNED)
	const size_t align = (sizeof(size_t) * 8) - (size_t)(__builtin_clzl(size - 1));
	void* ptr =
	    mmap(0, map_size, PROT_READ | PROT_WRITE, (global_use_huge_pages ? MAP_ALIGNED(align) : 0) | flags, -1, 0);
#elif defined(MAP_ALIGN)
	caddr_t base = (global_use_huge_pages ? (caddr_t)(4 << 20) : 0);
	void* ptr = mmap(base, map_size, PROT_READ | PROT_WRITE, (global_use_huge_pages ? MAP_ALIGN : 0) | flags, -1, 0);
#else
	void* ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, flags, -1, 0);
#endif
	if (ptr == MAP_FAILED)
		ptr = 0;
#endif
	if (!ptr) {
		if (global_memory_interface->map_fail_callback) {
			if (global_memory_interface->map_fail_callback(map_size))
				return os_mmap(size, alignment, offset, mapped_size);
		} else {
			rpmalloc_assert(ptr != 0, "Failed to map more virtual memory");
		}
		return 0;
	}
	if (alignment) {
		size_t padding = ((uintptr_t)ptr & (uintptr_t)(alignment - 1));
		if (padding)
			padding = alignment - padding;
		rpmalloc_assert(padding <= alignment, "Internal failure in padding");
		rpmalloc_assert(!(padding % 8), "Internal failure in padding");
		ptr = pointer_offset(ptr, padding);
		*offset = padding;
	}
	*mapped_size = map_size;
	return ptr;
}

static void
os_munmap(void* address, size_t size, size_t alignment, size_t offset, size_t release) {
	(void)sizeof(alignment);
	if (release)
		address = pointer_offset(address, -(int32_t)offset);
#if ENABLE_UNMAP
#if PLATFORM_WINDOWS
	if (!VirtualFree(address, release ? 0 : size, release ? MEM_RELEASE : MEM_DECOMMIT)) {
		rpmalloc_assert(0, "Failed to unmap virtual memory block");
	}
#else
	if (release) {
		if (munmap(address, release)) {
			rpmalloc_assert(0, "Failed to unmap virtual memory block");
		}
	} else {
#if defined(MADV_DONTNEED)
		if (madvise(address, size, MADV_DONTNEED)) {
#elif defined(MADV_FREE_REUSABLE)
		int ret;
		while ((ret = madvise(address, size, MADV_FREE_REUSABLE)) == -1 && (errno == EAGAIN))
			errno = 0;
		if ((ret == -1) && (errno != 0)) {
#elif defined(MADV_PAGEOUT)
		if (madvise(address, size, MADV_PAGEOUT)) {
#elif defined(MADV_FREE)
		if (madvise(address, size, MADV_FREE)) {
#else
		if (posix_madvise(address, size, POSIX_MADV_DONTNEED)) {
#endif
			rpmalloc_assert(0, "Failed to madvise virtual memory block as free");
		}
	}
#endif
#else
	(void)sizeof(size);
#endif
}

////////////
///
/// Page interface
///
//////

static inline span_t*
page_get_span(page_t* page) {
	return (span_t*)((uintptr_t)page & SPAN_MASK);
}

static inline size_t
page_get_size(page_t* page) {
	if (page->page_type == PAGE_SMALL)
		return SMALL_PAGE_SIZE;
	else if (page->page_type == PAGE_MEDIUM)
		return MEDIUM_PAGE_SIZE;
	else if (page->page_type == PAGE_LARGE)
		return LARGE_PAGE_SIZE;
	else
		return page_get_span(page)->page_size;
}

static inline block_t*
page_block_start(page_t* page) {
	return pointer_offset(page, PAGE_HEADER_SIZE);
}

static inline block_t*
page_block(page_t* page, uint32_t block_index) {
	return pointer_offset(page, PAGE_HEADER_SIZE + (page->block_size * block_index));
}

static inline uint32_t
page_block_index(page_t* page, block_t* block) {
	block_t* block_first = page_block_start(page);
	return (uint32_t)pointer_diff(block, block_first) / page->block_size;
}

static inline uint32_t
page_block_from_thread_free_list(page_t* page, uint64_t token, block_t** block) {
	uint32_t block_index = (uint32_t)(token & 0xFFFFFFFFULL);
	uint32_t list_count = (uint32_t)((token >> 32ULL) & 0xFFFFFFFFULL);
	*block = list_count ? page_block(page, block_index) : 0;
	return list_count;
}

static inline uint64_t
page_block_to_thread_free_list(page_t* page, uint32_t block_index, uint32_t list_count) {
	(void)sizeof(page);
	return ((uint64_t)list_count << 32ULL) | (uint64_t)block_index;
}

static inline block_t*
page_block_realign(page_t* page, block_t* block) {
	void* blocks_start = page_block_start(page);
	uint32_t block_offset = (uint32_t)pointer_diff(block, blocks_start);
	return pointer_offset(block, -(int32_t)(block_offset % page->block_size));
}

static inline block_t*
page_get_local_free_block(page_t* page) {
	if (EXPECTED(page->local_free != 0)) {
		block_t* block = page->local_free;
		page->local_free = block->next;
		--page->local_free_count;
		++page->block_used;
		return block;
	}
	return 0;
}

static inline void
page_evict_memory_pages(page_t* page) {
	if (page->block_initialized < (page->block_count >> 1))
		return;
	void* extra_page = pointer_offset(page, os_page_size);
	size_t extra_page_size = page_get_size(page) - os_page_size;
	global_memory_interface->memory_unmap(extra_page, extra_page_size, 0, 0, 0);
}

static inline void
page_put_local_free_block(page_t* page, block_t* block) {
	block->next = page->local_free;
	page->local_free = block;
	++page->local_free_count;
	--page->block_used;

	heap_t* heap = page->heap;
	if (page->block_used == 0) {
		rpmalloc_assert(page->is_available, "Internal page state failure");
		if (heap->page_available[page->size_class] == page) {
			heap->page_available[page->size_class] = page->next;
		} else {
			page->prev->next = page->next;
			if (page->next)
				page->next->prev = page->prev;
		}
		page->is_available = 0;
		page->is_free = 1;
		page_evict_memory_pages(page);
		page->next = heap->page_free[page->page_type];
		heap->page_free[page->page_type] = page;
	} else if (page->is_full) {
		page->next = heap->page_available[page->size_class];
		if (page->next)
			page->next->prev = page;
		heap->page_available[page->size_class] = page;
		page->is_full = 0;
		page->is_available = 1;
	}
}

static inline void
page_adopt_thread_free_block_list(page_t* page) {
	uint64_t thread_free = atomic_load_explicit(&page->thread_free, memory_order_relaxed);
	if (EXPECTED(thread_free != 0)) {
		// Other threads can only replace with another valid list head, this will never change to 0 in other threads
		while (!atomic_compare_exchange_weak_explicit(&page->thread_free, &thread_free, 0, memory_order_relaxed,
		                                              memory_order_relaxed))
			wait_spin();
		page->local_free_count = page_block_from_thread_free_list(page, thread_free, &page->local_free);
		rpmalloc_assert(page->local_free_count <= page->block_used, "Page thread free list count internal failure");
		page->block_used -= page->local_free_count;
	}
}

static inline block_t*
page_get_thread_free_block(page_t* page) {
	page_adopt_thread_free_block_list(page);
	block_t* block = page->local_free;
	if (block) {
		page->local_free = block->next;
		--page->local_free_count;
		++page->block_used;
	}
	return block;
}

static inline void
page_put_thread_free_block(page_t* page, block_t* block) {
	uint64_t prev_thread_free = atomic_load_explicit(&page->thread_free, memory_order_relaxed);
	uint32_t block_index = page_block_index(page, block);
	rpmalloc_assert(page_block(page, block_index) == block, "Block pointer is not aligned to start of block");
	uint32_t list_size = page_block_from_thread_free_list(page, prev_thread_free, &block->next) + 1;
	uint64_t thread_free = page_block_to_thread_free_list(page, block_index, list_size);
	while (!atomic_compare_exchange_weak_explicit(&page->thread_free, &prev_thread_free, thread_free,
	                                              memory_order_relaxed, memory_order_relaxed)) {
		list_size = page_block_from_thread_free_list(page, prev_thread_free, &block->next) + 1;
		thread_free = page_block_to_thread_free_list(page, block_index, list_size);
		wait_spin();
	}
	if ((list_size == 1) && page->is_full) {
		// TODO: Add the page to heap list of potentially available pages
		// rpmalloc_assert(0, "Not implemented");
	} else if (list_size >= page->block_count) {
		// Page is completely freed by multithreaded deallocations, clean up
		// Safe since the page is marked as full and will never be touched by owning heap
		rpmalloc_assert(page->is_full, "Mismatch between page full flag and thread free list");
		page_evict_memory_pages(page);
		heap_t* heap = page->heap;
		void* prev_head = atomic_load_explicit(&heap->page_free_thread[page->page_type], memory_order_relaxed);
		page->next = prev_head;
		while (!atomic_compare_exchange_weak_explicit(&heap->page_free_thread[page->page_type], &prev_head, page,
		                                              memory_order_relaxed, memory_order_relaxed)) {
			page->next = prev_head;
			wait_spin();
		}
	}
}

static inline void
page_push_local_free_to_heap(page_t* page) {
	if ((page->size_class < SMALL_SIZE_CLASS_COUNT) && page->local_free) {
		// Push the page free list as the fast track list of free blocks for heap
		page->heap->small_free[page->size_class] = page->local_free;
		page->block_used += page->local_free_count;
		page->local_free = 0;
		page->local_free_count = 0;
	}
}

static inline void*
page_initialize_blocks(page_t* page) {
	rpmalloc_assert(page->block_initialized < page->block_count, "Block initialization internal failure");
	block_t* block = page_block(page, page->block_initialized++);
	++page->block_used;
	if ((page->page_type == PAGE_SMALL) && (page->block_size < (os_page_size >> 1))) {
		// Link up until next memory page in free list
		void* memory_page_start = (void*)((uintptr_t)block & ~(uintptr_t)(os_page_size - 1));
		void* memory_page_next = pointer_offset(memory_page_start, os_page_size);
		block_t* free_block = pointer_offset(block, page->block_size);
		block_t* first_block = free_block;
		block_t* last_block = free_block;
		while (((void*)free_block < memory_page_next) && (page->block_initialized < page->block_count)) {
			last_block = free_block;
			free_block->next = pointer_offset(free_block, page->block_size);
			free_block = free_block->next;
			++page->block_initialized;
			++page->local_free_count;
		}
		if (first_block != free_block) {
			last_block->next = 0;
			page->local_free = first_block;
		} else {
			page->local_free_count = 0;
		}
	}
	return block;
}

static inline RPMALLOC_ALLOCATOR void*
page_allocate_block(page_t* page, unsigned int zero) {
	unsigned int is_zero = 0;
	block_t* block = page_get_local_free_block(page);
	if (UNEXPECTED(block == 0)) {
		block = page_get_thread_free_block(page);
		if (block == 0) {
			block = page_initialize_blocks(page);
			is_zero = page->is_zero;
		}
	}

	rpmalloc_assert(page->block_used <= page->block_count, "Page block use counter out of sync");

	page_push_local_free_to_heap(page);

	if (page->block_used == page->block_count)
		page_adopt_thread_free_block_list(page);

	if (page->block_used == page->block_count) {
		// Page is fully utilized
		if (page->is_available) {
			heap_t* heap = page->heap;
			if (heap->page_available[page->size_class] == page) {
				heap->page_available[page->size_class] = page->next;
			} else {
				page->prev->next = page->next;
				if (page->next)
					page->next->prev = page->prev;
			}
		}
		page->is_full = 1;
		page->is_zero = 0;
		page->is_available = 0;
	}

	if (zero && !is_zero && block)
		memset(block, 0, page->block_size);

	return block;
}

static inline void
page_deallocate_block(page_t* page, block_t* block) {
	if (page->has_aligned_block) {
		// Realign pointer to block start
		block = page_block_realign(page, block);
	}

	uintptr_t calling_thread = get_thread_id();
	if (EXPECTED(page->heap && (page->heap->owner_thread == calling_thread))) {
		page_put_local_free_block(page, block);
	} else {
		if (page->page_type == PAGE_HUGE) {
			span_t* span = page_get_span(page);
			global_memory_interface->memory_unmap(span, span->page_size, SPAN_SIZE, span->offset, span->mapped_size);
		} else {
			// Multithreaded deallocation, push to deferred deallocation list
			page_put_thread_free_block(page, block);
		}
	}
}

////////////
///
/// Span interface
///
//////

static inline page_t*
span_get_page_from_block(span_t* span, void* block) {
	size_t page_count = (size_t)pointer_diff(block, span) / span->page_size;
	return pointer_offset(span, page_count * span->page_size);
}

//! Find or allocate a page from the given span
static inline page_t*
span_allocate_page(span_t* span, uint32_t size_class) {
	// Allocate path, initialize a new chunk of memory for a page in the given span
	rpmalloc_assert(span->page_initialized < span->page_count, "Page initialization internal failure");
	page_t* page = pointer_offset(span, span->page_size * span->page_initialized);
	page->size_class = size_class;
	page->block_size = global_size_class[size_class].block_size;
	page->block_count = global_size_class[size_class].block_count;
	page->block_initialized = 0;
	page->block_used = 0;
	if (span->page_initialized) {
		page->page_type = span->page.page_type;
		page->heap = span->page.heap;
	}
	page->is_zero = 1;
	++span->page_initialized;

	if (span->page_initialized == span->page_count) {
		// Span fully utilized, unlink from available list and add to full list
		heap_t* heap = span->page.heap;
		if (span == heap->span_partial[span->page.page_type])
			heap->span_partial[span->page.page_type] = span->next;
		else
			span->prev->next = span->next;
		span->next = heap->span_used[span->page.page_type];
		if (span->next)
			span->next->prev = span;
		heap->span_used[span->page.page_type] = span;
	}

	return page;
}

////////////
///
/// Block interface
///
//////

static inline span_t*
block_get_span(block_t* block) {
	return (span_t*)((uintptr_t)block & SPAN_MASK);
}

static inline void
block_deallocate(block_t* block) {
	span_t* span = (span_t*)((uintptr_t)block & SPAN_MASK);
	if (EXPECTED(span != 0)) {
		page_t* page = span_get_page_from_block(span, block);
		page_deallocate_block(page, block);
	}
}

static inline size_t
block_usable_size(block_t* block) {
	span_t* span = (span_t*)((uintptr_t)block & SPAN_MASK);
	page_t* page = span_get_page_from_block(span, block);
	void* blocks_start = pointer_offset(page, PAGE_HEADER_SIZE);
	return page->block_size - ((size_t)pointer_diff(block, blocks_start) % page->block_size);
}

////////////
///
/// Heap interface
///
//////

static inline void
heap_lock_acquire(void) {
	void* lock = 0;
	uintptr_t this_lock = get_thread_id();
	while (!atomic_compare_exchange_strong(&global_heap_lock, &lock, (void*)this_lock)) {
		lock = 0;
		wait_spin();
	}
}

static inline void
heap_lock_release(void) {
	rpmalloc_assert((uintptr_t)atomic_load_explicit(&global_heap_lock, memory_order_relaxed) == get_thread_id(),
	                "Bad heap lock");
	atomic_store_explicit(&global_heap_lock, 0, memory_order_release);
}

static inline heap_t*
heap_initialize(void* block) {
	heap_t* heap = block;
	memset_const(heap, 0, sizeof(heap_t));
	heap->id = 1 + atomic_fetch_add_explicit(&global_heap_id, 1, memory_order_relaxed);
	return heap;
}

static inline heap_t*
heap_allocate_new(void) {
	size_t heap_size = get_page_aligned_size(sizeof(heap_t));
	size_t offset = 0;
	size_t mapped_size = 0;
	block_t* block = global_memory_interface->memory_map(heap_size, 0, &offset, &mapped_size);
	heap_t* heap = heap_initialize((void*)block);
	heap->offset = (uint32_t)offset;
	heap->mapped_size = mapped_size;
	return heap;
}

static inline heap_t*
heap_allocate(int first_class) {
	heap_t* heap = 0;
	void* heap_default = 0;
	if (!first_class) {
		heap_default = atomic_load_explicit(&global_heap_default, memory_order_relaxed);
		if (heap_default) {
			if (atomic_compare_exchange_strong(&global_heap_default, &heap_default, 0))
				heap = heap_default;
		}
		if (!heap) {
			heap_lock_acquire();
			heap = global_heap_queue;
			global_heap_queue = heap ? heap->next : 0;
			heap_lock_release();
		}
	}
	if (!heap)
		heap = heap_allocate_new();
	if (heap) {
		heap->next = 0;
		heap->owner_thread = get_thread_id();
		if (heap == heap_default)
			rpmalloc_initialize(0);
	}
	return heap;
}

static inline void
heap_release(heap_t* heap) {
	heap_lock_acquire();
	heap->next = global_heap_queue;
	global_heap_queue = heap;
	heap_lock_release();
}

static inline void
heap_make_free_page_available(heap_t* heap, uint32_t size_class, page_t* page) {
	page->size_class = size_class;
	page->block_size = global_size_class[size_class].block_size;
	page->block_count = global_size_class[size_class].block_count;
	page->block_used = 0;
	page->block_initialized = 0;
	page->local_free = 0;
	page->local_free_count = 0;
	atomic_store_explicit(&page->thread_free, 0, memory_order_relaxed);
	page->is_full = 0;
	page->is_free = 0;
	page->is_available = 1;
	page->has_aligned_block = 0;
	page_t* head = heap->page_available[size_class];
	page->next = head;
	page->prev = 0;
	if (head)
		head->prev = page;
	heap->page_available[size_class] = page;
}

//! Find or allocate a span for the given page type with the given size class
static inline span_t*
heap_get_span(heap_t* heap, page_type_t page_type) {
	// Fast path, available span for given page type
	if (EXPECTED(heap->span_partial[page_type] != 0))
		return heap->span_partial[page_type];

	// Fallback path, map more memory
	size_t offset = 0;
	size_t mapped_size = 0;
	span_t* span = global_memory_interface->memory_map(SPAN_SIZE, SPAN_SIZE, &offset, &mapped_size);
	if (EXPECTED(span != 0)) {
		uint32_t page_size = (page_type == PAGE_SMALL) ?
		                         SMALL_PAGE_SIZE :
                                 ((page_type == PAGE_MEDIUM) ? MEDIUM_PAGE_SIZE : LARGE_PAGE_SIZE);
		span->page.page_type = page_type;
		span->page.is_zero = 1;
		span->page.heap = heap;
		span->page_count = SPAN_SIZE / page_size;
		span->page_size = page_size;
		span->offset = (uint32_t)offset;
		span->mapped_size = mapped_size;

		heap->span_partial[page_type] = span;
	}

	// Make sure default heap has owning thread
	if (!heap->owner_thread)
		heap->owner_thread = get_thread_id();

	return span;
}

//! Find or allocate a page for the given size class
static inline page_t*
heap_get_page(heap_t* heap, uint32_t size_class) {
	// Fast path, available page for given size class
	page_t* page = heap->page_available[size_class];
	if (EXPECTED(page != 0))
		return page;

	// Check if there is a free page
	page_type_t page_type = get_page_type(size_class);
	page = heap->page_free[page_type];
	if (EXPECTED(page != 0)) {
		heap->page_free[page_type] = page->next;
		heap_make_free_page_available(heap, size_class, page);
		return page;
	}

	// Check if there is a free page from multithreaded deallocations
	page = atomic_load_explicit(&heap->page_free_thread[page_type], memory_order_relaxed);
	if (UNEXPECTED(page != 0)) {
		atomic_store_explicit(&heap->page_free_thread[page_type], 0, memory_order_relaxed);
		heap->page_free[page_type] = page->next;
		heap_make_free_page_available(heap, size_class, page);
		return page;
	}

	// Fallback path, find or allocate span for given size class
	span_t* span = heap_get_span(heap, page_type);
	if (EXPECTED(span != 0)) {
		page = span_allocate_page(span, size_class);
		heap_make_free_page_available(heap, size_class, page);
	}

	return page;
}

//! Find or allocate a block of the given size
static inline RPMALLOC_ALLOCATOR void*
heap_allocate_block(heap_t* heap, size_t size, unsigned int zero) {
	// Fast track with small block available in heap level local free list
	uint32_t size_class = get_size_class(size);
	if (EXPECTED(size <= SMALL_BLOCK_SIZE_LIMIT)) {
		block_t* block = heap->small_free[size_class];
		if (EXPECTED(block != 0)) {
			heap->small_free[size_class] = block->next;
			return block;
		}
	}
	// Fallback path. find or allocate page and span for block
	if (size <= LARGE_BLOCK_SIZE_LIMIT) {
		page_t* page = heap_get_page(heap, size_class);
		if (EXPECTED(page != 0))
			return page_allocate_block(page, zero);
	} else {
		size_t alloc_size = get_page_aligned_size(size + SPAN_HEADER_SIZE);
		size_t offset = 0;
		size_t mapped_size = 0;
		void* block = global_memory_interface->memory_map(alloc_size, SPAN_SIZE, &offset, &mapped_size);
		if (block) {
			span_t* span = block;
			span->page.page_type = PAGE_HUGE;
			span->page_size = (uint32_t)size;
			span->offset = (uint32_t)offset;
			span->mapped_size = mapped_size;
			return pointer_offset(block, SPAN_HEADER_SIZE);
		}
	}
	return 0;
}

static RPMALLOC_ALLOCATOR void*
heap_allocate_block_aligned(heap_t* heap, size_t alignment, size_t size, unsigned int zero) {
	if (alignment <= SMALL_GRANULARITY)
		return heap_allocate_block(heap, size, zero);

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

	block_t* block = 0;
	size_t align_mask = alignment - 1;
	if (alignment <= MEDIUM_BLOCK_SIZE_LIMIT) {
		block = heap_allocate_block(heap, size + alignment, zero);
		if ((uintptr_t)block & align_mask) {
			block = (void*)(((uintptr_t)block & ~(uintptr_t)align_mask) + alignment);
			// Mark as having aligned blocks
			span_t* span = block_get_span(block);
			page_t* page = span_get_page_from_block(span, block);
			page->has_aligned_block = 1;
		}
		return block;
	}
#if 0
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
	if (alignment >= MAX_ALIGNMENT) {
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

	span = (span_t*)_rpmalloc_mmap(mapped_size, &align_offset);
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
		_rpmalloc_unmap(span, mapped_size, align_offset, mapped_size);
		++num_pages;
		if (num_pages > limit_pages) {
			errno = EINVAL;
			return 0;
		}
		goto retry;
	}

	// Store page count in span_count
	span->size_class = SIZE_CLASS_HUGE;
	span->span_count = (uint32_t)num_pages;
	span->align_offset = (uint32_t)align_offset;
	span->heap = heap;
	_rpmalloc_stat_add_peak(&_huge_pages_current, num_pages, _huge_pages_peak);

#if RPMALLOC_FIRST_CLASS_HEAPS
	_rpmalloc_span_double_link_list_add(&heap->large_huge_span, span);
#endif
	++heap->full_span_count;

	_rpmalloc_stat_add64(&_allocation_counter, 1);

	return ptr;
#endif
	return 0;
}

static void*
heap_reallocate_block(heap_t* heap, void* block, size_t size, size_t old_size, unsigned int flags) {
	if (block) {
		// Grab the span using guaranteed span alignment
		span_t* span = block_get_span(block);
		if (EXPECTED(span->page.page_type <= PAGE_LARGE)) {
			// Normal sized block
			page_t* page = span_get_page_from_block(span, block);
			void* blocks_start = pointer_offset(page, PAGE_HEADER_SIZE);
			uint32_t block_offset = (uint32_t)pointer_diff(block, blocks_start);
			uint32_t block_idx = block_offset / page->block_size;
			void* block_origin = pointer_offset(blocks_start, (size_t)block_idx * page->block_size);
			if (!old_size)
				old_size = (size_t)((ptrdiff_t)page->block_size - pointer_diff(block, block_origin));
			if ((size_t)page->block_size >= size) {
				// Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((block != block_origin) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block_origin, block, old_size);
				return block_origin;
			}
		} else {
// Oversized block
#if 0
			size_t total_size = size + SPAN_HEADER_SIZE;
			size_t num_pages = total_size >> _memory_page_size_shift;
			if (total_size & (_memory_page_size - 1))
				++num_pages;
			// Page count is stored in span_count
			size_t current_pages = span->span_count;
			void* block = pointer_offset(span, SPAN_HEADER_SIZE);
			if (!oldsize)
				oldsize = (current_pages * _memory_page_size) - (size_t)pointer_diff(p, block) - SPAN_HEADER_SIZE;
			if ((current_pages >= num_pages) && (num_pages >= (current_pages / 2))) {
				// Still fits in block, never mind trying to save memory, but preserve data if alignment changed
				if ((p != block) && !(flags & RPMALLOC_NO_PRESERVE))
					memmove(block, p, oldsize);
				return block;
			}
#endif
		}
	} else {
		old_size = 0;
	}

	if (!!(flags & RPMALLOC_GROW_OR_FAIL))
		return 0;

	// Size is greater than block size, need to allocate a new block and deallocate the old
	// Avoid hysteresis by overallocating if increase is small (below 37%)
	size_t lower_bound = old_size + (old_size >> 2) + (old_size >> 3);
	size_t new_size = (size > lower_bound) ? size : ((size > old_size) ? lower_bound : size);
	void* old_block = block;
	block = heap_allocate_block(heap, new_size, 0);
	if (block && old_block) {
		if (!(flags & RPMALLOC_NO_PRESERVE))
			memcpy(block, old_block, old_size < new_size ? old_size : new_size);
		block_deallocate(old_block);
	}

	return block;
}

static void*
heap_reallocate_block_aligned(heap_t* heap, void* block, size_t alignment, size_t size, size_t old_size,
                              unsigned int flags) {
	if (alignment <= SMALL_GRANULARITY)
		return heap_reallocate_block(heap, block, size, old_size, flags);

	int no_alloc = !!(flags & RPMALLOC_GROW_OR_FAIL);
	size_t usable_size = (block ? block_usable_size(block) : 0);
	if ((usable_size >= size) && !((uintptr_t)block & (alignment - 1))) {
		if (no_alloc || (size >= (usable_size / 2)))
			return block;
	}
	// Aligned alloc marks span as having aligned blocks
	void* old_block = block;
	block = (!no_alloc ? heap_allocate_block_aligned(heap, alignment, size, 0) : 0);
	if (EXPECTED(block != 0)) {
		if (!(flags & RPMALLOC_NO_PRESERVE) && old_block) {
			if (!old_size)
				old_size = usable_size;
			memcpy(block, old_block, old_size < size ? old_size : size);
		}
		block_deallocate(old_block);
	}
	return block;
}

////////////
///
/// Extern interface
///
//////

int
rpmalloc_is_thread_initialized(void) {
	return (get_thread_heap_raw() != 0) ? 1 : 0;
}

extern inline RPMALLOC_ALLOCATOR void*
rpmalloc(size_t size) {
#if ENABLE_VALIDATE_ARGS
	if (size >= MAX_ALLOC_SIZE) {
		errno = EINVAL;
		return 0;
	}
#endif
	heap_t* heap = get_thread_heap();
	return heap_allocate_block(heap, size, 0);
}

extern inline void
rpfree(void* ptr) {
	block_deallocate(ptr);
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
	return heap_allocate_block(heap, total, 1);
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
	return heap_reallocate_block(heap, ptr, size, 0, 0);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_realloc(void* ptr, size_t alignment, size_t size, size_t oldsize, unsigned int flags) {
#if ENABLE_VALIDATE_ARGS
	if ((size + alignment < size) || (alignment > _memory_page_size)) {
		errno = EINVAL;
		return 0;
	}
#endif
	heap_t* heap = get_thread_heap();
	return heap_reallocate_block_aligned(heap, ptr, alignment, size, oldsize, flags);
}

extern RPMALLOC_ALLOCATOR void*
rpaligned_alloc(size_t alignment, size_t size) {
	heap_t* heap = get_thread_heap();
	return heap_allocate_block_aligned(heap, alignment, size, 0);
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
	heap_t* heap = get_thread_heap();
	return heap_allocate_block_aligned(heap, alignment, total, 1);
}

extern inline RPMALLOC_ALLOCATOR void*
rpmemalign(size_t alignment, size_t size) {
	heap_t* heap = get_thread_heap();
	return heap_allocate_block_aligned(heap, alignment, size, 0);
}

extern inline int
rpposix_memalign(void** memptr, size_t alignment, size_t size) {
	heap_t* heap = get_thread_heap();
	if (memptr)
		*memptr = heap_allocate_block_aligned(heap, alignment, size, 0);
	else
		return EINVAL;
	return *memptr ? 0 : ENOMEM;
}

extern inline size_t
rpmalloc_usable_size(void* ptr) {
	return (ptr ? block_usable_size(ptr) : 0);
}

////////////
///
/// Initialization and finalization
///
//////

extern int
rpmalloc_initialize(rpmalloc_interface_t* memory_interface) {
	if (global_memory_interface)
		return 0;

	global_memory_interface = memory_interface ? memory_interface : &global_memory_interface_default;
	if (!global_memory_interface->memory_map || !global_memory_interface->memory_unmap) {
		global_memory_interface->memory_map = os_mmap;
		global_memory_interface->memory_unmap = os_munmap;
	}

#if PLATFORM_WINDOWS
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	os_map_granularity = system_info.dwAllocationGranularity;
#else
	os_map_granularity = (size_t)sysconf(_SC_PAGESIZE);
#endif

#if PLATFORM_WINDOWS
	os_page_size = system_info.dwPageSize;
#else
	os_page_size = os_map_granularity;
#endif
	if (global_use_huge_pages) {
#if PLATFORM_WINDOWS
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
					if (GetLastError() == ERROR_SUCCESS)
						os_huge_pages = 1;
				}
			}
			CloseHandle(token);
		}
		if (os_huge_pages) {
			if (large_page_minimum > os_page_size)
				os_page_size = large_page_minimum;
			if (large_page_minimum > os_map_granularity)
				os_map_granularity = large_page_minimum;
		}
#elif defined(__linux__)
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
			os_huge_pages = 1;
			os_page_size = huge_page_size;
			os_map_granularity = huge_page_size;
		}
#elif defined(__FreeBSD__)
		int rc;
		size_t sz = sizeof(rc);

		if (sysctlbyname("vm.pmap.pg_ps_enabled", &rc, &sz, NULL, 0) == 0 && rc == 1) {
			os_huge_pages = 1;
			os_page_size = 2 * 1024 * 1024;
			os_map_granularity = _memory_page_size;
		}
#elif defined(__APPLE__) || defined(__NetBSD__)
		os_huge_pages = 1;
		os_page_size = 2 * 1024 * 1024;
		os_map_granularity = os_page_size;
#endif
	}

	rpmalloc_thread_initialize();

	return 0;
}

extern const rpmalloc_config_t*
rpmalloc_config(void) {
	static const rpmalloc_config_t config = {0};
	return &config;
}

extern void
rpmalloc_finalize(void) {
	global_memory_interface = 0;
}

extern void
rpmalloc_thread_initialize(void) {
	get_thread_heap();
}

extern void
rpmalloc_thread_finalize(int release_caches) {
	(void)sizeof(release_caches);
	heap_t* heap = get_thread_heap_raw();
	if (heap) {
		set_thread_heap(0);
		heap_release(heap);
	}
}

extern void
rpmalloc_thread_collect(void) {
}
