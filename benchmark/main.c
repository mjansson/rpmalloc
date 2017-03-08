
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <benchmark.h>
#include <thread.h>
#include <timer.h>
#include <atomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MODE_RANDOM 0
#define MODE_FIXED  1

typedef struct benchmark_arg benchmark_arg;
typedef struct thread_pointers thread_pointers;

struct benchmark_arg {
	size_t numthreads;
	size_t index;
	size_t mode;
	size_t cross_rate;
	size_t min_size;
	size_t max_size;
	size_t accumulator;
	uint64_t ticks;
	uint64_t mops;
	atomicptr_t foreign;
	atomic32_t allocated;
	int32_t peak_allocated;
	thread_arg thread_arg;
	benchmark_arg* args;
};

struct thread_pointers {
	void** pointers;
	size_t count;
	void* next;
	atomic32_t* allocated;
};

static int benchmark_start;
static atomic32_t benchmark_threads_sync;
static atomic32_t cross_thread_counter;

//Fixed set of random numbers
static size_t random_size[1000] = {
	78794, 124151, 116751, 176328, 140210, 147411, 163883, 122755, 164390, 137611, 105592, 195502, 173723, 119453, 167223, 166610, 174827, 142160, 140564, 122314,
	76108, 117221, 93631, 106087, 153435, 159973, 140686, 133996, 163118, 185097, 119858, 187560, 159776, 114289, 125872, 192756, 111347, 145499, 84026, 146525,
	99335, 68791, 153243, 83899, 104452, 96995, 86031, 156214, 180388, 115373, 149330, 157237, 178765, 150092, 141134, 88726, 162529, 152542, 130581, 159707,
	75675, 99458, 139996, 142248, 133691, 120285, 70488, 132283, 138481, 111761, 123817, 72000, 170276, 137761, 180351, 192668, 94953, 88636, 101767, 156175,
	135154, 170324, 82389, 75516, 175287, 118155, 77486, 133290, 114395, 117428, 98201, 155066, 182828, 109893, 103678, 172660, 75027, 192932, 91353, 190491,
	73116, 122867, 113902, 140528, 68074, 105495, 99464, 105135, 105598, 184184, 96791, 174354, 197353, 183449, 198237, 66752, 75716, 129378, 125198, 192774,
	171752, 198772, 90633, 138080, 157645, 105115, 158602, 104857, 90708, 128908, 132508, 193995, 153083, 93788, 121656, 139311, 183647, 194575, 108465, 124570,
	151987, 110930, 116141, 84501, 78176, 84889, 80241, 125195, 189174, 135620, 72374, 180421, 145625, 101426, 134138, 179815, 173081, 156461, 73581, 155139,
	136729, 140953, 122280, 121138, 167636, 68374, 140020, 117680, 173433, 68158, 125685, 117651, 189052, 110193, 193310, 146590, 184779, 180427, 82110, 193647,
	144425, 132246, 190124, 66715, 120850, 65839, 113577, 198162, 73772, 136805, 68942, 136927, 144703, 76617, 161296, 85801, 100047, 135555, 189156, 151304,
	192803, 155251, 156612, 133236, 70867, 175584, 160835, 76191, 130632, 78790, 195083, 165658, 119675, 80373, 189103, 198519, 76881, 130941, 162545, 93485,
	144592, 169844, 111247, 162105, 159337, 97972, 93851, 76682, 108969, 147985, 66578, 71182, 83856, 69999, 151121, 96325, 182727, 81045, 171175, 69021,
	174462, 133456, 107247, 147968, 101437, 115554, 163618, 159255, 129456, 162747, 141319, 78260, 133328, 186290, 71101, 180726, 175931, 163612, 93268, 195124,
	108341, 81791, 181805, 123624, 132317, 169852, 109155, 142523, 70895, 92859, 152220, 127520, 67329, 100457, 164774, 142950, 107492, 69920, 175570, 93440,
	142155, 198517, 109226, 175237, 153359, 67726, 95330, 110203, 169849, 90897, 66951, 147548, 142651, 157215, 174133, 93759, 181512, 98657, 86165, 75059,
	124218, 165194, 75238, 115828, 153401, 121856, 146268, 127731, 154809, 154007, 101586, 92850, 92678, 111684, 102030, 156892, 119500, 182750, 100229, 160549,
	185147, 100973, 176650, 187245, 140946, 173470, 65699, 167511, 78520, 136701, 145803, 123483, 160327, 190139, 107476, 103750, 196898, 170276, 122013, 98596,
	118598, 98724, 117729, 177841, 78016, 145571, 191351, 114042, 86004, 118514, 107637, 170108, 122904, 98022, 111936, 106403, 100772, 90584, 183214, 157392,
	127641, 72405, 162957, 159280, 103681, 114762, 112373, 141410, 173995, 108089, 141879, 111057, 185088, 101364, 129905, 128877, 74763, 89050, 127361, 84387,
	111077, 171686, 137151, 157713, 117296, 99300, 128673, 113717, 77645, 76316, 86467, 180360, 176498, 82596, 157533, 79152, 86149, 168819, 194505, 125075,
	136499, 141377, 137034, 171369, 192853, 184801, 69998, 176213, 101203, 96172, 147262, 190853, 104055, 145518, 67925, 163501, 195299, 143317, 189714, 72991,
	146092, 85856, 79686, 164113, 162729, 191565, 191813, 102158, 84789, 169401, 140126, 73373, 73398, 93173, 122220, 73113, 135791, 137345, 74310, 112730,
	82585, 85701, 171432, 155993, 190722, 163053, 142249, 82272, 78625, 78534, 102090, 72073, 163354, 166817, 174284, 153674, 77104, 107656, 115696, 101988,
	114099, 121249, 157973, 142034, 118623, 129881, 155269, 160592, 126897, 97532, 197124, 149559, 75310, 171692, 122130, 178770, 82738, 96735, 169828, 172459,
	157294, 98960, 120771, 102659, 162488, 77809, 193016, 183780, 196420, 77165, 194644, 151226, 134263, 109125, 142019, 136246, 129468, 144078, 185006, 166570,
	174661, 126227, 185101, 87946, 153510, 92505, 104064, 143145, 136563, 108129, 82921, 140550, 103606, 112370, 119876, 149827, 76513, 79075, 110568, 111865,
	185042, 74532, 125971, 141790, 167898, 114759, 156049, 94674, 174690, 166202, 145815, 118741, 88018, 83609, 97554, 73617, 188148, 148269, 91559, 132386,
	197056, 122257, 75554, 181465, 117907, 124926, 188177, 96209, 88109, 195269, 90949, 69870, 67845, 112495, 82665, 150678, 154227, 128549, 127583, 173328,
	105546, 124227, 79288, 68003, 116202, 101177, 114650, 75155, 134118, 110498, 180346, 100927, 142553, 123334, 128535, 183714, 78743, 86598, 110358, 112887,
	157862, 131640, 155976, 151138, 138254, 90450, 71992, 104683, 115981, 167632, 114973, 115095, 68418, 170116, 83428, 110079, 118532, 181472, 103105, 174935,
	120109, 147070, 164379, 179860, 129001, 103460, 135002, 178093, 174961, 67234, 131929, 140239, 70434, 158629, 164598, 90414, 112016, 94034, 193805, 95170,
	132168, 139389, 173469, 82024, 163512, 190079, 118750, 73629, 187549, 100677, 84133, 108858, 157071, 94365, 115682, 179829, 194499, 175055, 108039, 65804,
	90664, 191380, 155488, 188184, 171561, 75716, 187784, 158516, 128150, 108368, 158584, 104132, 134261, 174455, 171601, 118168, 93344, 122248, 78758, 161552,
	76016, 75809, 123976, 68678, 157842, 145781, 123674, 71770, 153542, 122173, 187940, 93543, 90524, 158596, 88937, 100819, 128963, 158555, 113079, 166878,
	190674, 99079, 185407, 179360, 182348, 173523, 93234, 114296, 125552, 82650, 114336, 71267, 189828, 179726, 182442, 97813, 87234, 150470, 159028, 189311,
	69392, 192201, 155256, 194925, 113115, 179229, 184300, 141063, 95146, 75181, 158477, 159923, 170293, 154116, 133026, 178898, 187518, 160605, 101043, 113738,
	77901, 75641, 142860, 80481, 141630, 143611, 173951, 156672, 143639, 67509, 111475, 175244, 150044, 181081, 80258, 73765, 73975, 100106, 194412, 178044,
	126405, 175711, 116850, 87963, 160275, 176634, 165610, 73062, 153603, 119714, 73862, 103081, 77861, 96456, 106641, 141185, 176317, 129475, 91592, 130472,
	121145, 135572, 177937, 183846, 84070, 169254, 129949, 82549, 167943, 88929, 146019, 186692, 166831, 99317, 120424, 158989, 76650, 96691, 153745, 120152,
	124694, 116587, 85327, 145841, 79641, 165664, 185563, 109654, 91572, 163847, 138828, 143412, 75173, 96784, 176319, 176052, 126150, 94435, 194588, 142455,
	112358, 117147, 198152, 85238, 70932, 131922, 84470, 76609, 184214, 68080, 191916, 68781, 152225, 116125, 105448, 113674, 141241, 83562, 182174, 103485,
	170998, 125118, 123918, 85939, 158238, 126700, 94606, 88399, 131866, 151044, 68010, 151446, 155814, 88939, 114396, 188415, 122965, 145581, 157474, 83768,
	150462, 83777, 72527, 92990, 175866, 92079, 131720, 116363, 190477, 193568, 161154, 177761, 192831, 114297, 106752, 113940, 142570, 196126, 179370, 137354,
	149055, 72051, 97167, 75485, 78676, 158918, 179833, 150571, 124160, 171898, 170352, 128775, 77670, 141479, 103147, 79161, 111731, 84436, 174255, 171725,
	98442, 125193, 166887, 146266, 68568, 107534, 123003, 182227, 156258, 142577, 167987, 100343, 74148, 171230, 111615, 195808, 72231, 96481, 128050, 176510,
	132141, 129845, 134057, 145321, 75820, 89733, 185050, 165330, 139704, 168856, 139976, 116257, 122353, 179321, 101752, 119864, 70279, 108782, 193028, 185217,
	192918, 150377, 78195, 101215, 89014, 91459, 86921, 109896, 132485, 156646, 82114, 135313, 180189, 84894, 75220, 129008, 170985, 158507, 193726, 193259,
	132539, 80847, 148331, 159192, 130365, 172897, 178984, 195855, 91171, 90653, 152149, 101931, 165849, 95107, 129167, 178512, 87048, 188436, 169720, 128556,
	122805, 150793, 122925, 176969, 117488, 185699, 163232, 177189, 78630, 90671, 96427, 120361, 194915, 146824, 71715, 141393, 66097, 94538, 84936, 125350,
	80063, 76531, 165159, 175679, 143788, 109614, 143201, 160342, 89189, 131795, 142290, 167402, 175460, 145524, 114132, 78436, 101233, 69705, 100518, 181953
};
static size_t random_size_lin[1000];
static size_t random_size_exp[1000];

static const size_t num_alloc_ops[] = {
	13,	18,	12,	16,	27,
	34,	24,	24,	18,	14,
	12,	18,	33,	16,	27,
	27,	5,	12,	28,	7,
	25,	27,	7,	8,	23,
	26,	25,	13,	23,	6,
	11,	35,	28,	10,	14,
	23,	14,	21,	30,	29,
	21,	12,	6,	8,	30,
	32,	14,	33,	32,	16,
	27,	10,	26,	21,	10,
	17,	20,	30,	6,	26,
	33,	22,	35,	21,	28,
	9,	31,	15,	10,	19,
	32,	28,	25,	10,	16,
	25,	20,	19,	27,	19,
	23,	29,	14,	10,	21,
	33,	18,	35,	11,	6,
	32,	19,	28,	28,	24,
	31,	7,	31,	19,	29
};

static const size_t num_free_ops[] = {
	8,	6,	5,	23,	24,
	22,	21,	13,	18,	13,
	5,	7,	11,	10,	17,
	11,	21,	11,	11,	25,
	13,	23,	20,	14,	25,
	15,	20,	25,	20,	24,
	16,	3,	24,	14,	23,
	10,	25,	16,	18,	22,
	3,	6,	4,	4,	14,
	11,	16,	12,	12,	6,
	18,	7,	14,	21,	8,
	8,	9,	11,	14,	11,
	13,	5,	23,	14,	22,
	23,	14,	15,	6,	10,
	6,	11,	3,	4,	8,
	24,	8,	22,	25,	13,
	14,	23,	21,	5,	5,
	13,	14,	18,	13,	18,
	15,	7,	14,	7,	22,
	13,	6,	9,	23,	16
};

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#endif

static size_t
get_process_memory_usage(void) {
#if defined(_WIN32)
	PROCESS_MEMORY_COUNTERS counters;
	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);
	GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
	return counters.WorkingSetSize;
#elif defined(__APPLE__)
	struct task_basic_info info;
	mach_msg_type_number_t info_count = TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_BASIC_INFO,
	    (task_info_t)&info, &info_count) != KERN_SUCCESS)
		return 0;
	return info.resident_size;
#else
	return 0;
#endif
}

static void
put_cross_thread_memory(atomicptr_t* ptr, thread_pointers* pointers) {
	void* prev;
	uintptr_t newval;
	do {
		prev = atomic_load_ptr(ptr);
		pointers->next = (void*)((uintptr_t)prev & ~(uintptr_t)0xF);
		newval = (uintptr_t)pointers | (atomic_incr32(&cross_thread_counter) & 0xF);
	} while (!atomic_cas_ptr(ptr, (void*)newval, prev));
}

static thread_pointers*
get_cross_thread_memory(atomicptr_t* ptr) {
	thread_pointers* current;
	do {
		current = atomic_load_ptr(ptr);
	} while (current && !atomic_cas_ptr(ptr, 0, current));
	return (void*)((uintptr_t)current & ~(uintptr_t)0xF);
}

static void
benchmark_worker(void* argptr) {
	benchmark_arg* arg = argptr;
	thread_pointers* foreign = 0;
	void** pointers;
	const size_t num_pointers = 8192*2;
	const size_t num_loops = 8192*1024;
	const size_t random_size_count = (sizeof(random_size) / sizeof(random_size[0]));
	const size_t alloc_ops_count = (sizeof(num_alloc_ops) / sizeof(num_alloc_ops[0]));
	const size_t free_ops_count = (sizeof(num_free_ops) / sizeof(num_free_ops[0]));
	const size_t alignment[3] = { 0, 8, 16 };

	size_t alloc_idx = 0;
	size_t free_idx = 0;
	size_t iop;
	size_t tick_start, ticks_elapsed;
	int32_t allocated;
	size_t cross_index = 0;
	int aborted = 0;

	benchmark_thread_initialize();

	size_t pointers_size = sizeof(void*) * num_pointers;
	pointers = benchmark_malloc(16, pointers_size);
	memset(pointers, 0, pointers_size);
	atomic_add32(&arg->allocated, (int32_t)pointers_size);

	while (!benchmark_start)
		thread_sleep(10);

	arg->ticks = 0;
	arg->mops = 0;
	for (size_t iter = 0; iter < 2; ++iter) {
		size_t size_index = 0;
		size_t iter_ticks_elapsed = 0;

		for (size_t iloop = 0; iloop < num_loops; ++iloop) {
			size_index = (iter * 3 + iloop * 7) % random_size_count;

			foreign = get_cross_thread_memory(&arg->foreign);

			allocated = 0;
			tick_start = timer_current();

			benchmark_thread_collect();

			const size_t alloc_op_count = num_alloc_ops[(iter + iloop) % alloc_ops_count];
			for (iop = 0; iop < alloc_op_count; ++iop) {
				if (pointers[alloc_idx]) {
					allocated -= *(int32_t*)pointers[alloc_idx];
					benchmark_free(pointers[alloc_idx]);
					++arg->mops;
				}
				size_t size = arg->min_size;
				if (arg->mode == MODE_RANDOM)
					size += random_size[size_index];
				pointers[alloc_idx] = benchmark_malloc(alignment[(size_index + iop) % 3], size);
				*(int32_t*)pointers[alloc_idx] = (int32_t)size;
				allocated += (int32_t)size;
				++arg->mops;

				alloc_idx = (alloc_idx + 1) % num_pointers;
				size_index = (size_index + 1) % random_size_count;
			}

			const size_t free_op_count = num_free_ops[(iter + iloop) % free_ops_count];
			for (iop = 0; iop < free_op_count; ++iop) {
				if (pointers[free_idx]) {
					allocated -= *(int32_t*)pointers[free_idx];
					benchmark_free(pointers[free_idx]);
					++arg->mops;
					pointers[free_idx] = 0;
				}

				free_idx = (free_idx + 1) % num_pointers;
			}

			for (iop = 0; iop < alloc_op_count; ++iop) {
				if (pointers[alloc_idx]) {
					allocated -= *(int32_t*)pointers[alloc_idx];
					benchmark_free(pointers[alloc_idx]);
					++arg->mops;
				}
				size_t size = arg->min_size;
				if (arg->mode == MODE_RANDOM)
					size += random_size_exp[(size_index + 2) % random_size_count];
				pointers[alloc_idx] = benchmark_malloc(alignment[(size_index + iop) % 3], size);
				*(int32_t*)pointers[alloc_idx] = (int32_t)size;
				allocated += (int32_t)size;
				++arg->mops;

				alloc_idx = (alloc_idx + 1) % num_pointers;
				size_index = (size_index + 1) % random_size_count;
			}

			while (foreign) {
				int32_t foreign_allocated = 0;
				for (iop = 0; iop < foreign->count; ++iop) {
					foreign_allocated -= *(int32_t*)foreign->pointers[iop];
					benchmark_free(foreign->pointers[iop]);
					++arg->mops;
				}

				void* next = foreign->next;
				foreign_allocated -= (int32_t)(foreign->count * sizeof(void*) + sizeof(thread_pointers));
				atomic_add32(foreign->allocated, foreign_allocated);
				benchmark_free(foreign->pointers);
				benchmark_free(foreign);
				arg->mops += 2;
				foreign = next;
			}

			foreign = 0;
			if (arg->cross_rate && ((iloop % arg->cross_rate) == 0)) {
				foreign = benchmark_malloc(16, sizeof(thread_pointers));
				foreign->count = alloc_op_count;
				foreign->pointers = benchmark_malloc(16, sizeof(void*) * alloc_op_count);
				foreign->allocated = &arg->allocated;
				allocated += (int32_t)(alloc_op_count * sizeof(void*) + sizeof(thread_pointers));
				arg->mops += 2;

				for (iop = 0; iop < alloc_op_count; ++iop) {
					size_t size = arg->min_size;
					if (arg->mode == MODE_RANDOM)
						size += random_size_lin[(size_index + 2) % random_size_count];
					foreign->pointers[iop] = benchmark_malloc(alignment[(size_index + iop) % 3], size);
					*(int32_t*)foreign->pointers[iop] = (int32_t)size;
					allocated += (int32_t)size;
					++arg->mops;

					size_index = (size_index + 1) % random_size_count;
				}
			}

			ticks_elapsed = timer_current() - tick_start;
			iter_ticks_elapsed += ticks_elapsed;
			arg->ticks += ticks_elapsed;

			int32_t current_allocated = atomic_add32(&arg->allocated, allocated);
			if (arg->peak_allocated < current_allocated)
				arg->peak_allocated = current_allocated;

			if (foreign) {
				cross_index = (cross_index + 1) % arg->numthreads;
				if ((arg->numthreads > 1) && (cross_index == arg->index))
					cross_index = (cross_index + 1) % arg->numthreads;
				benchmark_arg* cross_arg = &arg->args[cross_index];
				put_cross_thread_memory(&cross_arg->foreign, foreign);
			}

			if (timer_ticks_to_seconds(iter_ticks_elapsed) > 300) {
				aborted = 1;
				break;
			}
		}

		allocated = 0;
		tick_start = timer_current();
		for (size_t iptr = 0; iptr < num_pointers; ++iptr) {
			if (!pointers[iptr]) {
				size_t size = arg->min_size;
				if (arg->mode == MODE_RANDOM)
					size += random_size_exp[(size_index + 2) % random_size_count];
				pointers[iptr] = benchmark_malloc(alignment[size_index % 3], size);
				*(int32_t*)pointers[iptr] = (int32_t)size;
				allocated += (int32_t)size;
				++arg->mops;

				size_index = (size_index + 1) % random_size_count;
			}
		}
		ticks_elapsed = timer_current() - tick_start;

		atomic_add32(&arg->allocated, allocated);

		iter_ticks_elapsed += ticks_elapsed;
		arg->ticks += ticks_elapsed;

		//Sync and allow main thread to gather allocation stats
		atomic_incr32(&benchmark_threads_sync);
		do {
			foreign = get_cross_thread_memory(&arg->foreign);
			if (foreign) {
				tick_start = timer_current();
				while (foreign) {
					allocated = 0;
					for (iop = 0; iop < foreign->count; ++iop) {
						allocated -= *(int32_t*)foreign->pointers[iop];
						benchmark_free(foreign->pointers[iop]);
						++arg->mops;
					}

					void* next = foreign->next;
					allocated -= (int32_t)(foreign->count * sizeof(void*) + sizeof(thread_pointers));
					atomic_add32(foreign->allocated, allocated);
					benchmark_free(foreign->pointers);
					benchmark_free(foreign);
					arg->mops += 2;
					foreign = next;
				}
				benchmark_thread_collect();
				ticks_elapsed = timer_current() - tick_start;
				arg->ticks += ticks_elapsed;
			}

			thread_yield();
			thread_fence();
		} while (atomic_load32(&benchmark_threads_sync));

		allocated = 0;
		tick_start = timer_current();
		for (size_t iptr = 0; iptr < num_pointers; ++iptr) {
			if (pointers[iptr]) {
				allocated -= *(int32_t*)pointers[iptr];
				benchmark_free(pointers[iptr]);
				++arg->mops;
				pointers[iptr] = 0;
			}
		}
		ticks_elapsed = timer_current() - tick_start;
		atomic_add32(&arg->allocated, allocated);

		iter_ticks_elapsed += ticks_elapsed;
		arg->ticks += ticks_elapsed;

		printf(".");
		fflush(stdout);

		printf(" %.2f ", timer_ticks_to_seconds(iter_ticks_elapsed));
		if (aborted)
			printf("(aborted) ");
		fflush(stdout);
		aborted = 0;
	}

	//Sync threads
	atomic_incr32(&benchmark_threads_sync);
	do {
		foreign = get_cross_thread_memory(&arg->foreign);
		if (foreign) {
			tick_start = timer_current();
			while (foreign) {
				allocated = 0;
				for (iop = 0; iop < foreign->count; ++iop) {
					allocated -= *(int32_t*)foreign->pointers[iop];
					benchmark_free(foreign->pointers[iop]);
					++arg->mops;
				}

				void* next = foreign->next;
				allocated -= (int32_t)(foreign->count * sizeof(void*) + sizeof(thread_pointers));
				atomic_add32(foreign->allocated, allocated);
				benchmark_free(foreign->pointers);
				benchmark_free(foreign);
				arg->mops += 2;
				foreign = next;
			}
			benchmark_thread_collect();
			ticks_elapsed = timer_current() - tick_start;
			arg->ticks += ticks_elapsed;
		}

		thread_yield();
		thread_fence();
	} while (atomic_load32(&benchmark_threads_sync));

	tick_start = timer_current();

	benchmark_free(pointers);
	atomic_add32(&arg->allocated, -(int32_t)pointers_size);

	pointers = benchmark_malloc(16, 64);
	benchmark_free(pointers);
	arg->mops += 3;

	ticks_elapsed = timer_current() - tick_start;
	arg->ticks += ticks_elapsed;

	benchmark_thread_finalize();

	arg->accumulator += arg->mops;
}

int main(int argc, char** argv) {
	if (timer_initialize() < 0)
		return -1;
	if (benchmark_initialize() < 0)
		return -2;

	if ((argc < 5) || (argc > 6)) {
		printf("Usage: benchmark <thread count> <mode> <cross rate> <min size> <max size>\n"
		       "         <thread count>     Number of execution threads\n"
		       "         <mode>             0 for random size [min, max], 1 for fixed size (min)\n"
		       "         <cross rate>       Rate of cross-thread deallocations (every n iterations), 0 for none\n"
		       "         <min size>         Minimum size for random mode, fixed size for fixed mode\n"
		       "         <max size>         Maximum size for random mode, ignored for fixed mode\n");
		return -3;
	}

	size_t thread_count = (size_t)strtol(argv[1], 0, 10);
	size_t mode = (size_t)strtol(argv[2], 0, 10);
	size_t cross_rate = (size_t)strtol(argv[3], 0, 10);
	size_t min_size = (size_t)strtol(argv[4], 0, 10);
	size_t max_size = (argc > 5) ? (size_t)strtol(argv[5], 0, 10) : 0;

	if ((thread_count < 1) || (thread_count > 64)) {
		printf("Invalid thread count: %s\n", argv[1]);
		return -3;
	}
	if ((mode != MODE_RANDOM) && (mode != MODE_FIXED)) {
		printf("Invalid mode: %s\n", argv[2]);
		return -3;
	}
	if ((mode == MODE_RANDOM) && (!max_size || (max_size < min_size))) {
		printf("Invalid min/max size for random mode: %s %s\n", argv[3], (argc > 4) ? argv[4] : "<missing>");
		return -3;
	}
	if ((mode == MODE_FIXED) && !min_size) {
		printf("Invalid size for fixed mode: %s\n", argv[3]);
		return -3;
	}

	if (thread_count == 1)
		cross_rate = 0;

	//Setup the random size tables
	size_t size_range = max_size - min_size;
	const size_t random_size_count = (sizeof(random_size) / sizeof(random_size[0]));
	for (size_t ir = 0; ir < sizeof(random_size) / sizeof(random_size[0]); ++ir)
		random_size[ir] %= size_range;

	for (size_t ir = 0; ir < sizeof(random_size) / sizeof(random_size[0]); ++ir) {
		double w0 = 1.0 - (double)random_size[ir] / (double)size_range;
		double w1 = 1.0 - (double)random_size[(ir + 1) % random_size_count] / (double)size_range;
		random_size_lin[ir] = (size_t)((double)random_size[(ir + 2) % random_size_count] * (w0 + w1) * 0.5);
		random_size_exp[ir] = (size_t)((double)random_size[(ir + 2) % random_size_count] * (w0 * w1));
	}

	benchmark_arg* arg;
	uintptr_t* thread_handle;
	FILE* fd;
	
	arg = benchmark_malloc(0, sizeof(benchmark_arg) * thread_count);
	thread_handle = benchmark_malloc(0, sizeof(thread_handle) * thread_count);

	char filebuf[64];
	if (mode == 0)
		sprintf(filebuf, "benchmark-random-%u-%u-%u-%s.txt",
		        (unsigned int)thread_count, (unsigned int)min_size,
		        (unsigned int)max_size, benchmark_name());
	else
		sprintf(filebuf, "benchmark-fixed-%u-%u-%s.txt",
		        (unsigned int)thread_count, (unsigned int)min_size,
		        benchmark_name());
	fd = fopen(filebuf, "w+b");

	benchmark_start = 0;

	if (mode == 0)
		printf("Running %s %u threads alloc/free random size [%u,%u]: ",
			    benchmark_name(),
			    (unsigned int)thread_count, (unsigned int)min_size,
			    (unsigned int)max_size);
	else
		printf("Running %s %u threads alloc/free fixed size [%u]: ",
			    benchmark_name(),
			    (unsigned int)thread_count, (unsigned int)min_size);
	fflush(stdout);

	size_t memory_usage = 0;
	size_t peak_allocated = 0;
	size_t cur_allocated = 0;
	uint64_t mops = 0;
	uint64_t ticks = 0;

	for (size_t iter = 0; iter < 2; ++iter) {
		benchmark_start = 0;
		atomic_store32(&benchmark_threads_sync, 0);
		thread_fence();

		for (size_t ithread = 0; ithread < thread_count; ++ithread) {
			arg[ithread].numthreads = thread_count;
			arg[ithread].index = ithread;
			arg[ithread].mode = mode;
			arg[ithread].cross_rate = cross_rate;
			arg[ithread].min_size = min_size;
			arg[ithread].max_size = max_size;
			arg[ithread].thread_arg.fn = benchmark_worker;
			arg[ithread].thread_arg.arg = &arg[ithread];
			atomic_store_ptr(&arg[ithread].foreign, 0);
			atomic_store32(&arg[ithread].allocated, 0);
			arg[ithread].peak_allocated = 0;
			arg[ithread].args = arg;
			thread_fence();
			thread_handle[ithread] = thread_run(&arg[ithread].thread_arg);
		}

		thread_sleep(1000);

		benchmark_start = 1;
		thread_fence();

		while (atomic_load32(&benchmark_threads_sync) < (int32_t)thread_count) {
			thread_sleep(1000);
			thread_fence();
		}
		thread_sleep(1000);
		thread_fence();

		cur_allocated = 0;
		for (size_t ithread = 0; ithread < thread_count; ++ithread) {
			size_t thread_allocated = (size_t)atomic_load32(&arg[ithread].allocated);
			cur_allocated += thread_allocated;
		}
		if (cur_allocated > peak_allocated) {
			peak_allocated = cur_allocated;
			memory_usage = get_process_memory_usage();
		}

		atomic_store32(&benchmark_threads_sync, 0);
		thread_fence();

		thread_sleep(1000);
		while (atomic_load32(&benchmark_threads_sync) < (int32_t)thread_count) {
			thread_sleep(1000);
			thread_fence();
		}
		thread_sleep(1000);
		thread_fence();

		cur_allocated = 0;
		for (size_t ithread = 0; ithread < thread_count; ++ithread) {
			size_t thread_allocated = (size_t)atomic_load32(&arg[ithread].allocated);
			cur_allocated += thread_allocated;
		}
		if (cur_allocated > peak_allocated) {
			peak_allocated = cur_allocated;
			memory_usage = get_process_memory_usage();
		}

		atomic_store32(&benchmark_threads_sync, 0);
		thread_fence();

		thread_sleep(1000);
		while (atomic_load32(&benchmark_threads_sync) < (int32_t)thread_count) {
			thread_sleep(1000);
			thread_fence();
		}
		thread_sleep(1000);
		thread_fence();

		atomic_store32(&benchmark_threads_sync, 0);
		thread_fence();

		for (size_t ithread = 0; ithread < thread_count; ++ithread) {
			thread_join(thread_handle[ithread]);
			ticks += arg[ithread].ticks;
			mops += arg[ithread].mops;
			if (!arg[ithread].accumulator)
				exit(-1);
		}
	}

	if (!ticks)
		ticks = 1;
	
	benchmark_free(thread_handle);
	benchmark_free(arg);

	double time_elapsed = timer_ticks_to_seconds(ticks);
	double average_mops = (double)mops / time_elapsed;
	char linebuf[128];
	int len = snprintf(linebuf, sizeof(linebuf), "%u,%u,%u\n",
		                (unsigned int)average_mops,
		                (unsigned int)memory_usage,
	                    (unsigned int)peak_allocated);
	fwrite(linebuf, (len > 0) ? (size_t)len : 0, 1, fd);
	fflush(fd);

	printf("%u memory ops/CPU second (%uMiB -> %uMiB bytes peak, %.0f%% overhead)\n",
		    (unsigned int)average_mops,
	        (unsigned int)(peak_allocated / (1024 * 1024)), (unsigned int)(memory_usage / (1024 * 1024)),
	        100.0 * ((double)memory_usage - (double)peak_allocated) / (double)peak_allocated);
	fflush(stdout);

	fclose(fd);

	if (benchmark_finalize() < 0)
		return -4;

	return 0;
}
