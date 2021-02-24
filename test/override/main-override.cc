
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <rpmalloc.h>
#include <rpnew.h>
#include <thread.h>
#include <test.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

extern "C" void* RPMALLOC_CDECL pvalloc(size_t size);
extern "C" void* RPMALLOC_CDECL valloc(size_t size);

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
	const rpmalloc_config_t* config = rpmalloc_config();

	void* p = malloc(371);
	if (!p)
		return test_fail("malloc failed");
	if ((rpmalloc_usable_size(p) < 371) || (rpmalloc_usable_size(p) > (371 + 16)))
		return test_fail("usable size invalid (1)");
	rpfree(p);

	p = new int;
	if (!p)
		return test_fail("new failed");
	if (rpmalloc_usable_size(p) != 16)
		return test_fail("usable size invalid (2)");
	delete static_cast<int*>(p);

	p = new int[16];
	if (!p)
		return test_fail("new[] failed");
	if (rpmalloc_usable_size(p) != 16*sizeof(int))
		return test_fail("usable size invalid (3)");
	delete[] static_cast<int*>(p);

	p = new int[32];
	if (!p)
		return test_fail("new[] failed");
	if (rpmalloc_usable_size(p) != 32*sizeof(int))
		return test_fail("usable size invalid (4)");
	delete[] static_cast<int*>(p);

	p = valloc(873);
	if (reinterpret_cast<uintptr_t>(p) & (config->page_size - 1)) {
		fprintf(stderr, "FAIL: pvalloc did not align address to page size (%p)\n", p);
		return -1;
	}
	free(p);

	p = pvalloc(275);
	if (reinterpret_cast<uintptr_t>(p) & (config->page_size - 1)) {
		fprintf(stderr, "FAIL: pvalloc did not align address to page size (%p)\n", p);
		return -1;
	}
	if (reinterpret_cast<uintptr_t>(p) < config->page_size) {
		fprintf(stderr, "FAIL: pvalloc did not align size to page size (%" PRIu64 ")\n", static_cast<uint64_t>(rpmalloc_usable_size(p)));
		return -1;
	}
	rpfree(p);

	printf("Allocation tests passed\n");
	return 0;
}

static int
test_free(void) {
	free(rpmalloc(371));
	free(new int);
	free(new int[16]);
	free(pvalloc(1275));
	printf("Free tests passed\n");
	return 0;	
}

static void
basic_thread(void* argp) {
	(void)sizeof(argp);
	int res = test_alloc();
	if (res) {
		thread_exit(static_cast<uintptr_t>(res));
		return;
	}
	res = test_free();
	if (res) {
		thread_exit(static_cast<uintptr_t>(res));
		return;
	}
	thread_exit(0);
}

static int
test_thread(void) {
	uintptr_t thread[2];
	uintptr_t threadres[2];

	thread_arg targ;
	memset(&targ, 0, sizeof(targ));
	targ.fn = basic_thread;
	for (int i = 0; i < 2; ++i)
		thread[i] = thread_run(&targ);

	for (int i = 0; i < 2; ++i) {
		threadres[i] = thread_join(thread[i]);
		if (threadres[i])
			return -1;
	}

	printf("Thread tests passed\n");
	return 0;
}

int
test_run(int argc, char** argv) {
	(void)sizeof(argc);
	(void)sizeof(argv);
	test_initialize();
	if (test_alloc())
		return -1;
	if (test_free())
		return -1;
	if (test_thread())
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
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wnonportable-system-include-path"
#endif
#include <windows.h>

static void
test_initialize(void) {
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	_hardware_threads = static_cast<size_t>(system_info.dwNumberOfProcessors);
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
	_hardware_threads = static_cast<size_t>(num > 1 ? num : 1);
}

#else

static void
test_initialize(void) {
	_hardware_threads = 1;
}

#endif
