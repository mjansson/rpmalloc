#!/usr/bin/env python

"""Ninja toolchain abstraction for Microsoft compiler suite"""

import os
import subprocess

import toolchain

class MSVCToolchain(toolchain.Toolchain):

  def initialize(self, project, archs, configs, includepaths, dependlibs, libpaths, variables, subninja):
    #Local variable defaults
    self.sdkpath = ''
    self.toolchain = ''
    self.includepaths = []
    self.libpaths = libpaths
    self.ccompiler = 'cl'
    self.cxxcompiler = 'cl'
    self.archiver = 'lib'
    self.linker = 'link'
    self.dller = 'dll'

    #Command definitions
    self.cccmd = '$toolchain$cc /showIncludes /I. $includepaths $moreincludepaths $cflags $carchflags $cconfigflags $cmoreflags /c $in /Fo$out /Fd$pdbpath /FS /nologo'
    self.cxxcmd = '$toolchain$cxx /showIncludes /I. $includepaths $moreincludepaths $cxxflags $carchflags $cconfigflags $cmoreflags /c $in /Fo$out /Fd$pdbpath /FS /nologo'
    self.ccdepfile = None
    self.ccdeps = 'msvc'
    self.arcmd = '$toolchain$ar $arflags $ararchflags $arconfigflags /NOLOGO /OUT:$out $in'
    self.linkcmd = '$toolchain$link $libpaths $configlibpaths $linkflags $linkarchflags $linkconfigflags /DEBUG /NOLOGO /SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT /MANIFEST /MANIFESTUAC:\"level=\'asInvoker\' uiAccess=\'false\'\" /TLBID:1 /PDB:$pdbpath /OUT:$out $in $libs $archlibs $oslibs'
    self.dllcmd = self.linkcmd + ' /DLL'

    self.cflags = ['/D', '"' + project.upper() + '_COMPILE=1"', '/D', '"_UNICODE"',  '/D', '"UNICODE"', '/Zi', '/Oi', '/Oy-', '/GS-', '/Gy-', '/Qpar-', '/fp:fast', '/fp:except-', '/Zc:forScope', '/Zc:wchar_t', '/GR-', '/openmp-']
    self.cwarnflags = ['/W3', '/WX']
    self.cmoreflags = []
    self.arflags = ['/ignore:4221'] #Ignore empty object file warning]
    self.linkflags = ['/DEBUG']
    self.oslibs = ['kernel32', 'user32', 'shell32', 'advapi32']

    self.initialize_subninja(subninja)
    self.initialize_archs(archs)
    self.initialize_configs(configs)
    self.initialize_project(project)
    self.initialize_toolchain()
    self.initialize_depends(dependlibs)

    self.parse_default_variables(variables)
    self.read_build_prefs()

    self.includepaths = self.prefix_includepaths((includepaths or []) + ['.'])

    if self.is_monolithic():
      self.cflags += ['/D', '"BUILD_MONOLITHIC=1"']

    if not 'nowarning' in variables or not variables['nowarning']:
      self.cflags += self.cwarnflags
    self.cxxflags = list(self.cflags)

    #Overrides
    self.objext = '.obj'

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

    #Setup toolchain
    self.build_toolchain()

  def name(self):
    return 'msvc'

  def parse_prefs(self, prefs):
    super(MSVCToolchain, self).parse_prefs(prefs)
    if 'msvc' in prefs:
      msvcprefs = prefs['msvc']
      if 'sdkpath' in msvcprefs:
        self.sdkpath = msvcprefs['sdkpath']
      if 'toolchain' in msvcprefs:
        self.toolchain = msvcprefs['toolchain']

  def write_variables(self, writer):
    super(MSVCToolchain, self).write_variables(writer)
    writer.variable('cc', self.ccompiler)
    writer.variable('cxx', self.cxxcompiler)
    writer.variable('ar', self.archiver)
    writer.variable('link', self.linker)
    writer.variable('dll', self.dller)
    writer.variable('toolchain', self.toolchain)
    writer.variable('includepaths', self.make_includepaths(self.includepaths))
    writer.variable('moreincludepaths', '')
    writer.variable('pdbpath', 'ninja.pdb')
    writer.variable('cflags', self.cflags)
    writer.variable('carchflags', '')
    writer.variable('cconfigflags', '')
    writer.variable('cxxflags', self.cxxflags)
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
    super(MSVCToolchain, self).write_rules(writer)
    writer.rule('cc', command = self.cccmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CC $in')
    writer.rule('cxx', command = self.cxxcmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CXX $in')
    writer.rule('ar', command = self.arcmd, description = 'LIB $out')
    writer.rule('link', command = self.linkcmd, description = 'LINK $out')
    writer.rule('dll', command = self.dllcmd, description = 'DLL $out')
    writer.newline()

  def build_toolchain(self):
    if self.toolchain == '':
      versions = ['15.0', '14.0', '13.0', '12.0', '11.0', '10.0']
      keys = [
        'HKLM\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VC7',
        'HKCU\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VC7',
        'HKLM\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7',
        'HKCU\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7',
        'HKLM\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VC7',
        'HKCU\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VC7',
        'HKLM\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VS7',
        'HKCU\\SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VS7'
      ]
      toolchain = ''
      for version in versions:
        for key in keys:
          try:
            query = subprocess.check_output(['reg', 'query', key, '/v', version ], stderr = subprocess.STDOUT).strip().splitlines()
            if len(query) == 2:
              toolchain = str(query[1]).split('REG_SZ')[-1].strip(" '\"\n\r\t")
          except:
            continue
          if not toolchain == '':
            #Thanks MS for making it _really_ hard to find the compiler
            if version == '15.0':
              tools_basepath = os.path.join(toolchain, 'VC', 'Tools', 'MSVC')
              tools_list = [item for item in os.listdir(tools_basepath) if os.path.isdir(os.path.join(tools_basepath, item))]
              from distutils.version import StrictVersion
              tools_list.sort(key=StrictVersion)
              toolchain = os.path.join(tools_basepath, tools_list[-1])
            self.includepaths += [os.path.join(toolchain, 'include')]
            self.toolchain = toolchain
            self.toolchain_version = version
            break
        if not toolchain == '':
          break
    if self.sdkpath == '':
      versions = ['v10.0', 'v8.1']
      keys = [
        'HKLM\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows',
        'HKCU\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows',
        'HKLM\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows',
        'HKCU\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows'
      ]
      include_path = 'include'
      for version in versions:
        for key in keys:
          sdkpath = ''
          try:
            query = subprocess.check_output(['reg', 'query', key + '\\' + version, '/v', 'InstallationFolder'], stderr = subprocess.STDOUT).strip().splitlines()
            if len(query) == 2:
              sdkpath = str(query[1]).split('REG_SZ')[-1].strip(" '\"\n\r\t")
              if not sdkpath == '' and version == 'v10.0':
                base_path = sdkpath
                sdkpath = ''
                query = subprocess.check_output(['reg', 'query', key + '\\' + version, '/v', 'ProductVersion'], stderr = subprocess.STDOUT).strip().splitlines()
                if len(query) == 2:
                  version_path = str(query[1]).split('REG_SZ')[-1].strip(" '\"\n\r\t")
                  if not version_path == '':
                    sdkpath = base_path
                    self.sdkversionpath = version_path
                    versioned_include_path = os.path.join(include_path, self.sdkversionpath)
                    if not os.path.exists(os.path.join(sdkpath, versioned_include_path)) and os.path.exists(os.path.join(sdkpath, versioned_include_path + '.0')):
                      self.sdkversionpath = self.sdkversionpath + '.0'
                      versioned_include_path = os.path.join(include_path, self.sdkversionpath)
                    include_path = versioned_include_path
          except subprocess.CalledProcessError as e:
            continue
          if not sdkpath == '':
            self.includepaths += [
              os.path.join(sdkpath, include_path, 'shared'),
              os.path.join(sdkpath, include_path, 'um'),
              os.path.join(sdkpath, include_path, 'winrt')
            ]
            if version == 'v10.0':
              self.includepaths += [
                os.path.join(sdkpath, include_path, 'ucrt')
              ]
            self.sdkpath = sdkpath
            self.sdkversion = version
            break
        if not sdkpath == '':
          break
    if self.toolchain != '' and not self.toolchain.endswith('/') and not self.toolchain.endswith('\\'):
      self.toolchain += os.sep

  def make_includepaths(self, includepaths):
    if not includepaths is None:
      return ['/I' + self.path_escape(path) for path in list(includepaths)]
    return []

  def make_libpath(self, path):
    return self.path_escape(path)

  def make_libpaths(self, libpaths):
    if not libpaths is None:
      return ['/LIBPATH:' + self.make_libpath(path) for path in libpaths]
    return []

  def make_arch_toolchain_path(self, arch):
    if self.toolchain_version == '15.0':
      if arch == 'x86-64':
        return os.path.join(self.toolchain, 'bin', 'HostX64', 'x64\\')
      elif arch == 'x86':
        return os.path.join(self.toolchain, 'bin', 'HostX64', 'x86\\')
    if arch == 'x86-64':
      return os.path.join(self.toolchain, 'bin', 'amd64\\')
    return os.path.join(self.toolchain, 'bin\\')

  def make_carchflags(self, arch, targettype):
    flags = []
    if targettype == 'sharedlib':
      flags += ['/MD', '/D', '"BUILD_DYNAMIC_LINK=1"']
    else:
      flags += ['/MT']
    if arch == 'x86':
      flags += ['/arch:SSE2']
    elif arch == 'x86-64':
      pass
    return flags

  def make_cconfigflags(self, config, targettype):
    flags = ['/Gm-']
    if config == 'debug':
      flags += ['/Od', '/D', '"BUILD_DEBUG=1"', '/GF-']
    else:
      flags += ['/Ob2', '/Ot', '/GT', '/GL', '/GF']
      if config == 'release':
        flags += ['/O2', '/D' '"BUILD_RELEASE=1"']
      elif config == 'profile':
        flags += ['/Ox', '/D', '"BUILD_PROFILE=1"']
      elif config == 'deploy':
        flags += ['/Ox', '/D', '"BUILD_DEPLOY=1"']
    return flags

  def make_ararchflags(self, arch, targettype):
    flags = []
    if arch == 'x86':
      flags += ['/MACHINE:X86']
    elif arch == 'x86-64':
      flags += ['/MACHINE:X64']
    return flags

  def make_arconfigflags(self, config, targettype):
    flags = []
    if config != 'debug':
      flags += ['/LTCG']
    return flags

  def make_linkarchflags(self, arch, targettype):
    flags = []
    if arch == 'x86':
      flags += ['/MACHINE:X86']
    elif arch == 'x86-64':
      flags += ['/MACHINE:X64']
    return flags

  def make_linkconfigflags(self, config, targettype):
    flags = []
    if config == 'debug':
      flags += ['/INCREMENTAL']
    else:
      flags += ['/LTCG', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF']
    return flags

  def make_libs(self, libs):
    if libs != None:
      return [lib + '.lib' for lib in libs]
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
    if self.sdkpath != '':
      if arch == 'x86':
        if self.toolchain_version == '15.0':
          libpaths += [os.path.join(self.toolchain, 'lib', 'x86')]
        else:
          libpaths += [os.path.join(self.toolchain, 'lib')]
        if self.sdkversion == 'v8.1':
          libpaths += [os.path.join( self.sdkpath, 'lib', 'winv6.3', 'um', 'x86')]
        if self.sdkversion == 'v10.0':
          libpaths += [os.path.join(self.sdkpath, 'lib', self.sdkversionpath, 'um', 'x86')]
          libpaths += [os.path.join(self.sdkpath, 'lib', self.sdkversionpath, 'ucrt', 'x86')]
      else:
        if self.toolchain_version == '15.0':
          libpaths += [os.path.join( self.toolchain, 'lib', 'x64')]
        else:
          libpaths += [os.path.join( self.toolchain, 'lib', 'amd64')]
        if self.sdkversion == 'v8.1':
          libpaths += [os.path.join( self.sdkpath, 'lib', 'winv6.3', 'um', 'x64')]
        if self.sdkversion == 'v10.0':
          libpaths += [os.path.join( self.sdkpath, 'lib', self.sdkversionpath, 'um', 'x64')]
          libpaths += [os.path.join( self.sdkpath, 'lib', self.sdkversionpath, 'ucrt', 'x64')]
    return self.make_libpaths(libpaths)

  def cc_variables(self, config, arch, targettype, variables):
    localvariables = [('toolchain', self.make_arch_toolchain_path(arch))]
    if 'includepaths' in variables:
      moreincludepaths = self.make_includepaths(variables['includepaths'])
      if not moreincludepaths == []:
        localvariables += [('moreincludepaths', moreincludepaths)]
    if 'modulepath' in variables:
      localvariables += [('pdbpath', os.path.join(variables['modulepath'], 'ninja.pdb'))]
    carchflags = self.make_carchflags(arch, targettype)
    if carchflags != []:
      localvariables += [('carchflags', carchflags)]
    cconfigflags = self.make_cconfigflags(config, targettype)
    if cconfigflags != []:
      localvariables += [('cconfigflags', cconfigflags)]
    if 'defines' in variables:
      definelist = []
      for define in variables['defines']:
        definelist += ['/D', '"' + define + '"']
      localvariables += [('cmoreflags', definelist)]
    return localvariables

  def ar_variables(self, config, arch, targettype, variables):
    localvariables = [('toolchain', self.make_arch_toolchain_path(arch))]
    ararchflags = self.make_ararchflags(arch, targettype)
    if ararchflags != []:
      localvariables += [('ararchflags', ararchflags)]
    arconfigflags = self.make_arconfigflags(config, targettype)
    if arconfigflags != []:
      localvariables += [('arconfigflags', arconfigflags)]
    return localvariables

  def link_variables(self, config, arch, targettype, variables):
    localvariables = [('toolchain', self.make_arch_toolchain_path(arch))]
    linkarchflags = self.make_linkarchflags(arch, targettype)
    if linkarchflags != []:
      localvariables += [('linkarchflags', linkarchflags)]
    linkconfigflags = self.make_linkconfigflags(config, targettype)
    if linkconfigflags != []:
      localvariables += [('linkconfigflags', linkconfigflags)]
    if 'modulepath' in variables:
      localvariables += [('pdbpath', os.path.join(variables['modulepath'], 'ninja.pdb'))]
    if 'libs' in variables:
      libvar = self.make_libs(variables['libs'])
      if libvar != []:
        localvariables += [('libs', libvar)]
    libpaths = []
    if 'libpaths' in variables:
      libpaths = variables['libpaths']
    localvariables += [('configlibpaths', self.make_configlibpaths(config, arch, libpaths))]
    return localvariables

  def builder_cc(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cc', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_cxx(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cxx', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_lib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'ar', infiles, implicit = self.implicit_deps(config, variables), variables = self.ar_variables(config, arch, targettype, variables))

  def builder_sharedlib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'dll', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

  def builder_bin(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'link', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

def create(host, target, toolchain):
  return MSVCToolchain(host, target, toolchain)
