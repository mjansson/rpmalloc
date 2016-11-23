
#include <thread.h>

#ifdef _WIN32
#  include <windows.h>
#  include <process.h>

unsigned __stdcall
thread_entry(void* argptr) {
	thread_arg* arg = argptr;
	arg->fn(arg->arg);
	return 0;
}

#endif

uintptr_t
thread_run(thread_arg* arg) {
#ifdef _WIN32
	return _beginthreadex(0, 0, thread_entry, arg, 0, 0);
#else
	return 0;
#endif
}

void
thread_join(uintptr_t handle) {
#ifdef _WIN32
	WaitForSingleObject((HANDLE)handle, INFINITE);
	CloseHandle((HANDLE)handle);
#endif
}

void
thread_sleep(int milliseconds) {
#ifdef _WIN32
	SleepEx(milliseconds, 1);
#endif
}

void
thread_yield(void) {
#ifdef _WIN32
	Sleep(0);
	_ReadWriteBarrier();
#endif
}

void
thread_fence(void) {
#ifdef _WIN32
	_ReadWriteBarrier();
#endif
}
