#!/usr/bin/env python

"""Ninja toolchain abstraction for GCC compiler suite"""

import os

import toolchain

class GCCToolchain(toolchain.Toolchain):

  def initialize(self, project, archs, configs, includepaths, dependlibs, libpaths, variables, subninja):
    #Local variable defaults
    self.toolchain = ''
    self.includepaths = []
    self.libpaths = libpaths
    self.ccompiler = 'gcc'
    self.cxxcompiler = 'g++'
    self.archiver = 'ar'
    self.linker = 'gcc'
    self.cxxlinker = 'g++'

    #Command definitions
    self.cccmd = '$toolchain$cc -MMD -MT $out -MF $out.d $includepaths $moreincludepaths $cflags $carchflags $cconfigflags $cmoreflags -c $in -o $out'
    self.cxxcmd = '$toolchain$cxx -MMD -MT $out -MF $out.d $includepaths $moreincludepaths $cxxflags $carchflags $cconfigflags $cmoreflags -c $in -o $out'
    self.ccdeps = 'gcc'
    self.ccdepfile = '$out.d'
    self.arcmd = self.rmcmd('$out') + ' && $toolchain$ar crsD $ararchflags $arflags $out $in'
    self.linkcmd = '$toolchain$link $libpaths $configlibpaths $linkflags $linkarchflags $linkconfigflags -o $out $in $libs $archlibs $oslibs'

    #Base flags
    self.cflags = ['-D' + project.upper() + '_COMPILE=1',
                   '-funit-at-a-time', '-fstrict-aliasing',
                   '-fno-math-errno','-ffinite-math-only', '-funsafe-math-optimizations',
                   '-fno-trapping-math', '-ffast-math']
    self.cwarnflags = ['-Wextra', '-Wall', '-Werror']
    self.cmoreflags = []
    self.mflags = []
    self.arflags = []
    self.linkflags = []
    self.oslibs = []

    self.initialize_subninja(subninja)
    self.initialize_archs(archs)
    self.initialize_configs(configs)
    self.initialize_project(project)
    self.initialize_toolchain()
    self.initialize_depends(dependlibs)

    self.parse_default_variables(variables)
    self.read_build_prefs()

    if self.target.is_linux() or self.target.is_bsd() or self.target.is_raspberrypi():
      self.cflags += ['-D_GNU_SOURCE=1']
      self.linkflags += ['-pthread']
      self.oslibs += ['m']
    if self.target.is_linux() or self.target.is_raspberrypi():
      self.oslibs += ['dl']
    if self.target.is_bsd():
      self.oslibs += ['execinfo']

    self.includepaths = self.prefix_includepaths((includepaths or []) + ['.'])

    if self.is_monolithic():
      self.cflags += ['-DBUILD_MONOLITHIC=1']
    if self.use_coverage():
      self.cflags += ['--coverage']
      self.linkflags += ['--coverage']

    if not 'nowarning' in variables or not variables['nowarning']:
      self.cflags += self.cwarnflags
    self.cxxflags = list(self.cflags)

    self.cflags += ['-std=c11']
    if self.target.is_macos() or self.target.is_ios():
      self.cxxflags += ['-std=c++14', '-stdlib=libc++']
    else:
      self.cxxflags += ['-std=gnu++14']

    #Overrides
    self.objext = '.o'

    #Builders
    self.builders['c'] = self.builder_cc
    self.builders['cc'] = self.builder_cxx
    self.builders['cpp'] = self.builder_cxx
    self.builders['lib'] = self.builder_lib
    self.builders['multilib'] = self.builder_multicopy
    self.builders['sharedlib'] = self.builder_sharedlib
    self.builders['multisharedlib'] = self.builder_multicopy
    self.builders['bin'] = self.builder_bin
    self.builders['multibin'] = self.builder_multicopy

    #Setup target platform
    self.build_target_toolchain(self.target)

  def name(self):
    return 'gcc'

  def parse_prefs(self, prefs):
    super(GCCToolchain, self).parse_prefs(prefs)
    if 'gcc' in prefs:
      gccprefs = prefs['gcc']
      if 'toolchain' in gccprefs:
        self.toolchain = gccprefs['toolchain']
        if os.path.split(self.toolchain)[1] != 'bin':
          self.toolchain = os.path.join(self.toolchain, 'bin')

  def write_variables(self, writer):
    super(GCCToolchain, self).write_variables(writer)
    writer.variable('toolchain', self.toolchain)
    writer.variable('cc', self.ccompiler)
    writer.variable('cxx', self.cxxcompiler)
    writer.variable('ar', self.archiver)
    writer.variable('link', self.linker)
    writer.variable('includepaths', self.make_includepaths(self.includepaths))
    writer.variable('moreincludepaths', '')
    writer.variable('cflags', self.cflags)
    writer.variable('cxxflags', self.cxxflags)
    writer.variable('carchflags', '')
    writer.variable('cconfigflags', '')
    writer.variable('cmoreflags', self.cmoreflags)
    writer.variable('arflags', self.arflags)
    writer.variable('ararchflags', '')
    writer.variable('arconfigflags', '')
    writer.variable('linkflags', self.linkflags)
    writer.variable('linkarchflags', '')
    writer.variable('linkconfigflags', '')
    writer.variable('libs', '')
    writer.variable('libpaths', self.make_libpaths(self.libpaths))
    writer.variable('configlibpaths', '')
    writer.variable('archlibs', '')
    writer.variable('oslibs', self.make_libs(self.oslibs))
    writer.newline()

  def write_rules(self, writer):
    super(GCCToolchain, self).write_rules(writer)
    writer.rule('cc', command = self.cccmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CC $in')
    writer.rule('cxx', command = self.cxxcmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CXX $in')
    writer.rule('ar', command = self.arcmd, description = 'LIB $out')
    writer.rule('link', command = self.linkcmd, description = 'LINK $out')
    writer.rule('so', command = self.linkcmd, description = 'SO $out')
    writer.newline()

  def build_target_toolchain(self, target):
    if target.is_windows():
      self.build_windows_toolchain()
    if self.toolchain != '' and not self.toolchain.endswith('/') and not self.toolchain.endswith('\\'):
      self.toolchain += os.sep

  def build_windows_toolchain(self):
    self.cflags += ['-U__STRICT_ANSI__']
    self.oslibs = ['kernel32', 'user32', 'shell32', 'advapi32']

  def make_includepath(self, path):
    if os.path.isabs(path) or self.subninja == '':
      return self.path_escape(path)
    if path == '.':
      return self.path_escape(self.subninja)
    return self.path_escape(os.path.join(self.subninja, path))

  def make_includepaths(self, includepaths):
    if not includepaths is None:
      return ['-I' + self.make_includepath(path) for path in list(includepaths)]
    return []

  def make_libpath(self, path):
    return self.path_escape(path)

  def make_libpaths(self, libpaths):
    if not libpaths is None:
      return ['-L' + self.make_libpath(path) for path in libpaths]
    return []

  def make_targetarchflags(self, arch, targettype):
    flags = []
    if arch == 'x86':
      flags += ['-m32']
    elif arch == 'x86-64':
      flags += ['-m64']
    return flags

  def make_carchflags(self, arch, targettype):
    flags = []
    if targettype == 'sharedlib':
      flags += ['-DBUILD_DYNAMIC_LINK=1']
      if self.target.is_linux():
        flags += ['-fPIC']
    flags += self.make_targetarchflags(arch, targettype)
    return flags

  def make_cconfigflags(self, config, targettype):
    flags = []
    if config == 'debug':
      flags += ['-DBUILD_DEBUG=1', '-g']
    elif config == 'release':
      flags += ['-DBUILD_RELEASE=1', '-O3', '-g', '-funroll-loops']
    elif config == 'profile':
      flags += ['-DBUILD_PROFILE=1', '-O3', '-g', '-funroll-loops']
    elif config == 'deploy':
      flags += ['-DBUILD_DEPLOY=1', '-O3', '-g', '-funroll-loops']
    return flags

  def make_ararchflags(self, arch, targettype):
    flags = []
    return flags

  def make_arconfigflags(self, config, targettype):
    flags = []
    return flags

  def make_linkarchflags(self, arch, targettype):
    flags = []
    flags += self.make_targetarchflags(arch, targettype)
    return flags

  def make_linkconfigflags(self, config, targettype):
    flags = []
    if self.target.is_windows():
      if targettype == 'sharedlib':
        flags += ['-Xlinker', '/DLL']
      elif targettype == 'bin':
        flags += ['-Xlinker', '/SUBSYSTEM:CONSOLE']
    elif self.target.is_macos() or self.target.is_ios():
      if targettype == 'sharedlib' or targettype == 'multisharedlib':
        flags += ['-dynamiclib']
    else:
      if targettype == 'sharedlib':
        flags += ['-shared']
    return flags

  def make_libs(self, libs):
    if libs != None:
      return ['-l' + lib for lib in libs]
    return []

  def make_configlibpaths(self, config, arch, extralibpaths):
    libpaths = [
      self.libpath,
      os.path.join(self.libpath, arch),
      os.path.join(self.libpath, config),
      os.path.join(self.libpath, config, arch)
      ]
    if extralibpaths != None:
      libpaths += [os.path.join(libpath, self.libpath) for libpath in extralibpaths]
      libpaths += [os.path.join(libpath, self.libpath, arch) for libpath in extralibpaths]
      libpaths += [os.path.join(libpath, self.libpath, config) for libpath in extralibpaths]
      libpaths += [os.path.join(libpath, self.libpath, config, arch) for libpath in extralibpaths]
    return self.make_libpaths(libpaths)

  def cc_variables(self, config, arch, targettype, variables):
    localvariables = []
    if 'includepaths' in variables:
      moreincludepaths = self.make_includepaths(variables['includepaths'])
      if not moreincludepaths == []:
        localvariables += [('moreincludepaths', moreincludepaths)]
    carchflags = self.make_carchflags(arch, targettype)
    if carchflags != []:
      localvariables += [('carchflags', carchflags)]
    cconfigflags = self.make_cconfigflags(config, targettype)
    if cconfigflags != []:
      localvariables += [('cconfigflags', cconfigflags)]
    if 'defines' in variables:
      localvariables += [('cmoreflags', ['-D' + define for define in variables['defines']])]
    return localvariables

  def ar_variables(self, config, arch, targettype, variables):
    localvariables = []
    ararchflags = self.make_ararchflags(arch, targettype)
    if ararchflags != []:
      localvariables += [('ararchflags', ararchflags)]
    arconfigflags = self.make_arconfigflags(config, targettype)
    if arconfigflags != []:
      localvariables += [('arconfigflags', arconfigflags)]
    return localvariables

  def link_variables(self, config, arch, targettype, variables):
    localvariables = []
    linkarchflags = self.make_linkarchflags(arch, targettype)
    if linkarchflags != []:
      localvariables += [('linkarchflags', linkarchflags)]
    linkconfigflags = self.make_linkconfigflags(config, targettype)
    if linkconfigflags != []:
      localvariables += [('linkconfigflags', linkconfigflags)]
    if 'libs' in variables:
      libvar = self.make_libs(variables['libs'])
      if libvar != []:
        localvariables += [('libs', libvar)]
    libpaths = []
    if 'libpaths' in variables:
      libpaths = variables['libpaths']
    localvariables += [('configlibpaths', self.make_configlibpaths(config, arch, libpaths))]

    if 'runtime' in variables and variables['runtime'] == 'c++':
      localvariables += [('link', self.cxxlinker)]

    return localvariables

  def builder_cc(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cc', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_cxx(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cxx', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_lib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'ar', infiles, implicit = self.implicit_deps(config, variables), variables = self.ar_variables(config, arch, targettype, variables))

  def builder_sharedlib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'so', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

  def builder_bin(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'link', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

def create(host, target, toolchain):
  return GCCToolchain(host, target, toolchain)
