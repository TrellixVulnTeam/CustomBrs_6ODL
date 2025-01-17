# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import psutil
import signal

from pylib import android_commands
from pylib.device import device_errors
from pylib.device import device_utils


def _KillWebServers():
  for s in [signal.SIGTERM, signal.SIGINT, signal.SIGQUIT, signal.SIGKILL]:
    signalled = []
    for server in ['lighttpd', 'webpagereplay']:
      for p in psutil.process_iter():
        try:
          if not server in ' '.join(p.cmdline):
            continue
          logging.info('Killing %s %s %s', s, server, p.pid)
          p.send_signal(s)
          signalled.append(p)
        except Exception as e:
          logging.warning('Failed killing %s %s %s', server, p.pid, e)
    for p in signalled:
      try:
        p.wait(1)
      except Exception as e:
        logging.warning('Failed waiting for %s to die. %s', p.pid, e)



def CleanupLeftoverProcesses():
  """Clean up the test environment, restarting fresh adb and HTTP daemons."""
  _KillWebServers()
  did_restart_host_adb = False
  for device_serial in android_commands.GetAttachedDevices():
    device = device_utils.DeviceUtils(device_serial)
    # Make sure we restart the host adb server only once.
    if not did_restart_host_adb:
      device_utils.RestartServer()
      did_restart_host_adb = True
    device.old_interface.RestartAdbdOnDevice()
    try:
      device.EnableRoot()
    except device_errors.CommandFailedError as e:
      # TODO(jbudorick) Handle this exception appropriately after interface
      #                 conversions are finished.
      logging.error(str(e))
    device.old_interface.WaitForDevicePm()

