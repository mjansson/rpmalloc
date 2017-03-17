# Thread caches
Both rpmalloc and tcmalloc have thread caches of free memory blocks which can be used in allocations without interfering with other threads or going to system to map more memory. Configuring the size of these caches can be crucial to obtaining good performance while minimizing memory overhead blowup. Below is a case study using the benchmark tool to compare different thread cache configurations for both allocators.

The rpmalloc thread cache is configured to be unlimited, "normal" as meaning default values, or "small", where both thread cache and global cache is reduced by a factor of 4.

The tcmalloc thread cache is configured to be "large", where it is using 64k pages and increasing the kMinThreadCacheSize by a factor 4, and "normal", meaning default values.

The benchmark is configured to run 4 threads allocating 30000 blocks evenly distributed in the `[16, 4000]` bytes range. It runs 500k loops, and does cross-thread deallocations every other loop iteration. Parameters: `benchmark 4 0 0 2 500000 30000 16 4000`. The benchmarks are run on a Windows 10 machine with 8 cores (4 physical, HT) and 12GiB RAM.

![Windows 10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1402969353&format=image)
![Windows 10 random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1200048857&format=image)

* rpmalloc consistently outperforms both tcmalloc configurations even with normal configuration, while also maintaining a much better memory overhead rate. Running with unlimited configuration does not warrant the increase in memory overhead.

* Reducing the rpmalloc configuration to "small" does not give a large enough decrease in memory overhead for the lost performance.

* tcmalloc has issues where it blows up and uses more than double the amount of requested memory in both configurations.
