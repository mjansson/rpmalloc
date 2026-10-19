/* SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 */

// Library for the thread exit key order test (keyorder.c), with the allocator compiled in

#undef ENABLE_OVERRIDE
#undef ENABLE_ASSERTS
#undef RPMALLOC_FIRST_CLASS_HEAPS
#define ENABLE_OVERRIDE 0
#define ENABLE_ASSERTS 1
#define RPMALLOC_FIRST_CLASS_HEAPS 1
#include "rpmalloc.c"

#if defined(_WIN32)
#define TEST_MODULE_EXPORT __declspec(dllexport)
#else
#define TEST_MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern TEST_MODULE_EXPORT int
test_module_thread_initialized(void);

extern TEST_MODULE_EXPORT int
test_module_initialize(void);

extern TEST_MODULE_EXPORT unsigned long
test_module_thread_key(void);

extern TEST_MODULE_EXPORT void*
test_module_alloc(size_t size);

extern TEST_MODULE_EXPORT void
test_module_free(void* ptr);

extern TEST_MODULE_EXPORT void*
test_module_heap(void* ptr);

int
test_module_thread_initialized(void) {
	return rpmalloc_is_thread_initialized();
}

int
test_module_initialize(void) {
	return rpmalloc_initialize(0);
}

//! Key of the allocator thread exit destructor
unsigned long
test_module_thread_key(void) {
#if PLATFORM_WINDOWS
	return (unsigned long)fls_key;
#else
	return (unsigned long)pthread_key;
#endif
}

void*
test_module_alloc(size_t size) {
	return rpmalloc(size);
}

void
test_module_free(void* ptr) {
	rpfree(ptr);
}

void*
test_module_heap(void* ptr) {
	return rpmalloc_get_heap_for_ptr(ptr);
}
