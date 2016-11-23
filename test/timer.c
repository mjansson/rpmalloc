
#include <timer.h>

#ifdef _WIN32
#  include <Windows.h>
#endif

static uint64_t _time_freq;

int
timer_initialize(void) {
#ifdef _WIN32
	uint64_t unused;
	if (!QueryPerformanceFrequency((LARGE_INTEGER*)&_time_freq) ||
		!QueryPerformanceCounter((LARGE_INTEGER*)&unused))
		return -1;
#endif
	return 0;
}

uint64_t
timer_current(void) {
#ifdef _WIN32
	uint64_t curclock;
	QueryPerformanceCounter((LARGE_INTEGER*)&curclock);
	return curclock;
#endif
	return 0;
}

uint64_t
timer_ticks_per_second(void) {
	return _time_freq;
}
