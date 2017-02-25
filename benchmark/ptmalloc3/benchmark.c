
#include <benchmark.h>

extern void*
ptmemalign(size_t alignment, size_t bytes);

extern void
ptfree(void* ptr);

int
benchmark_initialize() {
	return 0;
}

int
benchmark_finalize(void) {
	return 0;
}

int
benchmark_thread_initialize(void) {
	return 0;
}

int
benchmark_thread_finalize(void) {
	return 0;
}

void*
benchmark_malloc(size_t alignment, size_t size) {
	return ptmemalign(alignment, size);
}

extern void
benchmark_free(void* ptr) {
	ptfree(ptr);
}

const char*
benchmark_name(void) {
	return "ptmalloc3";
}
