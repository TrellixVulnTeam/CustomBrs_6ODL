# Copyright (c) 2014 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

import SCons.Errors

from collections import defaultdict
from conditions import *

from properties import ConvertIfSingle, ParsePropertyTable


class Environment(object):
  def __init__(self, tracker, cond_obj):
    self.add_attributes = defaultdict()
    self.del_attributes = defaultdict()
    self.tracker = tracker
    self.cond_obj = cond_obj
    self.props = {}

  def __getitem__(self, key):
    return self.props[key]

  def __setitem__(self, key, value):
    self.props[key] = value

  def get(self, key, default=None):
    if key in self.props:
      return self.props[key]
    return self.cond_obj.get(key, default)

  def Bit(self, name):
    return self.cond_obj.Bit(name)

  def Clone(self):
    env = Environment(self.tracker, self.cond_obj)
    env.add_attributes = dict(self.add_attributes)
    env.del_attributes = dict(self.del_attributes)
    return env

  def Append(self, **kwargs):
    table = ParsePropertyTable(kwargs)
    for k,v in table.iteritems():
      self.add_attributes[k] = self.add_attributes.get(k,[]) + v

  def FilterOut(self, **kwargs):
    table = ParsePropertyTable(kwargs)
    for k,v in table.iteritems():
      self.del_attributes[k] = self.del_attributes.get(k,[]) + v

  def AddHeaderToSdk(self, nodes):
    return nodes

  def AddLibraryToSdk(self, nodes):
    return nodes

  def AddObject(self, name, sources, objtype, **kwargs):
    add_props = { 'sources': sources }

    table = ParsePropertyTable(kwargs)
    for k,v in table.iteritems():
      add_props[k] = add_props.get(k, []) + v

    # For all GN property:value pairs in the environment
    for k,v in self.add_attributes.iteritems():
      add_props[k] = add_props.get(k, []) + v

    self.tracker.AddObject(name, objtype, add_props, self.del_attributes)

  def ComponentLibrary(self, name, sources, **kwargs):
    self.AddObject(name[3:], sources, 'library', **kwargs)
    return name

  def DualLibrary(self, name, sources, **kwargs):
    self.AddObject(name[3:], sources, 'dual', **kwargs)
    return name

  def ComponentObject(self, name, source):
    self.AddObject(name, [source], 'source_set')
    return name

  def ComponentProgram(self, name, sources, **kwargs):
    self.AddObject(name, sources, 'executable', **kwargs)
    return name

  def Command(self, name, command, other, **kwargs):
    print "Command: %s, %s, %s, : %s" % (name, command, other, str(kwargs))

  def CommandTest(self, name, command, size='small', direct_emulation=True,
                extra_deps=[], posix_path=False, capture_output=True,
                capture_stderr=True, wrapper_program_prefix=None,
                scale_timeout=None, **kwargs):

    ARGS = kwargs
    ARGS['direct_emulation']=direct_emulation
    ARGS['extra_deps']=extra_deps
    ARGS['posix_path']=posix_path
    ARGS['capture_output']=capture_output
    ARGS['capture_stderr']=capture_stderr
    ARGS['wrapper_program_prefix']=wrapper_program_prefix
    ARGS['scale_timeout'] = scale_timeout
    self.AddObject(name, [], 'test', **ARGS)
    return name

  def AddNodeToTestSuite(self, node, suites, name, **kwargs):
    self.tracker.AddObject('Add %s as %s to %s.' %
                           (node, name, ' and '.join(suites)),
                           'note')
    return name

  def NaClSharedLibrary(env, lib_name, *args, **kwargs):
    return self.AddObject(lib_name[3:], sources, 'NaClSharedibrary', **kwargs)

  def NaClSdkLibrary(self, lib_name, sources, **kwargs):
    return self.AddObject(lib_name[3:], sources, 'NaClDualLibrary', **kwargs)

  def Requires(self, *args):
    return args
