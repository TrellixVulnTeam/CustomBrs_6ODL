#!/usr/bin/env python
# Copyright 2013 The Swarming Authors. All rights reserved.
# Use of this source code is governed under the Apache License, Version 2.0 that
# can be found in the LICENSE file.

import datetime
import getpass
import hashlib
import inspect
import json
import logging
import os
import shutil
import StringIO
import sys
import tempfile
import threading
import unittest

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)
sys.path.insert(0, os.path.join(ROOT_DIR, 'third_party'))

from depot_tools import auto_stub
import swarming
import test_utils
from utils import net


ALGO = hashlib.sha1
FILE_NAME = u'test.isolated'
FILE_HASH = u'1' * 40
TEST_NAME = u'unit_tests'


TEST_CASE_SUCCESS = (
  '[----------] 2 tests from StaticCookiePolicyTest\n'
  '[ RUN      ] StaticCookiePolicyTest.AllowAllCookiesTest\n'
  '[       OK ] StaticCookiePolicyTest.AllowAllCookiesTest (0 ms)\n'
  '[ RUN      ] StaticCookiePolicyTest.BlockAllCookiesTest\n'
  '[       OK ] StaticCookiePolicyTest.BlockAllCookiesTest (0 ms)\n'
  '[----------] 2 tests from StaticCookiePolicyTest (0 ms total)\n'
  '\n'
  '[----------] 1 test from TCPListenSocketTest\n'
  '[ RUN      ] TCPListenSocketTest.ServerSend\n'
  '[       OK ] TCPListenSocketTest.ServerSend (1 ms)\n'
  '[----------] 1 test from TCPListenSocketTest (1 ms total)\n')


TEST_CASE_FAILURE = (
  '[----------] 2 tests from StaticCookiePolicyTest\n'
  '[ RUN      ] StaticCookiePolicyTest.AllowAllCookiesTest\n'
  '[       OK ] StaticCookiePolicyTest.AllowAllCookiesTest (0 ms)\n'
  '[ RUN      ] StaticCookiePolicyTest.BlockAllCookiesTest\n'
  'C:\\win\\build\\src\\chrome\\test.cc: error: Value of: result()\n'
  '  Actual: false\n'
  'Expected: true\n'
  '[  FAILED  ] StaticCookiePolicyTest.BlockAllCookiesTest (0 ms)\n'
  '[----------] 2 tests from StaticCookiePolicyTest (0 ms total)\n'
  '\n'
  '[----------] 1 test from TCPListenSocketTest\n'
  '[ RUN      ] TCPListenSocketTest.ServerSend\n'
  '[       OK ] TCPListenSocketTest.ServerSend (1 ms)\n'
  '[----------] 1 test from TCPListenSocketTest (1 ms total)\n')


SWARM_OUTPUT_SUCCESS = (
  '[ RUN      ] unittests.Run Test\n' +
  TEST_CASE_SUCCESS +
  '[       OK ] unittests.Run Test (2549 ms)\n'
  '[ RUN      ] unittests.Clean Up\n'
  'No output!\n'
  '[       OK ] unittests.Clean Up (6 ms)\n'
  '\n'
  '[----------] unittests summary\n'
  '[==========] 2 tests ran. (2556 ms total)\n')


SWARM_OUTPUT_FAILURE = (
  '[ RUN      ] unittests.Run Test\n' +
  TEST_CASE_FAILURE +
  '[       OK ] unittests.Run Test (2549 ms)\n'
  '[ RUN      ] unittests.Clean Up\n'
  'No output!\n'
  '[       OK ] unittests.Clean Up (6 ms)\n'
  '\n'
  '[----------] unittests summary\n'
  '[==========] 2 tests ran. (2556 ms total)\n')


SWARM_OUTPUT_WITH_NO_TEST_OUTPUT = (
  '\n'
  'Unable to connection to swarm machine.\n')


TEST_SHARD_1 = 'Note: This is test shard 1 of 3.'
TEST_SHARD_2 = 'Note: This is test shard 2 of 3.'
TEST_SHARD_3 = 'Note: This is test shard 3 of 3.'


SWARM_SHARD_OUTPUT = (
  '[ RUN      ] unittests.Run Test\n'
  '%s\n'
  '[       OK ] unittests.Run Test (2549 ms)\n'
  '[ RUN      ] unittests.Clean Up\n'
  'No output!\n'
  '[       OK ] unittests.Clean Up (6 ms)\n'
  '\n'
  '[----------] unittests summary\n'
  '[==========] 2 tests ran. (2556 ms total)\n')


TEST_SHARD_OUTPUT_1 = SWARM_SHARD_OUTPUT % TEST_SHARD_1
TEST_SHARD_OUTPUT_2 = SWARM_SHARD_OUTPUT % TEST_SHARD_2
TEST_SHARD_OUTPUT_3 = SWARM_SHARD_OUTPUT % TEST_SHARD_3


def gen_data(index, shard_output, exit_codes):
  return {
    u'config_instance_index': index,
    u'exit_codes': unicode(exit_codes),
    u'machine_id': u'host',
    u'machine_tag': u'localhost',
    u'output': unicode(shard_output),
  }


def gen_yielded_data(index, shard_output, exit_codes):
  """Returns an entry as it would be yielded by yield_results()."""
  return index, gen_data(index, shard_output, exit_codes)


def generate_url_response(index, shard_output, exit_codes):
  return net.HttpResponse.get_fake_response(
      json.dumps(gen_data(index, shard_output, exit_codes)), 'mocked_url')


def get_swarm_results(keys, output_collector=None):
  """Simplifies the call to yield_results().

  The timeout is hard-coded to 10 seconds.
  """
  return list(
      swarming.yield_results(
          'http://host:9001', keys, 10., None, True, output_collector))


def main(args):
  """Bypassies swarming.main()'s exception handling.

  It gets in the way when debugging test failures.
  """
  dispatcher = swarming.subcommand.CommandDispatcher('swarming')
  return dispatcher.execute(swarming.OptionParserSwarming(), args)


# Silence pylint 'Access to a protected member _Event of a client class'.
class NonBlockingEvent(threading._Event):  # pylint: disable=W0212
  """Just like threading.Event, but a class and ignores timeout in 'wait'.

  Intended to be used as a mock for threading.Event in tests.
  """

  def wait(self, timeout=None):
    return super(NonBlockingEvent, self).wait(0)


class TestCase(auto_stub.TestCase):
  """Base class that defines the url_open mock."""
  def setUp(self):
    super(TestCase, self).setUp()
    self._lock = threading.Lock()
    self.requests = []
    self.mock(swarming.net.HttpService, 'request', self._url_open)
    self.mock(swarming.time, 'sleep', lambda x: None)
    self.mock(swarming.subprocess, 'call', lambda *_: self.fail())
    self.mock(swarming.threading, 'Event', NonBlockingEvent)
    self.mock(sys, 'stdout', StringIO.StringIO())
    self.mock(sys, 'stderr', StringIO.StringIO())

  def tearDown(self):
    try:
      if not self.has_failed():
        self._check_output('', '')
        self.assertEqual([], self.requests)
    finally:
      super(TestCase, self).tearDown()

  def _check_output(self, out, err):
    self.assertEqual(out, sys.stdout.getvalue())
    self.assertEqual(err, sys.stderr.getvalue())

    # Flush their content by mocking them again.
    self.mock(sys, 'stdout', StringIO.StringIO())
    self.mock(sys, 'stderr', StringIO.StringIO())

  def _url_open(self, url, **kwargs):
    logging.info('url_open(%s)', url)
    # Ignore 'stream' argument, it's not important for these tests.
    kwargs.pop('stream')
    with self._lock:
      # Since the client is multi-threaded, requests can be processed out of
      # order.
      for index, r in enumerate(self.requests):
        if r[0] == url and r[1] == kwargs:
          _, _, returned = self.requests.pop(index)
          break
      else:
        self.fail(
            'Failed to find url %s\n%s\nRemaining:\n%s' % (
              url,
              json.dumps(kwargs, indent=2, sort_keys=True),
              json.dumps(
                  [(i[0], i[1]) for i in self.requests],
                  indent=2, sort_keys=True)))
    return returned


class TestGetTestKeys(TestCase):
  def test_no_keys(self):
    self.mock(swarming.time, 'sleep', lambda x: x)
    self.requests = [
      (
        '/get_matching_test_cases?name=my_test',
        {'retry_404': True},
        StringIO.StringIO('No matching Test Cases'),
      ) for _ in range(net.URL_OPEN_MAX_ATTEMPTS)
    ]
    try:
      swarming.get_task_keys('http://host:9001', 'my_test')
      self.fail()
    except swarming.Failure as e:
      msg = (
          'Error: Unable to find any task with the name, my_test, on swarming '
          'server')
      self.assertEqual(msg, e.args[0])

  def test_no_keys_on_first_attempt(self):
    self.mock(swarming.time, 'sleep', lambda x: x)
    keys = ['key_1', 'key_2']
    self.requests = [
      (
        '/get_matching_test_cases?name=my_test',
        {'retry_404': True},
        StringIO.StringIO('No matching Test Cases'),
      ),
      (
        '/get_matching_test_cases?name=my_test',
        {'retry_404': True},
        StringIO.StringIO(json.dumps(keys)),
      ),
    ]
    actual = swarming.get_task_keys('http://host:9001', 'my_test')
    self.assertEqual(keys, actual)

  def test_find_keys(self):
    keys = ['key_1', 'key_2']
    self.requests = [
      (
        '/get_matching_test_cases?name=my_test',
        {'retry_404': True},
        StringIO.StringIO(json.dumps(keys)),
      ),
    ]
    actual = swarming.get_task_keys('http://host:9001', 'my_test')
    self.assertEqual(keys, actual)


class TestGetSwarmResults(TestCase):
  def test_success(self):
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, SWARM_OUTPUT_SUCCESS, '0, 0'),
      ),
    ]
    expected = [gen_yielded_data(0, SWARM_OUTPUT_SUCCESS, '0, 0')]
    actual = get_swarm_results(['key1'])
    self.assertEqual(expected, actual)

  def test_failure(self):
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, SWARM_OUTPUT_FAILURE, '0, 1'),
      ),
    ]
    expected = [gen_yielded_data(0, SWARM_OUTPUT_FAILURE, '0, 1')]
    actual = get_swarm_results(['key1'])
    self.assertEqual(expected, actual)

  def test_no_test_output(self):
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, SWARM_OUTPUT_WITH_NO_TEST_OUTPUT, '0, 0'),
      ),
    ]
    expected = [gen_yielded_data(0, SWARM_OUTPUT_WITH_NO_TEST_OUTPUT, '0, 0')]
    actual = get_swarm_results(['key1'])
    self.assertEqual(expected, actual)

  def test_no_keys(self):
    actual = get_swarm_results([])
    self.assertEqual([], actual)

  def test_url_errors(self):
    self.mock(logging, 'error', lambda *_, **__: None)
    # NOTE: get_swarm_results() hardcodes timeout=10.
    now = {}
    lock = threading.Lock()
    def get_now():
      t = threading.current_thread()
      with lock:
        return now.setdefault(t, range(10)).pop(0)
    self.mock(swarming.net, 'sleep_before_retry', lambda _x, _y: None)
    self.mock(swarming, 'now', get_now)
    # The actual number of requests here depends on 'now' progressing to 10
    # seconds. It's called once per loop. Loop makes 9 iterations.
    self.requests = 9 * [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        None,
      )
    ]
    actual = get_swarm_results(['key1'])
    self.assertEqual([], actual)
    self.assertTrue(all(not v for v in now.itervalues()), now)

  def test_shard_repeated(self):
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, SWARM_OUTPUT_SUCCESS, '0, 0'),
      ),
      (
        '/get_result?r=key1-repeat',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, SWARM_OUTPUT_SUCCESS, '0, 0'),
      ),
    ]
    expected = [gen_yielded_data(0, SWARM_OUTPUT_SUCCESS, '0, 0')]
    actual = get_swarm_results(['key1', 'key1-repeat'])
    self.assertEqual(expected, actual)

  def test_one_shard_repeated(self):
    """Have shard 1 repeated twice, then shard 2 and 3."""
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, TEST_SHARD_OUTPUT_1, '0, 0'),
      ),
      (
        '/get_result?r=key1-repeat',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, TEST_SHARD_OUTPUT_1, '0, 0'),
      ),
      (
        '/get_result?r=key2',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(1, TEST_SHARD_OUTPUT_2, '0, 0'),
      ),
      (
        '/get_result?r=key3',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(2, TEST_SHARD_OUTPUT_3, '0, 0'),
      ),
    ]
    expected = [
      gen_yielded_data(0, TEST_SHARD_OUTPUT_1, '0, 0'),
      gen_yielded_data(1, TEST_SHARD_OUTPUT_2, '0, 0'),
      gen_yielded_data(2, TEST_SHARD_OUTPUT_3, '0, 0'),
    ]
    actual = get_swarm_results(['key1', 'key1-repeat', 'key2', 'key3'])
    self.assertEqual(expected, sorted(actual))

  def test_output_collector_called(self):
    # Three shards, one failed. All results are passed to output collector.
    self.requests = [
      (
        '/get_result?r=key1',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(0, TEST_SHARD_OUTPUT_1, '0, 0'),
      ),
      (
        '/get_result?r=key2',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(1, TEST_SHARD_OUTPUT_2, '0, 0'),
      ),
      (
        '/get_result?r=key3',
        {'retry_404': False, 'retry_50x': False},
        generate_url_response(2, SWARM_OUTPUT_FAILURE, '0, 1'),
      ),
    ]

    class FakeOutputCollector(object):
      def __init__(self):
        self.results = []
        self._lock = threading.Lock()

      def process_shard_result(self, result):
        with self._lock:
          self.results.append(result)

    output_collector = FakeOutputCollector()
    get_swarm_results(['key1', 'key2', 'key3'], output_collector)

    expected = [
      gen_data(0, TEST_SHARD_OUTPUT_1, '0, 0'),
      gen_data(1, TEST_SHARD_OUTPUT_2, '0, 0'),
      gen_data(2, SWARM_OUTPUT_FAILURE, '0, 1')
    ]
    self.assertEqual(sorted(expected), sorted(output_collector.results))

  def test_collect_nothing(self):
    self.mock(swarming, 'get_task_keys', lambda *_: [1, 2])
    self.mock(swarming, 'yield_results', lambda *_: [])
    self.assertEqual(
        1,
        swarming.collect('url', 'test_name', 'timeout', 'decorate', True, None))
    self._check_output('', 'Results from some shards are missing: 0, 1\n')

  def test_collect_success(self):
    self.mock(swarming, 'get_task_keys', lambda *_: [1])
    data = {
      'config_instance_index': 0,
      'exit_codes': '0',
      'machine_id': 0,
      'output': 'Foo',
    }
    self.mock(swarming, 'yield_results', lambda *_: [(0, data)])
    self.assertEqual(
        0,
        swarming.collect('url', 'test_name', 'timeout', 'decorate', True, None))
    self._check_output(
        '\n================================================================\n'
        'Begin output from shard index 0 (machine tag: 0, id: unknown)\n'
        '================================================================\n\n'
        'Foo================================================================\n'
        'End output from shard index 0 (machine tag: 0, id: unknown). Return 0'
          '\n'
        '================================================================\n\n',
        '')

  def test_collect_fail(self):
    self.mock(swarming, 'get_task_keys', lambda *_: [1])
    data = {
      'config_instance_index': 0,
      'exit_codes': '0,8',
      'machine_id': 0,
      'output': 'Foo',
    }
    self.mock(swarming, 'yield_results', lambda *_: [(0, data)])
    self.assertEqual(
        8,
        swarming.collect('url', 'test_name', 'timeout', 'decorate', True, None))
    self._check_output(
        '\n================================================================\n'
        'Begin output from shard index 0 (machine tag: 0, id: unknown)\n'
        '================================================================\n\n'
        'Foo================================================================\n'
        'End output from shard index 0 (machine tag: 0, id: unknown). Return 8'
          '\n'
        '================================================================\n\n',
        '')

  def test_collect_one_missing(self):
    self.mock(swarming, 'get_task_keys', lambda *_: [1, 2])
    data = {
      'config_instance_index': 0,
      'exit_codes': '0',
      'machine_id': 0,
      'output': 'Foo',
    }
    self.mock(swarming, 'yield_results', lambda *_: [(0, data)])
    self.assertEqual(
        1,
        swarming.collect('url', 'test_name', 'timeout', 'decorate', True, None))
    self._check_output(
        '\n================================================================\n'
        'Begin output from shard index 0 (machine tag: 0, id: unknown)\n'
        '================================================================\n\n'
        'Foo================================================================\n'
        'End output from shard index 0 (machine tag: 0, id: unknown). Return 0'
          '\n'
        '================================================================\n\n',
        'Results from some shards are missing: 1\n')


def chromium_tasks(retrieval_url, file_hash, extra_args):
  return [
    {
      u'action': [
        u'python', u'run_isolated.zip',
        u'--hash', file_hash,
        u'--namespace', u'default-gzip',
        u'--isolate-server', retrieval_url,
      ] + (['--'] + list(extra_args) if extra_args else []),
      u'decorate_output': False,
      u'test_name': u'Run Test',
      u'hard_time_out': 2*60*60,
    },
    {
      u'action' : [
          u'python', u'swarm_cleanup.py',
      ],
      u'decorate_output': False,
      u'test_name': u'Clean Up',
      u'hard_time_out': 2*60*60,
    }
  ]


def generate_expected_json(
    shards,
    dimensions,
    env,
    working_dir,
    isolate_server,
    profile,
    test_case_name=TEST_NAME,
    file_hash=FILE_HASH,
    extra_args=None):
  expected = {
    u'cleanup': u'root',
    u'configurations': [
      {
        u'config_name': u'isolated',
        u'deadline_to_run': 60*60,
        u'dimensions': dimensions,
        u'min_instances': shards,
        u'priority': 101,
      },
    ],
    u'data': [],
    u'encoding': u'UTF-8',
    u'env_vars': env.copy(),
    u'restart_on_failure': True,
    u'test_case_name': test_case_name,
    u'tests': chromium_tasks(isolate_server, file_hash, extra_args),
    u'working_dir': unicode(working_dir),
  }
  if shards > 1:
    expected[u'env_vars'][u'GTEST_SHARD_INDEX'] = u'%(instance_index)s'
    expected[u'env_vars'][u'GTEST_TOTAL_SHARDS'] = u'%(num_instances)s'
  if profile:
    expected[u'tests'][0][u'action'].append(u'--verbose')
  return expected


class MockedStorage(object):
  def __init__(self, warm_cache):
    self._warm_cache = warm_cache

  def __enter__(self):
    pass

  def __exit__(self, *_args):
    pass

  def upload_items(self, items):
    return [] if self._warm_cache else items

  def get_fetch_url(self, _item):  # pylint: disable=R0201
    return 'http://localhost:8081/fetch_url'


class ManifestTest(TestCase):
  def test_basic_manifest(self):
    env = {
      u'GTEST_SHARD_INDEX': u'%(instance_index)s',
      u'GTEST_TOTAL_SHARDS': u'%(num_instances)s',
    }
    dimensions = {'os': 'Windows'}
    manifest = swarming.Manifest(
        isolate_server='http://localhost:8081',
        namespace='default-gzip',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=None,
        shards=2,
        env=env,
        dimensions=dimensions,
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=False,
        priority=101)

    swarming.chromium_setup(manifest)
    manifest_json = json.loads(manifest.to_json())

    expected = generate_expected_json(
        shards=2,
        dimensions={u'os': u'Windows'},
        env={},
        working_dir='swarm_tests',
        isolate_server=u'http://localhost:8081',
        profile=False)
    self.assertEqual(expected, manifest_json)

  def test_basic_linux(self):
    """A basic linux manifest test to ensure that windows specific values
       aren't used.
    """
    dimensions = {'os': 'Linux'}
    manifest = swarming.Manifest(
        isolate_server='http://localhost:8081',
        namespace='default-gzip',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=None,
        shards=1,
        env={},
        dimensions=dimensions,
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=False,
        priority=101)

    swarming.chromium_setup(manifest)
    manifest_json = json.loads(manifest.to_json())

    expected = generate_expected_json(
        shards=1,
        dimensions={u'os': u'Linux'},
        env={},
        working_dir='swarm_tests',
        isolate_server=u'http://localhost:8081',
        profile=False)
    self.assertEqual(expected, manifest_json)

  def test_basic_linux_profile(self):
    dimensions = {'os': 'Linux'}
    manifest = swarming.Manifest(
        isolate_server='http://localhost:8081',
        namespace='default-gzip',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=None,
        shards=1,
        env={},
        dimensions=dimensions,
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=True,
        priority=101)

    swarming.chromium_setup(manifest)
    manifest_json = json.loads(manifest.to_json())

    expected = generate_expected_json(
        shards=1,
        dimensions={u'os': u'Linux'},
        env={},
        working_dir='swarm_tests',
        isolate_server=u'http://localhost:8081',
        profile=True)
    self.assertEqual(expected, manifest_json)

  def test_manifest_with_extra_args(self):
    env = {
      u'GTEST_SHARD_INDEX': u'%(instance_index)s',
      u'GTEST_TOTAL_SHARDS': u'%(num_instances)s',
    }
    dimensions = {'os': 'Windows'}
    manifest = swarming.Manifest(
        isolate_server='http://localhost:8081',
        namespace='default-gzip',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=['--extra-cmd-arg=1234', 'some more'],
        shards=2,
        env=env,
        dimensions=dimensions,
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=False,
        priority=101)

    swarming.chromium_setup(manifest)
    manifest_json = json.loads(manifest.to_json())

    expected = generate_expected_json(
        shards=2,
        dimensions={u'os': u'Windows'},
        env={},
        working_dir='swarm_tests',
        isolate_server=u'http://localhost:8081',
        profile=False,
        extra_args=['--extra-cmd-arg=1234', 'some more'])
    self.assertEqual(expected, manifest_json)

  def test_process_manifest_success(self):
    self.mock(swarming.net, 'url_read', lambda url, data=None: '{}')
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=False))

    result = swarming.process_manifest(
        swarming='http://localhost:8082',
        isolate_server='http://localhost:8081',
        namespace='default',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=['--some-arg', '123'],
        shards=1,
        dimensions={},
        env={},
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=False,
        priority=101)
    self.assertEqual(0, result)

  def test_process_manifest_success_zip_already_uploaded(self):
    self.mock(swarming.net, 'url_read', lambda url, data=None: '{}')
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=True))

    dimensions = {'os': 'linux2'}
    result = swarming.process_manifest(
        swarming='http://localhost:8082',
        isolate_server='http://localhost:8081',
        namespace='default',
        isolated_hash=FILE_HASH,
        task_name=TEST_NAME,
        extra_args=['--some-arg', '123'],
        shards=1,
        dimensions=dimensions,
        env={},
        working_dir='swarm_tests',
        deadline=60*60,
        verbose=False,
        profile=False,
        priority=101)
    self.assertEqual(0, result)

  def test_isolated_to_hash(self):
    calls = []
    self.mock(swarming.subprocess, 'call', lambda *c: calls.append(c))
    content = '{}'
    expected_hash = hashlib.sha1(content).hexdigest()
    handle, isolated = tempfile.mkstemp(
        prefix='swarming_test_', suffix='.isolated')
    os.close(handle)
    try:
      with open(isolated, 'w') as f:
        f.write(content)
      hash_value, is_file = swarming.isolated_to_hash(
          'http://localhost:1', 'default', isolated, hashlib.sha1, False)
    finally:
      os.remove(isolated)
    self.assertEqual(expected_hash, hash_value)
    self.assertEqual(True, is_file)
    expected_calls = [
        (
          [
            sys.executable,
            os.path.join(ROOT_DIR, 'isolate.py'),
            'archive',
            '--isolate-server', 'http://localhost:1',
            '--namespace', 'default',
            '--isolated',
            isolated,
          ],
          False,
        ),
    ]
    self.assertEqual(expected_calls, calls)
    self._check_output('Archiving: %s\n' % isolated, '')


def mock_swarming_api_v1_bots():
  """Returns fake /swarming/api/v1/bots data."""
  now = datetime.datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S')
  dead = '2006-01-02 03:04:05'
  return {
    'machine_death_timeout': 10,
    'machines': [
      {
        'tag': 'no-dimensions',
        'dimensions': {},
        'last_seen': now,
      },
      {
        'tag': 'amig1',
        'dimensions': {'os': 'amiga'},
        'last_seen': now,
      },
      {
        'tag': 'amig2',
        'dimensions': {'os': ['amiga', 'atari'], 'foo': 1},
        'last_seen': now,
      },
      {
        'tag': 'dead',
        'dimensions': {'os': 'amiga'},
        'last_seen': dead,
      },
    ],
  }


class MainTest(TestCase):
  def setUp(self):
    super(MainTest, self).setUp()
    self._tmpdir = None

  def tearDown(self):
    try:
      if self._tmpdir:
        shutil.rmtree(self._tmpdir)
    finally:
      super(MainTest, self).tearDown()

  @property
  def tmpdir(self):
    if not self._tmpdir:
      self._tmpdir = tempfile.mkdtemp(prefix='swarming')
    return self._tmpdir

  def test_run_hash(self):
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=False))
    self.mock(swarming, 'now', lambda: 123456)

    task_name = (
        '%s/foo=bar_os=Mac/1111111111111111111111111111111111111111/123456000' %
        getpass.getuser())
    j = generate_expected_json(
        shards=1,
        dimensions={'foo': 'bar', 'os': 'Mac'},
        env={},
        working_dir='swarm_tests',
        isolate_server='https://host2',
        profile=False,
        test_case_name=task_name)
    j['data'] = [['http://localhost:8081/fetch_url', 'swarm_data.zip']]
    data = {
      'request': json.dumps(j, sort_keys=True, separators=(',',':')),
    }
    self.requests = [
      (
        '/test',
        {'data': data},
        # The actual output is ignored as long as it is valid json.
        StringIO.StringIO('{}'),
      ),
    ]
    ret = main([
        'trigger',
        '--swarming', 'https://host1',
        '--isolate-server', 'https://host2',
        '--shards', '1',
        '--priority', '101',
        '--dimension', 'foo', 'bar',
        '--dimension', 'os', 'Mac',
        '--deadline', '3600',
        FILE_HASH,
      ])
    actual = sys.stdout.getvalue()
    self.assertEqual(0, ret, (actual, sys.stderr.getvalue()))
    self._check_output('Triggered task: %s\n' % task_name, '')

  def test_run_isolated(self):
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=False))
    calls = []
    self.mock(swarming.subprocess, 'call', lambda *c: calls.append(c))
    self.mock(swarming, 'now', lambda: 123456)

    isolated = os.path.join(self.tmpdir, 'zaz.isolated')
    content = '{}'
    with open(isolated, 'wb') as f:
      f.write(content)

    isolated_hash = ALGO(content).hexdigest()
    task_name = 'zaz/foo=bar_os=Mac/%s/123456000' % isolated_hash
    j = generate_expected_json(
        shards=1,
        dimensions={'foo': 'bar', 'os': 'Mac'},
        env={},
        working_dir='swarm_tests',
        isolate_server='https://host2',
        profile=False,
        test_case_name=task_name,
        file_hash=isolated_hash)
    j['data'] = [['http://localhost:8081/fetch_url', 'swarm_data.zip']]
    data = {
      'request': json.dumps(j, sort_keys=True, separators=(',',':')),
    }
    self.requests = [
      (
        '/test',
        {'data': data},
        # The actual output is ignored as long as it is valid json.
        StringIO.StringIO('{}'),
      ),
    ]
    ret = main([
        'trigger',
        '--swarming', 'https://host1',
        '--isolate-server', 'https://host2',
        '--shards', '1',
        '--priority', '101',
        '--dimension', 'foo', 'bar',
        '--dimension', 'os', 'Mac',
        '--deadline', '3600',
        isolated,
      ])
    actual = sys.stdout.getvalue()
    self.assertEqual(0, ret, (actual, sys.stderr.getvalue()))
    expected = [
      (
        [
          sys.executable,
          os.path.join(ROOT_DIR, 'isolate.py'), 'archive',
          '--isolate-server', 'https://host2',
          '--namespace' ,'default-gzip',
          '--isolated', isolated,
        ],
      0),
    ]
    self.assertEqual(expected, calls)
    expected = 'Archiving: %s\nTriggered task: %s\n' % (isolated, task_name)
    self._check_output(expected, '')

  def test_query(self):
    self.requests = [
      (
        '/swarming/api/v1/bots',
        {
          "content_type": None,
          "data": None,
          "headers": None,
          "max_attempts": 30,
          "method": "GET",
          "retry_404": False,
          "retry_50x": True,
          "timeout": 360.0
        },
        StringIO.StringIO(json.dumps(mock_swarming_api_v1_bots())),
      ),
    ]
    main(['query', '--swarming', 'https://localhost:1'])
    expected = (
        "amig1\n  {u'os': u'amiga'}\n"
        "amig2\n  {u'foo': 1, u'os': [u'amiga', u'atari']}\n"
        "no-dimensions\n  {}\n")
    self._check_output(expected, '')

  def test_query_bare(self):
    self.requests = [
      (
        '/swarming/api/v1/bots',
        {
          "content_type": None,
          "data": None,
          "headers": None,
          "max_attempts": 30,
          "method": "GET",
          "retry_404": False,
          "retry_50x": True,
          "timeout": 360.0
        },
        StringIO.StringIO(json.dumps(mock_swarming_api_v1_bots())),
      ),
    ]
    main(['query', '--swarming', 'https://localhost:1', '--bare'])
    self._check_output("amig1\namig2\nno-dimensions\n", '')

  def test_query_filter(self):
    self.requests = [
      (
        '/swarming/api/v1/bots',
        {
          "content_type": None,
          "data": None,
          "headers": None,
          "max_attempts": 30,
          "method": "GET",
          "retry_404": False,
          "retry_50x": True,
          "timeout": 360.0
        },
        StringIO.StringIO(json.dumps(mock_swarming_api_v1_bots())),
      ),
    ]
    main(
        [
          'query', '--swarming', 'https://localhost:1',
          '--dimension', 'os', 'amiga',
        ])
    expected = (
        "amig1\n  {u'os': u'amiga'}\n"
        "amig2\n  {u'foo': 1, u'os': [u'amiga', u'atari']}\n")
    self._check_output(expected, '')

  def test_query_filter_keep_dead(self):
    self.requests = [
      (
        '/swarming/api/v1/bots',
        {
          "content_type": None,
          "data": None,
          "headers": None,
          "max_attempts": 30,
          "method": "GET",
          "retry_404": False,
          "retry_50x": True,
          "timeout": 360.0
        },
        StringIO.StringIO(json.dumps(mock_swarming_api_v1_bots())),
      ),
    ]
    main(
        [
          'query', '--swarming', 'https://localhost:1',
          '--dimension', 'os', 'amiga', '--keep-dead',
        ])
    expected = (
        "amig1\n  {u'os': u'amiga'}\n"
        "amig2\n  {u'foo': 1, u'os': [u'amiga', u'atari']}\n"
        "dead\n  {u'os': u'amiga'}\n")
    self._check_output(expected, '')

  def test_query_filter_dead_only(self):
    self.requests = [
      (
        '/swarming/api/v1/bots',
        {
          "content_type": None,
          "data": None,
          "headers": None,
          "max_attempts": 30,
          "method": "GET",
          "retry_404": False,
          "retry_50x": True,
          "timeout": 360.0
        },
        StringIO.StringIO(json.dumps(mock_swarming_api_v1_bots())),
      ),
    ]
    main(
        [
          'query', '--swarming', 'https://localhost:1',
          '--dimension', 'os', 'amiga', '--dead-only',
        ])
    expected = (
        "dead\n  {u'os': u'amiga'}\n")
    self._check_output(expected, '')

  def test_trigger_no_request(self):
    with self.assertRaises(SystemExit):
      main([
            'trigger', '--swarming', 'https://host',
            '--isolate-server', 'https://host', '-T', 'foo',
          ])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: Must pass one .isolated file or its hash (sha1).'
        '\n')

  def test_trigger_no_env_vars(self):
    with self.assertRaises(SystemExit):
      main(['trigger'])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: --swarming is required.'
        '\n')

  def test_trigger_no_swarming_env_var(self):
    with self.assertRaises(SystemExit):
      with test_utils.EnvVars({'ISOLATE_SERVER': 'https://host'}):
        main(['trigger', '-T' 'foo', 'foo.isolated'])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: --swarming is required.'
        '\n')

  def test_trigger_no_isolate_env_var(self):
    with self.assertRaises(SystemExit):
      with test_utils.EnvVars({'SWARMING_SERVER': 'https://host'}):
        main(['trigger', 'T', 'foo', 'foo.isolated'])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: Use one of --indir or --isolate-server.'
        '\n')

  def test_trigger_env_var(self):
    with self.assertRaises(SystemExit):
      with test_utils.EnvVars({'ISOLATE_SERVER': 'https://host',
                               'SWARMING_SERVER': 'https://host'}):
        main(['trigger', '-T', 'foo'])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: Must pass one .isolated file or its hash (sha1).'
        '\n')

  def test_trigger_no_task(self):
    with self.assertRaises(SystemExit):
      main([
            'trigger', '--swarming', 'https://host',
            '--isolate-server', 'https://host', 'foo.isolated',
          ])
    self._check_output(
        '',
        'Usage: swarming.py trigger [options] (hash|isolated) [-- extra_args]'
        '\n\n'
        'swarming.py: error: Please at least specify one --dimension\n')

  def test_trigger_env(self):
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=False))
    j = generate_expected_json(
        shards=1,
        dimensions={'os': 'Mac'},
        env={'foo': 'bar'},
        working_dir='swarm_tests',
        isolate_server='https://host2',
        profile=False)
    j['data'] = [['http://localhost:8081/fetch_url', 'swarm_data.zip']]
    data = {
      'request': json.dumps(j, sort_keys=True, separators=(',',':')),
    }
    self.requests = [
      (
        '/test',
        {'data': data},
        # The actual output is ignored as long as it is valid json.
        StringIO.StringIO('{}'),
      ),
    ]
    ret = main([
        'trigger',
        '--swarming', 'https://host1',
        '--isolate-server', 'https://host2',
        '--shards', '1',
        '--priority', '101',
        '--env', 'foo', 'bar',
        '--dimension', 'os', 'Mac',
        '--task-name', TEST_NAME,
        '--deadline', '3600',
        FILE_HASH,
      ])
    actual = sys.stdout.getvalue()
    self.assertEqual(0, ret, (actual, sys.stderr.getvalue()))

  def test_trigger_dimension_filter(self):
    self.mock(swarming.isolateserver, 'get_storage',
        lambda *_: MockedStorage(warm_cache=False))
    j = generate_expected_json(
        shards=1,
        dimensions={'foo': 'bar', 'os': 'Mac'},
        env={},
        working_dir='swarm_tests',
        isolate_server='https://host2',
        profile=False)
    j['data'] = [['http://localhost:8081/fetch_url', 'swarm_data.zip']]
    data = {
      'request': json.dumps(j, sort_keys=True, separators=(',',':')),
    }
    self.requests = [
      (
        '/test',
        {'data': data},
        # The actual output is ignored as long as it is valid json.
        StringIO.StringIO('{}'),
      ),
    ]
    ret = main([
        'trigger',
        '--swarming', 'https://host1',
        '--isolate-server', 'https://host2',
        '--shards', '1',
        '--priority', '101',
        '--dimension', 'foo', 'bar',
        '--dimension', 'os', 'Mac',
        '--task-name', TEST_NAME,
        '--deadline', '3600',
        FILE_HASH,
      ])
    actual = sys.stdout.getvalue()
    self.assertEqual(0, ret, (actual, sys.stderr.getvalue()))


def gen_run_isolated_out_hack_log(isolate_server, namespace, isolated_hash):
  data = {
    'hash': isolated_hash,
    'namespace': namespace,
    'storage': isolate_server,
  }
  return (SWARM_OUTPUT_SUCCESS +
      '[run_isolated_out_hack]%s[/run_isolated_out_hack]\n' % (
          json.dumps(data, sort_keys=True, separators=(',',':'))))


class ExtractOutputFilesLocationTest(auto_stub.TestCase):
  def test_ok(self):
    task_log = '\n'.join((
      'some log',
      'some more log',
      gen_run_isolated_out_hack_log('https://fake', 'default', '12345'),
      'more log',
    ))
    self.assertEqual(
        ('https://fake', 'default', '12345'),
        swarming.extract_output_files_location(task_log))

  def test_empty(self):
    task_log = '\n'.join((
      'some log',
      'some more log',
      '[run_isolated_out_hack]',
      '[/run_isolated_out_hack]',
    ))
    self.assertEqual(
        None,
        swarming.extract_output_files_location(task_log))

  def test_missing(self):
    task_log = '\n'.join((
      'some log',
      'some more log',
      'more log',
    ))
    self.assertEqual(
        None,
        swarming.extract_output_files_location(task_log))

  def test_corrupt(self):
    task_log = '\n'.join((
      'some log',
      'some more log',
      '[run_isolated_out_hack]',
      '{"hash": "12345","namespace":}',
      '[/run_isolated_out_hack]',
      'more log',
    ))
    self.assertEqual(
        None,
        swarming.extract_output_files_location(task_log))

  def test_not_url(self):
    task_log = '\n'.join((
      'some log',
      'some more log',
      gen_run_isolated_out_hack_log('/local/path', 'default', '12345'),
      'more log',
    ))
    self.assertEqual(
        None,
        swarming.extract_output_files_location(task_log))


class TaskOutputCollectorTest(auto_stub.TestCase):
  def setUp(self):
    super(TaskOutputCollectorTest, self).setUp()

    # Silence error log.
    self.mock(logging, 'error', lambda *_, **__: None)

    # Collect calls to 'isolateserver.fetch_isolated'.
    self.fetch_isolated_calls = []
    def fetch_isolated(isolated_hash, storage, cache, outdir, require_command):
      self.fetch_isolated_calls.append(
          (isolated_hash, storage, cache, outdir, require_command))
    # Ensure mock has exact same signature as the original, otherwise tests may
    # miss changes to real 'fetch_isolated' arg list.
    self.assertEqual(
        inspect.getargspec(swarming.isolateserver.fetch_isolated),
        inspect.getargspec(fetch_isolated))
    self.mock(swarming.isolateserver, 'fetch_isolated', fetch_isolated)

    # TaskOutputCollector creates directories. Put them in a temp directory.
    self.tempdir = tempfile.mkdtemp(prefix='swarming_test')

  def tearDown(self):
    shutil.rmtree(self.tempdir)
    super(TaskOutputCollectorTest, self).tearDown()

  def test_works(self):
    # Output logs of shards.
    logs = [
      gen_run_isolated_out_hack_log('https://server', 'namespace', 'hash1'),
      gen_run_isolated_out_hack_log('https://server', 'namespace', 'hash2'),
      SWARM_OUTPUT_SUCCESS,
    ]

    # Feed three shard results to collector, last one without output files.
    collector = swarming.TaskOutputCollector(
        self.tempdir, 'task/name', len(logs))
    for index, log in enumerate(logs):
      collector.process_shard_result(gen_data(index, log, '0, 0'))
    collector.finalize()

    # Ensure it fetches the files from first two shards only.
    expected_calls = [
      ('hash1', None, None, os.path.join(self.tempdir, '0'), False),
      ('hash2', None, None, os.path.join(self.tempdir, '1'), False),
    ]
    self.assertEqual(len(expected_calls), len(self.fetch_isolated_calls))
    storage_instances = set()
    for expected, used in zip(expected_calls, self.fetch_isolated_calls):
      isolated_hash, storage, cache, outdir, require_command = used
      storage_instances.add(storage)
      # Compare everything but |storage| and |cache| (use None in their place).
      self.assertEqual(
          expected, (isolated_hash, None, None, outdir, require_command))
      # Ensure cache is set.
      self.assertTrue(cache)

    # Only one instance of Storage should be used.
    self.assertEqual(1, len(storage_instances))

    # Ensure storage is pointing to required location.
    storage = storage_instances.pop()
    self.assertEqual('https://server', storage.location)
    self.assertEqual('namespace', storage.namespace)

    # Ensure summary dump is correct.
    with open(os.path.join(self.tempdir, 'summary.json'), 'r') as f:
      summary = json.load(f)
    expected_summary = {
      'task_name': 'task/name',
      'shards': [
        gen_data(index, log, '0, 0') for index, log in enumerate(logs)
      ]
    }
    self.assertEqual(expected_summary, summary)

  def test_ignores_broken_index(self):
    # Generate fake response with index replaced by a string.
    data = gen_data(
        0,
        gen_run_isolated_out_hack_log('https://server', 'namespace', 'hash1'),
        '0, 0')
    data['config_instance_index'] = '../../secret/file'

    # Feed it, collector should raise ValueError.
    collector = swarming.TaskOutputCollector(self.tempdir, 'task/name', 1)
    with self.assertRaises(ValueError):
      collector.process_shard_result(data)
    collector.finalize()

    # And no fetches should be made.
    self.assertFalse(self.fetch_isolated_calls)

  def test_ignores_index_out_of_range(self):
    # Index is >= shard count.
    data = gen_data(
        1000,
        gen_run_isolated_out_hack_log('https://server', 'namespace', 'hash1'),
        '0, 0')

    # Feed it, collector should just ignore it.
    collector = swarming.TaskOutputCollector(self.tempdir, 'task/name', 1)
    collector.process_shard_result(data)
    collector.finalize()

    # And no fetches should be made.
    self.assertFalse(self.fetch_isolated_calls)

  def test_ignores_duplicate(self):
    data = gen_data(
        0,
        gen_run_isolated_out_hack_log('https://server', 'namespace', 'hash1'),
        '0, 0')

    # Feed same result twice.
    collector = swarming.TaskOutputCollector(self.tempdir, 'task/name', 1)
    collector.process_shard_result(data)
    collector.process_shard_result(data)
    collector.finalize()

    # Only one fetch should be made.
    self.assertEqual(1, len(self.fetch_isolated_calls))

  def test_ensures_same_server(self):
    # Two shard results, attempt to use different servers.
    data = [
      gen_data(
        0,
        gen_run_isolated_out_hack_log('https://server1', 'namespace', 'hash1'),
        '0, 0'),
      gen_data(
        1,
        gen_run_isolated_out_hack_log('https://server2', 'namespace', 'hash2'),
        '0, 0'),
    ]

    # Feed them to collector.
    collector = swarming.TaskOutputCollector(self.tempdir, 'task/name', 2)
    for result in data:
      collector.process_shard_result(result)
    collector.finalize()

    # Only first fetch is made, second one is ignored.
    self.assertEqual(1, len(self.fetch_isolated_calls))
    isolated_hash, storage, _, outdir, _ = self.fetch_isolated_calls[0]
    self.assertEqual(
        ('hash1', os.path.join(self.tempdir, '0')),
        (isolated_hash, outdir))
    self.assertEqual('https://server1', storage.location)


def clear_env_vars():
  for e in ('ISOLATE_SERVER', 'SWARMING_SERVER'):
    os.environ.pop(e, None)


if __name__ == '__main__':
  logging.basicConfig(
      level=logging.DEBUG if '-v' in sys.argv else logging.ERROR)
  if '-v' in sys.argv:
    unittest.TestCase.maxDiff = None
  clear_env_vars()
  unittest.main()
