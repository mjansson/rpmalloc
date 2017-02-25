
#include <benchmark.h>

extern void*
tc_malloc(size_t size);

extern void*
tc_memalign(size_t alignment, size_t size);

extern void
tc_free(void* ptr);

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
	return tc_memalign(alignment, size);
}

void
benchmark_free(void* ptr) {
	tc_free(ptr);
}

const char*
benchmark_name(void) {
	return "tcmalloc";
}
