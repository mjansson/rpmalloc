#!/usr/bin/env python

"""Ninja toolchain abstraction for Clang compiler suite"""

import os
import subprocess

import toolchain

class ClangToolchain(toolchain.Toolchain):

  def initialize(self, project, archs, configs, includepaths, dependlibs, libpaths, variables, subninja):
    #Local variable defaults
    self.toolchain = ''
    self.sdkpath = ''
    self.includepaths = []
    self.libpaths = libpaths
    self.ccompiler = os.environ.get('CC') or 'clang'
    self.cxxcompiler = os.environ.get('CXX') or 'clang++'
    if self.target.is_windows():
      self.archiver = os.environ.get('AR') or 'llvm-ar'
      self.linker = os.environ.get('CC') or 'lld-link'
      self.cxxlinker = os.environ.get('CXX') or 'lld-link'
    else:
      self.archiver = os.environ.get('AR') or 'ar'
      self.linker = os.environ.get('CC') or 'clang'
      self.cxxlinker = os.environ.get('CXX') or 'clang++'

    #Default variables
    self.sysroot = ''
    if self.target.is_ios():
      self.deploymenttarget = '9.0'
    if self.target.is_macos():
      self.deploymenttarget = '10.7'

    #Command definitions
    self.cccmd = '$toolchain$cc -MMD -MT $out -MF $out.d $includepaths $moreincludepaths $cflags $carchflags $cconfigflags $cmoreflags $cenvflags -c $in -o $out'
    self.cxxcmd = '$toolchain$cxx -MMD -MT $out -MF $out.d $includepaths $moreincludepaths $cxxflags $carchflags $cconfigflags $cmoreflags $cxxenvflags -c $in -o $out'
    self.ccdeps = 'gcc'
    self.ccdepfile = '$out.d'
    self.arcmd = self.rmcmd('$out') + ' && $toolchain$ar crsD $ararchflags $arflags $arenvflags $out $in'
    if self.target.is_windows():
      self.linkcmd = '$toolchain$link $libpaths $configlibpaths $linkflags $linkarchflags $linkconfigflags $linkenvflags /debug /nologo /subsystem:console /dynamicbase /nxcompat /manifest /manifestuac:\"level=\'asInvoker\' uiAccess=\'false\'\" /tlbid:1 /pdb:$pdbpath /out:$out $in $libs $archlibs $oslibs $frameworks'
      self.dllcmd = self.linkcmd + ' /dll'
    else:
      self.linkcmd = '$toolchain$link $libpaths $configlibpaths $linkflags $linkarchflags $linkconfigflags $linkenvflags -o $out $in $libs $archlibs $oslibs $frameworks'

    #Base flags
    self.cflags = ['-D' + project.upper() + '_COMPILE=1',
                   '-funit-at-a-time', '-fstrict-aliasing', '-fvisibility=hidden', '-fno-stack-protector',
                   '-fomit-frame-pointer', '-fno-math-errno','-ffinite-math-only', '-funsafe-math-optimizations',
                   '-fno-trapping-math', '-ffast-math']
    self.cwarnflags = ['-W', '-Werror', '-pedantic', '-Wall', '-Weverything',
                       '-Wno-padded', '-Wno-documentation-unknown-command',
                       '-Wno-implicit-fallthrough', '-Wno-static-in-inline', '-Wno-reserved-id-macro']
    self.cmoreflags = []
    self.mflags = []
    self.arflags = []
    self.linkflags = []
    self.oslibs = []
    self.frameworks = []

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
    if not self.target.is_windows():
      self.linkflags += ['-fomit-frame-pointer']

    self.includepaths = self.prefix_includepaths((includepaths or []) + ['.'])

    if self.is_monolithic():
      self.cflags += ['-DBUILD_MONOLITHIC=1']
    if self.use_coverage():
      self.cflags += ['--coverage']
      self.linkflags += ['--coverage']

    if not 'nowarning' in variables or not variables['nowarning']:
      self.cflags += self.cwarnflags
    else:
      self.cflags += ['-w']
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
    self.builders['sharedlib'] = self.builder_sharedlib
    self.builders['bin'] = self.builder_bin
    if self.target.is_macos() or self.target.is_ios():
      self.builders['m'] = self.builder_cm
      self.builders['multilib'] = self.builder_apple_multilib
      self.builders['multisharedlib'] = self.builder_apple_multisharedlib
      self.builders['multibin'] = self.builder_apple_multibin
    else:
      self.builders['multilib'] = self.builder_multicopy
      self.builders['multisharedlib'] = self.builder_multicopy
      self.builders['multibin'] = self.builder_multicopy

    #Setup target platform
    self.build_toolchain()

  def name(self):
    return 'clang'

  def parse_prefs(self, prefs):
    super(ClangToolchain, self).parse_prefs(prefs)
    if 'clang' in prefs:
      clangprefs = prefs['clang']
      if 'toolchain' in clangprefs:
        self.toolchain = clangprefs['toolchain']
        if os.path.split(self.toolchain)[1] != 'bin':
          self.toolchain = os.path.join(self.toolchain, 'bin')
      if 'archiver' in clangprefs:
        self.archiver = clangprefs['archiver']
    if self.target.is_ios() and 'ios' in prefs:
      iosprefs = prefs['ios']
      if 'deploymenttarget' in iosprefs:
        self.deploymenttarget = iosprefs['deploymenttarget']
    if self.target.is_macos() and 'macos' in prefs:
      macosprefs = prefs['macos']
      if 'deploymenttarget' in macosprefs:
        self.deploymenttarget = macosprefs['deploymenttarget']

  def write_variables(self, writer):
    super(ClangToolchain, self).write_variables(writer)
    writer.variable('toolchain', self.toolchain)
    writer.variable('sdkpath', self.sdkpath)
    writer.variable('sysroot', self.sysroot)
    writer.variable('cc', self.ccompiler)
    writer.variable('cxx', self.cxxcompiler)
    writer.variable('ar', self.archiver)
    writer.variable('link', self.linker)
    if self.target.is_macos() or self.target.is_ios():
      writer.variable('lipo', self.lipo)
    writer.variable('includepaths', self.make_includepaths(self.includepaths))
    writer.variable('moreincludepaths', '')
    writer.variable('cflags', self.cflags)
    writer.variable('cxxflags', self.cxxflags)
    if self.target.is_macos() or self.target.is_ios():
      writer.variable('mflags', self.mflags)
    writer.variable('carchflags', '')
    writer.variable('cconfigflags', '')
    writer.variable('cmoreflags', self.cmoreflags)
    writer.variable('cenvflags', (os.environ.get('CFLAGS') or '').split())
    writer.variable('cxxenvflags', (os.environ.get('CXXFLAGS') or '').split())
    writer.variable('arflags', self.arflags)
    writer.variable('ararchflags', '')
    writer.variable('arconfigflags', '')
    writer.variable('arenvflags', (os.environ.get('ARFLAGS') or '').split())
    writer.variable('linkflags', self.linkflags)
    writer.variable('linkarchflags', '')
    writer.variable('linkconfigflags', '')
    writer.variable('linkenvflags', (os.environ.get('LDFLAGS') or '').split())
    writer.variable('libs', '')
    writer.variable('libpaths', self.make_libpaths(self.libpaths))
    writer.variable('configlibpaths', '')
    writer.variable('archlibs', '')
    writer.variable('oslibs', self.make_libs(self.oslibs))
    writer.variable('frameworks', '')
    if self.target.is_windows():
      writer.variable('pdbpath', 'ninja.pdb')
    writer.newline()

  def write_rules(self, writer):
    super(ClangToolchain, self).write_rules(writer)
    writer.rule('cc', command = self.cccmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CC $in')
    writer.rule('cxx', command = self.cxxcmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CXX $in')
    if self.target.is_macos() or self.target.is_ios():
      writer.rule('cm', command = self.cmcmd, depfile = self.ccdepfile, deps = self.ccdeps, description = 'CM $in')
      writer.rule( 'lipo', command = self.lipocmd, description = 'LIPO $out' )
    writer.rule('ar', command = self.arcmd, description = 'LIB $out')
    writer.rule('link', command = self.linkcmd, description = 'LINK $out')
    if self.target.is_windows():
      writer.rule('dll', command = self.dllcmd, description = 'DLL $out')
    else:
      writer.rule('so', command = self.linkcmd, description = 'SO $out')
    writer.newline()

  def build_toolchain(self):
    super(ClangToolchain, self).build_toolchain()
    if self.target.is_windows():
      self.build_windows_toolchain()
    elif self.target.is_android():
      self.build_android_toolchain()
    elif self.target.is_macos() or self.target.is_ios():
      self.build_xcode_toolchain()
    if self.toolchain != '' and not self.toolchain.endswith('/') and not self.toolchain.endswith('\\'):
      self.toolchain += os.sep

  def build_windows_toolchain(self):
    self.cflags += ['-U__STRICT_ANSI__', '-Wno-reserved-id-macro']
    self.oslibs = ['kernel32', 'user32', 'shell32', 'advapi32']

  def build_android_toolchain(self):
    self.archiver = 'ar'

    self.cccmd += ' --sysroot=$sysroot'
    self.linkcmd += ' -shared -Wl,-soname,$liblinkname --sysroot=$sysroot'
    self.cflags += ['-fpic', '-ffunction-sections', '-funwind-tables', '-fstack-protector', '-fomit-frame-pointer',
                    '-no-canonical-prefixes', '-Wa,--noexecstack']

    self.linkflags += ['-no-canonical-prefixes', '-Wl,--no-undefined', '-Wl,-z,noexecstack', '-Wl,-z,relro', '-Wl,-z,now']

    self.includepaths += [os.path.join('$ndk', 'sources', 'android', 'native_app_glue'),
                          os.path.join('$ndk', 'sources', 'android', 'cpufeatures')]

    self.oslibs += ['log']

    self.toolchain = os.path.join('$ndk', 'toolchains', 'llvm', 'prebuilt', self.android.hostarchname, 'bin', '')

  def build_xcode_toolchain(self):
    if self.target.is_macos():
      sdk = 'macosx'
      deploytarget = 'MACOSX_DEPLOYMENT_TARGET=' + self.deploymenttarget
      self.cflags += ['-fasm-blocks', '-mmacosx-version-min=' + self.deploymenttarget, '-isysroot', '$sysroot']
      self.cxxflags += ['-fasm-blocks', '-mmacosx-version-min=' + self.deploymenttarget, '-isysroot', '$sysroot']
      self.arflags += ['-static', '-no_warning_for_no_symbols']
      self.linkflags += ['-isysroot', '$sysroot']
    elif self.target.is_ios():
      sdk = 'iphoneos'
      deploytarget = 'IPHONEOS_DEPLOYMENT_TARGET=' + self.deploymenttarget
      self.cflags += ['-fasm-blocks', '-miphoneos-version-min=' + self.deploymenttarget, '-isysroot', '$sysroot']
      self.cxxflags += ['-fasm-blocks', '-miphoneos-version-min=' + self.deploymenttarget, '-isysroot', '$sysroot']
      self.arflags += ['-static', '-no_warning_for_no_symbols']
      self.linkflags += ['-isysroot', '$sysroot']
    self.cflags += ['-fembed-bitcode-marker']

    platformpath = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-platform-path']).strip()
    localpath = platformpath + "/Developer/usr/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"

    self.sysroot = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-path']).strip()

    self.ccompiler = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'clang']).strip()
    self.archiver = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'libtool']).strip()
    self.linker = deploytarget + " " + self.ccompiler
    self.lipo = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'lipo']).strip()

    self.mflags += list(self.cflags) + ['-fobjc-arc', '-fno-objc-exceptions', '-x', 'objective-c']
    self.cflags += ['-x', 'c']
    self.cxxflags += ['-x', 'c++']

    self.cmcmd = self.cccmd.replace('$cflags', '$mflags')
    self.arcmd = self.rmcmd('$out') + ' && $ar $ararchflags $arflags $in -o $out'
    self.lipocmd = '$lipo $in -create -output $out'

    if self.target.is_macos():
      self.frameworks = ['Cocoa', 'CoreFoundation']
    if self.target.is_ios():
      self.frameworks = ['CoreGraphics', 'UIKit', 'Foundation']

  def make_includepaths(self, includepaths):
    if not includepaths is None:
      return ['-I' + path for path in list(includepaths)]
    return []

  def make_libpath(self, path):
    return self.path_escape(path)

  def make_libpaths(self, libpaths):
    if not libpaths is None:
      if self.target.is_windows():
        return ['/libpath:' + self.path_escape(path) for path in libpaths]
      return ['-L' + self.make_libpath(path) for path in libpaths]
    return []

  def make_targetarchflags(self, arch, targettype):
    flags = []
    if self.target.is_android():
      if arch == 'x86':
        flags += ['-target', 'i686-none-linux-android']
        flags += ['-march=i686', '-mtune=intel', '-mssse3', '-mfpmath=sse', '-m32']
      elif arch == 'x86-64':
        flags += ['-target', 'x86_64-none-linux-android']
        flags += ['-march=x86-64', '-msse4.2', '-mpopcnt', '-m64', '-mtune=intel']
      elif arch == 'arm6':
        flags += ['-target', 'armv5te-none-linux-androideabi']
        flags += ['-march=armv5te', '-mtune=xscale', '-msoft-float', '-marm']
      elif arch == 'arm7':
        flags += ['-target', 'armv7-none-linux-androideabi']
        flags += ['-march=armv7-a', '-mhard-float', '-mfpu=vfpv3-d16', '-mfpu=neon', '-D_NDK_MATH_NO_SOFTFP=1', '-marm']
      elif arch == 'arm64':
        flags += ['-target', 'aarch64-none-linux-android']
      elif arch == 'mips':
        flags += ['-target', 'mipsel-none-linux-android']
      elif arch == 'mips64':
        flags += ['-target', 'mips64el-none-linux-android']
      flags += ['-gcc-toolchain', self.android.make_gcc_toolchain_path(arch)]
    elif self.target.is_macos() or self.target.is_ios():
      if arch == 'x86':
        flags += ['-arch', 'x86']
      elif arch == 'x86-64':
        flags += ['-arch', 'x86_64']
      elif arch == 'arm7':
        flags += ['-arch', 'armv7']
      elif arch == 'arm64':
        flags += ['-arch', 'arm64']
    elif self.target.is_windows():
      if arch == 'x86':
        flags += ['-target', 'x86-pc-windows-msvc']
      elif arch == 'x64':
        flags += ['-target', 'x86_64-pc-windows-msvc']
    else:
      if arch == 'x86':
        flags += ['-m32']
      elif arch == 'x86-64':
        flags += ['-m64']
    return flags

  def make_carchflags(self, arch, targettype):
    flags = []
    if targettype == 'sharedlib':
      flags += ['-DBUILD_DYNAMIC_LINK=1']
      if self.target.is_linux() or self.target.is_bsd():
       flags += ['-fPIC']
    flags += self.make_targetarchflags(arch, targettype)
    return flags

  def make_cconfigflags(self, config, targettype):
    flags = ['-g']
    if config == 'debug':
      flags += ['-DBUILD_DEBUG=1']
    elif config == 'release':
      flags += ['-DBUILD_RELEASE=1', '-O3', '-funroll-loops']
    elif config == 'profile':
      flags += ['-DBUILD_PROFILE=1', '-O3', '-funroll-loops']
    elif config == 'deploy':
      flags += ['-DBUILD_DEPLOY=1', '-O3', '-funroll-loops']
    return flags

  def make_ararchflags(self, arch, targettype):
    flags = []
    return flags

  def make_arconfigflags(self, config, targettype):
    flags = []
    return flags

  def make_linkarchflags(self, arch, targettype, variables):
    flags = []
    flags += self.make_targetarchflags(arch, targettype)
    if self.target.is_android():
      if arch == 'arm7':
        flags += ['-Wl,--no-warn-mismatch', '-Wl,--fix-cortex-a8']
    if self.target.is_windows():
      # Ignore target arch flags from above, add link style arch instead
      flags = []
      if arch == 'x86':
        flags += ['/machine:x86']
      elif arch == 'x86-64':
        flags += ['/machine:x64']
    if self.target.is_macos() and variables != None and 'support_lua' in variables and variables['support_lua']:
      flags += ['-pagezero_size', '10000', '-image_base', '100000000']
    return flags

  def make_linkconfigflags(self, config, targettype, variables):
    flags = []
    if self.target.is_windows():
      if config == 'debug':
        flags += ['/incremental', '/defaultlib:libcmtd']
      else:
        flags += ['/incremental:no', '/opt:ref', '/opt:icf', '/defaultlib:libcmt']
    elif self.target.is_macos() or self.target.is_ios():
      if targettype == 'sharedlib' or targettype == 'multisharedlib':
        flags += ['-dynamiclib']
    else:
      if targettype == 'sharedlib':
        flags += ['-shared', '-fPIC']
    if config != 'debug':
      if targettype == 'bin' or targettype == 'sharedlib':
        flags += ['-flto']
    return flags

  def make_linkarchlibs(self, arch, targettype):
    archlibs = []
    if self.target.is_android():
      if arch == 'arm7':
        archlibs += ['m_hard']
      else:
        archlibs += ['m']
      archlibs += ['gcc', 'android']
    return archlibs

  def make_libs(self, libs):
    if libs != None:
      if self.target.is_windows():
        return [lib + ".lib" for lib in libs]
      return ['-l' + lib for lib in libs]
    return []

  def make_frameworks(self, frameworks):
    if frameworks != None:
      return ['-framework ' + framework for framework in frameworks]
    return []

  def make_configlibpaths(self, config, arch, extralibpaths):
    libpaths = [self.libpath, os.path.join(self.libpath, config)]
    if not self.target.is_macos() and not self.target.is_ios():
      libpaths += [os.path.join(self.libpath, arch)]
      libpaths += [os.path.join(self.libpath, config, arch)]
    if extralibpaths != None:
      libpaths += [os.path.join(libpath, self.libpath) for libpath in extralibpaths]
      libpaths += [os.path.join(libpath, self.libpath, config) for libpath in extralibpaths]
      if not self.target.is_macos() and not self.target.is_ios():
        libpaths += [os.path.join(libpath, self.libpath, arch) for libpath in extralibpaths]
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
    if self.target.is_android():
      localvariables += [('sysroot', self.android.make_sysroot_path(arch))]
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
    if self.target.is_android():
      localvariables += [('toolchain', self.android.make_gcc_bin_path(arch))]
    return localvariables

  def link_variables(self, config, arch, targettype, variables):
    if variables == None:
        variables = {}
    localvariables = []
    linkarchflags = self.make_linkarchflags(arch, targettype, variables)
    if linkarchflags != []:
      localvariables += [('linkarchflags', linkarchflags)]
    linkconfigflags = self.make_linkconfigflags(config, targettype, variables)
    if linkconfigflags != []:
      localvariables += [('linkconfigflags', linkconfigflags)]
    if 'libs' in variables:
      libvar = self.make_libs(variables['libs'])
      if libvar != []:
        localvariables += [('libs', libvar)]

    localframeworks = self.frameworks or []
    if 'frameworks' in variables and variables['frameworks'] != None:
      localframeworks += list(variables['frameworks'])
    if len(localframeworks) > 0:
      localvariables += [('frameworks', self.make_frameworks(list(localframeworks)))]
      
    libpaths = []
    if 'libpaths' in variables:
      libpaths = variables['libpaths']
    localvariables += [('configlibpaths', self.make_configlibpaths(config, arch, libpaths))]
    if self.target.is_android():
      localvariables += [('sysroot', self.android.make_sysroot_path(arch))]
    archlibs = self.make_linkarchlibs(arch, targettype)
    if archlibs != []:
      localvariables += [('archlibs', self.make_libs(archlibs))]

    if 'runtime' in variables and variables['runtime'] == 'c++':
      localvariables += [('link', self.cxxlinker)]

    return localvariables

  def builder_cc(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cc', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_cxx(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cxx', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_cm(self, writer, config, arch, targettype, infile, outfile, variables):
    return writer.build(outfile, 'cm', infile, implicit = self.implicit_deps(config, variables), variables = self.cc_variables(config, arch, targettype, variables))

  def builder_lib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'ar', infiles, implicit = self.implicit_deps(config, variables), variables = self.ar_variables(config, arch, targettype, variables))

  def builder_sharedlib(self, writer, config, arch, targettype, infiles, outfile, variables):
    if self.target.is_windows():
      return writer.build(outfile, 'dll', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))
    return writer.build(outfile, 'so', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

  def builder_bin(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(outfile, 'link', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

  #Apple universal targets
  def builder_apple_multilib(self, writer, config, arch, targettype, infiles, outfile, variables):
    localvariables = [('arflags', '-static -no_warning_for_no_symbols')]
    if variables != None:
      localvariables = variables + localvariables
    return writer.build(os.path.join(outfile, self.buildtarget), 'ar', infiles, variables = localvariables);

  def builder_apple_multisharedlib(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(os.path.join(outfile, self.buildtarget), 'so', infiles, implicit = self.implicit_deps(config, variables), variables = self.link_variables(config, arch, targettype, variables))

  def builder_apple_multibin(self, writer, config, arch, targettype, infiles, outfile, variables):
    return writer.build(os.path.join(outfile, self.buildtarget), 'lipo', infiles, variables = variables)

def create(host, target, toolchain):
  return ClangToolchain(host, target, toolchain)
