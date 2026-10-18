/* stress.c  -  rpmalloc long-running stress test  -  2026 Mattias Jansson
 *
 * SPDX-FileCopyrightText: 2026 Mattias Jansson
 * SPDX-License-Identifier: Unlicense OR MIT
 *
 * A long-running, multithreaded stress harness for rpmalloc. It is intentionally
 * NOT part of the CI workflow - it is meant to be run manually, ideally under a
 * sanitizer, to shake out races, corruption and lifetime bugs. It exercises:
 *
 *   - many concurrent worker threads
 *   - allocations spanning every size type (tiny, small, medium, large, huge)
 *   - reallocations across size classes
 *   - cross-thread frees (a block allocated by one thread is freed by another)
 *
 * Every block carries a header and a seeded payload pattern that is verified on
 * free, so silent corruption is caught even without a sanitizer.
 *
 * Build and run under AddressSanitizer + UndefinedBehaviorSanitizer (Linux/macOS):
 *
 *   clang -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
 *     -D_GNU_SOURCE -DENABLE_OVERRIDE=0 -DENABLE_STATISTICS=1 -DENABLE_ASSERTS=1 \
 *     -DRPMALLOC_FIRST_CLASS_HEAPS=1 -Irpmalloc -Itest \
 *     rpmalloc/rpmalloc.c test/thread.c test/stress.c -lpthread -o rpmalloc-stress
 *   ASAN_OPTIONS=use_sigaltstack=0 ./rpmalloc-stress
 *
 * Tunables (environment variables):
 *   RPMALLOC_STRESS_SECONDS  wall-clock run time (default 30)
 *   RPMALLOC_STRESS_THREADS  worker thread count (default: hardware concurrency)
 *   RPMALLOC_STRESS_MAX_SIZE cap on allocation size in bytes (default: unlimited). Useful
 *                            under ThreadSanitizer, where the huge band crawls; stress.sh
 *                            defaults this to 262144 for tsan runs.
 *   RPMALLOC_STRESS_HUGE     set to 1 to also churn 2-32MiB blocks through a small shared pool
 *                            (default off), exercising the huge block cache from all threads with
 *                            allocations, zeroed allocations checked for zero, in place growth and
 *                            shrinking and cross-thread frees. Not limited by RPMALLOC_STRESS_MAX_SIZE.
 *
 * Suggested run durations (RPMALLOC_STRESS_SECONDS):
 *   30      quick smoke test (default)
 *   300     pre-merge confidence run (~5 min)
 *   3600    pre-release soak (~1 hour) - ideally under ThreadSanitizer for races
 *   28800   overnight deep race hunt (~8 hours)
 * Time under TSAN matters more for finding races than raw wall-clock time.
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
#include <time.h>
#include <unistd.h>
#endif

#define BLOCK_MAGIC 0x52504d41u  // 'RPMA'
#define DEAD_MAGIC 0xDEADBEEFu
#define FILL_CAP 256u            // bytes of payload pattern written/verified per block
#define LOCAL_RING 64u           // blocks kept live per thread before recycling locally
#define POOL_SLOTS 4096u         // shared slots used to hand blocks between threads
#define HUGE_POOL_SLOTS 32u      // shared slots for huge churn blocks, bounding the live huge memory
#define HUGE_MIN_SIZE (2 * 1024 * 1024 + 1)
#define HUGE_MAX_SIZE (32 * 1024 * 1024)

typedef struct block_header_t {
	uint32_t magic;
	uint32_t seed;
	uint64_t size;
} block_header_t;

// Size bands covering every rpmalloc size type. {min, max} in bytes.
static const size_t size_band[][2] = {
    {16, 64},                                // tiny
    {65, 1024},                              // small
    {1025, 16 * 1024},                       // medium-small
    {16 * 1024 + 1, 256 * 1024},             // medium-large
    {256 * 1024 + 1, 2 * 1024 * 1024},       // large (up to span limit)
    {2 * 1024 * 1024 + 1, 5 * 1024 * 1024},  // huge (mapped directly)
};
#define NUM_BANDS (sizeof(size_band) / sizeof(size_band[0]))
_Static_assert(NUM_BANDS == 6, "size band weighting in pick_size assumes 6 bands");

static atomic_uintptr_t shared_pool[POOL_SLOTS];
static atomic_uintptr_t huge_pool[HUGE_POOL_SLOTS];
static atomic_int stop_requested;

static atomic_ullong total_allocations;
static atomic_ullong total_local_frees;
static atomic_ullong total_cross_frees;
static atomic_ullong total_reallocations;
static atomic_ullong total_bytes;
static atomic_ullong total_huge_churns;

static size_t worker_count;
static int run_seconds;
//! Optional cap on allocation size (0 = unlimited). Useful under ThreadSanitizer, where the
//  huge/large bands are dominated by shadow-memory overhead and throttle the run to a crawl.
static size_t max_alloc_size;
//! Whether to issue aligned allocations (default on). Set RPMALLOC_STRESS_ALIGN=0 to disable;
//  useful to isolate the (benign) aligned-block page-flag race under ThreadSanitizer.
static int use_aligned_alloc = 1;
//! Whether to churn huge blocks through the huge pool (default off). Set RPMALLOC_STRESS_HUGE=1 to enable.
static int use_huge_churn;

static uint64_t
monotonic_ms(void) {
#if defined(_WIN32)
	return (uint64_t)GetTickCount64();
#else
	struct timespec time_spec;
	clock_gettime(CLOCK_MONOTONIC, &time_spec);
	return (uint64_t)time_spec.tv_sec * 1000u + (uint64_t)(time_spec.tv_nsec / 1000000);
#endif
}

// Per-thread xorshift PRNG, no shared state.
static uint32_t
random_next(uint32_t* random_state) {
	uint32_t scrambled = *random_state;
	scrambled ^= scrambled << 13;
	scrambled ^= scrambled >> 17;
	scrambled ^= scrambled << 5;
	*random_state = scrambled;
	return scrambled;
}

static size_t
pick_size(uint32_t* random_state) {
	// Weight the distribution toward small sizes (as in real workloads) so the
	// shared pool's live set stays bounded in memory, while still exercising the
	// large and huge bands regularly. Weights (per mille): tiny 400, small 350,
	// medium-small 150, medium-large 70, large 29, huge 1.
	uint32_t roll = random_next(random_state) % 1000u;
	size_t band = roll < 400 ? 0 : roll < 750 ? 1 : roll < 900 ? 2 : roll < 970 ? 3 : roll < 999 ? 4 : 5;
	// Drop to a lower band if this one exceeds the configured size cap.
	while (max_alloc_size && band > 0 && size_band[band][0] > max_alloc_size)
		--band;
	size_t min_size = size_band[band][0];
	size_t max_size = size_band[band][1];
	if (max_alloc_size && max_size > max_alloc_size)
		max_size = max_alloc_size < min_size ? min_size : max_alloc_size;
	return min_size + (size_t)(random_next(random_state) % (uint32_t)(max_size - min_size + 1));
}

static void
block_fill(void* block, size_t size, uint32_t seed) {
	block_header_t* header = (block_header_t*)block;
	header->magic = BLOCK_MAGIC;
	header->seed = seed;
	header->size = (uint64_t)size;
	uint8_t* payload = (uint8_t*)block + sizeof(block_header_t);
	size_t available = size - sizeof(block_header_t);
	size_t fill_count = available < FILL_CAP ? available : FILL_CAP;
	for (size_t offset = 0; offset < fill_count; ++offset)
		payload[offset] = (uint8_t)(seed + (uint32_t)offset);
}

// Verify a block. Aborts the process on any corruption.
static void
block_verify(void* block) {
	block_header_t* header = (block_header_t*)block;
	if (header->magic != BLOCK_MAGIC) {
		fprintf(stderr, "STRESS FAIL: bad magic 0x%08x (double free or corruption) at %p\n", header->magic, block);
		abort();
	}
	size_t size = (size_t)header->size;
	uint8_t* payload = (uint8_t*)block + sizeof(block_header_t);
	size_t available = size - sizeof(block_header_t);
	size_t fill_count = available < FILL_CAP ? available : FILL_CAP;
	for (size_t offset = 0; offset < fill_count; ++offset) {
		if (payload[offset] != (uint8_t)(header->seed + (uint32_t)offset)) {
			fprintf(stderr, "STRESS FAIL: payload corruption at %p offset %zu\n", block, offset);
			abort();
		}
	}
}

// Verify and poison a block. Aborts the process on any corruption.
static void
block_check(void* block) {
	block_verify(block);
	((block_header_t*)block)->magic = DEAD_MAGIC;
}

static void
block_free(void* block, int cross_thread) {
	block_check(block);
	rpfree(block);
	if (cross_thread)
		atomic_fetch_add_explicit(&total_cross_frees, 1, memory_order_relaxed);
	else
		atomic_fetch_add_explicit(&total_local_frees, 1, memory_order_relaxed);
}

static void*
block_alloc(uint32_t* random_state) {
	size_t size = pick_size(random_state);
	uint32_t seed = random_next(random_state);
	void* block;
	// Occasionally request extra alignment (unless disabled for diagnostics).
	if (use_aligned_alloc && (seed & 7u) == 0) {
		size_t alignment = (size_t)1 << (4 + (seed % 5));  // 16..256
		block = rpaligned_alloc(alignment, size);
	} else {
		block = rpmalloc(size);
	}
	if (!block) {
		fprintf(stderr, "STRESS FAIL: allocation of %zu bytes failed\n", size);
		abort();
	}
	block_fill(block, size, seed);
	atomic_fetch_add_explicit(&total_allocations, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&total_bytes, (unsigned long long)size, memory_order_relaxed);
	return block;
}

// Hand a block to a random pool slot; free whatever was evicted (almost always
// a block originally allocated by a different thread -> cross-thread free).
static void
pool_handoff(void* block, uint32_t* random_state) {
	size_t slot = random_next(random_state) % POOL_SLOTS;
	uintptr_t evicted = atomic_exchange_explicit(&shared_pool[slot], (uintptr_t)block, memory_order_acq_rel);
	if (evicted)
		block_free((void*)evicted, 1);
}

static size_t
pick_huge_size(uint32_t* random_state) {
	return HUGE_MIN_SIZE + (size_t)(random_next(random_state) % (uint32_t)(HUGE_MAX_SIZE - HUGE_MIN_SIZE + 1));
}

// Allocate a huge block, zeroed a quarter of the time and checked for zero, grow or shrink it a quarter of the
// time and check its content survives, then hand it to a random huge pool slot and free the evicted block. The
// pool is shared by all threads, so the huge block cache sees concurrent allocations, reuse and cross-thread frees
static void
huge_churn(uint32_t* random_state) {
	size_t size = pick_huge_size(random_state);
	uint32_t seed = random_next(random_state);
	void* block;
	if ((seed & 3u) == 0) {
		block = rpcalloc(1, size);
		if (!block) {
			fprintf(stderr, "STRESS FAIL: zeroed huge allocation of %zu bytes failed\n", size);
			abort();
		}
		// The whole block must read as zero, including pages a reused mapping commits again
		const uint8_t* bytes = (const uint8_t*)block;
		for (size_t offset = 0; offset < size; offset += 4096) {
			if (bytes[offset]) {
				fprintf(stderr, "STRESS FAIL: zeroed huge block %p not zero at offset %zu\n", block, offset);
				abort();
			}
		}
		if (bytes[size - 1]) {
			fprintf(stderr, "STRESS FAIL: zeroed huge block %p not zero at its end\n", block);
			abort();
		}
	} else {
		block = rpmalloc(size);
		if (!block) {
			fprintf(stderr, "STRESS FAIL: huge allocation of %zu bytes failed\n", size);
			abort();
		}
	}
	block_fill(block, size, seed);
	if ((seed & 12u) == 0) {
		size_t new_size = pick_huge_size(random_state);
		void* resized = rprealloc(block, new_size);
		if (!resized) {
			fprintf(stderr, "STRESS FAIL: huge realloc to %zu failed\n", new_size);
			abort();
		}
		block_verify(resized);
		block_fill(resized, new_size, random_next(random_state));
		block = resized;
	}
	size_t slot = random_next(random_state) % HUGE_POOL_SLOTS;
	uintptr_t evicted = atomic_exchange_explicit(&huge_pool[slot], (uintptr_t)block, memory_order_acq_rel);
	if (evicted)
		block_free((void*)evicted, 1);
	atomic_fetch_add_explicit(&total_huge_churns, 1, memory_order_relaxed);
}

static void
worker(void* worker_index_arg) {
	uint32_t random_state = (uint32_t)((uintptr_t)worker_index_arg ^ 0x9e3779b9u);
	if (!random_state)
		random_state = 1;

	rpmalloc_thread_initialize();

	void* local_ring[LOCAL_RING];
	memset(local_ring, 0, sizeof(local_ring));
	uint32_t ring_index = 0;

	while (!atomic_load_explicit(&stop_requested, memory_order_relaxed)) {
		for (unsigned int burst = 0; burst < 32; ++burst) {
			void* block = block_alloc(&random_state);
			uint32_t action = random_next(&random_state) & 3u;
			if (action == 0) {
				// Reallocate across a (possibly different) size class, then keep it local.
				size_t new_size = pick_size(&random_state);
				void* resized = rprealloc(block, new_size);
				if (!resized) {
					fprintf(stderr, "STRESS FAIL: realloc to %zu failed\n", new_size);
					abort();
				}
				block_fill(resized, new_size, random_next(&random_state));
				atomic_fetch_add_explicit(&total_reallocations, 1, memory_order_relaxed);
				block = resized;
			}
			if ((random_next(&random_state) & 1u) == 0) {
				// Hand off for a cross-thread free.
				pool_handoff(block, &random_state);
			} else {
				// Keep live in the local ring; recycle the slot's previous block.
				void* recycled = local_ring[ring_index];
				local_ring[ring_index] = block;
				ring_index = (ring_index + 1) % LOCAL_RING;
				if (recycled)
					block_free(recycled, 0);
			}
			if (use_huge_churn && ((random_next(&random_state) & 15u) == 0))
				huge_churn(&random_state);
		}
		thread_yield();
	}

	for (uint32_t ring_slot = 0; ring_slot < LOCAL_RING; ++ring_slot) {
		if (local_ring[ring_slot])
			block_free(local_ring[ring_slot], 0);
	}

	rpmalloc_thread_finalize();
}

int
main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	run_seconds = 30;
	const char* env_seconds = getenv("RPMALLOC_STRESS_SECONDS");
	if (env_seconds) {
		long parsed_seconds = atol(env_seconds);
		if (parsed_seconds > 0)
			run_seconds = (int)parsed_seconds;
	}

#if defined(_WIN32)
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	worker_count = (size_t)system_info.dwNumberOfProcessors;
#else
	long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
	worker_count = (size_t)(cpu_count > 0 ? cpu_count : 4);
#endif
	const char* env_threads = getenv("RPMALLOC_STRESS_THREADS");
	if (env_threads) {
		long parsed_threads = atol(env_threads);
		if (parsed_threads > 0)
			worker_count = (size_t)parsed_threads;
	}
	if (worker_count < 2)
		worker_count = 2;

	const char* env_max_size = getenv("RPMALLOC_STRESS_MAX_SIZE");
	if (env_max_size) {
		long long parsed_max = atoll(env_max_size);
		if (parsed_max > 0)
			max_alloc_size = (size_t)parsed_max;
	}

	const char* env_align = getenv("RPMALLOC_STRESS_ALIGN");
	if (env_align)
		use_aligned_alloc = atoi(env_align) != 0;

	const char* env_huge = getenv("RPMALLOC_STRESS_HUGE");
	if (env_huge)
		use_huge_churn = atoi(env_huge) != 0;

	printf("rpmalloc stress: %zu threads, %d seconds", worker_count, run_seconds);
	if (max_alloc_size)
		printf(", max alloc %zu bytes", max_alloc_size);
	if (use_huge_churn)
		printf(", huge churn");
	printf("\n");
	fflush(stdout);

	if (rpmalloc_initialize(0)) {
		fprintf(stderr, "STRESS FAIL: rpmalloc_initialize failed\n");
		return 1;
	}

	uintptr_t* thread_handles = (uintptr_t*)malloc(sizeof(uintptr_t) * worker_count);
	// Each thread needs its own arg: thread_run hands the pointer straight to the new
	// thread, which reads it asynchronously, so a single reused struct would race.
	thread_arg* worker_descriptors = (thread_arg*)malloc(sizeof(thread_arg) * worker_count);

	uint64_t start_ms = monotonic_ms();
	for (size_t thread_index = 0; thread_index < worker_count; ++thread_index) {
		worker_descriptors[thread_index].fn = worker;
		worker_descriptors[thread_index].arg = (void*)(uintptr_t)(thread_index + 1);
		thread_handles[thread_index] = thread_run(&worker_descriptors[thread_index]);
	}

	// Tick the clock down, reporting progress, then signal threads to stop.
	while ((int)((monotonic_ms() - start_ms) / 1000u) < run_seconds) {
		thread_sleep(1000);
		printf("  ... %d/%d s, allocs=%llu cross-frees=%llu\n", (int)((monotonic_ms() - start_ms) / 1000u), run_seconds,
		       (unsigned long long)atomic_load(&total_allocations), (unsigned long long)atomic_load(&total_cross_frees));
		fflush(stdout);
	}
	atomic_store_explicit(&stop_requested, 1, memory_order_relaxed);

	for (size_t thread_index = 0; thread_index < worker_count; ++thread_index)
		thread_join(thread_handles[thread_index]);
	free(thread_handles);
	free(worker_descriptors);

	// Drain any blocks still parked in the shared pool.
	rpmalloc_thread_initialize();
	unsigned long long drained = 0;
	for (size_t slot = 0; slot < POOL_SLOTS; ++slot) {
		uintptr_t evicted = atomic_exchange_explicit(&shared_pool[slot], 0, memory_order_acq_rel);
		if (evicted) {
			block_free((void*)evicted, 1);
			++drained;
		}
	}
	for (size_t slot = 0; slot < HUGE_POOL_SLOTS; ++slot) {
		uintptr_t evicted = atomic_exchange_explicit(&huge_pool[slot], 0, memory_order_acq_rel);
		if (evicted) {
			block_free((void*)evicted, 1);
			++drained;
		}
	}
	rpmalloc_thread_finalize();

	printf("rpmalloc stress: PASSED\n");
	printf("  allocs       = %llu\n", (unsigned long long)atomic_load(&total_allocations));
	printf("  reallocs     = %llu\n", (unsigned long long)atomic_load(&total_reallocations));
	printf("  local frees  = %llu\n", (unsigned long long)atomic_load(&total_local_frees));
	printf("  cross frees  = %llu (drained %llu at exit)\n", (unsigned long long)atomic_load(&total_cross_frees), drained);
	printf("  total bytes  = %llu\n", (unsigned long long)atomic_load(&total_bytes));
	if (use_huge_churn)
		printf("  huge churns  = %llu\n", (unsigned long long)atomic_load(&total_huge_churns));

	rpmalloc_finalize();
	return 0;
}
