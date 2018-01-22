
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <rpmalloc.h>
#include <thread.h>
#include <test.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#  define PRIsize "Iu"
#else
#  define PRIsize "zu"
#endif

#ifndef ENABLE_GUARDS
#  define ENABLE_GUARDS 0
#endif

#if ENABLE_GUARDS

static int test_overwrite_detected;

static void
test_overwrite(void* addr) {
	++test_overwrite_detected;
}

int
test_run(int argc, char** argv) {
	int ret = 0;
	char* addr;
	size_t istep, size;

	rpmalloc_config_t config;
	memset(&config, 0, sizeof(config));
	config.memory_overwrite = test_overwrite;

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

	printf("Memory ovewrite tests passed\n");

cleanup:
	rpmalloc_finalize();
	return ret;
}

#else

int
test_run(int argc, char** argv) {
	rpmalloc_initialize();

	rpmalloc_finalize();
	return 0;
}

#endif

#if ( defined( __APPLE__ ) && __APPLE__ )
#  include <TargetConditionals.h>
#  if defined( __IPHONE__ ) || ( defined( TARGET_OS_IPHONE ) && TARGET_OS_IPHONE ) || ( defined( TARGET_IPHONE_SIMULATOR ) && TARGET_IPHONE_SIMULATOR )
#    define NO_MAIN 1
#  endif
#endif

#if !defined(NO_MAIN)

int
main(int argc, char** argv) {
	return test_run(argc, argv);
}

#endif
