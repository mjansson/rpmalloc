#!/usr/bin/env python3

"""Ninja build configurator for rpmalloc library"""

import sys
import os

sys.path.insert(0, os.path.join('build', 'ninja'))

import generator

generator = generator.Generator(project = 'rpmalloc', variables = [('bundleidentifier', 'com.maniccoder.rpmalloc.$(binname)')])

rpmalloc_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])
rpmalloc_test_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc-test', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_OVERRIDE=1', 'ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1']})
# Library variant without the standard library override, used by the no-override test binary which
# exercises unmap_on_finalize (a no-op under the override, see rpmalloc.h).
rpmalloc_test_nooverride_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc-test-nooverride', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_OVERRIDE=0', 'ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1']})

if not generator.target.is_android() and not generator.target.is_ios():
	rpmalloc_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_DYNAMIC_LINK=1']})

	generator.bin(module = 'test', sources = ['thread.c', 'main.c', 'main-override.cc'], binname = 'rpmalloc-test', implicit_deps = [rpmalloc_test_lib], libs = ['rpmalloc-test'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1', 'RPMALLOC_TEST_OVERRIDE=1']})

	# No-override test binary: runs the unmap_on_finalize tests (test_finalize_unmap, test_heap_packing)
	# against a library built without the override, where unmap_on_finalize is actually honored. It does
	# not link main-override.cc and leaves RPMALLOC_TEST_OVERRIDE at its default 0 so the override tests
	# are skipped and the unmap tests run.
	generator.bin(module = 'test', sources = ['thread.c', 'main.c'], binname = 'rpmalloc-test-nooverride', implicit_deps = [rpmalloc_test_nooverride_lib], libs = ['rpmalloc-test-nooverride'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1']})

	# Long-running multithreaded stress harness. Built here so it keeps compiling, but it is meant
	# to be run manually (ideally under a sanitizer), not as part of CI - see test/stress.c.
	generator.bin(module = 'test', sources = ['thread.c', 'stress.c'], binname = 'rpmalloc-stress', implicit_deps = [rpmalloc_test_nooverride_lib], libs = ['rpmalloc-test-nooverride'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1']})
