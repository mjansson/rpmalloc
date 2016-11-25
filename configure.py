#!/usr/bin/env python

"""Ninja build configurator for foundation library"""

import sys
import os

sys.path.insert(0, os.path.join('ninja'))

import generator

generator = generator.Generator(project = 'rpmalloc', variables = [('bundleidentifier', 'com.rampantpixels.rpmalloc.$(binname)')])
target = generator.target
writer = generator.writer
toolchain = generator.toolchain

rpmalloc_lib = generator.lib(module = 'rpmalloc', sources = ['rpmalloc.c'])

includepaths = ['test', 'benchmark']
test_lib = generator.lib(module = 'test', sources = ['thread.c', 'timer.c'], includepaths = includepaths)

#Build one binary per benchmark
generator.bin(module = '', sources = ['main.c', 'rpmalloc/benchmark.c'], binname = 'benchmark-rpmalloc', basepath = 'benchmark', implicit_deps = [test_lib, rpmalloc_lib], libs = ['test', 'rpmalloc'], includepaths = includepaths)
