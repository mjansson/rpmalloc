#!/usr/bin/env python

"""Codesign utility"""

import argparse
import subprocess
import os
import time
import shutil
import json

parser = argparse.ArgumentParser(description = 'Codesign utility for Ninja builds')
parser.add_argument('file', type=str,
                    help = 'Bundle/package to sign')
parser.add_argument('--target', type=str,
                    help = 'Target',
                    choices = ['macos', 'ios', 'android'],
                    default = '')
parser.add_argument('--bundle', type=str,
                    help = 'Bundle identifier (OSX/iOS)',
                    default = '')
parser.add_argument('--organisation', type=str,
                    help = 'Organisation identifier (OSX/iOS)',
                    default = '')
parser.add_argument('--provisioning', type=str,
                    help = 'Provisioning profile (OSX/iOS)',
                    default = '')
parser.add_argument('--builddir', type=str,
                    help = 'Build directory (OSX/iOS)',
                    default = '')
parser.add_argument('--binname', type=str,
                    help = 'Binary name (OSX/iOS)',
                    default = '')
parser.add_argument('--zipfile', type=str,
                    help = 'Zip file (Android)',
                    default = '')
parser.add_argument('--tsacert', type=str,
                    help = 'TSA cert (Android)',
                    default = '')
parser.add_argument('--tsa', type=str,
                    help = 'TSA (Android)',
                    default = '')
parser.add_argument('--keystore', type=str,
                    help = 'Keystore (Android)',
                    default = '')
parser.add_argument('--keystorepass', type=str,
                    help = 'Keystore password (Android)',
                    default = '')
parser.add_argument('--keyalias', type=str,
                    help = 'Key alias (Android)',
                    default = '')
parser.add_argument('--keypass', type=str,
                    help = 'Key password (Android)',
                    default = '')
parser.add_argument('--jarsigner', type=str,
                    help = 'JAR signer (Android)',
                    default = 'jarsigner')
parser.add_argument('--prefs', type=str,
                    help = 'Preferences file',
                    default = '')
parser.add_argument('--config', type=str,
                    help = 'Build configuration',
                    default = '')
options = parser.parse_args()

androidprefs = {}
iosprefs = {}
macosprefs = {}


def parse_prefs( prefsfile ):
  global androidprefs
  global iosprefs
  global macosprefs
  if not os.path.isfile( prefsfile ):
    return
  file = open( prefsfile, 'r' )
  prefs = json.load( file )
  file.close()
  if 'android' in prefs:
    androidprefs = prefs['android']
  if 'ios' in prefs:
    iosprefs = prefs['ios']
  if 'macos' in prefs:
    macosprefs = prefs['macos']


def codesign_ios():
  if not 'organisation' in iosprefs:
    iosprefs['organisation'] = options.organisation
  if not 'bundleidentifier' in iosprefs:
    iosprefs['bundleidentifier'] = options.bundle
  if not 'provisioning' in iosprefs:
    iosprefs['provisioning'] = options.provisioning

  sdkdir = subprocess.check_output( [ 'xcrun', '--sdk', 'iphoneos', '--show-sdk-path' ] ).strip()
  entitlements = os.path.join( sdkdir, 'Entitlements.plist' )
  plistpath = os.path.join( options.builddir, 'Entitlements.xcent' )

  platformpath = subprocess.check_output( [ 'xcrun', '--sdk', 'iphoneos', '--show-sdk-platform-path' ] ).strip()
  localpath = platformpath + "/Developer/usr/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"
  plutil = "PATH=" + localpath + " " + subprocess.check_output( [ 'xcrun', '--sdk', 'iphoneos', '-f', 'plutil' ] ).strip()

  shutil.copyfile( entitlements, plistpath )
  os.system( plutil + ' -convert xml1 ' + plistpath )

  f = open( plistpath, 'r' )
  lines = [ line.strip( '\n\r' ) for line in f ]
  f.close()

  for i in range( 0, len( lines ) ):
    if lines[i].find( '$(AppIdentifierPrefix)' ) != -1:
      lines[i] = lines[i].replace( '$(AppIdentifierPrefix)', iosprefs['organisation'] + '.' )
    if lines[i].find( '$(CFBundleIdentifier)' ) != -1:
      lines[i] = lines[i].replace( '$(CFBundleIdentifier)', iosprefs['bundleidentifier'] )
    if lines[i].find( '$(binname)' ) != -1:
      lines[i] = lines[i].replace( '$(binname)', options.binname )

  with open( plistpath, 'w' ) as plist_file:
    for line in lines:
      if options.config != 'deploy' and line == '</dict>':
        plist_file.write( '\t<key>get-task-allow</key>\n' )
        plist_file.write( '\t<true/>\n' )
      plist_file.write( line + '\n' )
    plist_file.close()

  if os.path.isfile( os.path.join( options.file, '_CodeSignature', 'CodeResources' ) ):
    os.remove( os.path.join( options.file, '_CodeSignature', 'CodeResources' ) )

  os.system( '/usr/bin/codesign --force --sign "' + iosprefs['signature'] + '" --entitlements ' + plistpath + ' ' + options.file )

  if os.path.isfile( os.path.join( options.file, '_CodeSignature', 'CodeResources' ) ):
    os.utime( os.path.join( options.file, '_CodeSignature', 'CodeResources' ), None )
    os.utime( os.path.join( options.file, '_CodeSignature' ), None )
    os.utime( options.file, None )


def codesign_macos():
  if not 'organisation' in macosprefs:
    macosprefs['organisation'] = options.organisation
  if not 'bundleidentifier' in macosprefs:
    macosprefs['bundleidentifier'] = options.bundle
  if not 'provisioning' in macosprefs:
    macosprefs['provisioning'] = options.provisioning

  codesign_allocate = subprocess.check_output( [ 'xcrun', '--sdk', 'macosx', '-f', 'codesign_allocate' ] ).strip()
  sdkdir = subprocess.check_output( [ 'xcrun', '--sdk', 'macosx', '--show-sdk-path' ] ).strip()
  entitlements = os.path.join( sdkdir, 'Entitlements.plist' )

  if os.path.isfile( os.path.join( options.file, 'Contents', '_CodeSignature', 'CodeResources' ) ):
    os.remove( os.path.join( options.file, 'Contents', '_CodeSignature', 'CodeResources' ) )

  if 'signature' in macosprefs:
    os.system( 'export CODESIGN_ALLOCATE=' + codesign_allocate + '; /usr/bin/codesign --force --sign ' + macosprefs['signature'] + ' ' + options.file )

  if os.path.isfile( os.path.join( options.file, 'Contents', '_CodeSignature', 'CodeResources' ) ):
    os.utime( os.path.join( options.file, 'Contents', '_CodeSignature', 'CodeResources' ), None )
    os.utime( os.path.join( options.file, 'Contents', '_CodeSignature' ), None )
    os.utime( os.path.join( options.file, 'Contents' ), None )
    os.utime( options.file, None )


def codesign_android():
  if not 'tsacert' in androidprefs:
    androidprefs['tsacert'] = options.tsacert
  if not 'tsa' in androidprefs:
    androidprefs['tsa'] = options.tsa
  if not 'keystore' in androidprefs:
    androidprefs['keystore'] = options.keystore
  if not 'keystorepass' in androidprefs:
    androidprefs['keystorepass'] = options.keystorepass
  if not 'keyalias' in androidprefs:
    androidprefs['keyalias'] = options.keyalias
  if not 'keypass' in androidprefs:
    androidprefs['keypass'] = options.keypass
  if not 'jarsigner' in androidprefs:
    androidprefs['jarsigner'] = options.jarsigner

  timestamp = ''
  if androidprefs['tsacert'] != '':
    timestamp = '-tsacert ' + androidprefs['tsacert']
  elif androidprefs['tsa'] != '':
    timestamp = '-tsa ' + androidprefs['tsa']

  proxy = ''
  if 'proxy' in androidprefs and androidprefs['proxy'] != '' and androidprefs['proxy'] != 'None':
    proxy = androidprefs['proxy']
    if proxy != '' and proxy != 'None':
      defstr = "-J-Dhttp.proxy"
      url = urlparse.urlparse(proxy)
      if url.scheme == 'https':
        defstr = "-J-Dhttps.proxy"
      host = url.netloc
      port = ''
      username = ''
      password = ''
      if '@' in host:
        username, host = host.split(':', 1)
        password, host = host.split('@', 1)
      if ':' in host:
        host, port = host.split(':', 1)
      proxy = defstr + "Host=" + host
      if port != '':
        proxy += " " + defstr + "Port=" + port
      if username != '':
        proxy += " " + defstr + "User=" + username
      if password != '':
        proxy += " " + defstr + "Password=" + password

  signcmd = androidprefs['jarsigner'] + ' ' + timestamp + ' -sigalg SHA1withRSA -digestalg SHA1 -keystore ' + androidprefs['keystore'] + ' -storepass ' + androidprefs['keystorepass'] + ' -keypass ' + androidprefs['keypass'] + ' -signedjar ' + options.file + ' ' + options.zipfile + ' ' + androidprefs['keyalias'] + ' ' + proxy
  os.system(signcmd)


parse_prefs( options.prefs )

if options.target == 'ios':
  codesign_ios()
elif options.target == 'macos':
  codesign_macos()
elif options.target == 'android':
  codesign_android()
