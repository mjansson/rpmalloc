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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#if !defined(_WIN32)
#include <sys/mman.h>
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

	// Query the small granularity
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
	int is_64bit = (sizeof(size_t) > 4);
	if (is_64bit) {
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
	arg.loops = 100;
	arg.passes = 4000;
#else
	arg.loops = 30;
	arg.passes = 1000;
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
		arg[ithread].loops = 50;
		arg[ithread].passes = 1024;
#else
		arg[ithread].loops = 20;
		arg[ithread].passes = 200;
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

	arg.loops = 500;
	arg.passes = 10;
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
		arg[i].loops = 100;
		arg[i].passes = 4000;
#else
		arg[i].loops = 50;
		arg[i].passes = 1000;
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
	// Verify zeroed allocations survive the free page commit/decommit cycles
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
		arg.loops = 30;
		arg.passes = 8;
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
	if (test_alloc())
		return -1;
	if (test_realloc())
		return -1;
	if (test_superalign())
		return -1;
	if (test_crossthread())
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
	if (test_large_pages())
		return -1;
	if (test_huge_pages())
		return -1;
	if (test_transparent_huge_pages())
		return -1;
	if (test_first_class_heaps())
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

#if !defined(NO_MAIN)

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
