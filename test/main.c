
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

#ifndef ENABLE_GUARDS
#  define ENABLE_GUARDS 0
#endif

#if ENABLE_GUARDS
#ifdef _MSC_VER
#  define PRIsize "Iu"
#else
#  define PRIsize "zu"
#endif
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
//#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

static size_t _hardware_threads;

static void
test_initialize(void);

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

	void* testptr = rpmalloc(253000);
	testptr = rprealloc(testptr, 154);
	rpfree(testptr);

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8142; ++ipass) {
			addr[ipass] = rpmalloc(500);
			if (addr[ipass] == 0)
				return -1;

			memcpy(addr[ipass], data + ipass, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return -1;
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], 500) > addr[ipass])
						return -1;
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], 500) > addr[icheck])
						return -1;
				}
			}
		}

		for (ipass = 0; ipass < 8142; ++ipass) {
			if (memcmp(addr[ipass], data + ipass, 500))
				return -1;
		}

		for (ipass = 0; ipass < 8142; ++ipass)
			rpfree(addr[ipass]);
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass%7] + ipass;

			addr[ipass] = rpmalloc(cursize);
			if (addr[ipass] == 0)
				return -1;

			memcpy(addr[ipass], data + ipass, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return -1;
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], rpmalloc_usable_size(addr[icheck])) > addr[ipass])
						return -1;
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], rpmalloc_usable_size(addr[ipass])) > addr[icheck])
						return -1;
				}
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass%7] + ipass;
			if (memcmp(addr[ipass], data + ipass, cursize))
				return -1;
		}

		for (ipass = 0; ipass < 1024; ++ipass)
			rpfree(addr[ipass]);
	}

	for (iloop = 0; iloop < 128; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			addr[ipass] = rpmalloc(500);
			if (addr[ipass] == 0)
				return -1;

			memcpy(addr[ipass], data + ipass, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass])
					return -1;
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], 500) > addr[ipass])
						return -1;
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], 500) > addr[icheck])
						return -1;
				}
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			if (memcmp(addr[ipass], data + ipass, 500))
				return -1;
		}

		for (ipass = 0; ipass < 1024; ++ipass)
			rpfree(addr[ipass]);
	}

	rpmalloc_finalize();

	for (iloop = 0; iloop < 2048; iloop += 16) {
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return -1;
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = 2048; iloop < (64 * 1024); iloop += 512) {
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return -1;
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	for (iloop = (64 * 1024); iloop < (2 * 1024 * 1024); iloop += 4096) {
		rpmalloc_initialize();
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return -1;
		rpfree(addr[0]);
		rpmalloc_finalize();
	}

	rpmalloc_initialize();
	for (iloop = 0; iloop < (2 * 1024 * 1024); iloop += 16) {
		addr[0] = rpmalloc(iloop);
		if (!addr[0])
			return -1;
		rpfree(addr[0]);
	}
	rpmalloc_finalize();

	printf("Memory allocation tests passed\n");

	return 0;
}

typedef struct _allocator_thread_arg {
	unsigned int        loops;
	unsigned int        passes; //max 4096
	unsigned int        datasize[32];
	unsigned int        num_datasize; //max 32
	void**              pointers;
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
				ret = -1;
				goto end;
			}

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass]) {
					ret = -1;
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass]) {
						if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass]) {
							ret = -1;
							goto end;
						}
					}
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[ipass]) {
						if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[icheck]) {
							ret = -1;
							goto end;
						}
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			if (memcmp(pointer_offset(addr[ipass], 4), data, cursize)) {
				ret = -1;
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
	unsigned int iwait = 0;
	int ret = 0;

	rpmalloc_thread_initialize();

	thread_sleep(10);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = arg.datasize[(iloop + ipass + iwait) % arg.num_datasize ] + ((iloop + ipass) % 1024);

			void* addr = rpmalloc(cursize);
			if (addr == 0) {
				ret = -1;
				goto end;
			}

			arg.pointers[iloop * arg.passes + ipass] = addr;
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

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = 4 + arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + ((iloop + ipass) % 1024);

			addr[ipass] = rpmalloc(4 + cursize);
			if (addr[ipass] == 0) {
				ret = -1;
				goto end;
			}

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				if (addr[icheck] == addr[ipass]) {
					ret = -1;
					goto end;
				}
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass]) {
						if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass]) {
							ret = -1;
							goto end;
						}
					}
				}
				else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[ipass]) {
						if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[icheck]) {
							ret = -1;
							goto end;
						}
					}
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			if (memcmp(pointer_offset(addr[ipass], 4), data, cursize)) {
				ret = -1;
				goto end;
			}
			
			rpfree(addr[ipass]);
		}

		rpmalloc_thread_finalize();
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

	thread_arg targ = { allocator_thread, &arg };
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
		unsigned int iadd = ithread * (16 + ithread);
		arg[ithread].loops = 50;
		arg[ithread].passes = 1024;
		arg[ithread].pointers = rpmalloc(sizeof(void*) * arg[ithread].loops * arg[ithread].passes);
		arg[ithread].datasize[0] = 19 + iadd;
		arg[ithread].datasize[1] = 249 + iadd;
		arg[ithread].datasize[2] = 797 + iadd;
		arg[ithread].datasize[3] = 3 + iadd;
		arg[ithread].datasize[4] = 7923 + iadd;
		arg[ithread].datasize[5] = 344 + iadd;
		arg[ithread].datasize[6] = 3892 + iadd;
		arg[ithread].datasize[7] = 19 + iadd;
		arg[ithread].datasize[8] = 14954 + iadd;
		arg[ithread].datasize[9] = 39723 + iadd;
		arg[ithread].datasize[10] = 15 + iadd;
		arg[ithread].datasize[11] = 493 + iadd;
		arg[ithread].datasize[12] = 34 + iadd;
		arg[ithread].datasize[13] = 894 + iadd;
		arg[ithread].datasize[14] = 6893 + iadd;
		arg[ithread].datasize[15] = 2893 + iadd;
		arg[ithread].num_datasize = 16;

		targ[ithread].fn = crossallocator_thread;
		targ[ithread].arg = &arg[ithread];
	}

	for (int iloop = 0; iloop < 32; ++iloop) {
		for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread)
			thread[ithread] = thread_run(&targ[ithread]);

		thread_sleep(100);

		for (unsigned int ithread = 0; ithread < num_alloc_threads; ++ithread) {
			if (thread_join(thread[ithread]) != 0)
				return -1;

			//Off-thread deallocation
			for (size_t iptr = 0; iptr < arg[ithread].loops * arg[ithread].passes; ++iptr)
				rpfree(arg[ithread].pointers[iptr]);
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

	thread_arg targ = { initfini_thread, &arg };
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

#if ENABLE_GUARDS

static int test_overwrite_detected;

static void
test_overwrite_cb(void* addr) {
	(void)sizeof(addr);
	++test_overwrite_detected;
}

static int
test_overwrite(void) {
	int ret = 0;
	char* addr;
	size_t istep, size;

	rpmalloc_config_t config;
	memset(&config, 0, sizeof(config));
	config.memory_overwrite = test_overwrite_cb;

	rpmalloc_initialize_config(&config);

	for (istep = 0, size = 16; size < 16 * 1024 * 1024; size <<= 1, ++istep) {
		test_overwrite_detected = 0;
		addr = rpmalloc(size);
		*(addr - 2) = 1;
		rpfree(addr);

		if (!test_overwrite_detected) {
			printf("Failed to detect memory overwrite before start of block in step %" PRIsize " size %" PRIsize "\n", istep, size);
			ret = -1;
			goto cleanup;
		}

		test_overwrite_detected = 0;
		addr = rpmalloc(size);
		*(addr + rpmalloc_usable_size(addr) + 1) = 1;
		rpfree(addr);

		if (!test_overwrite_detected) {
			printf("Failed to detect memory overwrite after end of block in step %" PRIsize " size %" PRIsize "\n", istep, size);
			ret = -1;
			goto cleanup;
		}
	}

	printf("Memory overwrite tests passed\n");

cleanup:
	rpmalloc_finalize();
	return ret;	
}

#else

static int
test_overwrite(void) {
	return 0;
}

#endif

int
test_run(int argc, char** argv) {
	(void)sizeof(argc);
	(void)sizeof(argv);
	test_initialize();
	if (test_alloc())
		return -1;
	if (test_crossthread())
		return -1;
	if (test_threadspam())
		return -1;
	if (test_overwrite())
		return -1;
	if (test_threaded())
		return -1;
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
