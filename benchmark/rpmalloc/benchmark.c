
#include <rpmalloc/rpmalloc.h>
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

void
benchmark_thread_collect(void) {
	rpmalloc_thread_collect();
}

void*
benchmark_malloc(size_t alignment, size_t size) {
	return rpmemalign(alignment, size);
}

extern void
benchmark_free(void* ptr) {
	rpfree(ptr);
}

const char*
benchmark_name(void) {
	return "rpmalloc";
}
