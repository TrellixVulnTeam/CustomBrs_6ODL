# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Defines TestPackageApk to help run APK-based native tests."""
# pylint: disable=W0212

import logging
import os
import shlex
import sys
import tempfile
import time

from pylib import android_commands
from pylib import constants
from pylib import pexpect
from pylib.device import device_errors
from pylib.gtest.test_package import TestPackage


class TestPackageApk(TestPackage):
  """A helper class for running APK-based native tests."""

  def __init__(self, suite_name):
    """
    Args:
      suite_name: Name of the test suite (e.g. base_unittests).
    """
    TestPackage.__init__(self, suite_name)
    if suite_name == 'content_browsertests':
      self.suite_path = os.path.join(
          constants.GetOutDirectory(), 'apks', '%s.apk' % suite_name)
      self._package_info = constants.PACKAGE_INFO['content_browsertests']
    else:
      self.suite_path = os.path.join(
          constants.GetOutDirectory(), '%s_apk' % suite_name,
          '%s-debug.apk' % suite_name)
      self._package_info = constants.PACKAGE_INFO['gtest']

  def _CreateCommandLineFileOnDevice(self, device, options):
    command_line_file = tempfile.NamedTemporaryFile()
    # GTest expects argv[0] to be the executable path.
    command_line_file.write(self.suite_name + ' ' + options)
    command_line_file.flush()
    device.old_interface.PushIfNeeded(
        command_line_file.name,
        self._package_info.cmdline_file)

  def _GetFifo(self):
    # The test.fifo path is determined by:
    # testing/android/java/src/org/chromium/native_test/
    #     ChromeNativeTestActivity.java and
    # testing/android/native_test_launcher.cc
    return '/data/data/' + self._package_info.package + '/files/test.fifo'

  def _ClearFifo(self, device):
    device.old_interface.RunShellCommand('rm -f ' + self._GetFifo())

  def _WatchFifo(self, device, timeout, logfile=None):
    for i in range(10):
      if device.old_interface.FileExistsOnDevice(self._GetFifo()):
        logging.info('Fifo created.')
        break
      time.sleep(i)
    else:
      raise device_errors.DeviceUnreachableError(
          'Unable to find fifo on device %s ' % self._GetFifo())
    args = shlex.split(device.old_interface.Adb()._target_arg)
    args += ['shell', 'cat', self._GetFifo()]
    return pexpect.spawn('adb', args, timeout=timeout, logfile=logfile)

  def _StartActivity(self, device):
    device.old_interface.StartActivity(
        self._package_info.package,
        self._package_info.activity,
        # No wait since the runner waits for FIFO creation anyway.
        wait_for_completion=False,
        action='android.intent.action.MAIN',
        force_stop=True)

  #override
  def ClearApplicationState(self, device):
    device.old_interface.ClearApplicationState(self._package_info.package)
    # Content shell creates a profile on the sdscard which accumulates cache
    # files over time.
    if self.suite_name == 'content_browsertests':
      device.old_interface.RunShellCommand(
          'rm -r %s/content_shell' % device.old_interface.GetExternalStorage(),
          timeout_time=60 * 2)

  #override
  def CreateCommandLineFileOnDevice(self, device, test_filter, test_arguments):
    self._CreateCommandLineFileOnDevice(
        device, '--gtest_filter=%s %s' % (test_filter, test_arguments))

  #override
  def GetAllTests(self, device):
    self._CreateCommandLineFileOnDevice(device, '--gtest_list_tests')
    try:
      self.tool.SetupEnvironment()
      # Clear and start monitoring logcat.
      self._ClearFifo(device)
      self._StartActivity(device)
      # Wait for native test to complete.
      p = self._WatchFifo(device, timeout=30 * self.tool.GetTimeoutScale())
      p.expect('<<ScopedMainEntryLogger')
      p.close()
    finally:
      self.tool.CleanUpEnvironment()
    # We need to strip the trailing newline.
    content = [line.rstrip() for line in p.before.splitlines()]
    return self._ParseGTestListTests(content)

  #override
  def SpawnTestProcess(self, device):
    try:
      self.tool.SetupEnvironment()
      self._ClearFifo(device)
      self._StartActivity(device)
    finally:
      self.tool.CleanUpEnvironment()
    logfile = android_commands.NewLineNormalizer(sys.stdout)
    return self._WatchFifo(device, timeout=10, logfile=logfile)

  #override
  def Install(self, device):
    self.tool.CopyFiles()
    device.old_interface.ManagedInstall(
        self.suite_path, False, package_name=self._package_info.package)
