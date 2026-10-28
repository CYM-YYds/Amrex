"""用独立进程验证单槽投递互斥、退出释放与超时后的未决命令保护。"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]


class SenderLockTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.session = self.root / ('session-' + 'a' * 32)
        self.session.mkdir()
        (self.root / 'current.json').write_text(json.dumps({'directory': str(self.session)}))
        (self.session / 'heartbeat').write_text(str(time.time()))
        self.environment = dict(os.environ, PV_BRIDGE_ROOT=str(self.root))
        with patch.dict(os.environ, self.environment):
            spec = importlib.util.spec_from_file_location('locked_sender', SCRIPTS / 'paraview_bridge_send.py')
            self.sender = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.sender)

    def test_concurrent_sender_is_rejected_and_exit_releases_lock(self):
        child = subprocess.Popen([sys.executable, str(SCRIPTS / 'paraview_bridge_send.py'),
                                  'current', 'inspect', '--timeout', '60'],
                                 env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: child.poll() is None and child.kill())
        try:
            command_file = self.session / 'command.json'
            deadline = time.monotonic() + 5
            while not command_file.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(command_file.exists())
            original = command_file.read_bytes()
            with self.assertRaisesRegex(RuntimeError, '已有投递器'):
                self.sender.send('current', 'background', color=[0, 0, 0], timeout=.1)
            self.assertEqual(command_file.read_bytes(), original)
        finally:
            child.terminate()
            child.communicate(timeout=5)
        # 进程退出自动释放锁，但未决命令仍阻止覆盖。
        with self.assertRaisesRegex(RuntimeError, '尚未返回'):
            self.sender.send('current', 'inspect', timeout=.1)
        self.assertEqual(command_file.read_bytes(), original)

    def test_timeout_retains_command_and_matching_receipt_allows_next_send(self):
        with self.assertRaises(TimeoutError):
            self.sender.send('current', 'inspect', timeout=.01)
        command_file = self.session / 'command.json'
        original = command_file.read_bytes()
        with self.assertRaisesRegex(RuntimeError, '尚未返回'):
            self.sender.send('current', 'inspect', timeout=.01)
        self.assertEqual(command_file.read_bytes(), original)
        old = json.loads(original)
        result_file = self.session / 'result.json'
        result_file.write_text(json.dumps({'id': old['id'], 'ok': True}))
        errors = []

        def reply():
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    command = json.loads(command_file.read_text())
                    if command['id'] != old['id']:
                        temporary = self.session / 'result.tmp'
                        temporary.write_text(json.dumps({'id': command['id'], 'ok': True}))
                        temporary.replace(result_file)
                        return
                    time.sleep(.01)
                raise TimeoutError('模拟客户端未收到新命令')
            except Exception as exc:
                errors.append(exc)
        worker = threading.Thread(target=reply)
        worker.start()
        try:
            result = self.sender.send('current', 'inspect', timeout=5)
            self.assertTrue(result['ok'])
            self.assertNotEqual(result['id'], old['id'])
        finally:
            worker.join(timeout=6)
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertTrue((self.session / 'sender.lock').exists())

    def test_custom_root_manifest_and_current_agree(self):
        self.assertEqual(self.sender.resolve_directory('current'), self.session)
        manifest = self.sender.client_manifest()
        self.assertEqual(manifest['bridge_root'], str(self.root))
        self.assertEqual(Path(manifest['client_source']), SCRIPTS / 'paraview_bridge_client.py')


if __name__ == '__main__':
    unittest.main()
