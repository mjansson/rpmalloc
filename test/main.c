/* SPDX-FileCopyrightText: 2017 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 */

#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef _MSC_VER
#if !defined(__clang__)
#pragma warning(disable : 5105)
#endif
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wnonportable-system-include-path"
#if __has_warning("-Wunsafe-buffer-usage")
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#if __has_warning("-Wimplicit-void-ptr-cast")
#pragma clang diagnostic ignored "-Wimplicit-void-ptr-cast"
#endif
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif

#include <rpmalloc.h>
#include <thread.h>
#include <test.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#if !defined(_WIN32)
#include <sys/mman.h>
#include <pthread.h>
#endif

// Set to 1 for the test binary built against the rpmalloc library with ENABLE_OVERRIDE=1 (runs the
// standard library override tests). The override-incompatible tests (unmap_on_finalize, which is a
// no-op under the override) instead run in a separate binary built with ENABLE_OVERRIDE=0.
#ifndef RPMALLOC_TEST_OVERRIDE
#define RPMALLOC_TEST_OVERRIDE 0
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

static size_t hardware_threads;

// Test load scaling, controllable via environment so the suite can run on constrained
// runners (e.g. Android emulators / iOS simulators) without overwhelming them:
//   RPMALLOC_TEST_THREADS  caps the worker thread count used by the threaded tests
//   RPMALLOC_TEST_SCALE    scales per-thread loops/passes, as a percentage (1-100)
static unsigned int test_loop_scale = 100;

static unsigned int
test_scale(unsigned int value) {
	unsigned int scaled = (unsigned int)(((uint64_t)value * (uint64_t)test_loop_scale) / 100u);
	return scaled ? scaled : 1u;
}
static int test_failed;

static void
test_initialize(void);

int
test_fail_cb(const char* reason, const char* file, int line) {
	fprintf(stderr, "FAIL: %s @ %s:%d\n", reason, file, line);
	fflush(stderr);
	test_failed = 1;
	return -1;
}

static void
defer_free_thread(void* arg) {
	rpfree(arg);
}

static int
test_alloc(void) {
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[18142];
	char data[40000];
	unsigned int datasize[7] = {473, 39, 195, 24, 73, 376, 245};

	rpmalloc_initialize(0);

	void* zero_alloc = rpmalloc(0);
	size_t small_granularity = rpmalloc_usable_size(zero_alloc);
	if (small_granularity != 16)
		return test_fail("Unexpected block granularity");
	rpfree(zero_alloc);

	for (id = 0; id < 30000; ++id)
		data[id] = (char)(id % 139 + id % 17);

	// Verify that blocks are aligned to small granularity
	void* testptr = rpmalloc(small_granularity);
	if (rpmalloc_usable_size(testptr) != small_granularity)
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(small_granularity + 1);
	if (rpmalloc_usable_size(testptr) != (small_granularity * 2))
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(small_granularity * 2);
	if (rpmalloc_usable_size(testptr) != (small_granularity * 2))
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(128);
	if (rpmalloc_usable_size(testptr) != 128)
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(129);
	if (rpmalloc_usable_size(testptr) < (128 + small_granularity))
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	for (iloop = 128; iloop <= 128 * 1024; ++iloop) {
		testptr = rpmalloc(iloop);
		size_t usable_size = rpmalloc_usable_size(testptr);
		double overhead = (double)usable_size / (double)iloop;
		if (overhead > 1.255) {
			printf("For %u got %zu - overhead %.2f\n", iloop, usable_size, (overhead - 1.0) * 100.0);
			return test_fail("Bad base alloc usable size");
		}
		rpfree(testptr);
	}

	// Reallocation tests
	for (iloop = 1; iloop < 24; ++iloop) {
		size_t size = 37 * iloop;
		testptr = rpmalloc(size);
		*((uintptr_t*)testptr) = 0x12345678;
		if (rpmalloc_usable_size(testptr) < size)
			return test_fail("Bad usable size (alloc)");
		testptr = rprealloc(testptr, size + 16);
		if (rpmalloc_usable_size(testptr) < (size + 16))
			return test_fail("Bad usable size (realloc)");
		if (*((uintptr_t*)testptr) != 0x12345678)
			return test_fail("Data not preserved on realloc");
		rpfree(testptr);

		testptr = rpaligned_alloc(128, size);
		*((uintptr_t*)testptr) = 0x12345678;
		if (rpmalloc_usable_size(testptr) < size)
			return test_fail("Bad usable size (aligned alloc)");
		testptr = rpaligned_realloc(testptr, 128, size + 32, 0, 0);
		if (rpmalloc_usable_size(testptr) < (size + 32))
			return test_fail("Bad usable size (aligned realloc)");
		if (*((uintptr_t*)testptr) != 0x12345678)
			return test_fail("Data not preserved on realloc");
		if (rpaligned_realloc(testptr, 128, size * 1024 * 4, 0, RPMALLOC_GROW_OR_FAIL))
			return test_fail("Realloc with grow-or-fail did not fail as expected");
		void* unaligned = rprealloc(testptr, size);
		if (unaligned != testptr) {
			ptrdiff_t diff = pointer_diff(testptr, unaligned);
			if (diff < 0)
				return test_fail("Bad realloc behaviour");
			if (diff >= 128)
				return test_fail("Bad realloc behaviour");
		}
		rpfree(testptr);
	}

	static size_t alignment[6] = {0, 32, 64, 128, 256, 1024};
	for (iloop = 0; iloop < 6; ++iloop) {
		for (ipass = 0; ipass < 128 * 1024; ++ipass) {
			size_t this_alignment = alignment[iloop];
			char* baseptr = rpaligned_alloc(this_alignment, ipass);
			if (this_alignment && ((uintptr_t)baseptr & (this_alignment - 1)))
				return test_fail("Alignment failed");
			rpfree(baseptr);
		}
	}
	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8142; ++ipass) {
			size_t this_alignment = alignment[ipass % 5];
			size_t size = iloop + ipass + datasize[(iloop + ipass) % 7];
			char* baseptr = rpaligned_alloc(this_alignment, size);
			if (this_alignment && ((uintptr_t)baseptr & (this_alignment - 1)))
				return test_fail("Alignment failed");
			for (size_t ibyte = 0; ibyte < size; ++ibyte)
				baseptr[ibyte] = (char)(ibyte & 0xFF);

			size_t resize = (iloop * ipass + datasize[(iloop + ipass) % 7]) & 0x2FF;
			size_t capsize = (size > resize ? resize : size);
			baseptr = rprealloc(baseptr, resize);
			for (size_t ibyte = 0; ibyte < capsize; ++ibyte) {
				if (baseptr[ibyte] != (char)(ibyte & 0xFF))
					return test_fail("Data not preserved on realloc");
			}

			size_t alignsize = (iloop * ipass + datasize[(iloop + ipass * 3) % 7]) & 0x2FF;
			this_alignment = alignment[(ipass + 1) % 5];
			capsize = (capsize > alignsize ? alignsize : capsize);
			baseptr = rpaligned_realloc(baseptr, this_alignment, alignsize, resize, 0);
			for (size_t ibyte = 0; ibyte < capsize; ++ibyte) {
				if (baseptr[ibyte] != (char)(ibyte & 0xFF))
					return test_fail("Data not preserved on realloc");
			}

			rpfree(baseptr);
		}
	}

	// Large reallocation test
	testptr = rpmalloc(256310000);
	testptr = rprealloc(testptr, 151);
	size_t usable_size = rpmalloc_usable_size(testptr);
	if (usable_size < 160)
		return test_fail("Bad usable size");
	if (rpmalloc_usable_size(pointer_offset(testptr, 16)) != (usable_size - 16))
		return test_fail("Bad offset usable size");
	rpfree(testptr);
	// Large (>4 GiB) allocation test, only meaningful and representable on 64-bit size_t.
#if SIZE_MAX > 0xFFFFFFFFu
	{
		testptr = rpmalloc(7525631000);
		memset(testptr, 0x13, 1024);
		testptr = rprealloc(testptr, 3151000);
		for (size_t ibyte = 0; ibyte < 1024; ++ibyte) {
			if (((char*)testptr)[ibyte] != 0x13)
				return test_fail("Bad realloc did not preserve memory content");
		}
		usable_size = rpmalloc_usable_size(testptr);
		if (usable_size > 4 * 1024 * 1024)
			return test_fail("Bad usable size");
		if (rpmalloc_usable_size(pointer_offset(testptr, 16)) != (usable_size - 16))
			return test_fail("Bad offset usable size");
		rpfree(testptr);
	}
#endif

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 18142; ++ipass) {
			addr[ipass] = rpzalloc(500);
			if (addr[ipass] == 0)
				return test_fail("Allocation failed");

			for (size_t ibyte = 0; ibyte < 500; ++ibyte) {
				if (((char*)addr[ipass])[ibyte])
					return test_fail("Zero allocation not zero");
			}

			memcpy(addr[ipass], data + ipass, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return test_fail("Bad allocation result");
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], 500) > addr[ipass])
						return test_fail("Bad allocation result");
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], 500) > addr[icheck])
						return test_fail("Bad allocation result");
				}
			}
		}

		for (ipass = 0; ipass < 18142; ++ipass) {
			if (memcmp(addr[ipass], data + ipass, 500))
				return test_fail("Data corruption");
		}

		for (ipass = 0; ipass < 18142; ++ipass)
			rpfree(addr[ipass]);
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 18142; ++ipass) {
			unsigned int cursize = datasize[ipass % 7] + ipass;

			addr[ipass] = rpzalloc(cursize);
			if (addr[ipass] == 0)
				return test_fail("Allocation failed");

			for (size_t ibyte = 0; ibyte < cursize; ++ibyte) {
				if (((char*)addr[ipass])[ibyte])
					return test_fail("Zero allocation not zero");
			}

			memcpy(addr[ipass], &data[ipass], cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return test_fail("Identical pointer returned from allocation");
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], rpmalloc_usable_size(addr[icheck])) > addr[ipass])
						return test_fail("Invalid pointer inside another block returned from allocation");
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], rpmalloc_usable_size(addr[ipass])) > addr[icheck])
						return test_fail("Invalid pointer inside another block returned from allocation");
				}
			}
		}

		for (ipass = 0; ipass < 18142; ++ipass) {
			unsigned int cursize = datasize[ipass % 7] + ipass;
			if (memcmp(addr[ipass], &data[ipass], cursize))
				return test_fail("Data corruption");
		}

		for (ipass = 0; ipass < 18142; ++ipass)
			rpfree(addr[ipass]);
	}

	for (iloop = 0; iloop < 128; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			addr[ipass] = rpmalloc(500);
			if (addr[ipass] == 0)
				return test_fail("Allocation failed");

			memcpy(addr[ipass], data + ipass, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return test_fail("Identical pointer returned from allocation");
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], 500) > addr[ipass])
						return test_fail("Invalid pointer inside another block returned from allocation");
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], 500) > addr[icheck])
						return test_fail("Invalid pointer inside another block returned from allocation");
				}
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			if (memcmp(addr[ipass], data + ipass, 500))
				return test_fail("Data corruption");
		}

		for (ipass = 0; ipass < 1024; ++ipass)
			rpfree(addr[ipass]);
	}

	rpmalloc_finalize();

	for (iloop = 0; iloop < 2048; iloop += 16) {
		rpmalloc_initialize(0);
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = 2048; iloop < (64 * 1024); iloop += 512) {
		rpmalloc_initialize(0);
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = (64 * 1024); iloop < (2 * 1024 * 1024); iloop += 4096) {
		rpmalloc_initialize(0);
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	rpmalloc_initialize(0);
	for (iloop = 0; iloop < (2 * 1024 * 1024); iloop += 16) {
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
	}
	rpmalloc_finalize();

	// Test that a full span with deferred block is finalized properly
	// Also test that a deferred huge span is finalized properly
	rpmalloc_initialize(0);
	{
		addr[0] = rpmalloc(23457);

		thread_arg targ;
		targ.fn = defer_free_thread;
		targ.arg = addr[0];
		uintptr_t thread = thread_run(&targ);
		thread_sleep(100);
		thread_join(thread);

		addr[0] = rpmalloc(234567890);

		targ.fn = defer_free_thread;
		targ.arg = addr[0];
		thread = thread_run(&targ);
		thread_sleep(100);
		thread_join(thread);
	}
	rpmalloc_finalize();

	printf("Memory allocation tests passed\n");

	return 0;
}

#if RPMALLOC_TEST_OVERRIDE
extern void*
rpreallocarray(void* ptr, size_t count, size_t size);
#endif

//! Allocate and free blocks of the given size with the requested bytes set, so the next allocation of the
//  size class reuses a dirty block
static void
test_zero_dirty(size_t size) {
	void* block[4];
	for (int i = 0; i < 4; ++i) {
		block[i] = rpmalloc(size);
		memset(block[i], 0xff, size);
	}
	for (int i = 0; i < 4; ++i)
		rpfree(block[i]);
}

static int
test_zero_check(const unsigned char* ptr, size_t size, size_t alignment) {
	if (!ptr)
		return test_fail("Zero allocation failed");
	if (alignment && ((uintptr_t)ptr & (alignment - 1)))
		return test_fail("Zero allocation not aligned");
	for (size_t ibyte = 0; ibyte < size; ++ibyte) {
		if (ptr[ibyte])
			return test_fail("Zero allocation not zero");
	}
	return 0;
}

static int
test_zero_alloc(void) {
	// calloc and zalloc zero the requested bytes. Blocks of the size classes involved are dirtied and freed
	// first so the zeroed allocations reuse them, across class boundaries, alignments, page types and huge
	// blocks (the freed huge block is cached and reused)
	rpmalloc_initialize(0);
	static const size_t sizes[] = {0,    1,    8,     9,     16,     17,     1023,   1024,
	                               1025, 2049, 5121,  20480, 20481,  131073, 300000, 2100000};
	static const size_t alignments[] = {0, 8, 16, 32, 64, 128, 256, 4096};
	for (size_t isize = 0; isize < sizeof(sizes) / sizeof(sizes[0]); ++isize) {
		for (size_t ialign = 0; ialign < sizeof(alignments) / sizeof(alignments[0]); ++ialign) {
			size_t size = sizes[isize];
			size_t alignment = alignments[ialign];
			size_t aligned_size = alignment ? ((size + alignment - 1) & ~(alignment - 1)) : size;
			for (int izero = 0; izero < 2; ++izero) {
				test_zero_dirty(size);
				test_zero_dirty(aligned_size);
				test_zero_dirty(size + alignment);
				unsigned char* ptr;
				if (izero)
					ptr = alignment ? rpaligned_zalloc(alignment, size) : rpzalloc(size);
				else
					ptr = alignment ? rpaligned_calloc(alignment, 1, size) : rpcalloc(1, size);
				if (test_zero_check(ptr, size, alignment))
					return -1;
				rpfree(ptr);
			}
		}
	}
	rpmalloc_finalize();
	printf("Zero allocation tests passed\n");
	return 0;
}

static int
test_mul_overflow(void) {
	// Volatile so the compiler cannot fold or reject the overflowing products at compile time
	volatile size_t half = (SIZE_MAX / 2) + 2;
	volatile size_t two = 2;
	volatile size_t big = SIZE_MAX / 4 + 1;
	volatile size_t sixteen = 16;

	rpmalloc_initialize(0);

	errno = 0;
	void* ptr = rpcalloc(half, two);
	if (ptr || (errno != ENOMEM))
		return test_fail("calloc with overflowing size did not fail");
	ptr = rpcalloc(two, half);
	if (ptr)
		return test_fail("calloc with overflowing size did not fail");
	ptr = rpcalloc(big, sixteen);
	if (ptr)
		return test_fail("calloc with overflowing size did not fail");
	ptr = rpaligned_calloc(64, half, two);
	if (ptr)
		return test_fail("aligned calloc with overflowing size did not fail");

	ptr = rpcalloc(two, sixteen);
	if (!ptr)
		return test_fail("calloc with valid size failed");

#if RPMALLOC_TEST_OVERRIDE
	errno = 0;
	void* newptr = rpreallocarray(ptr, half, two);
	if (newptr || (errno != ENOMEM))
		return test_fail("reallocarray with overflowing size did not fail");
	newptr = rpreallocarray(0, big, sixteen);
	if (newptr)
		return test_fail("reallocarray with overflowing size did not fail");
	newptr = rpreallocarray(ptr, two, two * sixteen);
	if (!newptr)
		return test_fail("reallocarray with valid size failed");
	ptr = newptr;
#endif
	rpfree(ptr);

#if RPMALLOC_FIRST_CLASS_HEAPS
	rpmalloc_heap_t* heap = rpmalloc_heap_acquire();
	if (rpmalloc_heap_calloc(heap, half, two))
		return test_fail("heap calloc with overflowing size did not fail");
	if (rpmalloc_heap_aligned_calloc(heap, 64, big, sixteen))
		return test_fail("heap aligned calloc with overflowing size did not fail");
	rpmalloc_heap_release(heap);
#endif

	rpmalloc_finalize();

	printf("Multiplication overflow tests passed\n");

	return 0;
}

static int
test_realloc(void) {
	srand((unsigned int)time(0));

	rpmalloc_initialize(0);

	size_t pointer_count = 4096;
	void** pointers = rpmalloc(sizeof(void*) * pointer_count);
	memset(pointers, 0, sizeof(void*) * pointer_count);

	size_t alignments[5] = {0, 16, 32, 64, 128};

	for (size_t iloop = 0; iloop < 8000; ++iloop) {
		for (size_t iptr = 0; iptr < pointer_count; ++iptr) {
			if (iloop)
				rpfree(rprealloc(pointers[iptr], (size_t)rand() % 4096));
			pointers[iptr] = rpaligned_alloc(alignments[(iptr + iloop) % 5], iloop + iptr);
		}
	}

	for (size_t iptr = 0; iptr < pointer_count; ++iptr)
		rpfree(pointers[iptr]);
	rpfree(pointers);

	size_t bigsize = 1024 * 1024;
	void* bigptr = rpmalloc(bigsize);
	while (bigsize < 3000000) {
		++bigsize;
		bigptr = rprealloc(bigptr, bigsize);
		if (rpaligned_realloc(bigptr, 0, bigsize * 32, 0, RPMALLOC_GROW_OR_FAIL))
			return test_fail("Reallocation with grow-or-fail did not fail as expected");
		if (rpaligned_realloc(bigptr, 128, bigsize * 32, 0, RPMALLOC_GROW_OR_FAIL))
			return test_fail("Reallocation with aligned grow-or-fail did not fail as expected");
	}
	rpfree(bigptr);

	rpmalloc_finalize();

	printf("Memory reallocation tests passed\n");

	return 0;
}

static int
test_superalign(void) {
	rpmalloc_initialize(0);

	size_t alignment[] = {2048, 4096, 8192, 16384, 32768};
	size_t sizes[] = {187, 1057, 2436, 5234, 9235, 17984, 35783, 72436};

	for (size_t ipass = 0; ipass < 8; ++ipass) {
		for (size_t iloop = 0; iloop < 4096; ++iloop) {
			for (size_t ialign = 0, asize = sizeof(alignment) / sizeof(alignment[0]); ialign < asize; ++ialign) {
				if (alignment[ialign] > RPMALLOC_MAX_ALIGNMENT)
					continue;
				for (size_t isize = 0, ssize = sizeof(sizes) / sizeof(sizes[0]); isize < ssize; ++isize) {
					size_t alloc_size = sizes[isize] + iloop + ipass;
					uint8_t* ptr = rpaligned_alloc(alignment[ialign], alloc_size);
					if (!ptr || ((uintptr_t)ptr & (alignment[ialign] - 1)))
						return test_fail("Super alignment allocation failed");
					ptr[0] = 1;
					ptr[alloc_size - 1] = 1;
					rpfree(ptr);
				}
			}
		}
	}

	rpmalloc_finalize();

	printf("Memory super aligned tests passed\n");

	return 0;
}

typedef struct allocator_thread_arg_t {
	unsigned int loops;
	unsigned int passes;  // max 4096
	unsigned int datasize[32];
	unsigned int num_datasize;  // max 32
	int init_fini_each_loop;
	void** pointers;
	void** crossthread_pointers;
} allocator_thread_arg_t;

static void
allocator_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void** addr;
	uint32_t* data;
	unsigned int cursize;
	unsigned int iwait = 0;
	int ret = 0;

	rpmalloc_thread_initialize();

	addr = rpmalloc(sizeof(void*) * arg.passes);
	data = rpmalloc(512 * 1024);
	for (id = 0; id < 512 * 1024 / 4; ++id)
		data[id] = id;

	thread_sleep(1);

	if (arg.init_fini_each_loop)
		rpmalloc_thread_finalize();

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		if (arg.init_fini_each_loop)
			rpmalloc_thread_initialize();

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = 4 + arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + ((iloop + ipass) % 1024);

			addr[ipass] = rpmalloc(4 + cursize);
			if (addr[ipass] == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass]) {
					ret = test_fail("Identical pointer returned from allocation");
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck] + 4) > addr[ipass]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass] + 4) > addr[icheck]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			if (memcmp(pointer_offset(addr[ipass], 4), data, cursize)) {
				ret = test_fail("Data corrupted");
				goto end;
			}

			rpfree(addr[ipass]);
		}

		if (arg.init_fini_each_loop)
			rpmalloc_thread_finalize();
	}

	if (arg.init_fini_each_loop)
		rpmalloc_thread_initialize();

	rpfree(data);
	rpfree(addr);

	rpmalloc_thread_finalize();

end:
	thread_exit((uintptr_t)ret);
}

#if RPMALLOC_FIRST_CLASS_HEAPS

static void
heap_allocator_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void** addr;
	uint32_t* data;
	unsigned int cursize;
	unsigned int iwait = 0;
	int ret = 0;

	rpmalloc_heap_t* outer_heap = rpmalloc_heap_acquire();

	addr = rpmalloc_heap_alloc(outer_heap, sizeof(void*) * arg.passes);
	data = rpmalloc_heap_alloc(outer_heap, 512 * 1024);
	for (id = 0; id < 512 * 1024 / 4; ++id)
		data[id] = id;

	thread_sleep(1);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		rpmalloc_heap_t* heap = rpmalloc_heap_acquire();

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = 4 + arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + ((iloop + ipass) % 1024);

			addr[ipass] = rpmalloc_heap_alloc(heap, 4 + cursize);
			if (addr[ipass] == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass]) {
					ret = test_fail("Identical pointer returned from allocation");
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck] + 4) > addr[ipass]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass] + 4) > addr[icheck]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			if (memcmp(pointer_offset(addr[ipass], 4), data, cursize)) {
				ret = test_fail("Data corrupted");
				goto end;
			}
		}

		rpmalloc_heap_calloc(heap, 2, 89273432);

		rpmalloc_heap_free_all(heap);
		rpmalloc_heap_release(heap);
	}

	rpmalloc_heap_calloc(outer_heap, 2, 69273432);

	rpmalloc_heap_free_all(outer_heap);
	rpmalloc_heap_release(outer_heap);

end:
	thread_exit((uintptr_t)ret);
}

#endif

static void
crossallocator_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int cursize;
	unsigned int iextra = 0;
	int ret = 0;

	rpmalloc_thread_initialize();

	thread_sleep(10);

	size_t next_crossthread = 0;
	size_t end_crossthread = arg.loops * arg.passes;

	void** extra_pointers = rpmalloc(sizeof(void*) * arg.loops * arg.passes);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			size_t iarg = (iloop + ipass + iextra++) % arg.num_datasize;
			cursize = arg.datasize[iarg] + ((iloop + ipass) % 439);
			void* first_addr = rpmalloc(cursize);
			if (first_addr == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			iarg = (iloop + ipass + iextra++) % arg.num_datasize;
			cursize = arg.datasize[iarg] + ((iloop + ipass) % 71);
			void* second_addr = rpmalloc(cursize);
			if (second_addr == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			iarg = (iloop + ipass + iextra++) % arg.num_datasize;
			cursize = arg.datasize[iarg] + ((iloop + ipass) % 751);
			void* third_addr = rpmalloc(cursize);
			if (third_addr == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			rpfree(first_addr);
			arg.pointers[iloop * arg.passes + ipass] = second_addr;
			extra_pointers[iloop * arg.passes + ipass] = third_addr;

			while ((next_crossthread < end_crossthread) && arg.crossthread_pointers[next_crossthread]) {
				rpfree(arg.crossthread_pointers[next_crossthread]);
				arg.crossthread_pointers[next_crossthread] = 0;
				++next_crossthread;
			}
		}
	}

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			rpfree(extra_pointers[(iloop * arg.passes) + ipass]);
		}
	}

	rpfree(extra_pointers);

	while ((next_crossthread < end_crossthread) && !test_failed) {
		if (arg.crossthread_pointers[next_crossthread]) {
			rpfree(arg.crossthread_pointers[next_crossthread]);
			arg.crossthread_pointers[next_crossthread] = 0;
			++next_crossthread;
		} else {
			thread_yield();
		}
	}

end:
	rpmalloc_thread_finalize();

	thread_exit((uintptr_t)ret);
}

static void
initfini_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	unsigned int iloop;
	unsigned int ipass;
	unsigned int icheck;
	unsigned int id = 0;
	uint32_t* addr[4096];
	uint32_t blocksize[4096];
	char data[8192];
	unsigned int cursize;
	unsigned int max_datasize = 0;
	uint32_t this_size;
	uint32_t check_size;
	int ret = 0;

	for (id = 0; id < sizeof(data); ++id)
		data[id] = (char)id;

	thread_yield();

	if (arg.passes > (sizeof(addr) / sizeof(addr[0])))
		arg.passes = sizeof(addr) / sizeof(addr[0]);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		rpmalloc_thread_initialize();

		max_datasize = 0;
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = arg.datasize[(iloop + ipass) % arg.num_datasize] + ((iloop + ipass) % 1024);
			if (cursize > sizeof(data))
				cursize = sizeof(data);
			if (cursize > max_datasize)
				max_datasize = cursize;

			addr[ipass] = rpmalloc(sizeof(uint32_t) + cursize);
			if (addr[ipass] == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			blocksize[ipass] = (uint32_t)cursize;
			addr[ipass][0] = (uint32_t)cursize;
			memcpy(addr[ipass] + 1, data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				this_size = addr[ipass][0];
				check_size = addr[icheck][0];
				if (this_size != cursize) {
					ret = test_fail("Data corrupted in this block (size)");
					goto end;
				}
				if (check_size != blocksize[icheck]) {
					printf("For %u:%u got previous block size %u (%x) wanted %u (%x)\n", iloop, ipass, check_size,
					       check_size, blocksize[icheck], blocksize[icheck]);
					ret = test_fail("Data corrupted in previous block (size)");
					goto end;
				}
				if (addr[icheck] == addr[ipass]) {
					ret = test_fail("Identical pointer returned from allocation");
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], check_size + sizeof(uint32_t)) > (void*)addr[ipass]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				} else {
					if (pointer_offset(addr[ipass], this_size + sizeof(uint32_t)) > (void*)addr[icheck]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = addr[ipass][0];

			if (cursize != blocksize[ipass]) {
				printf("For %u:%u got size %u (%x) wanted %u (%x)\n", iloop, ipass, cursize, cursize, blocksize[ipass],
				       blocksize[ipass]);
				ret = test_fail("Data corrupted (size)");
				goto end;
			}
			if (cursize > max_datasize) {
				printf("For %u:%u got size %u (%x) >= %u\n", iloop, ipass, cursize, cursize, max_datasize);
				ret = test_fail("Data corrupted (size)");
				goto end;
			}
			if (memcmp(addr[ipass] + 1, data, cursize)) {
				ret = test_fail("Data corrupted");
				goto end;
			}

			rpfree(addr[ipass]);
		}

		rpmalloc_thread_finalize();
		thread_yield();
	}

end:
	rpmalloc_thread_finalize();
	thread_exit((uintptr_t)ret);
}

static int
test_thread_implementation(void) {
	uintptr_t thread[32];
	uintptr_t threadres[32];
	unsigned int i;
	size_t num_alloc_threads;
	allocator_thread_arg_t arg;

	num_alloc_threads = hardware_threads;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
	if (num_alloc_threads > 32)
		num_alloc_threads = 32;

	arg.datasize[0] = 19;
	arg.datasize[1] = 249;
	arg.datasize[2] = 797;
	arg.datasize[3] = 3058;
	arg.datasize[4] = 47892;
	arg.datasize[5] = 173902;
	arg.datasize[6] = 389;
	arg.datasize[7] = 19;
	arg.datasize[8] = 2493;
	arg.datasize[9] = 7979;
	arg.datasize[10] = 3;
	arg.datasize[11] = 79374;
	arg.datasize[12] = 3432;
	arg.datasize[13] = 548;
	arg.datasize[14] = 38934;
	arg.datasize[15] = 234;
	arg.num_datasize = 16;
#if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
	arg.loops = test_scale(100);
	arg.passes = test_scale(4000);
#else
	arg.loops = test_scale(30);
	arg.passes = test_scale(1000);
#endif
	arg.init_fini_each_loop = 0;

	thread_arg targ;
	targ.fn = allocator_thread;
	targ.arg = &arg;
	for (i = 0; i < num_alloc_threads; ++i)
		thread[i] = thread_run(&targ);

	thread_sleep(1000);

	for (i = 0; i < num_alloc_threads; ++i)
		threadres[i] = thread_join(thread[i]);

	rpmalloc_finalize();

	for (i = 0; i < num_alloc_threads; ++i) {
		if (threadres[i])
			return -1;
	}

	return 0;
}

static int
test_threaded(void) {
	rpmalloc_config_t config = {0};
	//config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);

	int ret = test_thread_implementation();

	rpmalloc_finalize();

	if (ret == 0)
		printf("Memory threaded tests passed\n");

	return ret;
}

static int
test_crossthread(void) {
	uintptr_t thread[32];
	allocator_thread_arg_t arg[32];
	thread_arg targ[32];

	rpmalloc_config_t config = {0};
	//config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);

	size_t num_alloc_threads = hardware_threads;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
	if (num_alloc_threads > 16)
		num_alloc_threads = 16;

	for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread) {
		unsigned int iadd = (ithread * (16 + ithread) + ithread) % 128;
#if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
		arg[ithread].loops = test_scale(50);
		arg[ithread].passes = test_scale(1024);
#else
		arg[ithread].loops = test_scale(20);
		arg[ithread].passes = test_scale(200);
#endif
		arg[ithread].pointers = rpmalloc(sizeof(void*) * arg[ithread].loops * arg[ithread].passes);
		memset(arg[ithread].pointers, 0, sizeof(void*) * arg[ithread].loops * arg[ithread].passes);
		arg[ithread].datasize[0] = 19 + iadd;
		arg[ithread].datasize[1] = 249 + iadd;
		arg[ithread].datasize[2] = 797 + iadd;
		arg[ithread].datasize[3] = 3 + iadd;
		arg[ithread].datasize[4] = 7923 + iadd;
		arg[ithread].datasize[5] = 344 + iadd;
		arg[ithread].datasize[6] = 3892 + iadd;
		arg[ithread].datasize[7] = 19 + iadd;
		arg[ithread].datasize[8] = 154 + iadd;
		arg[ithread].datasize[9] = 9723 + iadd;
		arg[ithread].datasize[10] = 15543 + iadd;
		arg[ithread].datasize[11] = 32493 + iadd;
		arg[ithread].datasize[12] = 34 + iadd;
		arg[ithread].datasize[13] = 1894 + iadd;
		arg[ithread].datasize[14] = 193 + iadd;
		arg[ithread].datasize[15] = 2893 + iadd;
		arg[ithread].num_datasize = 16;

		targ[ithread].fn = crossallocator_thread;
		targ[ithread].arg = &arg[ithread];
	}

	for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread) {
		arg[ithread].crossthread_pointers = arg[(ithread + 1) % num_alloc_threads].pointers;
	}

	for (int iloop = 0; iloop < 32; ++iloop) {
		for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread)
			thread[ithread] = thread_run(&targ[ithread]);

		thread_sleep(100);

		for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread) {
			if (thread_join(thread[ithread]) != 0)
				return -1;
		}
	}

	for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread)
		rpfree(arg[ithread].pointers);

	printf("Memory cross thread free tests passed\n");

	rpmalloc_finalize();

	return 0;
}

static int
test_threadspam(void) {
	uintptr_t thread[64];
	uintptr_t threadres[64];
	unsigned int i, j;
	size_t num_passes, num_alloc_threads;
	allocator_thread_arg_t arg;

	rpmalloc_config_t config = {0};
	//config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);

	num_passes = 100;
	num_alloc_threads = hardware_threads;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
#if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
	if (num_alloc_threads > 32)
		num_alloc_threads = 32;
#else
	if (num_alloc_threads > 16)
		num_alloc_threads = 16;
#endif

	arg.loops = test_scale(500);
	arg.passes = test_scale(10);
	arg.datasize[0] = 19;
	arg.datasize[1] = 249;
	arg.datasize[2] = 797;
	arg.datasize[3] = 3;
	arg.datasize[4] = 79;
	arg.datasize[5] = 34;
	arg.datasize[6] = 389;
	arg.num_datasize = 7;

	thread_arg targ;
	targ.fn = initfini_thread;
	targ.arg = &arg;
	for (i = 0; i < num_alloc_threads; ++i)
		thread[i] = thread_run(&targ);

	for (j = 0; j < num_passes; ++j) {
		thread_sleep(100);

		for (i = 0; i < num_alloc_threads; ++i) {
			threadres[i] = thread_join(thread[i]);
			if (threadres[i])
				return -1;
			thread[i] = thread_run(&targ);
		}
	}

	thread_sleep(1000);

	for (i = 0; i < num_alloc_threads; ++i)
		threadres[i] = thread_join(thread[i]);

	rpmalloc_finalize();

	for (i = 0; i < num_alloc_threads; ++i) {
		if (threadres[i])
			return -1;
	}

	printf("Memory thread spam tests passed\n");

	return 0;
}

typedef struct thread_exit_heap_arg_t {
	void* block;
	int finalize;
} thread_exit_heap_arg_t;

static void
thread_exit_heap_thread(void* argp) {
	thread_exit_heap_arg_t* arg = argp;
	void* block = rpmalloc(100);
	arg->block = rpmalloc(1000);
	rpfree(block);
	if (arg->finalize)
		rpmalloc_thread_finalize();
}

static int
test_thread_exit_heap(void) {
	// The thread exit destructor releases the heap of a thread that exits without finalizing, for the next
	// thread to reuse. A thread that finalizes first leaves the fallback heap as the destructor value
	rpmalloc_initialize(0);

	rpmalloc_global_statistics_t stats;
	rpmalloc_global_statistics(&stats);
	size_t heap_count = stats.heap_count;

	for (unsigned int ithread = 0; ithread < 16; ++ithread) {
		thread_exit_heap_arg_t arg = {0};
		arg.finalize = (ithread % 4) == 3;
		thread_arg targ;
		targ.fn = thread_exit_heap_thread;
		targ.arg = &arg;
		uintptr_t thread = thread_run(&targ);
		if (!thread)
			return test_fail("Unable to create thread");
		thread_join(thread);
		if (!arg.block)
			return test_fail("Allocation failed");
		rpfree(arg.block);
	}

	rpmalloc_global_statistics(&stats);
	if (stats.heap_count != heap_count)
		return test_fail("Heap of thread exiting without finalize was not released");

	rpmalloc_finalize();

	printf("Thread exit heap tests passed\n");

	return 0;
}

static int
test_first_class_heaps(void) {
#if RPMALLOC_FIRST_CLASS_HEAPS
	uintptr_t thread[32];
	uintptr_t threadres[32];
	unsigned int i;
	size_t num_alloc_threads;
	allocator_thread_arg_t arg[32];
	thread_arg targ[32];

	rpmalloc_config_t config = {0};
	//config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);

	num_alloc_threads = hardware_threads * 2;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
	if (num_alloc_threads > 16)
		num_alloc_threads = 16;

	for (i = 0; i < num_alloc_threads; ++i) {
		arg[i].datasize[0] = 19;
		arg[i].datasize[1] = 249;
		arg[i].datasize[2] = 797;
		arg[i].datasize[3] = 3058;
		arg[i].datasize[4] = 47892;
		arg[i].datasize[5] = 173932;
		arg[i].datasize[6] = 389;
		arg[i].datasize[7] = 19;
		arg[i].datasize[8] = 2493;
		arg[i].datasize[9] = 7979;
		arg[i].datasize[10] = 3;
		arg[i].datasize[11] = 79374;
		arg[i].datasize[12] = 3432;
		arg[i].datasize[13] = 548;
		arg[i].datasize[14] = 38934;
		arg[i].datasize[15] = 234;
		arg[i].num_datasize = 16;
#if defined(__LLP64__) || defined(__LP64__) || defined(_WIN64)
		arg[i].loops = test_scale(100);
		arg[i].passes = test_scale(4000);
#else
		arg[i].loops = test_scale(50);
		arg[i].passes = test_scale(1000);
#endif
		arg[i].init_fini_each_loop = 1;

		// targ must outlive the thread, which reads it asynchronously until join below, so it
		// cannot be a per-iteration stack local
		targ[i].fn = heap_allocator_thread;
		if ((i % 2) != 0)
			targ[i].fn = allocator_thread;
		targ[i].arg = &arg[i];

		thread[i] = thread_run(&targ[i]);
	}

	thread_sleep(1000);

	for (i = 0; i < num_alloc_threads; ++i)
		threadres[i] = thread_join(thread[i]);

	rpmalloc_finalize();

	for (i = 0; i < num_alloc_threads; ++i) {
		if (threadres[i])
			return -1;
	}

	printf("First class heap tests passed\n");
#endif
	return 0;
}

#if RPMALLOC_FIRST_CLASS_HEAPS
typedef struct heapless_free_arg_t {
	void* aligned_block;
	void* huge_block;
} heapless_free_arg_t;

static void
heapless_free_thread(void* argp) {
	heapless_free_arg_t* arg = argp;
	// Without a current heap every free is a cross-thread free into pages of other heaps
	rpmalloc_thread_initialize();
	rpmalloc_heap_thread_set_current(0);
	rpfree(arg->aligned_block);
	rpfree(arg->huge_block);
}
#endif

static int
test_heapless_free(void) {
#if RPMALLOC_FIRST_CLASS_HEAPS
	// Blocks freed by a thread without a heap must take the generic cross-thread path. An aligned block
	// pushed as is to the local free list of its page would later be handed out from inside the block,
	// overlapping the next block, and a huge block would never be released
	enum { BLOCK_COUNT = 512 };
	const size_t block_size = 556;
	void* blocks[BLOCK_COUNT];
	unsigned int block_used = 0;
	heapless_free_arg_t arg = {0};

	rpmalloc_initialize(0);

	rpmalloc_global_statistics_t stats;
	rpmalloc_global_statistics(&stats);
	size_t huge_alloc = stats.huge_alloc;

	// Plain blocks of the size class the aligned block comes from keep its page in use. Retry until the
	// aligned block starts inside its block, which is what marks the page as holding aligned blocks
	for (unsigned int iblock = 0; iblock < 8; ++iblock)
		blocks[block_used++] = rpmalloc(block_size);
	size_t plain_usable = rpmalloc_usable_size(blocks[0]);
	for (unsigned int itry = 0; (itry < 16) && !arg.aligned_block; ++itry) {
		void* block = rpaligned_alloc(256, block_size - 256);
		if (!block)
			return test_fail("Aligned allocation failed");
		if (rpmalloc_usable_size(block) < plain_usable)
			arg.aligned_block = block;
		else
			blocks[block_used++] = block;
		blocks[block_used++] = rpmalloc(block_size);
	}
	if (!arg.aligned_block)
		return test_fail("Aligned allocation never started inside its block");
	arg.huge_block = rpmalloc(4 * 1024 * 1024);
	if (!arg.huge_block)
		return test_fail("Huge allocation failed");

	thread_arg targ;
	targ.fn = heapless_free_thread;
	targ.arg = &arg;
	uintptr_t thread = thread_run(&targ);
	thread_join(thread);

	// Fill the page the aligned block was freed into, and more, then check that no blocks overlap
	while (block_used < BLOCK_COUNT) {
		blocks[block_used] = rpmalloc(block_size);
		if (!blocks[block_used])
			return test_fail("Allocation failed after free from thread without heap");
		++block_used;
	}
	for (unsigned int iblock = 0; iblock < block_used; ++iblock)
		memset(blocks[iblock], (int)(iblock & 0xFF), block_size);
	for (unsigned int iblock = 0; iblock < block_used; ++iblock) {
		const unsigned char* data = blocks[iblock];
		for (size_t ibyte = 0; ibyte < block_size; ++ibyte) {
			if (data[ibyte] != (unsigned char)(iblock & 0xFF))
				return test_fail("Block overlap after free from thread without heap");
		}
	}

	rpmalloc_global_statistics(&stats);
	if (stats.huge_alloc != huge_alloc)
		return test_fail("Huge block freed from thread without heap was not released");

	for (unsigned int iblock = 0; iblock < block_used; ++iblock)
		rpfree(blocks[iblock]);

	rpmalloc_finalize();

	printf("Heapless free tests passed\n");
#endif
	return 0;
}

#if RPMALLOC_FIRST_CLASS_HEAPS
typedef struct first_class_huge_free_arg_t {
	rpmalloc_heap_t* heap;
	void* block[2];
	size_t huge_alloc[2];
} first_class_huge_free_arg_t;

static size_t
first_class_huge_alloc(void) {
	rpmalloc_global_statistics_t stats;
	rpmalloc_global_statistics(&stats);
	return stats.huge_alloc;
}

static void
first_class_huge_free_thread(void* argp) {
	// Free huge blocks of a first class heap from another thread, with the heap API while the heap is acquired
	// and with rpfree once it is released
	first_class_huge_free_arg_t* arg = argp;
	for (unsigned int iblock = 0; iblock < 2; ++iblock) {
		if (arg->heap)
			rpmalloc_heap_free(arg->heap, arg->block[iblock]);
		else
			rpfree(arg->block[iblock]);
	}
}

static void
first_class_huge_current_thread(void* argp) {
	// Free a huge block allocated before its heap became the thread heap, then free all memory of the heap
	first_class_huge_free_arg_t* arg = argp;
	rpmalloc_thread_initialize();
	rpmalloc_heap_t* heap = rpmalloc_heap_acquire();
	arg->huge_alloc[0] = first_class_huge_alloc();
	arg->block[0] = rpmalloc_heap_alloc(heap, 6 * 1024 * 1024);
	rpmalloc_heap_thread_set_current(heap);
	rpfree(arg->block[0]);
	rpmalloc_heap_free_all(heap);
	arg->huge_alloc[1] = first_class_huge_alloc();
}
#endif

static int
test_first_class_huge_free(void) {
#if RPMALLOC_FIRST_CLASS_HEAPS
	// Huge blocks of a first class heap are tracked in its span list for rpmalloc_heap_free_all. A huge block freed
	// on its own must leave the list before its mapping is cached or unmapped, or free_all reads the span of an
	// unmapped mapping, or unmaps a cached mapping that another heap may already have reused. The huge byte count
	// must match the live huge blocks after each free_all. A huge block of the thread heap stays live throughout,
	// so a freed block released a second time lowers the count
	const size_t mib = 1024 * 1024;
	first_class_huge_free_arg_t arg = {0};
	thread_arg targ;
	uintptr_t thread;

	rpmalloc_initialize(0);
	void* live = rpmalloc(3 * mib);
	if (!live)
		return test_fail("Huge allocation failed");
	rpmalloc_heap_t* heap = rpmalloc_heap_acquire();

	// A block whose mapping is cached when freed and reused by the thread heap before free_all
	void* block = rpmalloc_heap_alloc(heap, 8 * mib);
	if (!block)
		return test_fail("First class heap huge allocation failed");
	memset(block, 1, 8 * mib);
	rpmalloc_heap_free(heap, block);
	void* reuse = rpmalloc(8 * mib);
	if (!reuse)
		return test_fail("Huge allocation failed");
	size_t huge_alloc = first_class_huge_alloc();
	rpmalloc_heap_free_all(heap);
	if (first_class_huge_alloc() != huge_alloc)
		return test_fail("Free all released a huge block freed on its own");
	memset(reuse, 2, 8 * mib);
	rpfree(reuse);

	// A block too large to be cached, unmapped when freed
	block = rpmalloc_heap_alloc(heap, 64 * mib);
	if (!block)
		return test_fail("First class heap huge allocation failed");
	memset(block, 3, mib);
	rpmalloc_heap_free(heap, block);
	huge_alloc = first_class_huge_alloc();
	rpmalloc_heap_free_all(heap);
	if (first_class_huge_alloc() != huge_alloc)
		return test_fail("Free all released an unmapped huge block");

	// Free from the middle, the head and the tail of the list, with aligned, reused and reallocated blocks, then free
	// all with two blocks left in the list
	void* tail = rpmalloc_heap_alloc(heap, 3 * mib);
	void* middle = rpmalloc_heap_aligned_alloc(heap, 64 * 1024, 4 * mib);
	void* head = rpmalloc_heap_alloc(heap, 5 * mib);
	if (!tail || !middle || !head)
		return test_fail("First class heap huge allocation failed");
	rpmalloc_heap_free(heap, middle);
	rpmalloc_heap_free(heap, head);
	void* reused = rpmalloc_heap_alloc(heap, 4 * mib);
	unsigned char* grown = rpmalloc_heap_alloc(heap, 3 * mib);
	if (!reused || !grown)
		return test_fail("First class heap huge allocation failed");
	memset(grown, 4, 3 * mib);
	grown = rpmalloc_heap_realloc(heap, grown, 12 * mib, 0);
	if (!grown)
		return test_fail("First class heap huge reallocation failed");
	for (size_t ibyte = 0; ibyte < (3 * mib); ibyte += 4093) {
		if (grown[ibyte] != 4)
			return test_fail("Data not preserved by first class heap huge reallocation");
	}
	rpmalloc_heap_free(heap, tail);
	rpmalloc_heap_free_all(heap);
	if (first_class_huge_alloc() != huge_alloc)
		return test_fail("Free all did not release exactly the huge blocks left in the heap");

	// Free from another thread with the heap API, then free all
	arg.heap = heap;
	arg.block[0] = rpmalloc_heap_alloc(heap, 4 * mib);
	arg.block[1] = rpmalloc_heap_alloc(heap, 6 * mib);
	if (!arg.block[0] || !arg.block[1])
		return test_fail("First class heap huge allocation failed");
	targ.fn = first_class_huge_free_thread;
	targ.arg = &arg;
	thread = thread_run(&targ);
	thread_join(thread);
	rpmalloc_heap_free_all(heap);
	if (first_class_huge_alloc() != huge_alloc)
		return test_fail("Free all released a huge block freed by another thread");

	// Release the heap with live huge blocks, freed later by another thread, which may adopt the heap
	arg.heap = 0;
	arg.block[0] = rpmalloc_heap_alloc(heap, 4 * mib);
	arg.block[1] = rpmalloc_heap_alloc(heap, 6 * mib);
	if (!arg.block[0] || !arg.block[1])
		return test_fail("First class heap huge allocation failed");
	rpmalloc_heap_release(heap);
	thread = thread_run(&targ);
	thread_join(thread);
	if (first_class_huge_alloc() != huge_alloc)
		return test_fail("Huge blocks of a released first class heap not released");

	// A huge block allocated before its heap became the thread heap and freed after is not released again by
	// free all
	targ.fn = first_class_huge_current_thread;
	thread = thread_run(&targ);
	thread_join(thread);
	if (!arg.block[0])
		return test_fail("First class heap huge allocation failed");
	if (arg.huge_alloc[1] != arg.huge_alloc[0])
		return test_fail("Free all released a huge block freed after the heap became the thread heap");

	rpfree(live);
	rpmalloc_finalize();

	printf("First class heap huge free tests passed\n");
#endif
	return 0;
}

static int
test_large_pages(void) {
	int ret = 0;

	rpmalloc_config_t config = {0};
	config.page_size = 16 * 1024 * 1024;

	rpmalloc_initialize_config(0, &config);

	ret = test_thread_implementation();

	rpmalloc_finalize();

	if (ret == 0)
		printf("Large page config test passed\n");

	return ret;
}

static int
test_huge_pages_alloc(void) {
	// Exercise all page types (small, medium, large) and huge blocks
	const size_t size[] = {16,         1000,            4000,            25 * 1024,
	                       150 * 1024, 2 * 1024 * 1024, 7 * 1024 * 1024, 26 * 1024 * 1024};
	const size_t size_count = sizeof(size) / sizeof(size[0]);
	const size_t alloc_count = 8;
	void* addr[sizeof(size) / sizeof(size[0])][8];
	for (size_t isize = 0; isize < size_count; ++isize) {
		for (size_t ialloc = 0; ialloc < alloc_count; ++ialloc) {
			void* ptr = rpmalloc(size[isize]);
			if (!ptr)
				return test_fail("Allocation failed");
			memset(ptr, 0xab, size[isize]);
			addr[isize][ialloc] = ptr;
		}
	}
	for (size_t isize = 0; isize < size_count; ++isize) {
		for (size_t ialloc = 0; ialloc < alloc_count; ++ialloc) {
			const unsigned char* ptr = addr[isize][ialloc];
			if ((ptr[0] != 0xab) || (ptr[size[isize] - 1] != 0xab))
				return test_fail("Data not preserved in allocation");
			rpfree(addr[isize][ialloc]);
		}
	}
	// Verify zeroed allocations survive the free page commit/decommit cycles (decommit is deferred,
	// force it so the recommit path is exercised)
	rpmalloc_thread_collect();
	for (size_t isize = 0; isize < size_count; ++isize) {
		void* ptr = rpcalloc(1, size[isize]);
		if (!ptr)
			return test_fail("Allocation failed");
		const unsigned char* bytes = ptr;
		for (size_t ibyte = 0; ibyte < size[isize]; ++ibyte) {
			if (bytes[ibyte] != 0)
				return test_fail("Zeroed allocation not zeroed");
		}
		rpfree(ptr);
	}
	return 0;
}

static int
test_huge_pages(void) {
	rpmalloc_config_t config = {0};
	config.enable_huge_pages = 1;

	// Explicit huge pages now fail initialization when no huge pages are available on the
	// system, rather than silently falling back to normal pages behind a huge page size.
	if (rpmalloc_initialize_config(0, &config) != 0) {
		printf("Huge pages test skipped (huge pages not supported)\n");
		return 0;
	}

	const rpmalloc_config_t* effective_config = rpmalloc_config();
	if (!effective_config->enable_huge_pages) {
		rpmalloc_finalize();
		return test_fail("Huge pages initialized but not enabled in effective config");
	}
	if (effective_config->page_size < (2 * 1024 * 1024)) {
		rpmalloc_finalize();
		return test_fail("Page size not raised to huge page size");
	}

	int ret = test_huge_pages_alloc();

	rpmalloc_finalize();

	if (ret == 0)
		printf("Huge pages test passed\n");

	return ret;
}

static int
test_transparent_huge_pages(void) {
	rpmalloc_config_t config = {0};
	config.enable_thp = 1;

	rpmalloc_initialize_config(0, &config);

	const rpmalloc_config_t* effective_config = rpmalloc_config();
	int thp_in_use = effective_config->enable_thp;
	if (thp_in_use && effective_config->enable_huge_pages) {
		rpmalloc_finalize();
		return test_fail("Transparent huge pages should not enable explicit huge pages");
	}

	int ret = test_huge_pages_alloc();

	rpmalloc_finalize();

	if (ret == 0) {
		if (thp_in_use)
			printf("Transparent huge pages test passed\n");
		else
			printf("Transparent huge pages test skipped (not supported)\n");
	}

	return ret;
}

static int
test_named_pages(void) {
	rpmalloc_config_t config = {0};
	char page_name[64] = {0};
	snprintf(page_name, sizeof(page_name), "rpmalloc ::%s::", __func__);
	config.page_name = page_name;
	//config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);

	void* testptr = rpmalloc(16 * 1024 * 1024);
#if defined(__linux__)
	char name[256], buf[4096] = {0};
	int pid = getpid();
	snprintf(name, sizeof(name), "/proc/%d/maps", pid);
	int fd = open(name, O_RDONLY);
	if (fd != -1) {
		ssize_t was_read = read(fd, buf, sizeof(buf));
		(void)sizeof(was_read);
		close(fd);
	}
#endif
	rpfree(testptr);

	rpmalloc_finalize();

	printf("Named pages test passed\n");
#if defined(__linux__)
	// Since it is kernel version and config dependent
	// we do not make an issue out of it.
	if (!strstr(buf, page_name))
		printf("\tbut the page did not get an id as expected\n");
#endif
	return 0;
}

#if !RPMALLOC_TEST_OVERRIDE
// unmap_on_finalize is a no-op under the standard library override (see rpmalloc.h), so the tests
// that exercise it are compiled only into the no-override test binary (RPMALLOC_TEST_OVERRIDE == 0).
static int
test_finalize_unmap(void) {
	// Exercises heap packing together with unmap_on_finalize, which the other tests leave disabled
	// (they let the OS reclaim memory at exit). Threads repeatedly create and destroy heaps, drawing
	// from the pristine and reuse pools, and first-class heaps draw pristine heaps from the same
	// shared blocks. rpmalloc_finalize then unmaps every shared block exactly once via its owner.
	// Runs with the default page size and with a forced larger page size so many heaps are carved
	// per block.
	size_t page_size[2];
	page_size[0] = 0;
	page_size[1] = 256 * 1024;

	for (unsigned int icfg = 0; icfg < 2; ++icfg) {
		rpmalloc_config_t config = {0};
		config.page_size = page_size[icfg];
		config.unmap_on_finalize = 1;
		rpmalloc_initialize_config(0, &config);

		allocator_thread_arg_t arg = {0};
		arg.loops = test_scale(30);
		arg.passes = test_scale(8);
		arg.datasize[0] = 19;
		arg.datasize[1] = 249;
		arg.datasize[2] = 797;
		arg.datasize[3] = 3;
		arg.datasize[4] = 7949;
		arg.datasize[5] = 34;
		arg.datasize[6] = 389;
		arg.num_datasize = 7;

		size_t num_threads = hardware_threads;
		if (num_threads < 2)
			num_threads = 2;
		if (num_threads > 16)
			num_threads = 16;

		thread_arg targ;
		targ.fn = initfini_thread;
		targ.arg = &arg;

		uintptr_t thread[16];
		for (size_t i = 0; i < num_threads; ++i)
			thread[i] = thread_run(&targ);

#if RPMALLOC_FIRST_CLASS_HEAPS
		// First-class heaps must come back pristine; here they reuse never-used heaps carved from
		// the shared blocks (mapped for the worker heaps or for earlier first-class heaps).
		rpmalloc_heap_t* heap[64];
		for (unsigned int i = 0; i < 64; ++i) {
			heap[i] = rpmalloc_heap_acquire();
			void* p = rpmalloc_heap_alloc(heap[i], 1000 + i * 137);
			if (!p)
				return test_fail("First class allocation failed in finalize unmap test");
			rpmalloc_heap_free(heap[i], p);
		}
		for (unsigned int i = 0; i < 64; ++i)
			rpmalloc_heap_release(heap[i]);
#endif

		int failed = 0;
		for (size_t i = 0; i < num_threads; ++i) {
			if (thread_join(thread[i]) != 0)
				failed = 1;
		}

		rpmalloc_finalize();

		if (failed)
			return test_fail("Thread failed in finalize unmap test");
	}

	printf("Finalize unmap (heap packing) tests passed\n");
	return 0;
}

#if !defined(_WIN32) && RPMALLOC_FIRST_CLASS_HEAPS

// Counting memory interface used by test_heap_packing. Heap control blocks are the only mappings
// requested with alignment 0 (spans and huge blocks use span-size alignment), so counting those
// maps tells us exactly how many heap blocks were mapped. The test drives this single-threaded, so
// a plain counter is sufficient.
static size_t test_pack_block_maps;

static void*
test_pack_map(size_t size, size_t alignment, size_t* offset, size_t* mapped_size) {
	size_t map_size = size + alignment;
	void* ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (ptr == MAP_FAILED)
		return 0;
	*offset = 0;
	if (alignment) {
		uintptr_t unalign = (uintptr_t)ptr & (uintptr_t)(alignment - 1);
		size_t padding = unalign ? (alignment - unalign) : 0;
		if (padding)
			munmap(ptr, padding);
		ptr = pointer_offset(ptr, padding);
		if (alignment - padding)
			munmap(pointer_offset(ptr, size), alignment - padding);
		map_size = size;
	} else {
		++test_pack_block_maps;
	}
	*mapped_size = map_size;
	return ptr;
}

static void
test_pack_unmap(void* address, size_t offset, size_t mapped_size) {
	munmap(pointer_offset(address, -(ptrdiff_t)offset), mapped_size);
}

static int
test_pack_commit(void* address, size_t size) {
	(void)address;
	(void)size;
	return 0;
}

static int
test_pack_decommit(void* address, size_t size) {
	(void)address;
	(void)size;
	return 0;
}

#endif

static int
test_heap_packing(void) {
	// Regression guard for the heap packing guarantees: many heaps are carved from a single mapped
	// block, and first-class heaps reuse the pristine carved heaps instead of mapping a fresh block
	// each time. Driven single-threaded through a counting memory interface so it is deterministic.
#if defined(_WIN32) || !RPMALLOC_FIRST_CLASS_HEAPS
	printf("Heap packing test skipped\n");
	return 0;
#else
	rpmalloc_interface_t iface = {0};
	iface.memory_map = test_pack_map;
	iface.memory_unmap = test_pack_unmap;
	iface.memory_commit = test_pack_commit;
	iface.memory_decommit = test_pack_decommit;

	rpmalloc_config_t config = {0};
	config.page_size = 256 * 1024;  // large page so many heaps pack into one block
	config.unmap_on_finalize = 1;

	test_pack_block_maps = 0;
	rpmalloc_initialize_config(&iface, &config);

	// Initializing the main thread heap maps one block and carves its pristine extras from it
	if (test_pack_block_maps < 1)
		return test_fail("No heap block was mapped at initialize");

	// Acquire first-class heaps one at a time. They must draw pristine carved heaps from the existing
	// block with no new mapping until the block's pristine heaps run out, at which point exactly one
	// new block is mapped. No acquire may map more than one block.
	rpmalloc_heap_t* heap[1024];
	unsigned int reused_before_remap = 0;
	unsigned int acquired = 0;
	while (acquired < 1024) {
		size_t before = test_pack_block_maps;
		heap[acquired] = rpmalloc_heap_acquire();
		if (!heap[acquired])
			return test_fail("First class heap acquire failed");
		size_t after = test_pack_block_maps;
		++acquired;
		if (after == before) {
			++reused_before_remap;
		} else if (after == before + 1) {
			break;  // pristine pool drained, one new block mapped
		} else {
			return test_fail("More than one heap block mapped for a single acquire");
		}
	}

	// Packing and first-class reuse: those acquires were served from the existing block with no new
	// mapping (the loop above errors out otherwise). With a 256KiB page and a heap control block of
	// at most 4KiB a block holds at least 64 heaps; require a comfortably lower bound so the test is
	// robust to heap_t size changes.
	if (reused_before_remap < 16)
		return test_fail("Heap block packed too few heaps, or first class did not reuse pristine heaps");

	for (unsigned int i = 0; i < acquired; ++i)
		rpmalloc_heap_release(heap[i]);
	rpmalloc_finalize();

	printf("Heap packing tests passed (%u first class heaps reused from one block)\n", reused_before_remap);
	return 0;
#endif
}
#endif /* !RPMALLOC_TEST_OVERRIDE */

static int
test_thread_statistics(void) {
	// Exercises rpmalloc_thread_statistics / rpmalloc_dump_statistics, which are otherwise never
	// called by the suite. When ENABLE_STATISTICS is disabled the counters read back zero and the
	// strict checks are skipped; the calls must still be safe.
	rpmalloc_initialize(0);

	rpmalloc_thread_statistics_t ts;
	size_t base_current = 0, base_total = 0, base_free = 0;
	rpmalloc_thread_statistics(&ts);
	for (unsigned int i = 0; i < 128; ++i) {
		base_current += ts.size_use[i].alloc_current;
		base_total += ts.size_use[i].alloc_total;
		base_free += ts.size_use[i].free_total;
	}

	enum { N = 100 };
	void* ptr[N];
	for (unsigned int i = 0; i < N; ++i) {
		ptr[i] = rpmalloc(123);
		if (!ptr[i])
			return test_fail("Allocation failed in statistics test");
	}

	rpmalloc_thread_statistics(&ts);
	size_t cur = 0, tot = 0, span_current = 0, span_maps = 0;
	for (unsigned int i = 0; i < 128; ++i) {
		cur += ts.size_use[i].alloc_current;
		tot += ts.size_use[i].alloc_total;
	}
	for (unsigned int i = 0; i < 5; ++i) {
		span_current += ts.span_use[i].current;
		span_maps += ts.span_use[i].map_calls;
	}

	int stats_enabled = (tot > base_total);
	if (stats_enabled) {
		if ((cur - base_current) < N)
			return test_fail("alloc_current did not reflect allocations");
		if ((tot - base_total) < N)
			return test_fail("alloc_total did not reflect allocations");
		if (span_current < 1)
			return test_fail("span current did not reflect mapped spans");
		if (span_maps < 1)
			return test_fail("span map_calls did not reflect mapped spans");
		if (ts.sizecache == 0 && ts.spancache == 0) {
			// At least the partially used page should hold free blocks
			return test_fail("sizecache/spancache both zero after allocations");
		}
	}

	for (unsigned int i = 0; i < N; ++i)
		rpfree(ptr[i]);

	rpmalloc_thread_statistics(&ts);
	size_t cur2 = 0, free2 = 0;
	for (unsigned int i = 0; i < 128; ++i) {
		cur2 += ts.size_use[i].alloc_current;
		free2 += ts.size_use[i].free_total;
	}
	if (stats_enabled) {
		if (cur2 >= cur || (cur - cur2) < N)
			return test_fail("alloc_current did not drop after free");
		if ((free2 - base_free) < N)
			return test_fail("free_total did not reflect frees");
	}

	// Dump must be safe to call (output discarded)
	FILE* f = tmpfile();
	if (f) {
		rpmalloc_dump_statistics(f);
		fclose(f);
	}

	rpmalloc_global_statistics_t gs;
	rpmalloc_global_statistics(&gs);
	if (stats_enabled && (gs.heap_count < 1))
		return test_fail("heap_count should count in-use heaps");

	rpmalloc_finalize();
	printf("Thread statistics tests passed%s\n", stats_enabled ? "" : " (statistics disabled)");
	return 0;
}

//! Blocks allocated and not freed, or freed but not yet pushed to their owner, as reported by leak
//  detection (allocated blocks minus the blocks waiting in the deferred cross-thread free lists). Exact
//  while no other thread uses the allocator. Returns -1 when leak detection is not built in
static long long
test_outstanding_blocks(void) {
	long long outstanding = -1;
	FILE* file = tmpfile();
	if (!file)
		return -1;
	rpmalloc_dump_statistics(file);
	fflush(file);
	rewind(file);
	char line[256];
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "Outstanding blocks: %lld", &outstanding) == 1)
			break;
	}
	fclose(file);
	return outstanding;
}

#define REMOTE_FREE_EXIT 0
#define REMOTE_FREE_FINALIZE 1
#define REMOTE_FREE_COLLECT 2

// Bytes of blocks a thread free chain holds before it is pushed, as the library is built (256KiB by default)
#ifndef THREAD_FREE_CHAIN_LIMIT_BYTES
#define THREAD_FREE_CHAIN_LIMIT_BYTES (256 * 1024)
#endif
// Length of the run of frees into one page that ends the frees in collect mode
#define REMOTE_FREE_RUN 96

typedef struct remote_free_arg_t {
	void** blocks;
	size_t count;
	int mode;
	atomic_int state;
} remote_free_arg_t;

static void
remote_free_thread(void* argp) {
	remote_free_arg_t* arg = argp;
	// In exit mode the thread calls nothing but rpfree, so it has no heap (unless the runtime allocates
	// on its behalf) and its free chain is pushed by the thread exit destructor
	if (arg->mode != REMOTE_FREE_EXIT)
		rpmalloc_thread_initialize();
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[iblock]);
	if (arg->mode == REMOTE_FREE_COLLECT) {
		atomic_store(&arg->state, 1);
		while (atomic_load(&arg->state) != 2)
			thread_yield();
		rpmalloc_thread_collect();
		atomic_store(&arg->state, 3);
		while (atomic_load(&arg->state) != 4)
			thread_yield();
	}
	if (arg->mode != REMOTE_FREE_EXIT)
		rpmalloc_thread_finalize();
	thread_exit(0);
}

static int
test_remote_free_flush(void) {
	// A thread freeing blocks into pages of another heap links consecutive frees into the same page in a
	// thread local chain, invisible to the owner until pushed. Verify the chain is pushed at the thread
	// exit destructor (for a thread without a heap), at rpmalloc_thread_finalize and at
	// rpmalloc_thread_collect, that a chain is pushed once it holds THREAD_FREE_CHAIN_LIMIT_BYTES of blocks,
	// that frees into medium large pages are not chained and that the owner reuses the blocks. The
	// outstanding block count is only available with leak detection, otherwise only the free paths are
	// exercised
	// Start from unmapped heaps and pages, so no page holds aligned blocks from earlier tests, which take
	// the aligned free path instead of being chained. The override build cannot unmap at finalize, and it
	// has no outstanding block count to check either
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	enum { BLOCKS = 4000 };
	static void* block[BLOCKS];
	static void* ordered[BLOCKS];
	const size_t block_size[3] = {64, 6000, 33000};
	// Blocks of 64 bytes are in small pages (64KiB), blocks of 6000 bytes in medium small pages (1MiB) and
	// blocks of 33000 bytes in medium large pages (4MiB), all aligned to their size
	const uintptr_t page_mask[3] = {~(uintptr_t)(64 * 1024 - 1), ~(uintptr_t)(1024 * 1024 - 1),
	                                ~(uintptr_t)(4 * 1024 * 1024 - 1)};
	for (int mode = REMOTE_FREE_EXIT; mode <= REMOTE_FREE_COLLECT; ++mode) {
		for (unsigned int isize = 0; isize < 3; ++isize) {
			// Several pages of each size, so the frees switch pages and the owner marks pages full
			size_t count = (isize == 0) ? BLOCKS : (BLOCKS / 16);
			long long baseline = test_outstanding_blocks();
			for (size_t iblock = 0; iblock < count; ++iblock) {
				block[iblock] = rpmalloc(block_size[isize]);
				if (!block[iblock])
					return test_fail("Allocation failed in remote free test");
				memset(block[iblock], 0x5a, block_size[isize]);
			}
			if ((baseline >= 0) && (test_outstanding_blocks() != (baseline + (long long)count)))
				return test_fail("Outstanding block count does not include allocations");

			long long held = 0;
			if (mode == REMOTE_FREE_COLLECT) {
				// End the frees with a run of REMOTE_FREE_RUN frees into one page. The other blocks of that
				// page are freed first and the blocks of the other pages in between, so the run starts a new
				// chain after its first block and leaves a known number of blocks in the chain
				uintptr_t run_page = 0;
				for (size_t iblock = 0; (iblock < count) && !run_page; ++iblock) {
					uintptr_t page = (uintptr_t)block[iblock] & page_mask[isize];
					size_t in_page = 0;
					for (size_t jblock = 0; jblock < count; ++jblock)
						in_page += (((uintptr_t)block[jblock] & page_mask[isize]) == page) ? 1 : 0;
					if ((in_page >= REMOTE_FREE_RUN) && (in_page < count))
						run_page = page;
				}
				if (!run_page)
					return test_fail("No page holds a run of blocks in remote free test");
				size_t ordered_count = 0;
				for (int pass = 0; pass < 3; ++pass) {
					size_t in_page = 0;
					for (size_t iblock = 0; iblock < count; ++iblock) {
						int is_run_page = (((uintptr_t)block[iblock] & page_mask[isize]) == run_page);
						int in_run = is_run_page && (in_page++ < REMOTE_FREE_RUN);
						if (((pass == 0) && is_run_page && !in_run) || ((pass == 1) && !is_run_page) ||
						    ((pass == 2) && in_run))
							ordered[ordered_count++] = block[iblock];
					}
				}
				memcpy(block, ordered, count * sizeof(void*));
				// The first block of the run is pushed at once, the rest are chained and pushed whenever the
				// chain holds THREAD_FREE_CHAIN_LIMIT_BYTES of blocks, which a small page never fills. Frees into
				// a medium large page are all pushed at once
				held = (isize < 2) ? (REMOTE_FREE_RUN - 1) : 0;
#if THREAD_FREE_CHAIN_LIMIT_BYTES
				long long class_size = (long long)rpmalloc_usable_size(block[0]);
				long long chain_max = (THREAD_FREE_CHAIN_LIMIT_BYTES + class_size - 1) / class_size;
				held %= chain_max;
#endif
			}

			remote_free_arg_t arg;
			arg.blocks = block;
			arg.count = count;
			arg.mode = mode;
			atomic_init(&arg.state, 0);
			thread_arg targ;
			targ.fn = remote_free_thread;
			targ.arg = &arg;
			uintptr_t thread = thread_run(&targ);
			if (!thread)
				return test_fail("Failed to start thread in remote free test");
			if (mode == REMOTE_FREE_COLLECT) {
				while (atomic_load(&arg.state) != 1)
					thread_yield();
				// The end of the run of frees into the last page is held in the freeing thread's chain
				if ((baseline >= 0) && (test_outstanding_blocks() != (baseline + held)))
					return test_fail("Remote free chain does not hold the expected blocks before collect");
				atomic_store(&arg.state, 2);
				while (atomic_load(&arg.state) != 3)
					thread_yield();
				if ((baseline >= 0) && (test_outstanding_blocks() != baseline))
					return test_fail("Remote free chain not pushed by thread collect");
				atomic_store(&arg.state, 4);
			}
			if (thread_join(thread) != 0)
				return test_fail("Thread failed in remote free test");
			if ((baseline >= 0) && (test_outstanding_blocks() != baseline))
				return test_fail("Remote free chain not pushed at thread exit or finalize");

			// The owner picks the blocks up again
			for (size_t iblock = 0; iblock < count; ++iblock) {
				block[iblock] = rpmalloc(block_size[isize]);
				if (!block[iblock])
					return test_fail("Allocation failed in remote free test");
			}
			for (size_t iblock = 0; iblock < count; ++iblock)
				rpfree(block[iblock]);
			if ((baseline >= 0) && (test_outstanding_blocks() != baseline))
				return test_fail("Outstanding block count does not match after local frees");
		}
	}

	rpmalloc_finalize();
	printf("Remote free flush tests passed\n");
	return 0;
}

#if RPMALLOC_FIRST_CLASS_HEAPS

typedef struct released_heap_arg_t {
	void** blocks;
	size_t count;
	atomic_int state;
} released_heap_arg_t;

static void
released_heap_thread(void* argp) {
	released_heap_arg_t* arg = argp;
	// The thread takes the released first class heap from the heap queue as its thread heap
	rpmalloc_thread_initialize();
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		arg->blocks[iblock] = rpmalloc(64);
	atomic_store(&arg->state, 1);
	while (atomic_load(&arg->state) != 2)
		thread_yield();
	rpmalloc_thread_finalize();
	thread_exit(0);
}

#endif

static int
test_released_heap_batching(void) {
	// A released first class heap is taken over as the heap of the next thread that needs one. The first class
	// heap functions must not be called on it once released, so cross-thread frees into its pages are chained as
	// for any thread heap. Starts from unmapped heaps so the released heap is the only one in the heap queue,
	// skipped when another heap is taken over (the override build cannot unmap at finalize)
#if RPMALLOC_FIRST_CLASS_HEAPS
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	// The first class heap allocates from another size class than the thread taking it over, a page initialized
	// while the heap was first class keeps its frees unchained until it is initialized again
	rpmalloc_heap_t* heap = rpmalloc_heap_acquire();
	rpmalloc_heap_free(heap, rpmalloc_heap_alloc(heap, 1000));
	rpmalloc_heap_release(heap);

	enum { BLOCKS = 1000 };
	static void* block[BLOCKS];
	released_heap_arg_t arg;
	arg.blocks = block;
	arg.count = BLOCKS;
	atomic_init(&arg.state, 0);
	thread_arg targ;
	targ.fn = released_heap_thread;
	targ.arg = &arg;
	uintptr_t thread = thread_run(&targ);
	if (!thread)
		return test_fail("Failed to start thread in released heap test");
	while (atomic_load(&arg.state) != 1)
		thread_yield();

	int taken_over = (rpmalloc_get_heap_for_ptr(block[0]) == heap);
	long long baseline = test_outstanding_blocks();
	for (size_t iblock = 0; iblock < BLOCKS; ++iblock)
		rpfree(block[iblock]);
	// The frees after the first one into the page of the last block are held in the chain of this thread, a
	// small page never fills the chain limit
	const uintptr_t page_mask = ~(uintptr_t)(64 * 1024 - 1);
	long long run = 0;
	for (size_t iblock = BLOCKS;
	     (iblock > 0) && (((uintptr_t)block[iblock - 1] & page_mask) == ((uintptr_t)block[BLOCKS - 1] & page_mask));
	     --iblock)
		++run;
	long long outstanding = test_outstanding_blocks();
	rpmalloc_thread_collect();
	long long collected = test_outstanding_blocks();

	atomic_store(&arg.state, 2);
	if (thread_join(thread) != 0)
		return test_fail("Thread failed in released heap test");

	if (!taken_over) {
		printf("Released heap batching test skipped (another heap taken over)\n");
	} else if (baseline >= 0) {
		if (outstanding != (baseline - BLOCKS + run - 1))
			return test_fail("Cross-thread frees into a released first class heap taken over by a thread not chained");
		if (collected != (baseline - BLOCKS))
			return test_fail("Chain of frees into a released first class heap not pushed by thread collect");
	}
	rpmalloc_finalize();
	printf("Released heap batching tests passed\n");
#endif
	return 0;
}

typedef struct chain_test_arg_t {
	void** blocks;
	size_t count;
	int result;
	atomic_int state;
} chain_test_arg_t;

static void
chain_test_wait(atomic_int* state, int value) {
	while (atomic_load(state) != value)
		thread_yield();
}

//! Check that the blocks are all in one small page (64KiB), as blocks allocated in a row from a heap with no
//  page of their size class yet are
static int
chain_test_one_page(void** block, size_t count) {
	const uintptr_t page_mask = ~(uintptr_t)(64 * 1024 - 1);
	for (size_t iblock = 0; iblock < count; ++iblock) {
		if (!block[iblock] || (((uintptr_t)block[iblock] & page_mask) != ((uintptr_t)block[0] & page_mask)))
			return 0;
	}
	return 1;
}

#if RPMALLOC_FIRST_CLASS_HEAPS

static void
heap_switch_thread(void* argp) {
	chain_test_arg_t* arg = argp;
	rpmalloc_thread_initialize();
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[iblock]);
	atomic_store(&arg->state, 1);
	chain_test_wait(&arg->state, 2);
	rpmalloc_heap_thread_set_current(rpmalloc_heap_acquire());
	atomic_store(&arg->state, 3);
	chain_test_wait(&arg->state, 4);
	rpmalloc_heap_thread_set_current(0);
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[arg->count + iblock]);
	atomic_store(&arg->state, 5);
	chain_test_wait(&arg->state, 6);
	// Finalizing needs a heap
	rpmalloc_heap_thread_set_current(rpmalloc_heap_acquire());
	rpmalloc_thread_finalize();
	thread_exit(0);
}

static void
first_class_owner_thread(void* argp) {
	chain_test_arg_t* arg = argp;
	// A first class heap set as the heap of a thread is owned by that thread, frees from other threads into its
	// pages are cross-thread frees. Without an owner thread every free into its pages is a local free
	rpmalloc_thread_initialize();
	rpmalloc_heap_thread_set_current(rpmalloc_heap_acquire());
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		arg->blocks[iblock] = rpmalloc(64);
	atomic_store(&arg->state, 1);
	chain_test_wait(&arg->state, 2);
	// The heap picks the blocks up again
	for (size_t iblock = 0; iblock < arg->count; ++iblock) {
		arg->blocks[iblock] = rpmalloc(64);
		if (!arg->blocks[iblock])
			arg->result = -1;
	}
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[iblock]);
	rpmalloc_thread_finalize();
	thread_exit(0);
}

#endif

static int
test_heap_switch_flush(void) {
	// A thread switching heap with rpmalloc_heap_thread_set_current pushes its free chain, and a thread with a
	// null heap has no flush points and does not chain. Starts from unmapped heaps so the blocks the main
	// thread allocates are in one page (the override build cannot unmap at finalize)
#if RPMALLOC_FIRST_CLASS_HEAPS
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	enum { RUN = 96 };
	static void* block[2 * RUN];
	long long baseline = test_outstanding_blocks();
	for (size_t iblock = 0; iblock < 2 * RUN; ++iblock)
		block[iblock] = rpmalloc(64);
	if ((baseline >= 0) && !chain_test_one_page(block, 2 * RUN))
		return test_fail("Blocks not allocated in one page in heap switch test");

	chain_test_arg_t arg;
	arg.blocks = block;
	arg.count = RUN;
	arg.result = 0;
	atomic_init(&arg.state, 0);
	thread_arg targ;
	targ.fn = heap_switch_thread;
	targ.arg = &arg;
	uintptr_t thread = thread_run(&targ);
	if (!thread)
		return test_fail("Failed to start thread in heap switch test");
	// The first free of the run is pushed at once, the rest are held in the chain
	chain_test_wait(&arg.state, 1);
	long long held = test_outstanding_blocks();
	atomic_store(&arg.state, 2);
	chain_test_wait(&arg.state, 3);
	long long switched = test_outstanding_blocks();
	atomic_store(&arg.state, 4);
	chain_test_wait(&arg.state, 5);
	long long unbatched = test_outstanding_blocks();
	atomic_store(&arg.state, 6);
	if (thread_join(thread) != 0)
		return test_fail("Thread failed in heap switch test");

	if (baseline >= 0) {
		if (held != (baseline + (2 * RUN) - 1))
			return test_fail("Cross-thread frees into one page not held in the free chain");
		if (switched != (baseline + RUN))
			return test_fail("Free chain not pushed when switching the thread heap");
		if (unbatched != baseline)
			return test_fail("Cross-thread frees chained by a thread with a null heap");
	}
	rpmalloc_finalize();
	printf("Heap switch chain flush tests passed\n");
#endif
	return 0;
}

static int
test_first_class_unbatched(void) {
	// Cross-thread frees into the pages of a first class heap are not chained, rpmalloc_heap_free_all unmaps the
	// pages regardless of the blocks other threads hold. Starts from unmapped heaps so the first class heap is
	// pristine and gives one page to the blocks (the override build cannot unmap at finalize)
#if RPMALLOC_FIRST_CLASS_HEAPS
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	enum { RUN = 96 };
	static void* block[RUN];
	chain_test_arg_t arg;
	arg.blocks = block;
	arg.count = RUN;
	arg.result = 0;
	atomic_init(&arg.state, 0);
	long long baseline = test_outstanding_blocks();
	thread_arg targ;
	targ.fn = first_class_owner_thread;
	targ.arg = &arg;
	uintptr_t thread = thread_run(&targ);
	if (!thread)
		return test_fail("Failed to start thread in first class heap free test");
	chain_test_wait(&arg.state, 1);
	if ((baseline >= 0) && !chain_test_one_page(block, RUN))
		return test_fail("Blocks not allocated in one page in first class heap free test");
	long long allocated = test_outstanding_blocks();
	for (size_t iblock = 0; iblock < RUN; ++iblock)
		rpfree(block[iblock]);
	long long freed = test_outstanding_blocks();
	atomic_store(&arg.state, 2);
	if (thread_join(thread) != 0)
		return test_fail("Thread failed in first class heap free test");
	long long reused = test_outstanding_blocks();

	if (arg.result)
		return test_fail("Allocation failed in first class heap free test");
	if (baseline >= 0) {
		if (allocated != (baseline + RUN))
			return test_fail("Outstanding block count does not include first class heap allocations");
		if (freed != baseline)
			return test_fail("Cross-thread frees into a first class heap page held in a free chain");
		if (reused != baseline)
			return test_fail("Outstanding block count does not match after first class heap reuse");
	}
	rpmalloc_finalize();
	printf("First class heap unbatched free tests passed\n");
#endif
	return 0;
}

static void
takeover_owner_thread(void* argp) {
	chain_test_arg_t* arg = argp;
	rpmalloc_thread_initialize();
	for (size_t iblock = 0; iblock < 2 * arg->count; ++iblock)
		arg->blocks[iblock] = rpmalloc(64);
	atomic_store(&arg->state, 1);
	chain_test_wait(&arg->state, 2);
	rpmalloc_thread_finalize();
	thread_exit(0);
}

static void
takeover_free_thread(void* argp) {
	chain_test_arg_t* arg = argp;
	// Frees without a heap, holding all but the first block of the run in the chain
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[iblock]);
	atomic_store(&arg->state, 3);
	chain_test_wait(&arg->state, 4);
	// Takes over the heap the owner released, which holds the chain page, so the following frees into the page
	// are frees into the own heap
	rpmalloc_thread_initialize();
	for (size_t iblock = 0; iblock < arg->count; ++iblock)
		rpfree(arg->blocks[arg->count + iblock]);
	atomic_store(&arg->state, 5);
	chain_test_wait(&arg->state, 6);
	rpmalloc_thread_collect();
	atomic_store(&arg->state, 7);
	chain_test_wait(&arg->state, 8);
	// Every block is handed out once, a block handed out twice has its index overwritten
	size_t total = 2 * arg->count;
	for (size_t iblock = 0; iblock < total; ++iblock) {
		arg->blocks[iblock] = rpmalloc(64);
		if (arg->blocks[iblock])
			*(size_t*)arg->blocks[iblock] = iblock;
	}
	for (size_t iblock = 0; iblock < total; ++iblock) {
		if (!arg->blocks[iblock] || (*(size_t*)arg->blocks[iblock] != iblock))
			arg->result = -1;
	}
	for (size_t iblock = 0; iblock < total; ++iblock)
		rpfree(arg->blocks[iblock]);
	rpmalloc_thread_finalize();
	thread_exit(0);
}

static int
test_heap_takeover_held_chain(void) {
	// A thread without a heap holds blocks of a page of another thread heap in its free chain when the owner
	// thread exits and the freeing thread takes the released heap over. The chained blocks are still counted
	// as used by the page, now of the own heap, and are pushed to it at the next flush point. Starts from
	// unmapped heaps so the released heap is the only one in the heap queue, skipped when another heap is taken
	// over (the override build cannot unmap at finalize)
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	enum { RUN = 96 };
	static void* block[2 * RUN];
	chain_test_arg_t arg;
	arg.blocks = block;
	arg.count = RUN;
	arg.result = 0;
	atomic_init(&arg.state, 0);
	long long baseline = test_outstanding_blocks();
	thread_arg owner_targ;
	owner_targ.fn = takeover_owner_thread;
	owner_targ.arg = &arg;
	uintptr_t owner = thread_run(&owner_targ);
	if (!owner)
		return test_fail("Failed to start owner thread in heap takeover test");
	chain_test_wait(&arg.state, 1);
	if ((baseline >= 0) && !chain_test_one_page(block, 2 * RUN))
		return test_fail("Blocks not allocated in one page in heap takeover test");

	thread_arg free_targ;
	free_targ.fn = takeover_free_thread;
	free_targ.arg = &arg;
	uintptr_t freer = thread_run(&free_targ);
	if (!freer)
		return test_fail("Failed to start freeing thread in heap takeover test");
	chain_test_wait(&arg.state, 3);
	long long held = test_outstanding_blocks();
	atomic_store(&arg.state, 2);
	if (thread_join(owner) != 0)
		return test_fail("Owner thread failed in heap takeover test");
	atomic_store(&arg.state, 4);
	chain_test_wait(&arg.state, 5);
	long long taken = test_outstanding_blocks();
	atomic_store(&arg.state, 6);
	chain_test_wait(&arg.state, 7);
	long long collected = test_outstanding_blocks();
	atomic_store(&arg.state, 8);
	if (thread_join(freer) != 0)
		return test_fail("Freeing thread failed in heap takeover test");
	long long reused = test_outstanding_blocks();

	if (arg.result)
		return test_fail("Block handed out twice after a heap takeover with a held free chain");
	// Without the takeover the frees after it are chained as well, and the outstanding count does not change
	int taken_over = (taken != held);
	if (baseline >= 0) {
		if (held != (baseline + (2 * RUN) - 1))
			return test_fail("Cross-thread frees of a thread without a heap not held in the free chain");
		if (taken_over && (taken != (baseline + RUN - 1)))
			return test_fail("Frees into a page of a heap taken over not freed locally");
		if (collected != baseline)
			return test_fail("Free chain held across a heap takeover not pushed by thread collect");
		if (reused != baseline)
			return test_fail("Outstanding block count does not match after a heap takeover");
	}
	rpmalloc_finalize();
	if ((baseline >= 0) && !taken_over)
		printf("Heap takeover with held chain test skipped (another heap taken over)\n");
	else
		printf("Heap takeover with held chain tests passed\n");
	return 0;
}

#if RPMALLOC_FIRST_CLASS_HEAPS

static void
set_current_unallocated_thread(void* argp) {
	(void)argp;
	rpmalloc_heap_thread_set_current(rpmalloc_heap_acquire());
	rpmalloc_thread_finalize();
	thread_exit(0);
}

typedef struct heap_queue_arg_t {
	int initialized;
	atomic_int* release;
	atomic_int ready;
} heap_queue_arg_t;

static void
heap_queue_thread(void* argp) {
	heap_queue_arg_t* arg = argp;
	rpmalloc_thread_initialize();
	arg->initialized = rpmalloc_is_thread_initialized();
	atomic_store(&arg->ready, 1);
	while (!atomic_load(arg->release))
		thread_yield();
	rpmalloc_thread_finalize();
	thread_exit(0);
}

#endif

static int
test_heap_set_current_unallocated(void) {
	// A thread that has not allocated has the default heap as its heap. Changing its heap with
	// rpmalloc_heap_thread_set_current must not release the default heap to the heap queue, where threads
	// take their heap from. Starts from unmapped heaps so the heap queue is empty (the override build cannot
	// unmap at finalize)
#if RPMALLOC_FIRST_CLASS_HEAPS
	rpmalloc_config_t config = {0};
	config.unmap_on_finalize = 1;
	rpmalloc_initialize_config(0, &config);
	rpmalloc_finalize();
	rpmalloc_initialize(0);

	thread_arg targ;
	targ.fn = set_current_unallocated_thread;
	targ.arg = 0;
	uintptr_t thread = thread_run(&targ);
	if (!thread)
		return test_fail("Failed to start thread in heap set current test");
	if (thread_join(thread) != 0)
		return test_fail("Thread failed in heap set current test");

	// The heap queue holds the first class heap the thread released at finalize, and the default heap had it
	// been released too. Threads alive at the same time take one heap each
	enum { THREADS = 2 };
	atomic_int release;
	atomic_init(&release, 0);
	heap_queue_arg_t arg[THREADS];
	thread_arg queue_targ[THREADS];
	uintptr_t queue_thread[THREADS];
	for (int ithread = 0; ithread < THREADS; ++ithread) {
		arg[ithread].initialized = 0;
		arg[ithread].release = &release;
		atomic_init(&arg[ithread].ready, 0);
		queue_targ[ithread].fn = heap_queue_thread;
		queue_targ[ithread].arg = &arg[ithread];
		queue_thread[ithread] = thread_run(&queue_targ[ithread]);
		if (!queue_thread[ithread])
			return test_fail("Failed to start thread in heap set current test");
		while (!atomic_load(&arg[ithread].ready))
			thread_yield();
	}
	atomic_store(&release, 1);
	for (int ithread = 0; ithread < THREADS; ++ithread) {
		if (thread_join(queue_thread[ithread]) != 0)
			return test_fail("Thread failed in heap set current test");
	}
	for (int ithread = 0; ithread < THREADS; ++ithread) {
		if (!arg[ithread].initialized)
			return test_fail("Thread took the default heap from the heap queue");
	}
	rpmalloc_finalize();
	printf("Heap set current without allocation tests passed\n");
#endif
	return 0;
}

static int
test_finalize_twice(void) {
	// Finalizing when not initialized does nothing. A second finalize deleted the thread local key the first one
	// left, zero, which can be the key of another library by then. Only detected when the key created between the
	// two takes slot zero
#if !defined(_WIN32)
	rpmalloc_initialize(0);
	rpmalloc_finalize();
	pthread_key_t other;
	if (pthread_key_create(&other, 0) != 0)
		return test_fail("Failed to create thread local key in finalize test");
	int value = 0;
	pthread_setspecific(other, &value);
	rpmalloc_finalize();
	int intact = (pthread_getspecific(other) == &value) && (pthread_setspecific(other, 0) == 0);
	pthread_key_delete(other);
	if (!intact)
		return test_fail("Finalize when not initialized deleted another thread local key");
	if (other != 0)
		printf("Finalize when not initialized test skipped (key slot zero taken)\n");
	else
		printf("Finalize when not initialized tests passed\n");
#endif
	return 0;
}

static int
test_page_decommit(void) {
	// Free pages above the overflow threshold are decommitted after a delay rather than immediately,
	// rpmalloc_thread_collect forces the decommit. Verify the free pages have been decommitted once
	// collect returns (when statistics are enabled; the snapshot is taken before the frees since a slow
	// run can pass the delay and decommit during the frees) and that blocks served from recommitted
	// pages are still zeroed by rpcalloc.
	rpmalloc_initialize(0);
	enum { BLOCKS = 48 * 16 };
	const size_t size = 4000;  // 4KiB size class, 15 blocks per 64KiB small page, about 50 pages
	static void* block[BLOCKS];
	for (unsigned int i = 0; i < BLOCKS; ++i) {
		block[i] = rpmalloc(size);
		if (!block[i])
			return test_fail("Allocation failed");
		memset(block[i], 0xcd, size);
	}

	rpmalloc_global_statistics_t before;
	rpmalloc_global_statistics(&before);
	for (unsigned int i = 0; i < BLOCKS; ++i)
		rpfree(block[i]);
	rpmalloc_thread_collect();
	rpmalloc_global_statistics_t after;
	rpmalloc_global_statistics(&after);
	const rpmalloc_config_t* config = rpmalloc_config();
	int can_decommit = !config->disable_decommit && (config->page_size < (64 * 1024));
	if (can_decommit && before.mapped && (after.decommitted <= before.decommitted))
		return test_fail("Free pages not decommitted after thread collect");

	for (unsigned int i = 0; i < BLOCKS; ++i) {
		void* ptr = rpcalloc(1, size);
		if (!ptr)
			return test_fail("Allocation failed");
		const unsigned char* bytes = ptr;
		for (size_t ibyte = 0; ibyte < size; ++ibyte) {
			if (bytes[ibyte] != 0)
				return test_fail("Zeroed allocation not zeroed after decommit");
		}
		block[i] = ptr;
	}
	for (unsigned int i = 0; i < BLOCKS; ++i)
		rpfree(block[i]);

	rpmalloc_finalize();
	printf("Page decommit tests passed\n");
	return 0;
}

#if !defined(_WIN32)

// Memory interface failing the next commit or decommit when asked to, used by test_huge_cache. It keeps the
// alignment padding mapped and reports it in the mapped size, so the mapped region extends beyond the requested size
static int test_commit_fail_next;
static int test_decommit_fail_next;
static size_t test_commit_fail_maps;

static void*
test_commit_fail_map(size_t size, size_t alignment, size_t* offset, size_t* mapped_size) {
	size_t map_size = size + alignment;
	void* ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (ptr == MAP_FAILED)
		return 0;
	uintptr_t unalign = alignment ? ((uintptr_t)ptr & (uintptr_t)(alignment - 1)) : 0;
	*offset = unalign ? (alignment - unalign) : 0;
	*mapped_size = map_size;
	++test_commit_fail_maps;
	return pointer_offset(ptr, *offset);
}

static void
test_commit_fail_unmap(void* address, size_t offset, size_t mapped_size) {
	munmap(pointer_offset(address, -(ptrdiff_t)offset), mapped_size);
}

static int
test_commit_fail_commit(void* address, size_t size) {
	(void)address;
	(void)size;
	if (!test_commit_fail_next)
		return 0;
	test_commit_fail_next = 0;
	return 1;
}

static int
test_commit_fail_decommit(void* address, size_t size) {
	if (test_decommit_fail_next) {
		test_decommit_fail_next = 0;
		return 1;
	}
	return madvise(address, size, MADV_DONTNEED);
}

#endif

//! Check the huge allocation statistics are back at the given value once the test has freed its huge blocks, so
//  reuse, growth and shrinking in place account the same committed bytes as the free
static int
test_huge_alloc_restored(size_t huge_alloc) {
	rpmalloc_global_statistics_t stats;
	rpmalloc_global_statistics(&stats);
	return stats.huge_alloc == huge_alloc;
}

static int
test_huge_cache(void) {
	// A freed huge block mapping is cached and reused for a smaller request (of at least a quarter of its size)
	// with the requested usable size, the reused block grows in place within the mapping and is zeroed for calloc.
	// A cached mapping is evicted when cached plus live huge bytes exceed the recent peak of live huge bytes plus
	// the headroom or the cached bytes exceed the cached limit, and unmapped by a collect or the page refill path
	// once idle for an epoch.
	rpmalloc_initialize(0);
	rpmalloc_global_statistics_t baseline;
	rpmalloc_global_statistics(&baseline);
	const size_t mib = 1024 * 1024;
	const size_t page_size = rpmalloc_config()->page_size;

	char* block = rpmalloc(48 * mib);
	if (!block)
		return test_fail("Huge allocation failed");
	memset(block, 0x5a, 48 * mib);
	rpfree(block);

	char* reuse = rpmalloc(16 * mib);
	if (reuse != block)
		return test_fail("Cached huge mapping not reused for a smaller request");
	size_t usable_size = rpmalloc_usable_size(reuse);
	if ((usable_size < (16 * mib)) || (usable_size > ((16 * mib) + page_size)))
		return test_fail("Reused huge mapping has bad usable size");
	memset(reuse, 0x11, 16 * mib);

	char* grown = rprealloc(reuse, 40 * mib);
	if (grown != block)
		return test_fail("Huge block not grown in place within its mapping");
	if (rpmalloc_usable_size(grown) < (40 * mib))
		return test_fail("Grown huge block has bad usable size");
	for (size_t ibyte = 0; ibyte < (16 * mib); ibyte += 4093) {
		if (grown[ibyte] != 0x11)
			return test_fail("Data not preserved by in place huge block growth");
	}
	memset(grown, 0x22, 40 * mib);
	rpfree(grown);

	unsigned char* zeroed = rpcalloc(1, 30 * mib);
	if ((void*)zeroed != (void*)block)
		return test_fail("Cached huge mapping not reused for calloc");
	for (size_t ibyte = 0; ibyte < (30 * mib); ibyte += 509) {
		if (zeroed[ibyte])
			return test_fail("Reused huge mapping not zeroed by calloc");
	}
	rpfree(zeroed);
	if (!test_huge_alloc_restored(baseline.huge_alloc))
		return test_fail("Huge allocation statistics not restored after freeing the reused and grown blocks");

	// The cache holds both freed mappings (48MiB and 100MiB address range). The 300MiB block fits neither and
	// raises live plus cached bytes over the peak plus headroom, so the least recently freed mappings are unmapped
	char* first = rpmalloc(100 * mib);
	if (!first)
		return test_fail("Huge allocation failed");
	memset(first, 0x33, 100 * mib);
	rpfree(first);
	rpmalloc_global_statistics_t before;
	rpmalloc_global_statistics(&before);
	char* second = rpmalloc(300 * mib);
	if (!second)
		return test_fail("Huge allocation failed");
	rpmalloc_global_statistics_t after;
	rpmalloc_global_statistics(&after);
	if (before.mapped && (after.mapped > (before.mapped + (200 * mib))))
		return test_fail("Cached huge mapping not evicted over the budget");
	rpfree(second);

	// Idle for an epoch, a collect unmaps the cached mappings
	rpmalloc_global_statistics(&before);
	thread_sleep(1100);
	rpmalloc_thread_collect();
	rpmalloc_global_statistics(&after);
	if (before.mapped && ((after.mapped + (300 * mib)) > before.mapped))
		return test_fail("Idle cached huge mapping not unmapped by collect");

	// Idle for an epoch, the page refill path of small block allocations unmaps the cached mapping without a
	// collect or a huge block allocation. The small block pages are mapped up front and freed, so the refills
	// reuse them and the mapped size only changes by the unmapped huge mapping
	enum { SMALL_COUNT = 4096 };
	static void* small[SMALL_COUNT];
	for (int pass = 0; pass < 2; ++pass) {
		if (pass) {
			block = rpmalloc(100 * mib);
			if (!block)
				return test_fail("Huge allocation failed");
			rpfree(block);
			rpmalloc_global_statistics(&before);
			thread_sleep(1100);
		}
		for (size_t iblock = 0; iblock < SMALL_COUNT; ++iblock) {
			small[iblock] = rpmalloc(1000);
			if (!small[iblock])
				return test_fail("Small allocation failed");
		}
		if (pass)
			rpmalloc_global_statistics(&after);
		for (size_t iblock = 0; iblock < SMALL_COUNT; ++iblock)
			rpfree(small[iblock]);
	}
	if (before.mapped && ((after.mapped + (100 * mib)) > before.mapped))
		return test_fail("Idle cached huge mapping not unmapped by the page refill path");

	// A reused mapping decommits its tail beyond twice the request, a later calloc reusing it commits part of the
	// tail again, which must read as zero along with the cleared previously committed pages
	char* dirty = rpmalloc(64 * mib);
	if (!dirty)
		return test_fail("Huge allocation failed");
	memset(dirty, 0x77, 64 * mib);
	rpfree(dirty);
	char* shrunk = rpmalloc(20 * mib);
	if (shrunk != dirty)
		return test_fail("Cached huge mapping not reused for a smaller request");
	rpfree(shrunk);
	zeroed = rpcalloc(1, 56 * mib);
	if ((void*)zeroed != (void*)dirty)
		return test_fail("Cached huge mapping not reused for calloc beyond its committed size");
	for (size_t ibyte = 0; ibyte < (56 * mib); ibyte += 509) {
		if (zeroed[ibyte])
			return test_fail("Reused huge mapping not zeroed by calloc beyond its committed size");
	}
	rpfree(zeroed);

	// A block held live through epochs without any huge block allocation or free counts in the peak when freed,
	// and is cached (only the idle cached mappings are unmapped)
	char* held = rpmalloc(200 * mib);
	if (!held)
		return test_fail("Huge allocation failed");
	thread_sleep(2200);
	rpmalloc_global_statistics(&before);
	rpfree(held);
	rpmalloc_global_statistics(&after);
	if (before.mapped && ((after.mapped + (100 * mib)) < before.mapped))
		return test_fail("Huge block held through idle epochs not cached when freed");

	// Shrinking a huge block to less than half its size keeps the block in place with the requested usable size
	// and its data, and decommits the committed pages beyond twice the new size
	char* large = rpmalloc(120 * mib);
	if (!large)
		return test_fail("Huge allocation failed");
	memset(large, 0x44, 120 * mib);
	rpmalloc_global_statistics(&before);
	char* shrunk_large = rprealloc(large, 20 * mib);
	rpmalloc_global_statistics(&after);
	if (shrunk_large != large)
		return test_fail("Huge block not shrunk in place");
	usable_size = rpmalloc_usable_size(shrunk_large);
	if ((usable_size < (20 * mib)) || (usable_size > ((20 * mib) + page_size)))
		return test_fail("Huge block shrunk in place has bad usable size");
	for (size_t ibyte = 0; ibyte < (20 * mib); ibyte += 4093) {
		if (shrunk_large[ibyte] != 0x44)
			return test_fail("Data not preserved by in place huge block shrink");
	}
	if (before.huge_alloc && (after.huge_alloc > (before.huge_alloc - (60 * mib))))
		return test_fail("Huge block shrunk in place did not decommit its tail");
	rpfree(shrunk_large);
	if (!test_huge_alloc_restored(baseline.huge_alloc))
		return test_fail("Huge allocation statistics not restored after freeing the block shrunk in place");

	// Freed mappings beyond the cached limit (HUGE_CACHE_CACHED_LIMIT, 1GiB by default) are unmapped even when
	// within the demand budget. The blocks are not touched, so they only reserve address space
	enum { LIMIT_COUNT = 5 };
	char* limit_block[LIMIT_COUNT];
	rpmalloc_global_statistics(&before);
	for (size_t iblock = 0; iblock < LIMIT_COUNT; ++iblock) {
		limit_block[iblock] = rpmalloc(256 * mib);
		if (!limit_block[iblock])
			return test_fail("Huge allocation failed");
	}
	for (size_t iblock = 0; iblock < LIMIT_COUNT; ++iblock)
		rpfree(limit_block[iblock]);
	rpmalloc_global_statistics(&after);
	if (before.mapped && (after.mapped > (before.mapped + (1024 * mib))))
		return test_fail("Cached huge mappings exceed the cached limit");
	if (!test_huge_alloc_restored(baseline.huge_alloc))
		return test_fail("Huge allocation statistics not restored after freeing the blocks beyond the cached limit");

	rpmalloc_finalize();

#if !defined(_WIN32)
	// When committing a reused mapping up to the request fails, the allocation maps a fresh mapping instead of
	// failing. Skipped when the interface is not in use (the allocator was initialized by another allocation)
	rpmalloc_interface_t iface = {0};
	iface.memory_map = test_commit_fail_map;
	iface.memory_unmap = test_commit_fail_unmap;
	iface.memory_commit = test_commit_fail_commit;
	iface.memory_decommit = test_commit_fail_decommit;
	test_commit_fail_next = 0;
	test_commit_fail_maps = 0;
	rpmalloc_initialize_config(&iface, 0);
	block = rpmalloc(48 * mib);
	if (!block)
		return test_fail("Huge allocation failed");
	if (test_commit_fail_maps) {
		rpfree(block);
		reuse = rpmalloc(16 * mib);
		if (reuse != block)
			return test_fail("Cached huge mapping not reused for a smaller request");
		rpfree(reuse);
		size_t maps_before = test_commit_fail_maps;
		test_commit_fail_next = 1;
		char* fallback = rpmalloc(40 * mib);
		if (!fallback)
			return test_fail("Huge allocation failed when committing a reused mapping failed");
		if (test_commit_fail_next || (test_commit_fail_maps != (maps_before + 1)))
			return test_fail("Failed commit of a reused mapping not replaced by a fresh mapping");
		memset(fallback, 0x55, 40 * mib);

		// When decommitting the tail of a reused mapping fails, the block keeps and accounts all its committed
		// pages (the fallback block stays live, leaving the freed mapping the only one in the cache)
		char* wide = rpmalloc(96 * mib);
		if (!wide)
			return test_fail("Huge allocation failed");
		rpfree(wide);
		rpmalloc_global_statistics(&before);
		test_decommit_fail_next = 1;
		char* narrow = rpmalloc(32 * mib);
		rpmalloc_global_statistics(&after);
		if (narrow != wide)
			return test_fail("Cached huge mapping not reused for a smaller request");
		if (test_decommit_fail_next)
			return test_fail("Tail of a reused huge mapping not decommitted");
		usable_size = rpmalloc_usable_size(narrow);
		if ((usable_size < (32 * mib)) || (usable_size > ((32 * mib) + page_size)))
			return test_fail("Reused huge mapping with a failed decommit has bad usable size");
		if (before.huge_alloc && (after.huge_alloc < (before.huge_alloc + (96 * mib))))
			return test_fail("Reused huge mapping with a failed decommit not accounted with all its pages");
		memset(narrow, 0x66, 32 * mib);
		rpfree(narrow);
		rpfree(fallback);
	} else {
		rpfree(block);
		printf("Huge cache commit failure test skipped\n");
	}
	rpmalloc_finalize();
#endif

	// A mapping that cannot decommit is reused only for requests of at least half its committed size and keeps all
	// its committed pages. Skipped when the allocator was initialized by another allocation without the config
	rpmalloc_config_t config = {0};
	config.disable_decommit = 1;
	rpmalloc_initialize_config(0, &config);
	if (rpmalloc_config()->disable_decommit) {
		block = rpmalloc(48 * mib);
		if (!block)
			return test_fail("Huge allocation failed");
		rpfree(block);
		reuse = rpmalloc(16 * mib);
		if (!reuse)
			return test_fail("Huge allocation failed");
		if (reuse == block)
			return test_fail("Huge mapping that cannot decommit reused for a request below half its size");
		rpfree(reuse);
		rpmalloc_global_statistics(&before);
		char* half = rpmalloc(30 * mib);
		rpmalloc_global_statistics(&after);
		if (half != block)
			return test_fail("Huge mapping that cannot decommit not reused for a request of half its size");
		usable_size = rpmalloc_usable_size(half);
		if ((usable_size < (30 * mib)) || (usable_size > ((30 * mib) + page_size)))
			return test_fail("Reused huge mapping that cannot decommit has bad usable size");
		if (after.huge_alloc && (after.huge_alloc < (before.huge_alloc + (48 * mib))))
			return test_fail("Reused huge mapping that cannot decommit not accounted with all its pages");
		memset(half, 0x77, 30 * mib);
		rpfree(half);
	} else {
		printf("Huge cache decommit disabled test skipped\n");
	}
	rpmalloc_finalize();

	printf("Huge cache tests passed\n");
	return 0;
}

extern int
test_malloc(int print_log);

extern int
test_free(int print_log);

extern int
test_malloc_thread(void);

int
test_run(int argc, char** argv) {
	(void)sizeof(argc);
	(void)sizeof(argv);
	test_initialize();

	{
		const char* env_threads = getenv("RPMALLOC_TEST_THREADS");
		if (env_threads) {
			long value = atol(env_threads);
			if (value > 0 && (size_t)value < hardware_threads)
				hardware_threads = (size_t)value;
		}
		const char* env_scale = getenv("RPMALLOC_TEST_SCALE");
		if (env_scale) {
			long value = atol(env_scale);
			if (value > 0 && value <= 100)
				test_loop_scale = (unsigned int)value;
		}
	}
	if (test_alloc())
		return -1;
	if (test_zero_alloc())
		return -1;
	if (test_realloc())
		return -1;
	if (test_mul_overflow())
		return -1;
	if (test_superalign())
		return -1;
	if (test_crossthread())
		return -1;
	if (test_remote_free_flush())
		return -1;
	if (test_released_heap_batching())
		return -1;
	if (test_heap_switch_flush())
		return -1;
	if (test_first_class_unbatched())
		return -1;
	if (test_heap_takeover_held_chain())
		return -1;
	if (test_heap_set_current_unallocated())
		return -1;
	if (test_finalize_twice())
		return -1;
	if (test_threaded())
		return -1;
#if RPMALLOC_TEST_OVERRIDE
	// Standard library override tests, only valid when rpmalloc is built with ENABLE_OVERRIDE
	// (these are linked from main-override.cc and cross-check malloc/new against rpmalloc).
	if (test_malloc(1))
		return -1;
	if (test_free(1))
		return -1;
	if (test_malloc_thread())
		return -1;
#endif
	if (test_threadspam())
		return -1;
	if (test_thread_exit_heap())
		return -1;
	if (test_large_pages())
		return -1;
	if (test_huge_pages())
		return -1;
	if (test_transparent_huge_pages())
		return -1;
	if (test_first_class_heaps())
		return -1;
	if (test_heapless_free())
		return -1;
	if (test_first_class_huge_free())
		return -1;
	if (test_named_pages())
		return -1;
#if !RPMALLOC_TEST_OVERRIDE
	// unmap_on_finalize is a no-op under the standard library override (see rpmalloc.h), so these
	// tests run only in the no-override binary where the option is actually honored.
	if (test_finalize_unmap())
		return -1;
	if (test_heap_packing())
		return -1;
#endif
	if (test_thread_statistics())
		return -1;
	if (test_page_decommit())
		return -1;
	if (test_huge_cache())
		return -1;
	printf("All tests passed\n");
	return 0;
}

#if (defined(__APPLE__) && __APPLE__)
#include <TargetConditionals.h>
#if defined(__IPHONE__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || \
    (defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR)
#define NO_MAIN 1
#endif
#elif (defined(__linux__) || defined(__linux))
#include <sched.h>
#endif

// RPMALLOC_TEST_FORCE_MAIN lets CI build a runnable console entry on platforms that otherwise
// suppress it (e.g. the iOS simulator, where the test is launched via `simctl spawn`).
#if !defined(NO_MAIN) || defined(RPMALLOC_TEST_FORCE_MAIN)

int
main(int argc, char** argv) {
	return test_run(argc, argv);
}

#endif

#ifdef _WIN32
#include <windows.h>

static void
test_initialize(void) {
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	hardware_threads = (size_t)system_info.dwNumberOfProcessors;
}

#elif (defined(__linux__) || defined(__linux))

static void
test_initialize(void) {
	cpu_set_t prevmask, testmask;
	CPU_ZERO(&prevmask);
	CPU_ZERO(&testmask);
	sched_getaffinity(0, sizeof(prevmask), &prevmask);  // Get current mask
	sched_setaffinity(0, sizeof(testmask), &testmask);  // Set zero mask
	sched_getaffinity(0, sizeof(testmask), &testmask);  // Get mask for all CPUs
	sched_setaffinity(0, sizeof(prevmask), &prevmask);  // Reset current mask
	int num = CPU_COUNT(&testmask);
	hardware_threads = (size_t)(num > 1 ? num : 1);
}

#else

static void
test_initialize(void) {
	hardware_threads = 1;
}

#endif
