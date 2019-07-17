
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <rpmalloc.h>
#include <thread.h>
#include <test.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

static size_t _hardware_threads;

static void
test_initialize(void);

static int
test_fail(const char* reason) {
	fprintf(stderr, "FAIL: %s\n", reason);
	return -1;
}

static int
test_alloc(void) {
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[8142];
	char data[20000];
	unsigned int datasize[7] = { 473, 39, 195, 24, 73, 376, 245 };

	rpmalloc_initialize();

	for (id = 0; id < 20000; ++id)
		data[id] = (char)(id % 139 + id % 17);

	//Verify that blocks are 16 byte size aligned
	void* testptr = rpmalloc(16);
	if (rpmalloc_usable_size(testptr) != 16)
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(32);
	if (rpmalloc_usable_size(testptr) != 32)
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	testptr = rpmalloc(128);
	if (rpmalloc_usable_size(testptr) != 128)
		return test_fail("Bad base alloc usable size");
	rpfree(testptr);
	for (iloop = 0; iloop < 1000; ++iloop) {
		testptr = rpmalloc(iloop);
		size_t wanted_usable_size = 16 * ((iloop / 16) + ((!iloop || (iloop % 16)) ? 1 : 0));
		if (rpmalloc_usable_size(testptr) != wanted_usable_size)
			return test_fail("Bad base alloc usable size");
		rpfree(testptr);
	}

	//Large reallocation test
	testptr = rpmalloc(253000);
	testptr = rprealloc(testptr, 151);
	if (rpmalloc_usable_size(testptr) != 160)
		return test_fail("Bad usable size");
	if (rpmalloc_usable_size(pointer_offset(testptr, 16)) != 144)
		return test_fail("Bad offset usable size");
	rpfree(testptr);

	//Reallocation tests
	for (iloop = 1; iloop < 24; ++iloop) {
		size_t size = 37 * iloop;
		testptr = rpmalloc(size);
		*((uintptr_t*)testptr) = 0x12345678;
		size_t wanted_usable_size = 16 * ((size / 16) + ((size % 16) ? 1 : 0));
		if (rpmalloc_usable_size(testptr) != wanted_usable_size)
			return test_fail("Bad usable size (alloc)");
		testptr = rprealloc(testptr, size + 16);
		if (rpmalloc_usable_size(testptr) < (wanted_usable_size + 16))
			return test_fail("Bad usable size (realloc)");
		if (*((uintptr_t*)testptr) != 0x12345678)
			return test_fail("Data not preserved on realloc");
		rpfree(testptr);

		testptr = rpaligned_alloc(128, size);
		*((uintptr_t*)testptr) = 0x12345678;
		wanted_usable_size = 16 * ((size / 16) + ((size % 16) ? 1 : 0));
		if (rpmalloc_usable_size(testptr) < wanted_usable_size)
			return test_fail("Bad usable size (aligned alloc)");
		if (rpmalloc_usable_size(testptr) > (wanted_usable_size + 128))
			return test_fail("Bad usable size (aligned alloc)");
		testptr = rpaligned_realloc(testptr, 128, size + 32, 0, 0);
		if (rpmalloc_usable_size(testptr) < (wanted_usable_size + 32))
			return test_fail("Bad usable size (aligned realloc)");
		if (*((uintptr_t*)testptr) != 0x12345678)
			return test_fail("Data not preserved on realloc");
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

	static size_t alignment[3] = { 0, 64, 256 };
	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8142; ++ipass) {
			size_t size = iloop + ipass + datasize[(iloop + ipass) % 7];
			char* baseptr = rpaligned_alloc(alignment[ipass % 3], size);
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
			capsize = (capsize > alignsize ? alignsize : capsize);
			baseptr = rpaligned_realloc(baseptr, 128, alignsize, resize, 0);
			for (size_t ibyte = 0; ibyte < capsize; ++ibyte) {
				if (baseptr[ibyte] != (char)(ibyte & 0xFF))
					return test_fail("Data not preserved on realloc");
			}

			rpfree(baseptr);
		}
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8142; ++ipass) {
			addr[ipass] = rpmalloc(500);
			if (addr[ipass] == 0)
				return test_fail("Allocation failed");

			memcpy(addr[ipass], data + ipass, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return test_fail("Bad allocation result");
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], 500) > addr[ipass])
						return test_fail("Bad allocation result");
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], 500) > addr[icheck])
						return test_fail("Bad allocation result");
				}
			}
		}

		for (ipass = 0; ipass < 8142; ++ipass) {
			if (memcmp(addr[ipass], data + ipass, 500))
				return test_fail("Data corruption");
		}

		for (ipass = 0; ipass < 8142; ++ipass)
			rpfree(addr[ipass]);
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass%7] + ipass;

			addr[ipass] = rpmalloc(cursize);
			if (addr[ipass] == 0)
				return test_fail("Allocation failed");

			memcpy(addr[ipass], data + ipass, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return test_fail("Identical pointer returned from allocation");
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], rpmalloc_usable_size(addr[icheck])) > addr[ipass])
						return test_fail("Invalid pointer inside another block returned from allocation");
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], rpmalloc_usable_size(addr[ipass])) > addr[icheck])
						return test_fail("Invalid pointer inside another block returned from allocation");
				}
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass%7] + ipass;
			if (memcmp(addr[ipass], data + ipass, cursize))
				return test_fail("Data corruption");
		}

		for (ipass = 0; ipass < 1024; ++ipass)
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
				}
				else if (addr[icheck] > addr[ipass]) {
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
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = 2048; iloop < (64 * 1024); iloop += 512) {
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = (64 * 1024); iloop < (2 * 1024 * 1024); iloop += 4096) {
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	rpmalloc_initialize();
	for (iloop = 0; iloop < (2 * 1024 * 1024); iloop += 16) {
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return test_fail("Allocation failed");
		rpfree(addr[0]);
	}
	rpmalloc_finalize();

	printf("Memory allocation tests passed\n");

	return 0;
}

static int
test_superalign(void) {

	rpmalloc_initialize();

	size_t alignment[] = { 2048, 4096, 8192, 16384, 32768 };
	size_t sizes[] = { 187, 1057, 2436, 5234, 9235, 17984, 35783, 72436 };

	for (size_t ipass = 0; ipass < 8; ++ipass) {
		for (size_t iloop = 0; iloop < 4096; ++iloop) {
			for (size_t ialign = 0, asize = sizeof(alignment) / sizeof(alignment[0]); ialign < asize; ++ialign) {
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

typedef struct _allocator_thread_arg {
	unsigned int        loops;
	unsigned int        passes; //max 4096
	unsigned int        datasize[32];
	unsigned int        num_datasize; //max 32
	void**              pointers;
	void**              crossthread_pointers;
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

	for (iloop = 0; iloop < arg.loops; ++iloop) {
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
				}
				else if (addr[icheck] > addr[ipass]) {
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
	}

	rpfree(data);
	rpfree(addr);

	rpmalloc_thread_finalize();

end:
	thread_exit((uintptr_t)ret);
}

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
			cursize = arg.datasize[iarg] + ((iloop + ipass) % 21);
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
			cursize = arg.datasize[iarg] + ((iloop + ipass) % 17);
			void* third_addr = rpmalloc(cursize);
			if (third_addr == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			rpfree(first_addr);
			arg.pointers[iloop * arg.passes + ipass] = second_addr;
			extra_pointers[iloop * arg.passes + ipass] = third_addr;

			while ((next_crossthread < end_crossthread) &&
			        arg.crossthread_pointers[next_crossthread]) {
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

	while (next_crossthread < end_crossthread) {
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
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[4096];
	char data[8192];
	unsigned int cursize;
	unsigned int iwait = 0;
	int ret = 0;

	for (id = 0; id < 8192; ++id)
		data[id] = (char)id;

	thread_yield();

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		rpmalloc_thread_initialize();

		unsigned int max_datasize = 0;
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + ((iloop + ipass) % 1024);
			if (cursize > max_datasize)
				max_datasize = cursize;

			addr[ipass] = rpmalloc(4 + cursize);
			if (addr[ipass] == 0) {
				ret = test_fail("Allocation failed");
				goto end;
			}

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				size_t this_size = *(uint32_t*)addr[ipass];
				size_t check_size = *(uint32_t*)addr[icheck];
				if (this_size != cursize) {
					ret = test_fail("Data corrupted in this block (size)");
					goto end;
				}
				if (check_size > max_datasize) {
					ret = test_fail("Data corrupted in previous block (size)");
					goto end;
				}
				if (addr[icheck] == addr[ipass]) {
					ret = test_fail("Identical pointer returned from allocation");
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], check_size + 4) > addr[ipass]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], cursize + 4) > addr[icheck]) {
						ret = test_fail("Invalid pointer inside another block returned from allocation");
						goto end;
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];
			if (cursize > max_datasize) {
				ret = test_fail("Data corrupted (size)");
				goto end;
			}

			if (memcmp(pointer_offset(addr[ipass], 4), data, cursize)) {
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
test_threaded(void) {
	uintptr_t thread[32];
	uintptr_t threadres[32];
	unsigned int i;
	size_t num_alloc_threads;
	allocator_thread_arg_t arg;

	rpmalloc_initialize();

	num_alloc_threads = _hardware_threads;
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
	arg.loops = 100;
	arg.passes = 4000;

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

	printf("Memory threaded tests passed\n");

	return 0;
}

static int 
test_crossthread(void) {
	uintptr_t thread[8];
	allocator_thread_arg_t arg[8];
	thread_arg targ[8];

	rpmalloc_initialize();

	size_t num_alloc_threads = _hardware_threads;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
	if (num_alloc_threads > 4)
		num_alloc_threads = 4;

	for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread) {
		unsigned int iadd = (ithread * (16 + ithread) + ithread) % 128;
		arg[ithread].loops = 50;
		arg[ithread].passes = 1024;
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
		arg[ithread].datasize[9] = 39723 + iadd;
		arg[ithread].datasize[10] = 15 + iadd;
		arg[ithread].datasize[11] = 493 + iadd;
		arg[ithread].datasize[12] = 34 + iadd;
		arg[ithread].datasize[13] = 894 + iadd;
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

	rpmalloc_finalize();

	printf("Memory cross thread free tests passed\n");

	return 0;
}

static int 
test_threadspam(void) {
	uintptr_t thread[64];
	uintptr_t threadres[64];
	unsigned int i, j;
	size_t num_passes, num_alloc_threads;
	allocator_thread_arg_t arg;

	rpmalloc_initialize();

	num_passes = 100;
	num_alloc_threads = _hardware_threads;
	if (num_alloc_threads < 2)
		num_alloc_threads = 2;
	if (num_alloc_threads > 64)
		num_alloc_threads = 64;

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
		thread_sleep(10);
		thread_fence();

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

int
test_run(int argc, char** argv) {
	(void)sizeof(argc);
	(void)sizeof(argv);
	test_initialize();
	if (test_alloc())
		return -1;
	if (test_superalign())
		return -1;
	if (test_crossthread())
		return -1;
	if (test_threadspam())
		return -1;
	if (test_threaded())
		return -1;
	printf("All tests passed\n");
	return 0;
}

#if (defined(__APPLE__) && __APPLE__)
#  include <TargetConditionals.h>
#  if defined(__IPHONE__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || (defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR)
#    define NO_MAIN 1
#  endif
#elif (defined(__linux__) || defined(__linux))
#  include <sched.h>
#endif

#if !defined(NO_MAIN)

int
main(int argc, char** argv) {
	return test_run(argc, argv);
}

#endif

#ifdef _WIN32
#include <Windows.h>

static void
test_initialize(void) {
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	_hardware_threads = (size_t)system_info.dwNumberOfProcessors;
}

#elif (defined(__linux__) || defined(__linux))

static void
test_initialize(void) {
	cpu_set_t prevmask, testmask;
	CPU_ZERO(&prevmask);
	CPU_ZERO(&testmask);
	sched_getaffinity(0, sizeof(prevmask), &prevmask);     //Get current mask
	sched_setaffinity(0, sizeof(testmask), &testmask);     //Set zero mask
	sched_getaffinity(0, sizeof(testmask), &testmask);     //Get mask for all CPUs
	sched_setaffinity(0, sizeof(prevmask), &prevmask);     //Reset current mask
	int num = CPU_COUNT(&testmask);
	_hardware_threads = (size_t)(num > 1 ? num : 1);
}

#else

static void
test_initialize(void) {
	_hardware_threads = 1;
}

#endif
