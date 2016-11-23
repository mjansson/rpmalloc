
#include <rpmalloc.h>
#include <benchmark.h>

int
benchmark_initialize() {
	return rpmalloc_initialize();
}

int
benchmark_finalize(void) {
	rpmalloc_finalize();
	return 0;
}

int
benchmark_thread_initialize(void) {
	return 0;
}

int
benchmark_thread_finalize(void) {
	rpmalloc_thread_finalize();
	return 0;
}

void*
benchmark_malloc(size_t size) {
	return rpmalloc(size);
}

extern void
benchmark_free(void* ptr) {
	rpfree(ptr);
}
