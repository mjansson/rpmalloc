#!/usr/bin/env python

"""Ninja build configurator for rpmalloc library"""

import sys
import os

sys.path.insert(0, os.path.join('build', 'ninja'))

import generator

generator = generator.Generator(project = 'rpmalloc', variables = [('bundleidentifier', 'com.rampantpixels.rpmalloc.$(binname)')])
target = generator.target
writer = generator.writer
toolchain = generator.toolchain

rpmalloc_lib = generator.lib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])
rpmallocguards_lib = generator.lib(module = 'rpmalloc', libname = 'rpmallocguards', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_GUARDS=1']})

if not target.is_android():
	rpmallocwrap_lib = generator.lib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'], variables = {'defines': ['ENABLE_PRELOAD=1']})

if not target.is_windows() and not target.is_android() and not target.is_ios():
	rpmalloc_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])
	rpmallocwrap_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'], variables = {'runtime': 'c++', 'defines': ['ENABLE_PRELOAD=1']})

if not target.is_ios() and not target.is_android():
	generator.bin(module = 'test', sources = ['thread.c', 'main.c'], binname = 'rpmalloc-test', implicit_deps = [rpmalloc_lib], libs = ['rpmalloc'], includepaths = ['rpmalloc', 'test'])
	generator.bin(module = 'test', sources = ['thread.c', 'main.c'], binname = 'rpmalloc-test-guards', implicit_deps = [rpmallocguards_lib], libs = ['rpmallocguards'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_GUARDS=1']})
