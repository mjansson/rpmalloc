# Benchmarks
Contained in a parallell repository is a benchmark utility that performs interleaved allocations (both aligned to 8 or 16 bytes, and unaligned) and deallocations (both in-thread and cross-thread) in multiple threads. It measures number of memory operations performed per CPU second, as well as memory overhead by comparing the virtual memory mapped with the number of bytes requested in allocation calls. The setup of number of thread, cross-thread deallocation rate and allocation size limits is configured by command line arguments.

https://github.com/rampantpixels/rpmalloc-benchmark

Benchmarks are run with parameters `benchmark <num threads> <mode> <distribution> <cross-thread rate> <loop count> <block count> <min size> <max size>`. It runs the given number of threads allocating randomly or fixed sized blocks in `[<min size>, <max size>]` bytes range. The `<mode>` parameter controls if it is random or fixed size. If random size, the `<distribution>` parameter controls if sizes are evenly distributed, have a linear falloff rate with size or an exponential falloff rate with size. In each thread, `<loop count>` number of loops are performed, allocating up to `<block count>` blocks in each thread. Every `<cross-thread rate>` loop iteration a cross-thread deallocation is performed (memory allocated in one thread is freed in another thread).

The benchmark also measures the maximum requested allocated size and the used virtual memory by the process to calculate a overhead percentage.

Below is a collection of benchmark results for various allocation sizes. The machines running the Windows 10 and Linux benchmarks have 8 cores (4 physical cores with HT) and 12GiB RAM. The macOS machine is a MacBook, 2 cores (1 physical with HT) and 8GiB RAM. (Linux and macOS results coming soon)

# Random size in [16, 1000] range
Parameters: `benchmark <num threads> 0 0 2 8000000 16000 16 1000`
Evenly distributed sizes in `[16, 1000]` range, 8 million loops with 16000 blocks per thread. Cross thread deallocations every other loop iteration.
![Windows 10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=137567195&format=image)
![Windows 10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1811210702&format=image)

# Random size in [16, 8000] range
Parameters: `benchmark <num threads> 0 1 2 6000000 14000 16 8000`
Linear falloff distributed sizes in `[16, 8000]` range, 6 million loops with 14000 blocks per thread. Cross thread deallocations every other loop iteration.
![Windows 10 random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=881719411&format=image)
![Windows 10 random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1083129746&format=image)

# Random size in [16, 16000] range
Parameters: `benchmark <num threads> 0 1 2 5000000 12000 16 16000`
Linear falloff distributed sizes in `[16, 16000]` range, 5 million loops with 12000 blocks per thread. Cross thread deallocations every other loop iteration.
![Windows 10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=300710721&format=image)
![Windows 10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=496715703&format=image)

# Random size in [128, 64000] range
Parameters: `benchmark <num threads> 0 2 2 4000000 10000 128 64000`
Exponential falloff distributed sizes in `[128, 64000]` range, 4 million loops with 10000 blocks per thread. Cross thread deallocations every other loop iteration.
![Windows 10 random [128, 64000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1042192224&format=image)
![Windows 10 random [128, 64000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1966484583&format=image)

# Random size in [512, 160000] range
Parameters: `benchmark <num threads> 0 2 2 3000000 8000 128 160000`
Exponential falloff distributed sizes in `[512, 160000]` range, 3 million loops with 8000 blocks per thread. Cross thread deallocations every other loop iteration.
![Windows 10 random [512, 160000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=694517188&format=image)
![Windows 10 random [512, 160000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1412665077&format=image)
