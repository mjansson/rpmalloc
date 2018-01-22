#!/usr/bin/env python

"""Ninja toolchain abstraction for Android platform"""

import os
import subprocess

import toolchain

def make_target(toolchain, host, target):
  return Android(toolchain, host, target)

class Android(object):
  def __init__(self, toolchain, host, target):
    self.host = host

    if host.is_windows():
      self.exe_suffix = '.exe'
    else:
      self.exe_suffix = ''

    self.javaccmd = toolchain.mkdircmd('$outpath') + ' && $javac -d $outpath -classpath $outpath -sourcepath $sourcepath -target 1.5 -bootclasspath $androidjar -g -source 1.5 -Xlint:-options $in'
    self.dexcmd = '$dex --dex --output $out $in'
    self.aaptcmd = toolchain.cdcmd('$apkbuildpath') + ' && $aapt p -f -m -M AndroidManifest.xml -F $apk -I $androidjar -S res --debug-mode --no-crunch -J gen $aaptflags'
    self.aaptdeploycmd = toolchain.cdcmd('$apkbuildpath') + ' && $aapt c -S res -C bin/res && $aapt p -f -m -M AndroidManifest.xml -F $apk -I $androidjar -S bin/res -S res -J gen $aaptflags'
    self.aaptaddcmd = toolchain.cdcmd('$apkbuildpath') + ' && ' + toolchain.copycmd('$apksource', '$apk' ) + ' && $aapt a $apk $apkaddfiles'
    self.zipcmd = '$zip -r -9 $out $in $implicitin'
    self.zipaligncmd = '$zipalign -f 4 $in $out'
    self.codesigncmd = 'build/ninja/codesign.py --target $target --prefs codesign.json --zipfile $in --config $config --jarsigner $jarsigner $out'

    if host.is_windows():
      self.codesigncmd = 'python ' + self.codesigncmd

  def initialize_toolchain(self):
    self.ndkpath = os.getenv('NDK_HOME', '')
    self.sdkpath = os.getenv('ANDROID_HOME', '')
    self.sysroot = ''
    self.platformversion = '21'
    self.gcc_toolchainversion = '4.9'
    self.javasdk = ''

    self.archname = dict()
    self.archname['x86'] = 'x86'
    self.archname['x86-64'] = 'x86_64'
    self.archname['arm6'] = 'arm'
    self.archname['arm7'] = 'arm'
    self.archname['arm64'] = 'arm64'
    self.archname['mips'] = 'mips'
    self.archname['mips64'] = 'mips64'

    self.archpath = dict()
    self.archpath['x86'] = 'x86'
    self.archpath['x86-64'] = 'x86-64'
    self.archpath['arm6'] = 'armeabi'
    self.archpath['arm7'] = 'armeabi-v7a'
    self.archpath['arm64'] = 'arm64-v8a'
    self.archpath['mips'] = 'mips'
    self.archpath['mips64'] = 'mips64'

    self.gcc_toolchainname = dict()
    self.gcc_toolchainname['x86'] = 'x86-' + self.gcc_toolchainversion
    self.gcc_toolchainname['x86-64'] = 'x86_64-' + self.gcc_toolchainversion
    self.gcc_toolchainname['arm6'] = 'arm-linux-androideabi-' + self.gcc_toolchainversion
    self.gcc_toolchainname['arm7'] = 'arm-linux-androideabi-' + self.gcc_toolchainversion
    self.gcc_toolchainname['arm64'] = 'aarch64-linux-android-' + self.gcc_toolchainversion
    self.gcc_toolchainname['mips'] = 'mipsel-linux-android-' + self.gcc_toolchainversion
    self.gcc_toolchainname['mips64'] = 'mips64el-linux-android-' + self.gcc_toolchainversion

    self.gcc_toolchainprefix = dict()
    self.gcc_toolchainprefix['x86'] = 'i686-linux-android-'
    self.gcc_toolchainprefix['x86-64'] = 'x86_64-linux-android-'
    self.gcc_toolchainprefix['arm6'] = 'arm-linux-androideabi-'
    self.gcc_toolchainprefix['arm7'] = 'arm-linux-androideabi-'
    self.gcc_toolchainprefix['arm64'] = 'aarch64-linux-android-'
    self.gcc_toolchainprefix['mips'] = 'mipsel-linux-android-'
    self.gcc_toolchainprefix['mips64'] = 'mips64el-linux-android-'

    if self.host.is_windows():
      if os.getenv('PROCESSOR_ARCHITECTURE', 'AMD64').find('64') != -1:
        self.hostarchname = 'windows-x86_64'
      else:
        self.hostarchname = 'windows-x86'
    elif self.host.is_linux():
        localarch = subprocess.check_output(['uname', '-m']).strip()
        if localarch == 'x86_64':
          self.hostarchname = 'linux-x86_64'
        else:
          self.hostarchname = 'linux-x86'
    elif self.host.is_macos():
      self.hostarchname = 'darwin-x86_64'

  def build_toolchain(self):
    buildtools_path = os.path.join(self.sdkpath, 'build-tools')
    buildtools_list = [item for item in os.listdir(buildtools_path) if os.path.isdir(os.path.join(buildtools_path, item))]
    buildtools_list.sort(key = lambda s: map(int, s.split('-')[0].split('.')))

    self.buildtools_path = os.path.join(self.sdkpath, 'build-tools', buildtools_list[-1])
    self.android_jar = os.path.join(self.sdkpath, 'platforms', 'android-' + self.platformversion, 'android.jar')

    self.javac = 'javac'
    self.jarsigner = 'jarsigner'
    if self.javasdk != '':
      self.javac = os.path.join(self.javasdk, 'bin', self.javac)
      self.jarsigner = os.path.join(self.javasdk, 'bin', self.jarsigner)
    if self.host.is_windows():
      self.dex = os.path.join(self.buildtools_path, 'dx.bat')
    else:
      self.dex = os.path.join(self.buildtools_path, 'dx' + self.exe_suffix)
    if not os.path.isfile(self.dex):
      self.dex = os.path.join(self.sdkpath, 'tools', 'dx' + self.exe_suffix)
    self.aapt = os.path.join(self.buildtools_path, 'aapt' + self.exe_suffix)
    self.zipalign = os.path.join(self.buildtools_path, 'zipalign' + self.exe_suffix)
    if not os.path.isfile( self.zipalign ):
      self.zipalign = os.path.join(self.sdkpath, 'tools', 'zipalign' + self.exe_suffix)

  def parse_prefs(self, prefs):
    if 'android' in prefs:
      androidprefs = prefs['android']
      if 'ndkpath' in androidprefs:
        self.ndkpath = os.path.expanduser(androidprefs['ndkpath'])
      if 'sdkpath' in androidprefs:
        self.sdkpath = os.path.expanduser(androidprefs['sdkpath'])
      if 'platformversion' in androidprefs:
        self.platformversion = androidprefs['platformversion']
      if 'gccversion' in androidprefs:
        self.gcc_toolchainversion = androidprefs['gccversion']
      if 'javasdk' in androidprefs:
        self.javasdk = androidprefs['javasdk']

  def write_variables(self, writer):
    writer.variable('ndk', self.ndkpath)
    writer.variable('sdk', self.sdkpath)
    writer.variable('sysroot', self.sysroot)
    writer.variable('androidjar', self.android_jar )
    writer.variable('apkbuildpath', '')
    writer.variable('apk', '')
    writer.variable('apksource', '')
    writer.variable('apkaddfiles', '')
    writer.variable('javac', self.javac)
    writer.variable('dex', self.dex)
    writer.variable('aapt', self.aapt)
    writer.variable('zipalign', self.zipalign)
    writer.variable('jarsigner', self.jarsigner)
    writer.variable('aaptflags', '')

  def write_rules(self, writer):
    writer.rule('aapt', command = self.aaptcmd, description = 'AAPT $out')
    writer.rule('aaptdeploy', command = self.aaptdeploycmd, description = 'AAPT $out')
    writer.rule('aaptadd', command = self.aaptaddcmd, description = 'AAPT $out')
    writer.rule('javac', command = self.javaccmd, description = 'JAVAC $in')
    writer.rule('dex', command = self.dexcmd, description = 'DEX $out')
    writer.rule('zip', command = self.zipcmd, description = 'ZIP $out')
    writer.rule('zipalign', command = self.zipaligncmd, description = 'ZIPALIGN $out')
    writer.rule('codesign', command = self.codesigncmd, description = 'CODESIGN $out')

  def make_sysroot_path(self, arch):
    return os.path.join(self.ndkpath, 'platforms', 'android-' + self.platformversion, 'arch-' + self.archname[arch])

  def make_gcc_toolchain_path(self, arch):
    return os.path.join(self.ndkpath, 'toolchains', self.gcc_toolchainname[arch], 'prebuilt', self.hostarchname)

  def make_gcc_bin_path(self, arch):
    return os.path.join(self.make_gcc_toolchain_path(arch), 'bin', self.gcc_toolchainprefix[arch])

  def archname(self):
    return self.archname

  def archpath(self):
    return self.archpath

  def hostarchname(self):
    return self.hostarchname

  def apk(self, toolchain, writer, module, archbins, javasources, outpath, binname, basepath, config, implicit_deps, resources):
    buildpath = os.path.join('$buildpath', config, 'apk', binname)
    baseapkname = binname + ".base.apk"
    unsignedapkname = binname + ".unsigned.apk"
    unalignedapkname = binname + ".unaligned.apk"
    apkname = binname + ".apk"
    apkfiles = []
    libfiles = []
    locallibs = []
    resfiles = []
    manifestfile = []

    writer.comment('Make APK')
    for _, value in archbins.iteritems():
      for archbin in value:
        archpair = os.path.split(archbin)
        libname = archpair[1]
        arch = os.path.split(archpair[0])[1]
        locallibpath = os.path.join('lib', self.archpath[arch], libname)
        archpath = os.path.join(buildpath, locallibpath)
        locallibs += [locallibpath + ' ']
        libfiles += toolchain.copy(writer, archbin, archpath)
    for resource in resources:
      filename = os.path.split(resource)[1]
      if filename == 'AndroidManifest.xml':
        manifestfile = toolchain.copy(writer, os.path.join(basepath, module, resource), os.path.join(buildpath, 'AndroidManifest.xml'))
      else:
        restype = os.path.split(os.path.split(resource)[0])[1]
        if restype == 'asset':
          pass #todo: implement
        else:
          resfiles += toolchain.copy(writer, os.path.join(basepath, module, resource), os.path.join(buildpath, 'res', restype, filename))

    #Make directories
    gendir = toolchain.mkdir(writer, os.path.join(buildpath, 'gen'))
    bindir = toolchain.mkdir(writer, os.path.join(buildpath, 'bin'))
    binresdir = toolchain.mkdir(writer, os.path.join(buildpath, 'bin', 'res'), order_only = bindir)
    alldirs = gendir + bindir + binresdir

    aaptvars = [('apkbuildpath', buildpath), ('apk', baseapkname)]
    aaptout = os.path.join(buildpath, baseapkname)
    if config == 'deploy':
      baseapkfile = writer.build(aaptout, 'aaptdeploy', manifestfile, variables = aaptvars, implicit = manifestfile + resfiles, order_only = alldirs)
    else:
      baseapkfile = writer.build(aaptout, 'aapt', manifestfile, variables = aaptvars, implicit = manifestfile + resfiles, order_only = alldirs)

    #Compile java code
    javafiles = []
    localjava = []
    if javasources != []:
      #self.javaccmd = '$javac -d $outpath -classpath $outpath -sourcepath $sourcepath -target 1.5 -bootclasspath $androidjar -g -source 1.5 -Xlint:-options $in'
      #self.dexcmd = '$dex --dex --output $out $in'
      javasourcepath = '.'
      if self.host.is_windows():
        javasourcepath += ';'
      else:
        javasourcepath += ':'
      javasourcepath += os.path.join(buildpath, 'gen')
      classpath = os.path.join(buildpath, 'classes')
      javavars = [('outpath', classpath), ('sourcepath', javasourcepath)]
      javaclasses = writer.build(classpath, 'javac', javasources, variables = javavars, implicit = baseapkfile)
      localjava += ['classes.dex']
      javafiles += writer.build(os.path.join(buildpath, 'classes.dex'), 'dex', classpath)

    #Add native libraries and java classes to apk
    aaptvars = [('apkbuildpath', buildpath), ('apk', unsignedapkname), ('apksource', baseapkname), ('apkaddfiles', toolchain.paths_forward_slash(locallibs + localjava))]
    unsignedapkfile = writer.build(os.path.join(buildpath, unsignedapkname), 'aaptadd', baseapkfile, variables = aaptvars, implicit = libfiles + javafiles, order_only = alldirs)

    #Sign the APK
    codesignvars = [('config', config)]
    unalignedapkfile = writer.build(os.path.join(buildpath, unalignedapkname), 'codesign', unsignedapkfile, variables = codesignvars)

    #Run zipalign
    outfile = writer.build(os.path.join(outpath, config, apkname), 'zipalign', unalignedapkfile)
    return outfile
