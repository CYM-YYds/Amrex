"""固定来源更新的完整性、回滚与运行时所有者回归，不接触用户窗口。"""
import builtins
import hashlib
import importlib.util
import os
from pathlib import Path
import runpy
import sys
import tempfile
import threading
import types
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
CLIENT = SCRIPTS / 'paraview_bridge_client.py'


class ClientUpdateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.local = Path(self.tmp.name) / 'client.py'
        self.original = CLIENT.read_bytes()
        self.local.write_bytes(self.original)
        self.pvs = types.SimpleNamespace(GetSources=lambda: {('existing_source', '7'): None},
                                         GetViews=lambda: [])
        connection = types.SimpleNamespace(Session=types.SimpleNamespace(GetURI=lambda: 'cs://localhost:11115'))
        self.modules = patch.dict(sys.modules, {'paraview': types.SimpleNamespace(
            simple=self.pvs, servermanager=types.SimpleNamespace(ActiveConnection=connection))})
        self.modules.start()
        self.environment = patch.dict(os.environ, {'PV_BRIDGE_SCRIPT': str(self.local)})
        self.environment.start()
        definitions = runpy.run_path(str(CLIENT), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})
        self.bridge = definitions['ParaViewBridge'].__new__(definitions['ParaViewBridge'])
        self.bridge.source_sha256 = hashlib.sha256(self.original).hexdigest()
        self.bridge.directory = self.tmp.name
        self.bridge.worker, self.bridge.timer = object(), object()
        self.bridge.closed = threading.Event()
        self.bridge.backgrounds = {'existing_view': ([0, 0, 0], 0)}
        self.target = self.original.replace(b"BRIDGE_VERSION = '4'", b"BRIDGE_VERSION = 'test-next'")
        self.bridge.remote = lambda command: self.target

    def tearDown(self):
        self.environment.stop()
        self.modules.stop()
        self.tmp.cleanup()

    def command(self):
        command = {'action': 'reload_client', 'expected_sha256': hashlib.sha256(self.target).hexdigest()}
        command['_client_source'] = self.bridge.prepare_reload(command)
        return command

    def test_upgrade_preserves_owner_thread_timer_state_and_pipeline(self):
        owner, worker, timer, backgrounds = self.bridge, self.bridge.worker, self.bridge.timer, self.bridge.backgrounds
        result = self.bridge.execute(self.command())
        self.assertTrue(result['updated'])
        self.assertEqual(result['version'], 'test-next')
        self.assertEqual(result['preserved_sources'], [['existing_source', '7']])
        self.assertIs(owner, self.bridge)
        self.assertIs(worker, self.bridge.worker)
        self.assertIs(timer, self.bridge.timer)
        self.assertIs(backgrounds, self.bridge.backgrounds)
        self.assertFalse(self.bridge.closed.is_set())
        self.assertEqual(self.local.read_bytes(), self.target)
        self.assertEqual(self.bridge.execute({'action': 'inspect'})['version'], 'test-next')
        self.assertFalse(self.bridge.execute(self.command())['updated'])

    def test_incomplete_download_is_rejected_before_execution(self):
        original_class = self.bridge.__class__
        with self.assertRaisesRegex(ValueError, 'changed or download'):
            self.bridge.prepare_reload({'expected_sha256': '0' * 64})
        self.assertIs(self.bridge.__class__, original_class)
        self.assertEqual(self.local.read_bytes(), self.original)

    def test_file_failure_rolls_back_class_and_retains_local_source(self):
        original_class = self.bridge.__class__
        command = self.command()
        with patch('os.replace', side_effect=OSError('read-only destination')):
            with self.assertRaisesRegex(OSError, 'read-only'):
                self.bridge.execute(command)
        self.assertIs(self.bridge.__class__, original_class)
        self.assertEqual(self.local.read_bytes(), self.original)
        self.assertFalse(self.local.with_suffix('.py.update').exists())

    def test_incompatible_transport_requires_restart(self):
        self.target = self.target.replace(b'RUNTIME_UPDATE_PROTOCOL = 1', b'RUNTIME_UPDATE_PROTOCOL = 2')
        with self.assertRaisesRegex(ValueError, 'Runtime protocol changed'):
            self.command()
        self.assertEqual(self.local.read_bytes(), self.original)

    def test_sender_distinguishes_legacy_connected_and_disconnected(self):
        spec = importlib.util.spec_from_file_location('update_sender', SCRIPTS / 'paraview_bridge_send.py')
        sender = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sender)
        expected = sender.client_manifest()
        legacy = {'version': '3', 'capabilities': ['inspect'], 'connection_uri': 'cs://localhost:11115'}
        status = sender.client_status({'ok': True, 'value': legacy}, expected)
        self.assertTrue(status['ok'])
        self.assertEqual(status['status'], 'client_update_required')
        self.assertFalse(status['can_hot_update'])
        current = dict(expected, connection_uri='cs://localhost:11115')
        self.assertEqual(sender.client_status({'ok': True, 'value': current}, expected)['status'], 'ready')
        current['connection_uri'] = None
        self.assertEqual(sender.client_status({'ok': True, 'value': current}, expected)['status'], 'disconnected')


if __name__ == '__main__':
    unittest.main()
