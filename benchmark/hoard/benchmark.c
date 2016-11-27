
#include <benchmark.h>

#ifdef _WIN32
#include <malloc.h>
#else
#include <memory.h>
#endif

extern void
hoardInitialize(void);

extern void
hoardFinalize(void);

extern void
hoardThreadInitialize(void);

extern void
hoardThreadFinalize(void);

extern void*
xxmalloc(size_t sz);

extern void
xxfree(void * ptr);

int
benchmark_initialize() {
	hoardInitialize();
	return 0;
}

int
benchmark_finalize(void) {
	hoardFinalize();
	return 0;
}

int
benchmark_thread_initialize(void) {
	hoardThreadInitialize();
	return 0;
}

int
benchmark_thread_finalize(void) {
	hoardThreadFinalize();
	return 0;
}

void*
benchmark_malloc(size_t size) {
	return xxmalloc(size);
}

extern void
benchmark_free(void* ptr) {
	xxfree(ptr);
}
