/* SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 */

// Unloads a library with rpmalloc compiled in (unload-module.c) and exits threads that allocated through it
// after the unload, which crashes if the thread destructor of the allocator outlives the library (issue #401)

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

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define TEST_MODULE_NAME "rpmalloc-test-unload-module.dll"
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#define TEST_MODULE_NAME "librpmalloc-test-unload-module.dylib"
#else
#define TEST_MODULE_NAME "librpmalloc-test-unload-module.so"
#endif
#endif

#define TEST_PASSED 0
#define TEST_FAILED 1
#define TEST_SKIPPED 2

// Results reported by the test module unload handler
#define TEST_MODULE_REINITIALIZED 1
#define TEST_MODULE_ALLOC_FAILED 2
#define TEST_MODULE_NOT_DETACHED 3
#define TEST_MODULE_REGISTERED 4

// Windows and glibc load and unload the module, elsewhere (macOS, musl) the test is skipped if they do not
#if defined(_WIN32) || defined(__GLIBC__)
#define TEST_UNLOAD_REQUIRED 1
#else
#define TEST_UNLOAD_REQUIRED 0
#endif

typedef void (*module_fn)(void);
typedef void* (*test_module_alloc_fn)(size_t);
typedef void (*test_module_free_fn)(void*);
typedef int (*test_module_reinitialize_on_unload_fn)(int*);

static test_module_alloc_fn test_module_alloc;
static test_module_free_fn test_module_free;
static test_module_reinitialize_on_unload_fn test_module_reinitialize_on_unload;

#if defined(_WIN32)

typedef HMODULE module_t;

static module_t
module_load(const char* path) {
	return LoadLibraryA(path);
}

static module_fn
module_symbol(module_t module, const char* name) {
	return (module_fn)GetProcAddress(module, name);
}

static int
module_unload(module_t module) {
	return FreeLibrary(module) ? 0 : -1;
}

static int
module_is_loaded(const char* path) {
	return GetModuleHandleA(path) ? 1 : 0;
}

static const char*
module_error(void) {
	static char buffer[32];
	snprintf(buffer, sizeof(buffer), "error %lu", (unsigned long)GetLastError());
	return buffer;
}

#else

typedef void* module_t;

static module_t
module_load(const char* path) {
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static module_fn
module_symbol(module_t module, const char* name) {
	module_fn fn = 0;
	void* symbol = dlsym(module, name);
	memcpy(&fn, &symbol, sizeof(fn));
	return fn;
}

static int
module_unload(module_t module) {
	return dlclose(module);
}

static int
module_is_loaded(const char* path) {
#ifdef RTLD_NOLOAD
	module_t module = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
	if (!module)
		return 0;
	dlclose(module);
	return 1;
#else
	(void)sizeof(path);
	return 1;
#endif
}

static const char*
module_error(void) {
	const char* error = dlerror();
	return error ? error : "no error";
}

#endif

static int
test_unload_fail(const char* reason) {
	fprintf(stderr, "Module unload test failed: %s (%s)\n", reason, module_error());
	return TEST_FAILED;
}

static int
test_unload_skip(const char* reason) {
#if TEST_UNLOAD_REQUIRED
	fprintf(stderr, "Module unload test failed: %s\n", reason);
	return TEST_FAILED;
#else
	printf("Module unload test skipped (%s)\n", reason);
	return TEST_SKIPPED;
#endif
}

static void
module_path(char* buffer, size_t capacity, const char* program) {
#if defined(_WIN32)
	(void)sizeof(program);
	snprintf(buffer, capacity, "%s", TEST_MODULE_NAME);
#else
	const char* separator = program ? strrchr(program, '/') : 0;
	if (separator)
		snprintf(buffer, capacity, "%.*s/%s", (int)(separator - program), program, TEST_MODULE_NAME);
	else
		snprintf(buffer, capacity, "./%s", TEST_MODULE_NAME);
#endif
}

static module_t
test_module_load(const char* path) {
	module_t module = module_load(path);
	if (!module) {
		fprintf(stderr, "Unable to load %s (%s)\n", path, module_error());
		return 0;
	}
	test_module_alloc = (test_module_alloc_fn)module_symbol(module, "test_module_alloc");
	test_module_free = (test_module_free_fn)module_symbol(module, "test_module_free");
	test_module_reinitialize_on_unload =
	    (test_module_reinitialize_on_unload_fn)module_symbol(module, "test_module_reinitialize_on_unload");
	if (!test_module_alloc || !test_module_free || !test_module_reinitialize_on_unload) {
		fprintf(stderr, "Unable to find test module entry points (%s)\n", module_error());
		return 0;
	}
	return module;
}

static int
allocate_through_module(void) {
	void* block = test_module_alloc(1024);
	if (!block)
		return -1;
	test_module_free(block);
	return 0;
}

static atomic_int thread_failures;
static atomic_int late_thread_state;

static void
early_thread(void* arg) {
	(void)sizeof(arg);
	if (allocate_through_module() != 0)
		atomic_fetch_add(&thread_failures, 1);
}

static void
late_thread(void* arg) {
	(void)sizeof(arg);
	if (allocate_through_module() != 0)
		atomic_fetch_add(&thread_failures, 1);
	atomic_store(&late_thread_state, 1);
	while (atomic_load(&late_thread_state) != 2)
		thread_sleep(1);
}

//! Unload with a thread that allocated through the module still running
static int
test_unload_running_thread(const char* path) {
	module_t module = test_module_load(path);
	if (!module)
		return test_unload_skip("unable to load test module");

	if (allocate_through_module() != 0)
		return test_unload_fail("Allocation through test module failed");

	thread_arg early_arg = {early_thread, 0};
	uintptr_t early = thread_run(&early_arg);
	if (!early)
		return test_unload_fail("Unable to start thread");
	thread_join(early);

	thread_arg late_arg = {late_thread, 0};
	uintptr_t late = thread_run(&late_arg);
	if (!late)
		return test_unload_fail("Unable to start thread");
	while (atomic_load(&late_thread_state) != 1)
		thread_sleep(1);

	int result = module_unload(module);
	int unloaded = (result == 0) && !module_is_loaded(path);
	test_module_alloc = 0;
	test_module_free = 0;

	atomic_store(&late_thread_state, 2);
	thread_join(late);

	if (result != 0)
		return test_unload_fail("Unable to unload test module");
	if (atomic_load(&thread_failures) != 0)
		return test_unload_fail("Allocation through test module failed in thread");
	if (!unloaded)
		return test_unload_skip("test module not unloaded");
	return TEST_PASSED;
}

static module_t unload_module;
static int unload_result;
static int module_reinitialized;

static void
unload_thread(void* arg) {
	(void)sizeof(arg);
	unload_result = module_unload(unload_module);
}

//! Unload from a thread that exits after the module has finalized and allocated again during the unload
static int
test_unload_reinitialize(const char* path) {
	module_t module = test_module_load(path);
	if (!module)
		return test_unload_fail("Unable to load test module");

	if (test_module_reinitialize_on_unload(&module_reinitialized) != 0)
		return test_unload_fail("Unable to register test module unload handler");
	if (allocate_through_module() != 0)
		return test_unload_fail("Allocation through test module failed");

	unload_module = module;
	unload_result = -1;
	thread_arg unload_arg = {unload_thread, 0};
	uintptr_t unloader = thread_run(&unload_arg);
	if (!unloader)
		return test_unload_fail("Unable to start thread");
	thread_join(unloader);
	test_module_alloc = 0;
	test_module_free = 0;

	if (unload_result != 0)
		return test_unload_fail("Unable to unload test module");
	if (module_is_loaded(path))
		return test_unload_fail("Test module not unloaded");
	if (module_reinitialized == TEST_MODULE_NOT_DETACHED)
		return test_unload_skip("test module unload handler ran before the allocator detach hook");
	if (module_reinitialized == TEST_MODULE_REGISTERED)
		return test_unload_fail("Thread destructor registered again during the unload");
	if (module_reinitialized != TEST_MODULE_REINITIALIZED)
		return test_unload_fail("Test module did not allocate again during the unload");
	return TEST_PASSED;
}

int
main(int argc, char** argv) {
	char path[1024];
	module_path(path, sizeof(path), (argc > 0) ? argv[0] : 0);

	int result = test_unload_running_thread(path);
	if (result != TEST_PASSED)
		return (result == TEST_SKIPPED) ? 0 : result;
	printf("Module unload with running thread test passed\n");
	fflush(stdout);

	result = test_unload_reinitialize(path);
	if (result != TEST_PASSED)
		return (result == TEST_SKIPPED) ? 0 : result;
	printf("Module unload with reinitialization test passed\n");
	return 0;
}
