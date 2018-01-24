
#include <stdint.h>


struct thread_arg {
	void (*fn)(void*);
	void* arg;
};
typedef struct thread_arg thread_arg;

extern uintptr_t
thread_run(thread_arg* arg);

extern void
thread_exit(uintptr_t value);

extern uintptr_t
thread_join(uintptr_t handle);

extern void
thread_sleep(int milliseconds);

extern void
thread_yield(void);

extern void
thread_fence(void);
