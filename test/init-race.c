/* init-race.c  -  rpmalloc concurrent startup race test  -  2026 Mattias Jansson
 *
 * SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 *
 * A focused reproducer for the lazy-initialization data race. rpmalloc initializes
 * itself on the first allocation of the first thread that touches it (the alloc
 * path calls rpmalloc_initialize(0) when the thread heap is still unassigned). When
 * several threads make their very first allocation simultaneously - with no prior
 * rpmalloc_initialize() on the main thread - they all enter rpmalloc_initialize()
 * concurrently and race on the global initialization flag and on global_config,
 * with one thread potentially allocating against a half-written global state.
 *
 * The threads are released from a spin gate at the same instant to maximize the
 * overlap, and the round is repeated so the window is exercised many times. Each
 * round finalizes the allocator, which resets it to the uninitialized state so the
 * next round re-runs first-touch initialization from scratch.
 *
 * This test is meant to be run under ThreadSanitizer, which reports the unsynchronized
 * access to the initialization state as a data race on the unfixed allocator:
 *
 *   clang -g -O1 -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all \
 *     -D_GNU_SOURCE -DENABLE_OVERRIDE=0 -DENABLE_STATISTICS=1 -DENABLE_ASSERTS=1 \
 *     -DRPMALLOC_FIRST_CLASS_HEAPS=1 -Irpmalloc -Itest \
 *     rpmalloc/rpmalloc.c test/thread.c test/init-race.c -lpthread -o rpmalloc-init-race
 *   ./rpmalloc-init-race
 *
 * See test/init-race.sh for a ready-made build-and-run wrapper.
 *
 * Tunables (environment variables):
 *   RPMALLOC_INIT_RACE_THREADS  concurrent thread count per round (default: 4x hardware, min 8)
 *   RPMALLOC_INIT_RACE_ROUNDS   number of init/finalize rounds     (default 32)
 */

#include <rpmalloc.h>
#include <thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

// Released by main once every worker has reached the gate, so all workers make their
// first allocation - and thus enter rpmalloc_initialize() - as close together as possible.
static atomic_int gate_open;
// Number of workers that have reached the gate this round.
static atomic_int workers_ready;
// Set to 1 by any worker whose allocation misbehaves (null, or fails a write/read check).
static atomic_int worker_failed;

// A spread of size classes so the first touch drives the small, medium and large paths.
static const size_t race_sizes[] = {16, 48, 96, 320, 1500, 24000, 200000};
#define RACE_SIZE_COUNT (sizeof(race_sizes) / sizeof(race_sizes[0]))

static void
race_worker(void* argp) {
	uintptr_t seed = (uintptr_t)argp;

	// Announce arrival, then busy-wait on the gate so the release is near-simultaneous.
	atomic_fetch_add_explicit(&workers_ready, 1, memory_order_acq_rel);
	while (atomic_load_explicit(&gate_open, memory_order_acquire) == 0)
		thread_yield();

	// The very first allocation on this thread triggers lazy rpmalloc_initialize(0).
	for (size_t i = 0; i < RACE_SIZE_COUNT; ++i) {
		size_t size = race_sizes[(seed + i) % RACE_SIZE_COUNT];
		unsigned char* block = (unsigned char*)rpmalloc(size);
		if (!block) {
			atomic_store_explicit(&worker_failed, 1, memory_order_relaxed);
			break;
		}
		// Touch both ends to catch a block handed out against half-initialized global state.
		block[0] = (unsigned char)(seed + i);
		block[size - 1] = (unsigned char)(seed - i);
		if (block[0] != (unsigned char)(seed + i) || block[size - 1] != (unsigned char)(seed - i))
			atomic_store_explicit(&worker_failed, 1, memory_order_relaxed);
		rpfree(block);
	}

	rpmalloc_thread_finalize();
}

int
main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	size_t thread_count;
#if defined(_WIN32)
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	thread_count = (size_t)system_info.dwNumberOfProcessors * 4;
#else
	long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
	thread_count = (size_t)(cpu_count > 0 ? cpu_count : 4) * 4;
#endif
	if (thread_count < 8)
		thread_count = 8;
	const char* env_threads = getenv("RPMALLOC_INIT_RACE_THREADS");
	if (env_threads) {
		long parsed = atol(env_threads);
		if (parsed > 0)
			thread_count = (size_t)parsed;
	}

	int rounds = 32;
	const char* env_rounds = getenv("RPMALLOC_INIT_RACE_ROUNDS");
	if (env_rounds) {
		long parsed = atol(env_rounds);
		if (parsed > 0)
			rounds = (int)parsed;
	}

	printf("rpmalloc init race: %zu threads/round, %d rounds\n", thread_count, rounds);
	fflush(stdout);

	// NOTE: main deliberately does NOT call rpmalloc_initialize() - the workers must be the
	// ones to trigger first-touch initialization, concurrently, which is the race under test.
	uintptr_t* handles = (uintptr_t*)malloc(sizeof(uintptr_t) * thread_count);
	thread_arg* descriptors = (thread_arg*)malloc(sizeof(thread_arg) * thread_count);
	if (!handles || !descriptors) {
		fprintf(stderr, "INIT RACE FAIL: out of memory setting up round\n");
		return 1;
	}

	for (int round = 0; round < rounds; ++round) {
		atomic_store_explicit(&gate_open, 0, memory_order_release);
		atomic_store_explicit(&workers_ready, 0, memory_order_release);

		for (size_t t = 0; t < thread_count; ++t) {
			descriptors[t].fn = race_worker;
			descriptors[t].arg = (void*)(uintptr_t)(round * thread_count + t + 1);
			handles[t] = thread_run(&descriptors[t]);
			if (!handles[t]) {
				fprintf(stderr, "INIT RACE FAIL: could not spawn thread\n");
				return 1;
			}
		}

		// Wait for all workers to reach the gate, then release them together.
		while ((size_t)atomic_load_explicit(&workers_ready, memory_order_acquire) < thread_count)
			thread_yield();
		atomic_store_explicit(&gate_open, 1, memory_order_release);

		for (size_t t = 0; t < thread_count; ++t)
			thread_join(handles[t]);

		// Reset to the uninitialized state so the next round re-runs first-touch init.
		rpmalloc_finalize();
	}

	free(handles);
	free(descriptors);

	if (atomic_load_explicit(&worker_failed, memory_order_acquire)) {
		fprintf(stderr, "INIT RACE FAIL: an allocation returned null or failed a write/read check\n");
		return 1;
	}

	printf("rpmalloc init race: PASSED\n");
	return 0;
}
