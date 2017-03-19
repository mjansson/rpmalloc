# rpmalloc - Rampant Pixels Memory Allocator
This library provides a public domain cross platform lock free thread caching 16-byte aligned memory allocator implemented in C. The latest source code is always available at

https://github.com/rampantpixels/rpmalloc

Platforms currently supported:

- Windows
- MacOS
- iOS
- Linux
- Android

The code should be easily portable to any platform with atomic operations and an mmap-style virtual memory management API.

This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.

Please consider our Patreon to support our work - https://www.patreon.com/rampantpixels

Created by Mattias Jansson / Rampant Pixels  -  http://www.rampantpixels.com

# Performance
We believe rpmalloc is faster than most popular memory allocators like tcmalloc, hoard, ptmalloc3 and others without causing extra allocated memory overhead in the thread caches. We also believe the implementation to be easier to read and modify compared to these allocators, as it is a single source file of ~1300 lines of C code.

Contained in a parallel repository is a benchmark utility that performs interleaved allocations (both aligned to 8 or 16 bytes, and unaligned) and deallocations (both in-thread and cross-thread) in multiple threads. It measures number of memory operations performed per CPU second, as well as memory overhead by comparing the virtual memory mapped with the number of bytes requested in allocation calls. The setup of number of thread, cross-thread deallocation rate and allocation size limits is configured by command line arguments.

https://github.com/rampantpixels/rpmalloc-benchmark

Below is an example performance comparison chart of rpmalloc and other popular allocator implementations.

![Windows random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=881719411&format=image)

The benchmark producing these numbers were run on a Windows 10 machine with 8 logical cores (4 physical, HT). The actual numbers are not to be interpreted as absolute performance figures, but rather as relative comparisons between the different allocators. For additional benchmark results, see the [BENCHMARKS](BENCHMARKS.md) file.

Configuration of the thread and global caches can be important depending on your use pattern. See [CACHE](CACHE.md) for a case study and some comments/guidelines.

# Using
The easiest way to use the library is simply adding rpmalloc.[h|c] to your project and compile them along with your sources. This contains only the rpmalloc specific entry points and does not provide internal hooks to process and/or thread creation at the moment. You are required to call these functions from your own code in order to initialize and finalize the allocator in your process and threads:

__rpmalloc_initialize__ : Call at process start to initialize the allocator

__rpmalloc_finalize__: Call at process exit to finalize the allocator

__rpmalloc_thread_initialize__: Call at each thread start to initialize the thread local data for the allocator

__rpmalloc_thread_finalize__: Call at each thread exit to finalize and release thread cache back to global cache

Then simply use the __rpmalloc__/__rpfree__ and the other malloc style replacement functions. Remember all allocations are 16-byte aligned, so no need to call the explicit rpmemalign/rpaligned_alloc/rpposix_memalign functions unless you need greater alignment, they are simply wrappers to make it easier to replace in existing code.

If you wish to override the standard library malloc family of functions and have automatic initialization/finalization of process and threads, also include the `malloc.c` file in your project. The automatic init/fini is only implemented for Linux and macOS targets.

# Building
To compile as a static library run the configure python script which generates a Ninja build script, then build using ninja. Or use the Visual Studio or XCode projects available in the build subdirectories. This also includes the malloc overrides and init/fini glue code.

The configure + ninja build also produces a shared object/dynamic library that can be used with LD_PRELOAD/DYLD_INSERT_LIBRARIES to inject in a preexisting binary. This is only implemented for Linux and macOS targets.

# Implementation details
The allocator is based on 64KiB alignment, where all runs of memory pages are mapped to 64KiB boundaries. On Windows this is automatically guaranteed by the VirtualAlloc granularity, and on mmap systems it is achieved by atomically incrementing the address where pages are mapped to. By aligning to 64KiB boundaries the free operation can locate the header of the memory block without having to do a table lookup (as tcmalloc does) by simply masking out the low 16 bits of the address.

Memory blocks are divided into three categories. Small blocks are [16, 2032] bytes, medium blocks (2032, 32720] bytes, and large blocks (32720, 2097120] bytes. The three categories are further divided in size classes.

Small blocks have a size class granularity of 16 bytes each in 127 buckets. Medium blocks have a granularity of 512 bytes, 60 buckets. Large blocks have a 64KiB granularity, 32 buckets. All allocations are fitted to these size class boundaries (an allocation of 34 bytes will allocate a block of 48 bytes). Each small and medium size class has an associated span (meaning a contiguous set of memory pages) configuration describing how many pages the size class will allocate each time the cache is empty and a new allocation is requested.

Spans for small and medium blocks are cached in four levels to avoid calls to map/unmap memory pages. The first level is a per thread single active span for each size class. The second level is a per thread list of partially free spans for each size class. The third level is a per thread list of free spans for each number of pages in the span configuration. The fourth level is a global list of free spans for each number of pages in the span configuration. Each cache level can be configured to control memory usage versus performance.

Each span for a small and medium size class keeps track of how many blocks are allocated/free, as well as a list of which blocks that are free for allocation. To avoid locks, each span is completely owned by the allocating thread, and all cross-thread deallocations will be deferred to the owner thread.

Large blocks, or super spans, are cached in two levels. The first level is a per thread list of free super spans. The second level is a global list of free super spans. Each cache level can be configured to control memory usage versus performance.

# Caveats
Cross-thread deallocations are more costly than in-thread deallocations, since the spans are completely owned by the allocating thread. The free operation will be deferred using an atomic list operation and the actual free operation will be performed when the owner thread requires a new block of the corresponding size class.

VirtualAlloc has an internal granularity of 64KiB. However, mmap lacks this granularity control, and the implementation instead keeps track and atomically increases a running address counter of where memory pages should be mapped to in the virtual address range. If some other code in the process uses mmap to reserve a part of virtual memory spance this counter needs to catch up and resync in order to keep the 64KiB granularity of span addresses, which could potentially be time consuming.

The free, realloc and usable size functions all require the passed pointer to be within the first 64KiB page block of the start of the memory block. You cannot pass in any pointer from the memory block address range.
