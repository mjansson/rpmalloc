
#include <benchmark.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif

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
#ifdef _WIN32
	return _aligned_malloc(size, alignment ? alignment : 4);
#else
	if (alignment) {
		void* ptr = 0;
		posix_memalign(&ptr, alignment, size);
		return ptr;
	}
	return malloc(size);
#endif
}

extern void
benchmark_free(void* ptr) {
#ifdef _WIN32
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

const char*
benchmark_name(void) {
	return "crt";
}

void
benchmark_thread_collect(void) {
}
