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
rpmallocguard_lib = generator.lib(module = 'rpmalloc', libname = 'rpmallocguard', sources = ['rpmalloc.c'], variables = {'defines': ['ENABLE_ASSERTS=1', 'ENABLE_STATISTICS=1', 'ENABLE_GUARDS=1']})

if not target.is_android() and not target.is_ios():
	rpmallocwrap_lib = generator.lib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'], variables = {'defines': ['ENABLE_PRELOAD=1']})

if not target.is_android() and not target.is_ios():
	rpmalloc_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmalloc', sources = ['rpmalloc.c'])

if not target.is_windows() and not target.is_android() and not target.is_ios():
	rpmallocwrap_so = generator.sharedlib(module = 'rpmalloc', libname = 'rpmallocwrap', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'], variables = {'runtime': 'c++', 'defines': ['ENABLE_PRELOAD=1']})

if not target.is_ios() and not target.is_android():
	generator.bin(module = 'test', sources = ['thread.c', 'main.c'], binname = 'rpmalloc-test', implicit_deps = [rpmallocguard_lib], libs = ['rpmallocguard'], includepaths = ['rpmalloc', 'test'], variables = {'defines': ['ENABLE_GUARDS=1']})
