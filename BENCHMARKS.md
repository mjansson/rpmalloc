# Benchmarks
Contained in a parallell repository is a benchmark utility that performs interleaved allocations (both aligned to 8 or 16 bytes, and unaligned) and deallocations (both in-thread and cross-thread) in multiple threads. It measures number of memory operations performed per CPU second, as well as memory overhead by comparing the virtual memory mapped with the number of bytes requested in allocation calls. The setup of number of thread, cross-thread deallocation rate and allocation size limits is configured by command line arguments.

https://github.com/rampantpixels/rpmalloc-benchmark

Benchmarks are run with parameters `benchmark <num threads> <mode> <distribution> <cross-thread rate> <loop count> <block count> <op count> <min size> <max size>`. It runs the given number of threads allocating randomly or fixed sized blocks in `[<min size>, <max size>]` bytes range. The `<mode>` parameter controls if it is random or fixed size. If random size, the `<distribution>` parameter controls if sizes are evenly distributed, have a linear falloff rate with size or an exponential falloff rate with size. In each thread, `<loop count>` number of loops are performed, allocating up to `<block count>` blocks in each thread. Every loop iteration `<op count>` number of blocks are deallocated, scattered across the entire set of blocks, then another set of `<op count>` number of blocks are allocated, also scattered across the entire set of slots in the block array (also deallocating any previous block in that slot). Every `<cross-thread rate>` loop iteration an `<op count>` number of blocks are allocated and handed off to another thread for cross-thread deallocation (memory allocated in one thread is freed in another thread).

The benchmark also measures the maximum requested allocated size and the used virtual memory by the process to calculate a overhead percentage. This is done at the end of the loop iterations, once all cross-thread deallocations are processed. This will naturally introduce overhead in allocator implementations that have some form of caching of free blocks, which is intended.

The loop sequence is run four times in all threads, first two times in each thread with a full free of all blocks in between. The threads are then terminated and restarted, running another set of similar two loop sequences. The idea is to measure both how well allocators perform at starting up fresh threads, as well as reusing memory within a thread and when warm starting another thread.

Below is a collection of benchmark results for various allocation sizes. The machines running the Windows 10 and Linux benchmarks have 8 cores (4 physical cores with HT) and 12GiB RAM. The macOS machine is a MacBook, 2 cores (1 physical with HT) and 8GiB RAM. (Windows and macOS results coming soon)

The benchmark configurations are to be interpreted as performing alloc/free pairs of 10% of the allocated blocks in each loop iteration (in each thread). Since the free and alloc operations are scattered the patterns of requested sizes and block addresses are random and does not follow any sequential order.

# Result analysis
rpmalloc is faster than all allocators, except lockfree-malloc in a few cases when number of threads exceeds number of processor cores. However, the latter suffers from massive memory overhead as the number of threads increases. Allocators such as tcmalloc and jemalloc trail behind ~15% in performance, and jemalloc also suffers from erratic higher memory overhead.

For an example of how rpmalloc cache configurations affect performance and memory overhead, see the [CACHE](CACHE.md) file.

# Random size in [16, 1000] range
Parameters: `benchmark <num threads> 0 0 2 20000 50000 5000 16 1000`
Evenly distributed sizes in `[16, 1000]` range, 20000 loops with 50000 blocks per thread. Every iteration 5000 blocks (10%) are freed and allocated in a scattered pattern. Cross thread allocations/deallocations of 5000 blocks in each thread every other loop iteration.
![Ubuntu 16.10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1979506104&format=image)
![Ubuntu 16.10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=853552429&format=image)

# Random size in [16, 8000] range
Parameters: `benchmark <num threads> 0 1 2 20000 50000 5000 16 8000`
Linear falloff distributed sizes in `[16, 8000]` range, 20000 loops with 50000 blocks per thread. Every iteration 5000 blocks (10%) are freed and allocated in a scattered pattern. Cross thread allocations/deallocations of 5000 blocks in each thread every other loop iteration.
![Ubuntu 16.10 random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=301017877&format=image)
![Ubuntu 16.10 random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1224595675&format=image)

Ignoring the lockfree-malloc and jemalloc memory overhead range and focusing on the other allocators we can see they are pretty close in memory overhead factors, with most of the multithreaded cases hovering around the 10-15% overhead mark.

![Ubuntu 16.10 random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=812830245&format=image)

# Random size in [16, 16000] range
Parameters: `benchmark <num threads> 0 1 2 20000 50000 5000 16 16000`
Linear falloff distributed sizes in `[16, 16000]` range, 20000 loops with 50000 blocks per thread. Every iteration 5000 blocks (10%) are freed and allocated in a scattered pattern. Cross thread allocations/deallocations of 5000 blocks in each thread every other loop iteration.
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=554347956&format=image)
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1568940233&format=image)

Once again focusing on the lower overhead allocators

![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1749852896&format=image)

# Random size in [128, 64000] range
Parameters: `benchmark <num threads> 0 2 2 20000 30000 3000 128 64000`
Exponential falloff distributed sizes in `[128, 64000]` range, 20000 loops with 30000 blocks per thread. Every iteration 3000 blocks (10%) are freed and allocated in a scattered pattern. Cross thread allocations/deallocations of 3000 blocks in each thread every other loop iteration.
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1077134401&format=image)
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=307285196&format=image)

# Random size in [512, 16000] range
Parameters: `benchmark <num threads> 0 2 2 20000 20000 2000 512 160000`
Exponential falloff distributed sizes in `[512, 160000]` range, 20000 loops with 20000 blocks per thread. Every iteration 2000 blocks (10%) are freed and allocated in a scattered pattern. Cross thread allocations/deallocations of 2000 blocks in each thread every other loop iteration.
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1203594510&format=image)
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1849152448&format=image)
