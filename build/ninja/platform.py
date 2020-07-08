#!/usr/bin/python

"""Ninja platform abstraction"""

import sys

def supported_platforms():
  return [ 'windows', 'linux', 'macos', 'bsd', 'ios', 'android', 'raspberrypi', 'tizen', 'sunos' ]

class Platform(object):
  def __init__(self, platform):
    self.platform = platform
    if self.platform is None:
      self.platform = sys.platform
    if self.platform.startswith('linux'):
      self.platform = 'linux'
    elif self.platform.startswith('darwin'):
      self.platform = 'macos'
    elif self.platform.startswith('macos'):
      self.platform = 'macos'
    elif self.platform.startswith('win'):
      self.platform = 'windows'
    elif 'bsd' in self.platform:
      self.platform = 'bsd'
    elif self.platform.startswith('ios'):
      self.platform = 'ios'
    elif self.platform.startswith('android'):
      self.platform = 'android'
    elif self.platform.startswith('raspberry'):
      self.platform = 'raspberrypi'
    elif self.platform.startswith('tizen'):
      self.platform = 'tizen'
    elif self.platform.startswith('sunos'):
      self.platform = 'sunos'

  def platform(self):
    return self.platform

  def is_linux(self):
    return self.platform == 'linux'

  def is_windows(self):
    return self.platform == 'windows'

  def is_macos(self):
    return self.platform == 'macos'

  def is_bsd(self):
    return self.platform == 'bsd'

  def is_ios(self):
    return self.platform == 'ios'

  def is_android(self):
    return self.platform == 'android'

  def is_raspberrypi(self):
    return self.platform == 'raspberrypi'

  def is_tizen(self):
    return self.platform == 'tizen'

  def is_sunos(self):
    return self.platform == 'sunos'

  def get(self):
    return self.platform
