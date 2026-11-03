"""桥接注册只对瞬时连接中断有限重试，丢失回执后可重复发布同一会话。"""
import json
import queue
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import threading
import types
import unittest
from unittest.mock import patch

CLIENT = Path(__file__).resolve().parents[1] / 'paraview_bridge_client.py'


class StartupTransportTests(unittest.TestCase):
    def bridge(self, directory):
        with patch.dict(sys.modules, {'paraview': types.SimpleNamespace(simple=object())}):
            definitions = runpy.run_path(str(CLIENT), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})
        bridge_type = definitions['ParaViewBridge']
        bridge = bridge_type.__new__(bridge_type)
        bridge.root = str(directory)
        bridge.directory = str(directory / 'session-test')
        bridge.launch_id = 'a' * 32
        bridge.source_sha256 = 'test-hash'
        bridge.closed = threading.Event()
        bridge.ssh = ['ssh']
        return bridge

    def test_registration_succeeds_after_lost_reply(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'bridge'
            bridge = self.bridge(root)
            attempts = []

            def remote(command, data=None):
                attempts.append(command)
                # 首次实际写入完成后模拟 SSH 回执丢失；再次 mkdir 不得失败。
                result = subprocess.run(['bash', '-c', command], input=data,
                                        capture_output=True, check=True)
                if len(attempts) == 1:
                    raise ConnectionError('Connection closed by host port 6022')
                bridge.closed.set()
                return result.stdout

            bridge.remote = remote
            with patch.object(bridge.closed, 'wait', return_value=False):
                bridge.poll()
            self.assertEqual(len(attempts), 2)
            self.assertEqual(json.loads((root / 'current.json').read_text())['launch_id'], bridge.launch_id)

    def test_retry_is_bounded(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            with patch.object(bridge, 'remote', side_effect=ConnectionError('Connection reset by peer')) as call:
                with patch.object(bridge.closed, 'wait', return_value=False) as wait:
                    bridge.poll()
            self.assertEqual(call.call_count, 3)
            self.assertEqual([c.args[0] for c in wait.call_args_list], [2, 4])
            self.assertTrue(bridge.closed.is_set())

    def test_authentication_error_is_not_retried(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            with patch.object(bridge, 'remote', side_effect=RuntimeError('Permission denied (publickey)')) as call:
                bridge.poll()
            self.assertEqual(call.call_count, 1)
            self.assertTrue(bridge.closed.is_set())

    def test_only_connection_failures_are_classified_for_retry(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            for message, error in [('Exceeded MaxStartups', ConnectionError),
                                   ('Permission denied (publickey)', RuntimeError),
                                   ('REMOTE HOST IDENTIFICATION HAS CHANGED', RuntimeError)]:
                # 本地模拟SSH握手失败，不连接网络。
                bridge.ssh = ['bash', '-c', 'printf "%s" "$1" >&2; exit 255', '--', message]
                with self.assertRaises(error):
                    bridge.remote('true')
                self.assertIsNone(bridge._transport)

    def test_connection_is_reused_and_reopened_after_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            bridge.ssh = ['bash', '-c']
            real_popen = subprocess.Popen
            with patch('subprocess.Popen', wraps=real_popen) as start:
                try:
                    payload = b'zero\x00\xff\nlast'
                    self.assertEqual(bridge.remote('cat', payload), payload)
                    self.assertEqual(bridge.remote('printf second'), b'second')
                    self.assertEqual(start.call_count, 1)
                    process = bridge._transport[0]
                    process.kill()
                    process.wait()
                    with self.assertRaises(ConnectionError):
                        bridge.remote('true')
                    self.assertEqual(bridge.remote('printf recovered'), b'recovered')
                    self.assertEqual(start.call_count, 2)
                finally:
                    bridge.close_transport()
            self.assertIsNone(bridge._transport)
            self.assertIsNotNone(process.poll())

    def test_remote_command_error_is_not_reported_as_authentication(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            bridge.ssh = ['bash', '-c']
            with self.assertRaisesRegex(RuntimeError, 'command failed'):
                bridge.remote('echo command failed >&2; exit 3')
            self.assertIsNone(bridge._transport)

    def test_poll_backoff_grows_and_resets_after_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = self.bridge(Path(tmp))
            bridge.outgoing = queue.Queue()
            bridge.incoming = queue.Queue()
            calls = []
            def remote(command, data=None):
                calls.append(command)
                if len(calls) in (2, 3, 5):
                    raise ConnectionError('Exceeded MaxStartups')
                if len(calls) == 6:
                    bridge.closed.set()
                return b''
            bridge.remote = remote
            with patch.object(bridge.closed, 'wait', return_value=False) as wait:
                bridge.poll()
            self.assertEqual([c.args[0] for c in wait.call_args_list], [4, 8, 2, 4, 2])
