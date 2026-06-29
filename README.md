# rpmalloc - General Purpose Memory Allocator
This library provides a public domain cross platform lock free thread caching 16-byte aligned memory allocator implemented in C. The latest source code is always available at https://github.com/mjansson/rpmalloc

Created by Mattias Jansson ([@maniccoder](https://twitter.com/maniccoder)) - Discord server for discussions at https://discord.gg/M8BwTQrt6c 

Platforms currently supported:

- Windows
- Linux
- MacOS
- iOS
- Android

The code should be easily portable to any platform with atomic operations and an mmap-style virtual memory management API. The API used to map/unmap memory pages can be configured in runtime to a custom implementation, as can the memory page size.

This library is put in the public domain; you can redistribute it and/or modify it without any restrictions. Or, if you choose, you can use it under the MIT license.

# Performance
We believe rpmalloc is faster than most popular memory allocators like tcmalloc, hoard, ptmalloc3 and others without causing extra allocated memory overhead in the thread caches compared to these allocators. We also believe the implementation to be easier to read and modify compared to these allocators, as it is a single source file of ~3300 lines of C code. All allocations have a natural 16-byte alignment.

The chart below shows throughput on the `rptest` benchmark, allocating randomly sized blocks in the `[16, 8000]` bytes range with a linear falloff distribution and cross-thread frees, as the number of threads scales from 1 to 16. Higher is better.

![rptest throughput, random [16,8000] bytes, 1-16 threads](benchmark/images/rptest-perf.png)

Peak resident memory for the same benchmark, lower is better:

![rptest peak memory, random [16,8000] bytes, 1-16 threads](benchmark/images/rptest-memory.png)

The numbers were collected on a 13th Gen Intel Core i7-13800H (14 cores / 20 threads) running Ubuntu 26.04, comparing rpmalloc against the current releases of several popular allocators. The actual numbers are not to be interpreted as absolute performance figures, but rather as relative comparisons between the different allocators. For a full allocator comparison across the mimalloc-bench benchmark suite, including peak memory usage, see the [BENCHMARKS](BENCHMARKS.md) file. The captured results and the script that produces these graphs live in the [benchmark](benchmark) directory.

# Required functions

The allocator is initialized automatically with default configuration on the first allocation request, so no explicit initialization is required. If you want to provide a custom memory interface or configuration you __MUST__ call the initialization function, either __rpmalloc_initialize__ or __rpmalloc_initialize_config__, before any other call into the allocator.

Before terminating your use of the allocator, you __SHOULD__ call __rpmalloc_finalize__ in order to release caches and unmap virtual memory, as well as prepare the allocator for global scope cleanup at process exit or dynamic library unload depending on your use case.

# Using
The easiest way to use the library is simply adding __rpmalloc.[h|c]__ to your project and compile them along with your sources. The allocator is completely self contained, you are not required to call the init/fini functions from your own code, but can do so in order to initialize and finalize the allocator in specific places or provide your own memory interface and/or configuration:

__rpmalloc_initialize__ : Call at process start to initialize the allocator, optionally passing a custom memory interface with functions to map, commit, decommit and unmap memory pages (pass null to use the default OS backed implementation)

__rpmalloc_initialize_config__ : Optional entry point to call at process start to initialize the allocator with a custom memory interface and/or a configuration of memory page size, huge page or transparent huge page usage, decommit behaviour, page naming and unmap-on-finalize behaviour

__rpmalloc_finalize__: Call at process exit to finalize the allocator

__rpmalloc_thread_initialize__: Call at each thread start to initialize the thread local data for the allocator (done automatically on the first allocation in a thread)

__rpmalloc_thread_finalize__: Call at each thread exit to finalize and release the thread heap for reuse by other threads (done automatically at thread exit on platforms supporting thread destructors)

__rpmalloc_config__: Get the current runtime configuration of the allocator

Then simply use the __rpmalloc__/__rpfree__ and the other malloc style replacement functions. Remember all allocations are 16-byte aligned, so no need to call the explicit rpmemalign/rpaligned_alloc/rpposix_memalign functions unless you need greater alignment, they are simply wrappers to make it easier to replace in existing code.

If you wish to override the standard library malloc family of functions and have automatic initialization/finalization of process and threads, define __ENABLE_OVERRIDE__ to non-zero (default is 1) which will include the `malloc.c` file in compilation of __rpmalloc.c__, and then rebuild the library or your project where you added the rpmalloc source. If you compile rpmalloc as a separate library you must make the linker use the override symbols from the library by referencing at least one symbol. The easiest way is to simply include `rpmalloc.h` in at least one source file and call `rpmalloc_linker_reference` somewhere - it's a dummy empty function. For C++ overrides you have to `#include <rpnew.h>` in at least one source file. The list of libc entry points replaced may not be complete, use libc/stdc++ replacement only as a convenience for testing the library on an existing code base, not a final solution.

For explicit first class heaps, see the __rpmalloc_heap_*__ API under [first class heaps](#first-class-heaps) section, requiring __RPMALLOC_FIRST_CLASS_HEAPS__ to be defined to 1 - default is 0, as it imposes a very slight performance hit in deallocation path from an extra conditinal instruction.

# Building
To compile as a static library run the configure python script which generates a Ninja build script, then build using ninja. The ninja build produces both a static and a dynamic library named `rpmalloc`.

By default the dynamic library can be used with `LD_PRELOAD`/`DYLD_INSERT_LIBRARIES` to inject in a preexisting binary, replacing any malloc/free family of function calls (when __ENABLE_OVERRIDE__ is defined to 1). This is only implemented for Linux and macOS targets. The list of libc entry points replaced may not be complete, use preloading as a convenience for testing the library on an existing binary, not a final solution.

The latest stable release is available in the master branch. For latest development code, use the develop branch.

# Configuration options
Detailed statistics are available if __ENABLE_STATISTICS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`. This will cause a slight overhead in runtime to collect statistics for memory page and huge block operations.

Detection of allocations still outstanding at `rpmalloc_finalize` is enabled if __ENABLE_LEAK_DETECTION__ is defined to 1. It requires __ENABLE_STATISTICS__ for the allocation counters and follows it by default, except when building with __ENABLE_OVERRIDE__ where it defaults to 0: with the standard library override rpmalloc is the backing store for the C runtime's own allocations (such as per-thread TLS), which are still live at finalize and indistinguishable from application leaks. When __ENABLE_ASSERTS__ is also enabled a detected leak triggers an assert.

Integer safety checks on all calls are enabled if __ENABLE_VALIDATE_ARGS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`. If enabled, size arguments to the global entry points are verified not to cause integer overflows in calculations.

Asserts are enabled if __ENABLE_ASSERTS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`.

To include __malloc.c__ in compilation and provide overrides of standard library malloc entry points define __ENABLE_OVERRIDE__ to 1 (this is the default).

To enable support for first class heaps, define __RPMALLOC_FIRST_CLASS_HEAPS__ to 1 (default is 0, or disabled).

# Huge pages
The allocator has support for huge/large pages on Windows, Linux and MacOS. To enable it, pass a non-zero value in the config value `enable_huge_pages` when initializing the allocator with `rpmalloc_initialize_config`. Explicit huge pages require system configuration (a preallocated huge page pool on Linux, the `SeLockMemoryPrivilege` on Windows). At initialization rpmalloc checks whether huge pages are actually available (on Linux by probing a real huge page mapping, not just reading the configured huge page size). If huge pages were requested but are not available on the system, `rpmalloc_initialize`/`rpmalloc_initialize_config` returns a non-zero value and leaves the allocator uninitialized (the effective `enable_huge_pages` is cleared), so you can detect this and re-initialize without huge pages if desired. On Linux, if an individual huge page backed mapping cannot later be served from the pool the allocator falls back to a normal mapping promoted to transparent huge pages. On Windows, large pages cannot be decommitted, so unused memory ranges are kept committed until unmapped when huge pages are enabled - on Linux unused memory ranges are decommitted if the kernel supports it (5.18 or later for huge page backed mappings).

On Linux and Android the allocator can alternatively use transparent huge pages without requiring any system configuration or preallocated huge page pool. Pass a non-zero value in the config value `enable_thp` and the allocator will advise the kernel to back the memory mappings with transparent huge pages using `madvise(MADV_HUGEPAGE)`. Unlike `enable_huge_pages` this does not affect page size or commit/decommit behaviour. The two modes are mutually exclusive, with `enable_huge_pages` taking precedence.

# Quick overview
The allocator uses separate heaps for each thread and partitions memory blocks according to a preconfigured set of size classes, up to 2MiB. Huge blocks above this limit are mapped and unmapped directly. Blocks are allocated from a `page` of multiple blocks, all of the same size class. Each `page` is one of four page types, small, medium-small, medium-large or large, depending on the size class. Each `page` belongs to an even larger `span` of pages, each of the same page type.

# Implementation details
The allocator is based on a fixed span alignment of 256MiB, a fixed page alignment per page type, and 16 byte block alignment within the page. The span alignment is achieved by oversizing the memory mapping and aligning the returned virtual memory address to the required boundary (on POSIX systems the excess address range is immediately unmapped). By aligning to fixed sizes the free operation can locate the span and page headers without having to do a table lookup by simply masking out the low bits of the block address.

Memory blocks are divided into the four page types. Small pages have blocks in [0, 4096] bytes, medium-small pages blocks in (4096, 32768] bytes, medium-large pages blocks in (32768, 262144] bytes, and large pages blocks in (262144, 2097152] bytes. The page types use page sizes of 64KiB, 1MiB, 4MiB and 16MiB respectively, and are further divided in block size classes, where the smallest block sizes have a fixed granularity and interval of 16 bytes, and larger blocks have a variable interval to limit overhead to a fixed ratio.

Each span belongs to a single heap that owns all contained pages and blocks. To avoid locks, each page is completely owned by the allocating thread, and all cross-thread deallocations will be deferred to the owner thread through a separate atomic free list per page.

# Memory mapping
By default the allocator uses OS APIs to map virtual memory pages as needed, either `VirtualAlloc` on Windows or `mmap` on POSIX systems. If you want to use your own custom memory mapping provider you can use __rpmalloc_initialize__ or __rpmalloc_initialize_config__ and pass a memory interface with function pointers to map, commit, decommit and unmap virtual memory. The map function should reserve the requested number of bytes, the commit and decommit functions control whether a range of the mapping is backed by physical memory, and the unmap function should release the entire mapped range. If you provide either a map or unmap function you must provide both, or the default implementation will be used for all functions.

The returned memory address from the memory map function MUST be aligned to the requested alignment, which is either zero or the span size. The map function can store an alignment offset and the actual mapped size in the provided output parameters, which will be passed back to the unmap function when the entire mapped range is released.

Memory mapping requests are always done in multiples of the memory page size. You can specify a custom page size when initializing rpmalloc with __rpmalloc_initialize_config__, or pass 0 to let rpmalloc determine the system memory page size using OS APIs. The page size MUST be a power of two.

On macOS and iOS mmap requests are tagged with tag 240 for easy identification with the vmmap tool.

# Memory fragmentation
There is no memory fragmentation by the allocator in the sense that it will not leave unallocated and unusable "holes" in the memory pages by calls to allocate and free blocks of different sizes. This is due to the fact that the memory pages allocated for each size class is split up in perfectly aligned blocks which are not reused for a request of a different size. The block freed by a call to `rpfree` will always be immediately available for an allocation request within the same size class.

However, there is memory fragmentation in the meaning that a request for x bytes followed by a request of y bytes where x and y are at least one size class different in size will return blocks that are at least one memory page apart in virtual address space. Only blocks of the same size will potentially be within the same memory page span.

rpmalloc keeps an "active page" and free list for each size class. This means back-to-back allocations will most likely be served from within the same memory page (unless the page runs out of free blocks). The rpmalloc implementation will also use any "holes" in semi-filled memory pages before initializing blocks in a new page.

# First class heaps
rpmalloc provides a first class heap type with explicit heap control API. Heaps are maintained with calls to __rpmalloc_heap_acquire__ and __rpmalloc_heap_release__ and allocations/frees are done with __rpmalloc_heap_alloc__ and __rpmalloc_heap_free__. See the `rpmalloc.h` documentation for the full list of functions in the heap API. The main use case of explicit heap control is to scope allocations in a heap and release everything with a single call to __rpmalloc_heap_free_all__ without having to maintain ownership of memory blocks. Note that the heap API is not thread-safe, the caller must make sure that each heap is only used in a single thread at any given time.

# Producer-consumer scenario
Compared to the some other allocators, rpmalloc does not suffer as much from a producer-consumer thread scenario where one thread allocates memory blocks and another thread frees the blocks. In some allocators the free blocks need to traverse both the thread cache of the thread doing the free operations as well as the global cache before being reused in the allocating thread. In rpmalloc the freed blocks are deferred to the owning thread and will be reused as soon as that thread needs more blocks. This enables faster release of completely freed memory pages as blocks in a memory page will not be aliased between different owning threads.

# Best case scenarios
Threads that keep ownership of allocated memory blocks within the thread and free the blocks from the same thread will have optimal performance.

Threads that have allocation patterns where the difference in memory usage high and low water marks fit within the free page retention thresholds in the allocator will never decommit or recommit memory pages and have optimal performance.

# Worst case scenarios
Since each heap maps a span of memory pages per page type, a thread that allocates just a few blocks of each size class (16, 32, ...) for many size classes will commit a memory page for each used size class, while only using a small fraction of the committed memory. However, memory pages are committed on demand and blocks are initialized only as needed, so the actual amount of touched physical memory is far lower than the mapped address space. Free memory pages are reused by all size classes of the same page type.

Threads that perform a lot of allocations and deallocations in a pattern that have a large difference in high and low water marks, and that difference is larger than the free page retention thresholds, will cause memory pages to be repeatedly decommitted and recommitted. This can be mitigated by setting `disable_decommit` in the configuration (at the cost of higher average memory overhead).

# Caveats
To guarantee the fixed span alignment the implementation oversizes the memory mapping and aligns the returned address to the span size boundary. On POSIX systems the excess address range is immediately unmapped, while on Windows the entire mapping is retained as the OS does not support partial release. Since the extra memory pages are never touched the oversized mapping will not result in extra physical memory usage, but rather only increase virtual memory address space.

All entry points assume the passed values are valid, for example passing an invalid pointer to free would most likely result in a segmentation fault. __The library does not try to guard against errors!__.

# Other languages

[Johan Andersson](https://github.com/repi) at Embark has created a Rust wrapper available at [rpmalloc-rs](https://github.com/EmbarkStudios/rpmalloc-rs)

[Stas Denisov](https://github.com/nxrighthere) has created a C# wrapper available at [Rpmalloc-CSharp](https://github.com/nxrighthere/Rpmalloc-CSharp)

# License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>


You can also use this software under the MIT license if public domain is
not recognized in your country


The MIT License (MIT)

Copyright (c) 2017 Mattias Jansson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
