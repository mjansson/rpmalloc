/* SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 */

// Runs the thread exit destructor of the allocator, in a library loaded at runtime (keyorder-module.c), before
// the thread local storage of the library is freed. Key destructors run in key order and keys reuse the lowest
// free index, so deleting keys created before the library was loaded gives the allocator a key below the one that
// dyld frees thread local storage in on macOS. A destructor running later on the exiting thread must not allocate
// from the heap the allocator released, which another thread can adopt at the same time

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
#endif

#include <thread.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

int
main(void) {
	printf("Thread exit key order test skipped (no pthread keys)\n");
	return 0;
}

#else

#include <dlfcn.h>
#include <pthread.h>

#if defined(__APPLE__)
#define TEST_MODULE_NAME "librpmalloc-test-keyorder-module.dylib"
#else
#define TEST_MODULE_NAME "librpmalloc-test-keyorder-module.so"
#endif

// macOS and glibc load the module, elsewhere (musl rejects the initial-exec TLS model in a loaded library) the
// test is skipped if they do not
#if defined(__APPLE__) || defined(__GLIBC__)
#define TEST_LOAD_REQUIRED 1
#else
#define TEST_LOAD_REQUIRED 0
#endif

typedef int (*test_module_int_fn)(void);
typedef unsigned long (*test_module_key_fn)(void);
typedef void* (*test_module_alloc_fn)(size_t);
typedef void (*test_module_free_fn)(void*);
typedef void* (*test_module_heap_fn)(void*);

static test_module_int_fn test_module_thread_initialized;
static test_module_int_fn test_module_initialize;
static test_module_key_fn test_module_thread_key;
static test_module_alloc_fn test_module_alloc;
static test_module_free_fn test_module_free;
static test_module_heap_fn test_module_heap;

static pthread_key_t later_key;
static void* exit_heap;
static void* later_heap;
static void* adopt_heap;
static int thread_failed;

static int
test_fail(const char* reason) {
	fprintf(stderr, "Thread exit key order test failed: %s\n", reason);
	return 1;
}

static void*
module_symbol(void* module, const char* name) {
	void* symbol = dlsym(module, name);
	if (!symbol)
		fprintf(stderr, "Unable to find %s in test module (%s)\n", name, dlerror());
	return symbol;
}

static int
test_module_load(const char* path) {
	void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!module) {
		fprintf(stderr, "Unable to load %s (%s)\n", path, dlerror());
		return -1;
	}
	void* symbol[6] = {module_symbol(module, "test_module_thread_initialized"),
	                   module_symbol(module, "test_module_initialize"),
	                   module_symbol(module, "test_module_thread_key"),
	                   module_symbol(module, "test_module_alloc"),
	                   module_symbol(module, "test_module_free"),
	                   module_symbol(module, "test_module_heap")};
	for (int isymbol = 0; isymbol < 6; ++isymbol) {
		if (!symbol[isymbol])
			return -1;
	}
	memcpy(&test_module_thread_initialized, &symbol[0], sizeof(void*));
	memcpy(&test_module_initialize, &symbol[1], sizeof(void*));
	memcpy(&test_module_thread_key, &symbol[2], sizeof(void*));
	memcpy(&test_module_alloc, &symbol[3], sizeof(void*));
	memcpy(&test_module_free, &symbol[4], sizeof(void*));
	memcpy(&test_module_heap, &symbol[5], sizeof(void*));
	return 0;
}

static void*
allocate_heap(void) {
	void* block = test_module_alloc(64);
	if (!block) {
		thread_failed = 1;
		return 0;
	}
	void* heap = test_module_heap(block);
	test_module_free(block);
	return heap;
}

static void
adopt_thread(void* arg) {
	(void)sizeof(arg);
	adopt_heap = allocate_heap();
}

//! Runs after the allocator destructor, and on macOS before the thread local storage of the module is freed. A
//  thread started here takes the most recently released heap
static void
later_destructor(void* value) {
	(void)sizeof(value);
	void* block = test_module_alloc(64);
	if (!block) {
		thread_failed = 1;
		return;
	}
	later_heap = test_module_heap(block);
	thread_arg arg = {adopt_thread, 0};
	uintptr_t thread = thread_run(&arg);
	if (thread)
		thread_join(thread);
	else
		thread_failed = 1;
	test_module_free(block);
}

static void
exit_thread(void* arg) {
	(void)sizeof(arg);
	exit_heap = allocate_heap();
	pthread_setspecific(later_key, &later_key);
}

int
main(int argc, char** argv) {
	char path[1024];
	const char* program = (argc > 0) ? argv[0] : 0;
	const char* separator = program ? strrchr(program, '/') : 0;
	if (separator)
		snprintf(path, sizeof(path), "%.*s/%s", (int)(separator - program), program, TEST_MODULE_NAME);
	else
		snprintf(path, sizeof(path), "./%s", TEST_MODULE_NAME);

	// Keys created before the module is loaded and deleted after it leave free indices below its key
	pthread_key_t free_key[2];
	if ((pthread_key_create(&free_key[0], 0) != 0) || (pthread_key_create(&free_key[1], 0) != 0))
		return test_fail("Unable to create pthread key");
	if (test_module_load(path) != 0) {
#if TEST_LOAD_REQUIRED
		return test_fail("Unable to load test module");
#else
		printf("Thread exit key order test skipped (unable to load test module)\n");
		return 0;
#endif
	}
	test_module_thread_initialized();
	pthread_key_delete(free_key[0]);
	pthread_key_delete(free_key[1]);

	if (test_module_initialize() != 0)
		return test_fail("Unable to initialize allocator in test module");
	if (pthread_key_create(&later_key, later_destructor) != 0)
		return test_fail("Unable to create pthread key");
	if (test_module_thread_key() > (unsigned long)later_key) {
#if TEST_LOAD_REQUIRED
		return test_fail("Allocator thread exit destructor key not below the later key");
#else
		printf("Thread exit key order test skipped (keys not reused in order)\n");
		return 0;
#endif
	}

	thread_arg arg = {exit_thread, 0};
	uintptr_t thread = thread_run(&arg);
	if (!thread)
		return test_fail("Unable to start thread");
	thread_join(thread);

	if (thread_failed || !exit_heap || !later_heap || !adopt_heap)
		return test_fail("Allocation through test module failed");
	if (later_heap == adopt_heap)
		return test_fail("Destructor after the allocator thread exit destructor allocated from a released heap");
	printf("Thread exit key order test passed\n");
	return 0;
}

#endif
