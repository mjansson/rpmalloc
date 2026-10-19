/* SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 */

// Library for the module unload test (unload.c), with the allocator compiled in

#undef ENABLE_OVERRIDE
#undef ENABLE_ASSERTS
#define ENABLE_OVERRIDE 0
#define ENABLE_ASSERTS 1
#include "rpmalloc.c"

#include <stdlib.h>

#define TEST_MODULE_REINITIALIZED 1
#define TEST_MODULE_ALLOC_FAILED 2
#define TEST_MODULE_NOT_DETACHED 3
#define TEST_MODULE_REGISTERED 4

#if defined(_WIN32)
#define TEST_MODULE_EXPORT __declspec(dllexport)
#else
#define TEST_MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern TEST_MODULE_EXPORT void*
test_module_alloc(size_t size);

extern TEST_MODULE_EXPORT void
test_module_free(void* ptr);

extern TEST_MODULE_EXPORT int
test_module_reinitialize_on_unload(int* reinitialized);

void*
test_module_alloc(size_t size) {
	return rpmalloc(size);
}

void
test_module_free(void* ptr) {
	rpfree(ptr);
}

static int* test_module_reinitialized;

//! Run during the unload, expected after the allocator module detach hook
static void
test_module_reinitialize(void) {
	int detached = global_module_detached;
	rpmalloc_finalize();
	void* block = rpmalloc(16);
	rpfree(block);
#if PLATFORM_WINDOWS
	int registered = (fls_key != FLS_OUT_OF_INDEXES);
#else
	int registered = pthread_key_created;
#endif
	if (!block)
		*test_module_reinitialized = TEST_MODULE_ALLOC_FAILED;
	else if (!detached)
		*test_module_reinitialized = TEST_MODULE_NOT_DETACHED;
	else if (registered)
		*test_module_reinitialized = TEST_MODULE_REGISTERED;
	else
		*test_module_reinitialized = TEST_MODULE_REINITIALIZED;
}

#if defined(_WIN32)

int
test_module_reinitialize_on_unload(int* reinitialized) {
	test_module_reinitialized = reinitialized;
	return atexit(test_module_reinitialize);
}

#else

// Registered like a C++ static destructor, run by __cxa_finalize when the module is unloaded
extern int
__cxa_atexit(void (*fn)(void*), void* arg, void* dso);
extern void* __dso_handle;

static void
test_module_reinitialize_handler(void* arg) {
	(void)sizeof(arg);
	test_module_reinitialize();
}

int
test_module_reinitialize_on_unload(int* reinitialized) {
	test_module_reinitialized = reinitialized;
	return __cxa_atexit(test_module_reinitialize_handler, 0, &__dso_handle);
}

#endif
