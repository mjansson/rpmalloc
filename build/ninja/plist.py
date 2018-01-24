#!/usr/bin/env python

"""PList utility"""

import argparse
import os
import subprocess
import re
import unicodedata

def normalize_char(c):
  try:
    cname = unicodedata.name( unicode(c) )
    cname = cname[:cname.index( ' WITH' )]
    return unicodedata.lookup( cname )
  except ( ValueError, KeyError ):
    return c

def normalize_string(s):
    return ''.join( normalize_char(c) for c in s )

def replace_var( str, var, val ):
  if str.find( '$(' + var + ')' ) != -1:
    return str.replace( '$(' + var + ')', val )
  if str.find( '${' + var + '}' ) != -1:
    return str.replace( '${' + var + '}', val )
  return str


parser = argparse.ArgumentParser( description = 'PList utility for Ninja builds' )
parser.add_argument( 'files',
                     metavar = 'file', type=file, nargs='+',
                     help = 'Source plist file' )
parser.add_argument( '--exename', type=str,
                     help = 'Executable name',
                     default = [] )
parser.add_argument( '--prodname', type=str,
                     help = 'Product name',
                     default = [] )
parser.add_argument( '--bundle', type=str,
                     help = 'Bundle identifier',
                     default = [] )
parser.add_argument( '--output', type=str,
                     help = 'Output path',
                     default = [] )
parser.add_argument( '--target', type=str,
                     help = 'Target OS',
                     default = [] )
parser.add_argument( '--deploymenttarget', type=str,
                     help = 'Target OS version',
                     default = [] )
options = parser.parse_args()

if not options.exename:
  options.exename = 'unknown'
if not options.prodname:
  options.prodname = 'unknown'
if not options.target:
  options.target = 'macos'
if not options.deploymenttarget:
  if options.target == 'macos':
    options.deploymenttarget = '10.7'
  else:
    options.deploymenttarget = '6.0'

buildversion = subprocess.check_output( [ 'sw_vers', '-buildVersion' ] ).strip()

#Merge inputs using first file as base
lines = []
for f in options.files:
  if lines == []:
    lines += [ line.strip( '\n\r' ) for line in f ]
  else:
    mergelines = [ line.strip( '\n\r' ) for line in f ]
    for i in range( 0, len( mergelines ) ):
      if re.match( '^<dict/>$', mergelines[i] ):
        break
      if re.match( '^<dict>$', mergelines[i] ):
        for j in range( 0, len( lines ) ):
          if re.match( '^</dict>$', lines[j] ):
            for k in range( i+1, len( mergelines ) ):
              if re.match( '^</dict>$', mergelines[k] ):
                break
              lines.insert( j+(k-(i+1)), mergelines[k] )
            break
        break

#Parse input plist to get package type and signature
bundle_package_type = 'APPL'
bundle_signature = '????'

for i in range( 0, len( lines ) ):
  if 'CFBundlePackageType' in lines[i]:
    match = re.match( '^.*>(.*)<.*$', lines[i+1] )
    if match:
      bundle_package_type = match.group(1)
  if 'CFBundleSignature' in lines[i]:
    match = re.match( '^.*>(.*)<.*$', lines[i+1] )
    if match:
      bundle_signature = match.group(1)

#Write package type and signature to PkgInfo in output path
with open( os.path.join( os.path.dirname( options.output ), 'PkgInfo' ), 'w' ) as pkginfo_file:
  pkginfo_file.write( bundle_package_type + bundle_signature )
  pkginfo_file.close()

#insert os version
for i in range( 0, len( lines ) ):
  if re.match( '^<dict>$', lines[i] ):
    lines.insert( i+1, '\t<key>BuildMachineOSBuild</key>' )
    lines.insert( i+2, '\t<string>' + buildversion + '</string>' )
    break

#replace build variables name
for i in range( 0, len( lines ) ):
  lines[i] = replace_var( lines[i], 'EXECUTABLE_NAME', options.exename )
  lines[i] = replace_var( lines[i], 'PRODUCT_NAME', options.prodname )
  lines[i] = replace_var( lines[i], 'PRODUCT_NAME:rfc1034identifier', normalize_string( options.exename ).lower() )
  lines[i] = replace_var( lines[i], 'PRODUCT_NAME:c99extidentifier', normalize_string( options.exename ).lower().replace( '-', '_' ).replace( '.', '_' ) )
  lines[i] = replace_var( lines[i], 'IOS_DEPLOYMENT_TARGET', options.deploymenttarget )
  lines[i] = replace_var( lines[i], 'MACOSX_DEPLOYMENT_TARGET', options.deploymenttarget )

#replace bundle identifier if given
if not options.bundle is None and options.bundle != '':
  for i in range( 0, len( lines ) ):
    if lines[i].find( 'CFBundleIdentifier' ) != -1:
      lines[i+1] = '\t<string>' + normalize_string( options.bundle ) + '</string>'
      break

#add supported platform, minimum os version and requirements
if options.target == 'ios':
  for i in range( 0, len( lines ) ):
    if 'CFBundleSignature' in lines[i]:
      lines.insert( i+2,  '\t<key>CFBundleSupportedPlatforms</key>' )
      lines.insert( i+3,  '\t<array>' )
      lines.insert( i+4,  '\t\t<string>iPhoneOS</string>' )
      lines.insert( i+5,  '\t</array>' )
      lines.insert( i+6,  '\t<key>MinimumOSVersion</key>' )
      lines.insert( i+7,  '\t<string>6.0</string>' )
      lines.insert( i+8,  '\t<key>UIDeviceFamily</key>' )
      lines.insert( i+9,  '\t<array>' )
      lines.insert( i+10, '\t\t<integer>1</integer>' )
      lines.insert( i+11, '\t\t<integer>2</integer>' )
      lines.insert( i+12, '\t</array>' )
      break

#add build info
#<key>DTCompiler</key>
#<string>com.apple.compilers.llvm.clang.1_0</string>
#<key>DTPlatformBuild</key>
#<string>12B411</string>
#<key>DTPlatformName</key>
#<string>iphoneos</string>
#<key>DTPlatformVersion</key>
#<string>8.1</string>
#<key>DTSDKBuild</key>
#<string>12B411</string>
#<key>DTSDKName</key>
#<string>iphoneos8.1</string>
#<key>DTXcode</key>
#<string>0611</string>
#<key>DTXcodeBuild</key>
#<string>6A2008a</string>

#write final Info.plist in output path
with open( options.output, 'w' ) as plist_file:
  for line in lines:
    #print lines[i]
    plist_file.write( line + '\n' )
  plist_file.close()

#run plutil -convert binary1
sdk = 'iphoneos'
platformpath = subprocess.check_output( [ 'xcrun', '--sdk', sdk, '--show-sdk-platform-path' ] ).strip()
localpath = platformpath + "/Developer/usr/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"
plutil = "PATH=" + localpath + " " + subprocess.check_output( [ 'xcrun', '--sdk', sdk, '-f', 'plutil' ] ).strip()
os.system( plutil + ' -convert binary1 ' + options.output )

