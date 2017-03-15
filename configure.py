#!/usr/bin/env python

"""Ninja build configurator for foundation library"""

import sys
import os

sys.path.insert(0, os.path.join('build', 'ninja'))

import generator

generator = generator.Generator(project = 'rpmalloc', variables = [('bundleidentifier', 'com.rampantpixels.rpmalloc.$(binname)')])
target = generator.target
writer = generator.writer
toolchain = generator.toolchain

rpmalloc_lib = generator.lib(module = 'rpmalloc', sources = ['rpmalloc.c'])

includepaths = ['test', 'benchmark']
test_lib = generator.lib(module = 'test', sources = ['thread.c', 'timer.c'], includepaths = includepaths)
benchmark_lib = generator.lib(module = 'benchmark', sources = ['main.c'], includepaths = includepaths)

#Build one binary per benchmark
generator.bin(module = 'rpmalloc', sources = ['benchmark.c'], binname = 'benchmark-rpmalloc', basepath = 'benchmark', implicit_deps = [benchmark_lib, test_lib, rpmalloc_lib], libs = ['benchmark', 'test', 'rpmalloc'], includepaths = includepaths)
generator.bin(module = 'crt', sources = ['benchmark.c'], binname = 'benchmark-crt', basepath = 'benchmark', implicit_deps = [benchmark_lib, test_lib], libs = ['benchmark', 'test'], includepaths = includepaths)
if not target.is_android():
	generator.bin(module = 'nedmalloc', sources = ['benchmark.c', 'nedmalloc.c'], binname = 'benchmark-nedmalloc', basepath = 'benchmark', implicit_deps = [benchmark_lib, test_lib], libs = ['benchmark', 'test'], includepaths = includepaths, externalsources = True)

platform_includepaths = [os.path.join('benchmark', 'ptmalloc3')]
if target.is_windows():
	platform_includepaths += [os.path.join('benchmark', 'ptmalloc3', 'sysdeps', 'windows')]
else:
	platform_includepaths += [os.path.join('benchmark', 'ptmalloc3', 'sysdeps', 'pthread')]
generator.bin(module = 'ptmalloc3', sources = ['benchmark.c', 'ptmalloc3.c', 'malloc.c'], binname = 'benchmark-ptmalloc3', basepath = 'benchmark', implicit_deps = [benchmark_lib, test_lib], libs = ['benchmark', 'test'], includepaths = includepaths + platform_includepaths, externalsources = True)

hoardincludepaths = [
	os.path.join('benchmark', 'hoard', 'include'),
	os.path.join('benchmark', 'hoard', 'include', 'hoard'),
	os.path.join('benchmark', 'hoard', 'include', 'util'),
	os.path.join('benchmark', 'hoard', 'include', 'superblocks'),
	os.path.join('benchmark', 'hoard'),
	os.path.join('benchmark', 'hoard', 'Heap-Layers')
]
hoardsources = ['source/libhoard.cpp']
if target.is_macosx() or target.is_ios():
	hoardsources += ['Heap-Layers/wrappers/macwrapper.cpp']
elif target.is_windows():
	hoardsources += ['Heap-Layers/wrappers/winwrapper.cpp']
else:
	hoardsources += ['Heap-Layers/wrappers/gnuwrapper.cpp']
if target.is_macosx() or target.is_ios():
	hoardsources += ['source/mactls.cpp']
elif target.is_windows():
	hoardsources += ['source/wintls.cpp']
else:
	hoardsources += ['source/unixtls.cpp']
if not target.is_android():
	hoard_lib = generator.lib(module = 'hoard', sources = hoardsources, basepath = 'benchmark', includepaths = includepaths + hoardincludepaths, externalsources = True)
	hoard_depend_libs = ['hoard', 'benchmark', 'test']
	generator.bin(module = 'hoard', sources = ['benchmark.c'], binname = 'benchmark-hoard', basepath = 'benchmark', implicit_deps = [hoard_lib, benchmark_lib, test_lib], libs = hoard_depend_libs, includepaths = includepaths, variables = {'runtime': 'c++'})

gperftoolsincludepaths = [
	os.path.join('benchmark', 'gperftools', 'src'),
	os.path.join('benchmark', 'gperftools', 'src', 'base'),
	os.path.join('benchmark', 'gperftools', 'src', target.get())
]
gperftoolsbasesources = [
	'dynamic_annotations.c', 'linuxthreads.cc', 'logging.cc', 'low_level_alloc.cc', 'spinlock.cc',
	'spinlock_internal.cc', 'sysinfo.cc'
]
if not target.is_windows():
	gperftoolsbasesources += ['thread_lister.c']
gperftoolsbasesources = [os.path.join('src', 'base', path) for path in gperftoolsbasesources]
gperftoolssources = [
	'central_freelist.cc', 'common.cc', 'fake_stacktrace_scope.cc', 'internal_logging.cc',
	'malloc_extension.cc', 'malloc_hook.cc', 'memfs_malloc.cc', 'memory_region_map.cc',
	'page_heap.cc', 'raw_printer.cc', 'sampler.cc',  'stacktrace.cc', 'stack_trace_table.cc',
	'static_vars.cc', 'span.cc', 'symbolize.cc', 'tcmalloc.cc', 'thread_cache.cc'
]
if not target.is_windows():
	gperftoolssources += ['maybe_threads.cc', 'system-alloc.cc']
if target.is_windows():
	gperftoolssources += [os.path.join('windows', 'port.cc'), os.path.join('windows', 'system-alloc.cc')]
gperftoolssources = [os.path.join('src', path) for path in gperftoolssources]
if not target.is_android():
	gperftools_lib = generator.lib(module = 'gperftools', sources = gperftoolsbasesources + gperftoolssources, basepath = 'benchmark', includepaths = includepaths + gperftoolsincludepaths, externalsources = True)
	gperftools_depend_libs = ['gperftools', 'benchmark', 'test']
	generator.bin(module = 'gperftools', sources = ['benchmark.c'], binname = 'benchmark-tcmalloc', basepath = 'benchmark', implicit_deps = [gperftools_lib, benchmark_lib, test_lib], libs = gperftools_depend_libs, includepaths = includepaths, variables = {'runtime': 'c++'})
