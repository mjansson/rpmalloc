#!/usr/bin/env python

"""Ninja toolchain abstraction"""

import sys
import os
import subprocess
import platform
import random
import string
import json
import zlib
import version
import android
import xcode

def supported_toolchains():
  return ['msvc', 'gcc', 'clang', 'intel']

def supported_architectures():
  return ['x86', 'x86-64', 'ppc', 'ppc64', 'arm6', 'arm7', 'arm64', 'mips', 'mips64', 'generic']

def get_boolean_flag(val):
  return (val == True or val == "True" or val == "true" or val == "1" or val == 1)

def make_toolchain(host, target, toolchain):
  if toolchain is None:
    if target.is_raspberrypi():
      toolchain = 'gcc'
    elif host.is_windows() and target.is_windows():
      toolchain = 'msvc'
    else:
      toolchain = 'clang'

  toolchainmodule = __import__(toolchain, globals(), locals())
  return toolchainmodule.create(host, target, toolchain)

def make_pathhash(path, targettype):
  return '-' + hex(zlib.adler32((path + targettype).encode()) & 0xffffffff)[2:-1]

class Toolchain(object):
  def __init__(self, host, target, toolchain):
    self.host = host
    self.target = target
    self.toolchain = toolchain
    self.subninja = ''

    #Set default values
    self.build_monolithic = False
    self.build_coverage = False
    self.support_lua = False
    self.internal_deps = False
    self.python = 'python'
    self.objext = '.o'
    if target.is_windows():
      self.libprefix = ''
      self.staticlibext = '.lib'
      self.dynamiclibext = '.dll'
      self.binprefix = ''
      self.binext = '.exe'
    elif target.is_android():
      self.libprefix = 'lib'
      self.staticlibext = '.a'
      self.dynamiclibext = '.so'
      self.binprefix = 'lib'
      self.binext = '.so'
    elif target.is_pnacl():
      self.libprefix = 'lib'
      self.staticlibext = '.a'
      self.dynamiclibext = '.so'
      self.binprefix = ''
      self.binext = '.bc'
    else:
      self.libprefix = 'lib'
      self.staticlibext = '.a'
      if target.is_macos() or target.is_ios():
        self.dynamiclibext = '.dylib'
      else:
        self.dynamiclibext = '.so'
      self.binprefix = ''
      self.binext = ''

    #Paths
    self.buildpath = os.path.join('build', 'ninja', target.platform)
    self.libpath = os.path.join('lib', target.platform)
    self.binpath = os.path.join('bin', target.platform)

    #Dependency paths
    self.depend_includepaths = []
    self.depend_libpaths = []

    #Target helpers
    self.android = None
    self.xcode = None

    #Command wrappers
    if host.is_windows():
      self.rmcmd = lambda p: 'cmd /C (IF exist ' + p + ' (del /F /Q ' + p + '))'
      self.cdcmd = lambda p: 'cmd /C cd ' + p
      self.mkdircmd = lambda p: 'cmd /C (IF NOT exist ' + p + ' (mkdir ' + p + '))'
      self.copycmd = lambda p, q: 'cmd /C (IF exist ' + q + ' (del /F /Q ' + q + ')) & copy /Y ' + p + ' ' + q + ' > NUL'
    else:
      self.rmcmd = lambda p: 'rm -f ' + p
      self.cdcmd = lambda p: 'cd ' + p
      self.mkdircmd = lambda p: 'mkdir -p ' + p
      self.copycmd = lambda p, q: 'cp -f ' + p + ' ' + q

    #Target functionality
    if target.is_android():
      self.android = android.make_target(self, host, target)
    if target.is_macos() or target.is_ios():
      self.xcode = xcode.make_target(self, host, target)

    #Builders
    self.builders = {}

    #Paths created
    self.paths_created = {}

  def initialize_subninja(self, path):
    self.subninja = path

  def initialize_project(self, project):
    self.project = project
    version.generate_version(self.project, self.project)

  def initialize_archs(self, archs):
    self.archs = list(archs)
    if self.archs is None or self.archs == []:
      self.initialize_default_archs()

  def initialize_default_archs(self):
    if self.target.is_windows():
      self.archs = ['x86-64']
    elif self.target.is_linux() or self.target.is_bsd():
      localarch = subprocess.check_output(['uname', '-m']).strip()
      if localarch == 'x86_64' or localarch == 'amd64':
        self.archs = ['x86-64']
      elif localarch == 'i686':
        self.archs = ['x86']
      else:
        self.archs = [str(localarch)]
    elif self.target.is_macos():
      self.archs = ['x86-64']
    elif self.target.is_ios():
      self.archs = ['arm7', 'arm64']
    elif self.target.is_raspberrypi():
      self.archs = ['arm6']
    elif self.target.is_android():
      self.archs = ['arm7', 'arm64', 'x86', 'x86-64'] #'mips', 'mips64'
    elif self.target.is_tizen():
      self.archs = ['x86', 'arm7']
    elif self.target.is_pnacl():
      self.archs = ['generic']

  def initialize_configs(self, configs):
    self.configs = list(configs)
    if self.configs is None or self.configs == []:
      self.initialize_default_configs()

  def initialize_default_configs(self):
    self.configs = ['debug', 'release']

  def initialize_toolchain(self):
    if self.android != None:
      self.android.initialize_toolchain()
    if self.xcode != None:
      self.xcode.initialize_toolchain()

  def initialize_depends(self, dependlibs):
    for lib in dependlibs:
      includepath = ''
      libpath = ''
      testpaths = [
        os.path.join('..', lib),
        os.path.join('..', lib + '_lib')
      ]
      for testpath in testpaths:
        if os.path.isfile(os.path.join(testpath, lib, lib + '.h')):
          if self.subninja != '':
            basepath, _ = os.path.split(self.subninja)
            _, libpath = os.path.split(testpath)
            testpath = os.path.join(basepath, libpath)
          includepath = testpath
          libpath = testpath
          break
      if includepath == '':
        print("Unable to locate dependent lib: " + lib)
        sys.exit(-1)
      else:
        self.depend_includepaths += [includepath]
        if self.subninja == '':
          self.depend_libpaths += [libpath]

  def build_toolchain(self):
    if self.android != None:
      self.android.build_toolchain()
    if self.xcode != None:
      self.xcode.build_toolchain()

  def parse_default_variables(self, variables):
    if not variables:
      return
    if isinstance(variables, dict):
      iterator = iter(variables.items())
    else:
      iterator = iter(variables)
    for key, val in iterator:
      if key == 'monolithic':
        self.build_monolithic = get_boolean_flag(val)
      elif key == 'coverage':
        self.build_coverage = get_boolean_flag(val)
      elif key == 'support_lua':
        self.support_lua = get_boolean_flag(val)
      elif key == 'internal_deps':
        self.internal_deps = get_boolean_flag(val)
    if self.xcode != None:
      self.xcode.parse_default_variables(variables)

  def read_build_prefs(self):
    self.read_prefs('build.json')
    self.read_prefs(os.path.join('build', 'ninja', 'build.json'))

  def read_prefs(self, filename):
    if not os.path.isfile( filename ):
      return
    file = open(filename, 'r')
    prefs = json.load(file)
    file.close()
    self.parse_prefs(prefs)

  def parse_prefs(self, prefs):
    if 'monolithic' in prefs:
      self.build_monolithic = get_boolean_flag(prefs['monolithic'])
    if 'coverage' in prefs:
      self.build_coverage = get_boolean_flag( prefs['coverage'] )
    if 'support_lua' in prefs:
      self.support_lua = get_boolean_flag(prefs['support_lua'])
    if 'python' in prefs:
      self.python = prefs['python']
    if self.android != None:
      self.android.parse_prefs(prefs)
    if self.xcode != None:
      self.xcode.parse_prefs(prefs)

  def archs(self):
    return self.archs

  def configs(self):
    return self.configs

  def project(self):
    return self.project

  def is_monolithic(self):
    return self.build_monolithic

  def use_coverage(self):
    return self.build_coverage

  def write_variables(self, writer):
    writer.variable('buildpath', self.buildpath)
    writer.variable('target', self.target.platform)
    writer.variable('config', '')
    if self.android != None:
      self.android.write_variables(writer)
    if self.xcode != None:
      self.xcode.write_variables(writer)

  def write_rules(self, writer):
    writer.pool('serial_pool', 1)
    writer.rule('copy', command = self.copycmd('$in', '$out'), description = 'COPY $in -> $out')
    writer.rule('mkdir', command = self.mkdircmd('$out'), description = 'MKDIR $out')
    if self.android != None:
      self.android.write_rules(writer)
    if self.xcode != None:
      self.xcode.write_rules(writer)

  def cdcmd(self):
    return self.cdcmd

  def mkdircmd(self):
    return self.mkdircmd

  def mkdir(self, writer, path, implicit = None, order_only = None):
    if path in self.paths_created:
      return self.paths_created[path]
    if self.subninja != '':
      return
    cmd = writer.build(path, 'mkdir', None, implicit = implicit, order_only = order_only)
    self.paths_created[path] = cmd
    return cmd

  def copy(self, writer, src, dst, implicit = None, order_only = None):
    return writer.build(dst, 'copy', src, implicit = implicit, order_only = order_only)

  def builder_multicopy(self, writer, config, archs, targettype, infiles, outpath, variables):
    output = []
    rootdir = self.mkdir(writer, outpath)
    for file in infiles:
      path, targetfile = os.path.split(file)
      archpath = outpath
      #Find which arch we are copying from and append to target path
      #unless on generic arch targets (only one generic arch)
      if not self.target.is_pnacl():
        for arch in archs:
          remainpath, subdir = os.path.split(path)
          while remainpath != '':
            if subdir == arch:
              archpath = os.path.join(outpath, arch)
              break
            remainpath, subdir = os.path.split(remainpath)
          if remainpath != '':
            break
      targetpath = os.path.join(archpath, targetfile)
      if os.path.normpath(file) != os.path.normpath(targetpath):
        archdir = self.mkdir(writer, archpath, implicit = rootdir)
        output += self.copy(writer, file, targetpath, order_only = archdir)
    return output

  def path_escape(self, path):
    if self.host.is_windows():
      return "\"%s\"" % path.replace("\"", "'")
    return path

  def paths_forward_slash(self, paths):
    return [path.replace('\\', '/') for path in paths]

  def prefix_includepath(self, path):
    if os.path.isabs(path) or self.subninja == '':
      return path
    if path == '.':
      return self.subninja
    return os.path.join(self.subninja, path)

  def prefix_includepaths(self, includepaths):
    return [self.prefix_includepath(path) for path in includepaths]

  def list_per_config(self, config_dicts, config):
    if config_dicts is None:
      return []
    config_list = []
    for config_dict in config_dicts:
      config_list += config_dict[config]
    return config_list

  def implicit_deps(self, config, variables):
    if variables == None:
      return None
    if 'implicit_deps' in variables:
      return self.list_per_config(variables['implicit_deps'], config)
    return None

  def make_implicit_deps(self, outpath, arch, config, dependlibs):
    deps = {}
    deps[config] = []
    for lib in dependlibs:
      if self.target.is_macos() or self.target.is_ios():
        finalpath = os.path.join(self.libpath, config, self.libprefix + lib + self.staticlibext)
      else:
        finalpath = os.path.join(self.libpath, config, arch, self.libprefix + lib + self.staticlibext)
      deps[config] += [finalpath]
    return [deps]

  def compile_file(self, writer, config, arch, targettype, infile, outfile, variables):
    extension = os.path.splitext(infile)[1][1:]
    if extension in self.builders:
      return self.builders[extension](writer, config, arch, targettype, infile, outfile, variables)
    return []

  def compile_node(self, writer, nodetype, config, arch, infiles, outfile, variables):
    if nodetype in self.builders:
      return self.builders[nodetype](writer, config, arch, nodetype, infiles, outfile, variables)
    return []

  def build_sources(self, writer, nodetype, multitype, module, sources, binfile, basepath, outpath, configs, includepaths, libpaths, dependlibs, libs, implicit_deps, variables, frameworks):
    if module != '':
      decoratedmodule = module + make_pathhash(self.subninja + module + binfile, nodetype)
    else:
      decoratedmodule = basepath + make_pathhash(self.subninja + basepath + binfile, nodetype)
    built = {}
    if includepaths is None:
      includepaths = []
    if libpaths is None:
      libpaths = []
    sourcevariables = (variables or {}).copy()
    sourcevariables.update({
                     'includepaths': self.depend_includepaths + self.prefix_includepaths(list(includepaths))})
    if not libs and dependlibs != None:
      libs = []
    if dependlibs != None:
      libs += (dependlibs or [])
    nodevariables = (variables or {}).copy()
    nodevariables.update({
                     'libs': libs,
                     'implicit_deps': implicit_deps,
                     'libpaths': self.depend_libpaths + list(libpaths),
                     'frameworks': frameworks})
    self.module = module
    self.buildtarget = binfile
    for config in configs:
      archnodes = []
      built[config] = []
      for arch in self.archs:
        objs = []
        buildpath = os.path.join('$buildpath', config, arch)
        modulepath = os.path.join(buildpath, basepath, decoratedmodule)
        sourcevariables['modulepath'] = modulepath
        nodevariables['modulepath'] = modulepath
        #Make per-arch-and-config list of final implicit deps, including dependent libs
        if self.internal_deps and dependlibs != None:
          dep_implicit_deps = []
          if implicit_deps:
            dep_implicit_deps += implicit_deps
          dep_implicit_deps += self.make_implicit_deps(outpath, arch, config, dependlibs)
          nodevariables['implicit_deps'] = dep_implicit_deps
        #Compile all sources
        for name in sources:
          if os.path.isabs(name):
            infile = name
            outfile = os.path.join(modulepath, os.path.splitext(os.path.basename(name))[0] + make_pathhash(infile, nodetype) + self.objext)
          else:
            infile = os.path.join(basepath, module, name)
            outfile = os.path.join(modulepath, os.path.splitext(name)[0] + make_pathhash(infile, nodetype) + self.objext)
            if self.subninja != '':
              infile = os.path.join(self.subninja, infile)
          objs += self.compile_file(writer, config, arch, nodetype, infile, outfile, sourcevariables)
        #Build arch node (per-config-and-arch binary)
        archoutpath = os.path.join(modulepath, binfile)
        archnodes += self.compile_node(writer, nodetype, config, arch, objs, archoutpath, nodevariables)
      #Build final config node (per-config binary)
      built[config] += self.compile_node(writer, multitype, config, self.archs, archnodes, os.path.join(outpath, config), None)
    writer.newline()
    return built

  def lib(self, writer, module, sources, libname, basepath, configs, includepaths, variables, outpath = None):
    built = {}
    if basepath == None:
      basepath = ''
    if configs is None:
      configs = list(self.configs)
    if libname is None:
      libname = module
    libfile = self.libprefix + libname + self.staticlibext
    if outpath is None:
      outpath = self.libpath
    return self.build_sources(writer, 'lib', 'multilib', module, sources, libfile, basepath, outpath, configs, includepaths, None, None, None, None, variables, None)

  def sharedlib(self, writer, module, sources, libname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables, outpath = None):
    built = {}
    if basepath == None:
      basepath = ''
    if configs is None:
      configs = list(self.configs)
    if libname is None:
      libname = module
    libfile = self.libprefix + libname + self.dynamiclibext
    if outpath is None:
      outpath = self.binpath
    return self.build_sources(writer, 'sharedlib', 'multisharedlib', module, sources, libfile, basepath, outpath, configs, includepaths, libpaths, dependlibs, libs, implicit_deps, variables, frameworks)

  def bin(self, writer, module, sources, binname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables, outpath = None):
    built = {}
    if basepath == None:
      basepath = ''
    if configs is None:
      configs = list(self.configs)
    binfile = self.binprefix + binname + self.binext
    if outpath is None:
      outpath = self.binpath
    return self.build_sources(writer, 'bin', 'multibin', module, sources, binfile, basepath, outpath, configs, includepaths, libpaths, dependlibs, libs, implicit_deps, variables, frameworks)

  def app(self, writer, module, sources, binname, basepath, configs, includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables, resources):
    builtbin = []
    # Filter out platforms that do not have app concept
    if not (self.target.is_macos() or self.target.is_ios() or self.target.is_android() or self.target.is_tizen()):
      return builtbin
    if basepath is None:
      basepath = ''
    if binname is None:
      binname = module
    if configs is None:
      configs = list(self.configs)
    for config in configs:
      archbins = self.bin(writer, module, sources, binname, basepath, [config], includepaths, libpaths, implicit_deps, dependlibs, libs, frameworks, variables, '$buildpath')
      if self.target.is_macos() or self.target.is_ios():
        binpath = os.path.join(self.binpath, config, binname + '.app')
        builtbin += self.xcode.app(self, writer, module, archbins, self.binpath, binname, basepath, config, None, resources, True)
      if self.target.is_android():
        javasources = [name for name in sources if name.endswith('.java')]
        builtbin += self.android.apk(self, writer, module, archbins, javasources, self.binpath, binname, basepath, config, None, resources)
      #elif self.target.is_tizen():
      #  builtbin += self.tizen.tpk( writer, config, basepath, module, binname = binname, archbins = archbins, resources = resources )
    return builtbin
