#!/usr/bin/env python

"""Ninja toolchain abstraction for XCode toolchain"""

import os
import subprocess

import toolchain
import syntax

def make_target(toolchain, host, target):
  return XCode(toolchain, host, target)

class XCode(object):
  def __init__(self, toolchain, host, target):
    self.toolchain = toolchain
    self.host = host
    self.target = target

  def initialize_toolchain(self):
    self.organisation = ''
    self.bundleidentifier = ''
    self.provisioning = ''
    if self.target.is_macos():
      self.deploymenttarget = '10.7'
    elif self.target.is_ios():
      self.deploymenttarget = '8.0'

  def build_toolchain(self):
    if self.target.is_macos():
      sdk = 'macosx'
      deploytarget = 'MACOSX_DEPLOYMENT_TARGET=' + self.deploymenttarget
    elif self.target.is_ios():
      sdk = 'iphoneos'
      deploytarget = 'IPHONEOS_DEPLOYMENT_TARGET=' + self.deploymenttarget

    platformpath = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-platform-path']).strip()
    localpath = platformpath + "/Developer/usr/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"

    self.plist = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'plutil']).strip()
    self.xcassets = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'actool']).strip()
    self.xib = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'ibtool']).strip()
    self.dsymutil = "PATH=" + localpath + " " + subprocess.check_output(['xcrun', '--sdk', sdk, '-f', 'dsymutil']).strip()

    self.plistcmd = 'build/ninja/plist.py --exename $exename --prodname $prodname --bundle $bundleidentifier --target $target --deploymenttarget $deploymenttarget --output $outpath $in'
    if self.target.is_macos():
      self.xcassetscmd = 'mkdir -p $outpath && $xcassets --output-format human-readable-text --output-partial-info-plist $outplist' \
                         ' --app-icon AppIcon --launch-image LaunchImage --platform macosx --minimum-deployment-target ' + self.deploymenttarget + \
                         ' --target-device mac --compress-pngs --compile $outpath $in >/dev/null'
      self.xibcmd = '$xib --target-device mac --module $module --minimum-deployment-target ' + self.deploymenttarget + \
                    ' --output-partial-info-plist $outplist --auto-activate-custom-fonts' \
                    ' --output-format human-readable-text --compile $outpath $in'
    elif self.target.is_ios():
      self.xcassetscmd = 'mkdir -p $outpath && $xcassets --output-format human-readable-text --output-partial-info-plist $outplist' \
                         ' --app-icon AppIcon --launch-image LaunchImage --platform iphoneos --minimum-deployment-target ' + self.deploymenttarget + \
                         ' --target-device iphone --target-device ipad --compress-pngs --compile $outpath $in >/dev/null'
      self.xibcmd = '$xib --target-device iphone --target-device ipad --module $module --minimum-deployment-target ' + self.deploymenttarget + \
                    ' --output-partial-info-plist $outplist --auto-activate-custom-fonts' \
                    ' --output-format human-readable-text --compile $outpath $in &> /dev/null '
    self.dsymutilcmd = '$dsymutil $in -o $outpath'
    self.codesigncmd = 'build/ninja/codesign.py --target $target --prefs codesign.json --builddir $builddir --binname $binname --config $config $outpath'

  def parse_default_variables(self, variables):
    if not variables:
      return
    if isinstance(variables, dict):
      iterator = iter(variables.items())
    else:
      iterator = iter(variables)
    for key, val in iterator:
      if key == 'deploymenttarget':
        self.deploymenttarget = val
      if key == 'organisation':
        self.organisation = val
      if key == 'bundleidentifier':
        self.bundleidentifier = val
      if key == 'provisioning':
        self.provisioning = val

  def parse_prefs(self, prefs):
    if self.target.is_ios() and 'ios' in prefs:
      iosprefs = prefs['ios']
      if 'deploymenttarget' in iosprefs:
        self.deploymenttarget = iosprefs['deploymenttarget']
      if 'organisation' in iosprefs:
        self.organisation = iosprefs['organisation']
      if 'bundleidentifier' in iosprefs:
        self.bundleidentifier = iosprefs['bundleidentifier']
      if 'provisioning' in iosprefs:
        self.provisioning = iosprefs['provisioning']
    elif self.target.is_macos() and 'macos' in prefs:
      macosprefs = prefs['macos']
      if 'deploymenttarget' in macosprefs:
        self.deploymenttarget = macosprefs['deploymenttarget']
      if 'organisation' in macosprefs:
        self.organisation = macosprefs['organisation']
      if 'bundleidentifier' in macosprefs:
        self.bundleidentifier = macosprefs['bundleidentifier']
      if 'provisioning' in macosprefs:
        self.provisioning = macosprefs['provisioning']

  def write_variables(self, writer):
    writer.variable('plist', self.plist)
    writer.variable('xcassets', self.xcassets)
    writer.variable('xib', self.xib)
    writer.variable('dsymutil', self.dsymutil)
    writer.variable('bundleidentifier', syntax.escape(self.bundleidentifier))
    writer.variable('deploymenttarget', self.deploymenttarget)

  def write_rules(self, writer):
    writer.rule('dsymutil', command = self.dsymutilcmd, description = 'DSYMUTIL $outpath')
    writer.rule('plist', command = self.plistcmd, description = 'PLIST $outpath')
    writer.rule('xcassets', command = self.xcassetscmd, description = 'XCASSETS $outpath')
    writer.rule('xib', command = self.xibcmd, description = 'XIB $outpath')
    writer.rule('codesign', command = self.codesigncmd, description = 'CODESIGN $outpath')

  def make_bundleidentifier(self, binname):
    return self.bundleidentifier.replace('$(binname)', binname)

  def app(self, toolchain, writer, module, archbins, outpath, binname, basepath, config, implicit_deps, resources, codesign):
    #Outputs
    builtbin = []
    builtres = []
    builtsym = []

    #Paths
    builddir = os.path.join('$buildpath', config, 'app', binname)
    configpath = os.path.join(outpath, config)
    apppath = os.path.join(configpath, binname + '.app')
    dsympath = os.path.join(outpath, config, binname + '.dSYM')

    #Extract debug symbols from universal binary
    dsymcontentpath = os.path.join(dsympath, 'Contents')
    builtsym = writer.build([os.path.join(dsymcontentpath, 'Resources', 'DWARF', binname), os.path.join(dsymcontentpath, 'Resources', 'DWARF' ), os.path.join(dsymcontentpath, 'Resources'), os.path.join(dsymcontentpath, 'Info.plist'), dsymcontentpath, dsympath], 'dsymutil', archbins[config], variables = [('outpath', dsympath)])

    #Copy final universal binary
    if self.target.is_ios():
      builtbin = toolchain.copy(writer, archbins[config], os.path.join(apppath, toolchain.binprefix + binname + toolchain.binext))
    else:
      builtbin = toolchain.copy(writer, archbins[config], os.path.join(apppath, 'Contents', 'MacOS', toolchain.binprefix + binname + toolchain.binext))

    #Build resources
    if resources:
      has_resources = False

      #Lists of input plists and partial plist files produced by resources
      plists = []
      assetsplists = []
      xibplists = []

      #All resource output files
      outfiles = []

      #First build everything except plist inputs
      for resource in resources:
        if resource.endswith('.xcassets'):
          if self.target.is_macos():
            assetsvars = [('outpath', os.path.join(os.getcwd(), apppath, 'Contents', 'Resources'))]
          else:
            assetsvars = [('outpath', apppath)]
          outplist = os.path.join(os.getcwd(), builddir, os.path.splitext(os.path.basename(resource))[0] + '-xcassets.plist')
          assetsvars += [('outplist', outplist)]
          outfiles = [outplist]
          if self.target.is_macos():
            outfiles += [os.path.join(os.getcwd(), apppath, 'Contents', 'Resources', 'AppIcon.icns')]
          elif self.target.is_ios():
            pass #TODO: Need to list all icon and launch image files here
          assetsplists += writer.build(outfiles, 'xcassets', os.path.join(os.getcwd(), basepath, module, resource), variables = assetsvars)
          has_resources = True
        elif resource.endswith('.xib'):
          xibmodule = binname.replace('-', '_').replace('.', '_')
          if self.target.is_macos():
            nibpath = os.path.join(apppath, 'Contents', 'Resources', os.path.splitext(os.path.basename(resource))[0] + '.nib')
          else:
            nibpath = os.path.join(apppath, os.path.splitext(os.path.basename(resource))[0] + '.nib')
          plistpath = os.path.join(builddir, os.path.splitext(os.path.basename(resource))[0] + '-xib.plist')
          xibplists += [plistpath]
          outfiles = []
          if self.target.is_ios():
            outfiles += [os.path.join(nibpath, 'objects.nib'), os.path.join(nibpath, 'objects-8.0+.nib'), os.path.join(nibpath, 'runtime.nib')]
          outfiles += [nibpath, plistpath]
          builtres += writer.build(outfiles, 'xib', os.path.join(basepath, module, resource), variables = [('outpath', nibpath), ('outplist', plistpath), ('module', xibmodule)])
          has_resources = True
        elif resource.endswith('.plist'):
          plists += [os.path.join(basepath, module, resource)]

      #Extra output files/directories
      outfiles = []
      if has_resources and self.target.is_macos():
        outfiles += [os.path.join(apppath, 'Contents', 'Resources')]

      #Now build input plists appending partial plists created by previous resources
      if self.target.is_macos():
        plistpath = os.path.join(apppath, 'Contents', 'Info.plist')
        pkginfopath = os.path.join(apppath, 'Contents', 'PkgInfo')
      else:
        plistpath = os.path.join(apppath, 'Info.plist')
        pkginfopath = os.path.join(apppath, 'PkgInfo')
      plistvars = [('exename', binname), ('prodname', binname), ('outpath', plistpath)]
      bundleidentifier = self.make_bundleidentifier(binname)
      if bundleidentifier != '':
        plistvars += [('bundleidentifier', bundleidentifier)]
      outfiles += [plistpath, pkginfopath]
      builtres += writer.build(outfiles, 'plist', plists + assetsplists + xibplists, implicit = [os.path.join( 'build', 'ninja', 'plist.py')], variables = plistvars)

    #Do code signing (might modify binary, but does not matter, nothing should have final binary as input anyway)
    if codesign:
      codesignvars = [('builddir', builddir), ('binname', binname), ('outpath', apppath), ('config', config)]
      if self.target.is_ios():
        if self.provisioning != '':
          codesignvars += [('provisioning', self.provisioning)]
        writer.build([os.path.join(apppath, '_CodeSignature', 'CodeResources'), os.path.join(apppath, '_CodeSignature'), apppath], 'codesign', builtbin, implicit = builtres + [os.path.join('build', 'ninja', 'codesign.py')], variables = codesignvars)
      elif self.target.is_macos():
        if self.provisioning != '':
          codesignvars += [('provisioning', self.provisioning)]
        writer.build([os.path.join(apppath, 'Contents', '_CodeSignature', 'CodeResources'), os.path.join(apppath, 'Contents', '_CodeSignature'), os.path.join(apppath, 'Contents'), apppath], 'codesign', builtbin, implicit = builtres + [os.path.join('build', 'ninja', 'codesign.py')], variables = codesignvars)

    return builtbin + builtsym + builtres
