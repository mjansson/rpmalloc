#!/usr/bin/env python

"""Version utility"""

import subprocess
import os
import sys

def generate_version_string(libname):

  version_numbers = []
  tokens = []

  gitcmd = 'git'
  if sys.platform.startswith('win'):
    gitcmd = 'git.exe'
  try:
    git_version = subprocess.check_output( [ gitcmd, 'describe', '--long' ], stderr = subprocess.STDOUT ).strip()
    tokens = git_version.split( '-' )
    version_numbers = tokens[0].split( '.' )
  except Exception:
    pass

  version_major = "0"
  version_minor = "0"
  version_revision = "1"
  version_build = "0"
  version_scm = "0"

  if version_numbers and len( version_numbers ) > 2:
    version_major = version_numbers[0]
    version_minor = version_numbers[1]
    version_revision = version_numbers[2]

  if tokens and len( tokens ) > 2:
    version_build = tokens[1]
    version_scm = tokens[2][1:]

  module = ""
  if not libname == "foundation":
    module = "_module"

  source = """/* ****** AUTOMATICALLY GENERATED, DO NOT EDIT ******
   This file is generated from the git describe command.
   Run the configure script to regenerate this file */

#include <foundation/version.h>
#include <""" + libname + "/" + libname + """.h>

version_t
""" + libname + module + """_version(void) {
"""
  source += "	return version_make(" + version_major + ", " + version_minor + ", " + version_revision + ", " + version_build + ", 0x" + version_scm + ");\n}\n"
  return source

def read_version_string(input_path):
  try:
    file = open( os.path.join( input_path, 'version.c' ), "r" )
    str = file.read()
    file.close()
  except IOError:
    str = ""
  return str

def write_version_string(output_path, str):
  file = open( os.path.join( output_path, 'version.c' ), "w" )
  file.write( str )
  file.close

def generate_version(libname, output_path):
  generated = generate_version_string(libname)
  if generated == None:
    return
  previous = read_version_string(output_path)

  if generated != previous:
    write_version_string(output_path, generated)

if __name__ == "__main__":
  generate_version(sys.argv[1], sys.argv[2])
