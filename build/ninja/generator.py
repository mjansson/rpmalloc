#!/usr/bin/env python

"""Ninja build generator"""

import argparse
import os
import pipes
import sys

import platform
import toolchain
import syntax

class Generator(object):
  def __init__(self, project, includepaths = [], dependlibs = [], libpaths = [], variables = None):
    parser = argparse.ArgumentParser(description = 'Ninja build generator')
    parser.add_argument('-t', '--target',
                        help = 'Target platform',
                        choices = platform.supported_platforms())
    parser.add_argument('--host',
                        help = 'Host platform',
                        choices = platform.supported_platforms())
    parser.add_argument('--toolchain',
                        help = 'Toolchain to use',
                        choices = toolchain.supported_toolchains())
    parser.add_argument('-c', '--config', action = 'append',
                        help = 'Build configuration',
                        choices = ['debug', 'release', 'profile', 'deploy'],
                        default = [])
    parser.add_argument('-a', '--arch', action = 'append',
                        help = 'Add architecture',
                        choices = toolchain.supported_architectures(),
                        default = [])
    parser.add_argument('-i', '--includepath', action = 'append',
                        help = 'Add include path',
                        default = [])
    parser.add_argument('--monolithic', action='store_true',
                        help = 'Build monolithic test suite',
                        default = False)
    parser.add_argument('--coverage', action='store_true',
                        help = 'Build with code coverage',
                        default = False)
    parser.add_argument('--subninja', action='store',
                        help = 'Build as subproject (exclude rules and pools) with the given subpath',
                        default = '')
    options = parser.parse_args()

    self.project = project
    self.target = platform.Platform(options.target)
    self.host = platform.Platform(options.host)
    self.subninja = options.subninja
    archs = options.arch
    configs = options.config
    if includepaths is None:
      includepaths = []
    if not options.includepath is None:
      includepaths += options.includepath

    buildfile = open('build.ninja', 'w')
    self.writer = syntax.Writer(buildfile)

    self.writer.variable('ninja_required_version', '1.3')
    self.writer.newline()

    self.writer.comment('configure.py arguments')
    self.writer.variable('configure_args', ' '.join(sys.argv[1:]))
    self.writer.newline()

    self.writer.comment('configure options')
    self.writer.variable('configure_target', self.target.platform)
    self.writer.variable('configure_host', self.host.platform)

    env_keys = set(['CC', 'AR', 'LINK', 'CFLAGS', 'ARFLAGS', 'LINKFLAGS'])
    configure_env = dict((key, os.environ[key]) for key in os.environ if key in env_keys)
    if configure_env:
      config_str = ' '.join([key + '=' + pipes.quote(configure_env[key]) for key in configure_env])
      writer.variable('configure_env', config_str + '$ ')

    if variables is None:
      variables = {}
    if not isinstance(variables, dict):
      variables = dict(variables)

    if options.monolithic:
      variables['monolithic'] = True
    if options.coverage:
      variables['coverage'] = True
    if self.subninja != '':
      variables['internal_deps'] = True

    self.toolchain = toolchain.make_toolchain(self.host, self.target, options.toolchain)
    self.toolchain.initialize(project, archs, configs, includepaths, dependlibs, libpaths, variables, self.subninja)

    self.writer.variable('configure_toolchain', self.toolchain.name())
    self.writer.variable('configure_archs', archs)
    self.writer.variable('configure_configs', configs)
    self.writer.newline()

    self.toolchain.write_variables(self.writer)
    if self.subninja == '':
      self.toolchain.write_rules(self.writer)

  def target(self):
    return self.target

  def host(self):
    return self.host

  def toolchain(self):
    return self.toolchain

  def writer(self):
    return self.writer

  def is_subninja(self):
    return self.subninja != ''

  def lib(self, module, sources, libname = None, basepath = None, configs = None, includepaths = None, variables = None):
    return self.toolchain.lib(self.writer, module, sources, libname, basepath, configs, includepaths, variables)

  def sharedlib(self, module, sources, libname = None, basepath = None, configs = None, includepaths = None, libpaths = None, implicit_deps = None, dependlibs = None, libs = None, frameworks = None, variables = None):
    return self.toolchain.sharedlib(self.writer, module, sources, libname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables)

  def bin(self, module, sources, binname, basepath = None, configs = None, includepaths = None, libpaths = None, implicit_deps = None, dependlibs = None, libs = None, frameworks = None, variables = None):
    return self.toolchain.bin(self.writer, module, sources, binname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables)

  def app(self, module, sources, binname, basepath = None, configs = None, includepaths = None, libpaths = None, implicit_deps = None, dependlibs = None, libs = None, frameworks = None, variables = None, resources = None):
    return self.toolchain.app(self.writer, module, sources, binname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables, resources)

  def test_includepaths(self):
    #TODO: This is ugly
    if self.project == "foundation":
      return ['test']
    return ['test', os.path.join('..', 'foundation_lib', 'test')]

  def test_monolithic(self):
    return self.toolchain.is_monolithic()
