
#include <stdint.h>
#include <stddef.h>

#ifdef _MSC_VER
#  define FORCENOINLINE __declspec(noinline)
#else
#  define FORCENOINLINE __attribute__((__noinline__))
#endif

extern FORCENOINLINE int
benchmark_initialize(void);

extern FORCENOINLINE int
benchmark_finalize(void);

extern FORCENOINLINE int
benchmark_thread_initialize(void);

extern FORCENOINLINE int
benchmark_thread_finalize(void);

extern FORCENOINLINE void*
benchmark_malloc(size_t alignment, size_t size);

extern FORCENOINLINE void
benchmark_free(void* ptr);

extern FORCENOINLINE const char*
benchmark_name(void);

#undef FORCENOINLINE
