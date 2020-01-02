#!/usr/bin/env python

"""Ninja build configurator for rpmalloc library"""

import sys
import os

sys.path.insert(0, os.path.join('build', 'ninja'))

import generator

generator = generator.Generator(project = 'rpmalloc', variables = [('bundleidentifier', 'com.rampantpixels.rpmalloc.$(binname)')])

rpmalloc_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])
rpmalloc_test_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc-test', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1', 'RPMALLOC_CONFIGURABLE=1']})

if not generator.target.is_android() and not generator.target.is_ios():
	rpmalloc_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])

	rpmallocwrap_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_PRELOAD=1', 'ENABLE_OVERRIDE=1']})
	rpmallocwrap_lib = generator.lib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_PRELOAD=1', 'ENABLE_OVERRIDE=1']})

	generator.bin(module = 'test', sources = ['thread.c', 'main.c'], binname = 'rpmalloc-test', implicit_deps = [rpmalloc_test_lib], libs = ['rpmalloc-test'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'RPMALLOC_FIRST_CLASS_HEAPS=1', 'RPMALLOC_CONFIGURABLE=1']})
	generator.bin(module = 'test', sources = ['thread.c', 'main-override.cc'], binname = 'rpmallocwrap-test', implicit_deps = [rpmallocwrap_lib], libs = ['rpmallocwrap'], includepaths = ['rpmalloc', 'test'], variables = {'runtime': 'c++', 'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1']})
