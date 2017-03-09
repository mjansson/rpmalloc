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
We believe rpmalloc is faster than most popular memory allocators like tcmalloc, hoard, ptmalloc3 and others. We also believe the implementation to be easier to read and modify compared to these allocators, as it is a single source file of ~1200 lines of C code.

Contained in the repository is a benchmark utility that performs interleaved allocations (both aligned to 8 or 16 bytes, and unaligned) and deallocations (both in-thread and cross-thread) in multiple threads. It measures number of memory operations performed per CPU second, as well as memory overhead by comparing the virtual memory mapped with the number of bytes requested in allocation calls. The setup of number of thread, cross-thread deallocation rate and allocation size limits is configured by command line arguments.

Below is comparison charts of performance and memory overhead of rpmalloc and other popular allocator implementations.

![Windows random [16, 1000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=137567195&format=image)

![Windows random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1511494420&format=image)

![Windows random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1778024863&format=image)

The benchmarks producing these numbers were run on a Windows 10 machine with 8 logical cores (4 physical, HT). The actual numbers are not to be interpreted as absolute performance figures, but rather as relative comparisons between the different allocators.

