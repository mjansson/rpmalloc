
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef _MSC_VER
#  if !defined(__clang__)
#    pragma warning (disable: 5105)
#  endif
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wnonportable-system-include-path"
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-result"
#define _GNU_SOURCE
#endif

#include <rpmalloc.h>
#include <thread.h>
#include <test.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#define pointer_offset(ptr, ofs) (void*)((char*)(ptr) + (ptrdiff_t)(ofs))
#define pointer_diff(first, second) (ptrdiff_t)((const char*)(first) - (const char*)(second))

thread_storage_create(int, gLocalVar)
static size_t hardware_threads;
static int test_failed;
thread_storage(int, gLocalVar)

static void
test_initialize(void);

static int
test_fail_cb(const char* reason, const char* file, int line) {
	fprintf(stderr, "FAIL: %s @ %s:%d\n", reason, file, line);
	fflush(stderr);
	test_failed = 1;
	return -1;
}

#define test_fail(msg) test_fail_cb(msg, __FILE__, __LINE__)

static void
defer_free_thread(void *arg) {
	rpfree(arg);
}

static int got_error = 0;

static void
test_error_callback(const char* message) {
	//printf("%s\n", message);
	(void)sizeof(message);
	got_error = 1;
}

static int
test_error(void) {
	//printf("Detecting memory leak\n");

	rpmalloc_config_t config = {0};
	config.error_callback = test_error_callback;
	rpmalloc_initialize_config(&config);

	rpmalloc(10);

	rpmalloc_finalize();

	if (!got_error) {
		printf("Leak not detected and reported as expected\n");
		return -1;
	}

	printf("Error detection test passed\n");
	return 0;
}

int test_malloc(int print_log) {
    void *p = malloc(371);
    if (!p)
        return test_fail("malloc failed");
    if ((rpmalloc_usable_size(p) < 371) || (rpmalloc_usable_size(p) > (371 + 16)))
        return test_fail("usable size invalid (1)");

    rpfree(p);
    if (print_log)
        printf("Memory override allocation tests passed\n");
    return 0;
}

int test_free(int print_log) {
    free(malloc(371));
    rpmalloc_finalize();
    if (print_log)
        printf("Memory override free tests passed\n");
    return 0;
}

/* Thread function: Compile time thread-local storage */
static int thread_test_local_storage(void *aArg) {
    int thread = *(int *)aArg;
    free(aArg);

    int data = thread + rand();
    *gLocalVar() = data;
    thread_sleep(5);
    if (*gLocalVar() != data)
        return test_fail("Emulated thread-local test failed\n");

    printf("Thread #%d, emulated thread-local storage test passed\n", thread);
    return 0;
}

#define THREAD_COUNT 5

int test_thread_storage(void) {
    intptr_t thread[THREAD_COUNT];
    thread_arg targ[THREAD_COUNT];
    if (rpmalloc_gLocalVar_tls != 0)
        return test_fail("thread_local_create macro test failed\n");

    /* Clear the TLS variable (it should keep this value after all
       threads are finished). */
    *gLocalVar() = 1;
    if (rpmalloc_gLocalVar_tls != sizeof(int))
        return test_fail("thread_local macro test failed\n");

    if (*gLocalVar() != 1)
        return test_fail("thread_local_get macro test failed\n");

    for (int i = 0; i < THREAD_COUNT; ++i) {
        int *n = malloc(sizeof * n);  // Holds a thread serial number
        if (!n)
            return test_fail("malloc failed");

        *n = i;
        targ[i].fn = thread_test_local_storage;
        targ[i].arg = n;
        /* Start a child thread that modifies gLocalVar */
        thread[i] = thread_run(&targ[i]);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_join(thread[i]);
    }

    /* Check if the TLS variable has changed */
    if (*gLocalVar() != 1)
        return test_fail("thread_local_get macro test failed\n");

    gLocalVar_delete();
    printf("Emulated thread-local storage tests passed\n");
    return 0;
}

int test_run(int argc, char **argv) {
    (void)sizeof(argc);
    (void)sizeof(argv);
    test_initialize();
 //   if (test_malloc(1))
 //       return -1;
  //  if (test_free(1))
  //      return -1;
    if (test_thread_storage())
        return -1;
    if (test_error())
        return -1;
    printf("\nAll tests passed\n");
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
#include <windows.h>

static void
test_initialize(void) {
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	hardware_threads = (size_t)system_info.dwNumberOfProcessors;
}

#elif (defined(__linux__) || defined(__linux))
#include <sched.h>
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
	hardware_threads = (size_t)(num > 1 ? num : 1);
}

#else

static void
test_initialize(void) {
	hardware_threads = 1;
}

#endif
