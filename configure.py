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

rpmalloc_lib = generator.lib(module = 'rpmalloc', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'])
if not target.is_windows():
	rpmalloc_so = generator.sharedlib(module = 'rpmalloc', sources = ['rpmalloc.c', 'malloc.c', 'new.cc'], variables = {'runtime': 'c++'})
