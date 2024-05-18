
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#if defined(__clang__)
#if __has_warning("-Wunsafe-buffer-usage")
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#endif

#include <rpmalloc.h>
#ifdef _WIN32
#include <rpnew.h>
#endif

extern "C" {
#include "test.h"
#include "thread.h"
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>

#if defined(_WIN32)
extern "C" void*
rpvalloc(size_t size);

static void*
valloc(size_t size) {
	return rpvalloc(size);
}
#endif

#if defined(_WIN32) || defined(__APPLE__)
extern "C" void*
rppvalloc(size_t size);

static void*
pvalloc(size_t size) {
	return rppvalloc(size);
}
#else
#include <malloc.h>
#endif

extern "C" int
test_malloc(int print_log);

extern "C" int
test_free(int print_log);

extern "C" int
test_malloc_thread(void);

int
test_malloc(int print_log) {
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
	if (rpmalloc_usable_size(p) != 16 * sizeof(int))
		return test_fail("usable size invalid (3)");
	delete[] static_cast<int*>(p);

	p = new int[32];
	if (!p)
		return test_fail("new[] failed");
	if (rpmalloc_usable_size(p) != 32 * sizeof(int))
		return test_fail("usable size invalid (4)");
	delete[] static_cast<int*>(p);

	p = valloc(873);
	if (reinterpret_cast<uintptr_t>(p) & (config->page_size - 1)) {
		fprintf(stderr, "FAIL: valloc did not align address to page size (%p)\n", p);
		return -1;
	}
	free(p);

	p = pvalloc(275);
	if (reinterpret_cast<uintptr_t>(p) & (config->page_size - 1)) {
		fprintf(stderr, "FAIL: pvalloc did not align address to page size (%p)\n", p);
		return -1;
	}
	if (reinterpret_cast<uintptr_t>(p) < config->page_size) {
		fprintf(stderr, "FAIL: pvalloc did not align size to page size (%" PRIu64 ")\n",
		        static_cast<uint64_t>(rpmalloc_usable_size(p)));
		return -1;
	}
	rpfree(p);

	for (int iloop = 0; iloop < 16; ++iloop) {
		char* ptr[1024];
		for (int i = 0; i < 1024; ++i) {
			ptr[i] = reinterpret_cast<char*>(calloc(3, 75));
			if (!ptr[i])
				return test_fail("calloc failed");
			if (rpmalloc_usable_size(ptr[i]) < (3 * 75))
				return test_fail("calloc usable size invalid");
			for (unsigned int j = 0; j < 3 * 75; ++j) {
				if (ptr[i][j])
					return test_fail("calloc memory not zero");
			}
		}
		for (int i = 0; i < 1024; ++i)
			free(ptr[i]);
	}

	if (print_log)
		printf("Memory override allocation tests passed\n");
	return 0;
}

int
test_free(int print_log) {
	free(rpmalloc(371));
	delete (new int);
	delete[] (new int[16]);
	free(pvalloc(1275));
	if (print_log)
		printf("Memory override free tests passed\n");
	return 0;
}

static void
basic_malloc_thread(void* argp) {
	(void)sizeof(argp);
	int res = test_malloc(0);
	if (res) {
		thread_exit(static_cast<uintptr_t>(res));
		return;
	}
	res = test_free(0);
	if (res) {
		thread_exit(static_cast<uintptr_t>(res));
		return;
	}
	thread_exit(0);
}

int
test_malloc_thread(void) {
	uintptr_t thread[2];
	uintptr_t threadres[2];

	thread_arg targ;
	memset(&targ, 0, sizeof(targ));
	targ.fn = basic_malloc_thread;
	for (int i = 0; i < 2; ++i)
		thread[i] = thread_run(&targ);

	for (int i = 0; i < 2; ++i) {
		threadres[i] = thread_join(thread[i]);
		if (threadres[i])
			return -1;
	}

	printf("Memory override thread tests passed\n");
	return 0;
}
