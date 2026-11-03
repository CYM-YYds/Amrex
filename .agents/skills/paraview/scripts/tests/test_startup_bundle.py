"""执行启动器内嵌只读预检，核对文件字节、层层校验和整段端口选择。"""
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


class StartupBundleTests(unittest.TestCase):
    def run_bundle(self, version='6.2.0', busy=(11115, 11116), corrupt=False):
        source = (SCRIPTS / 'connect_paraview.ps1').read_text(encoding='utf-8-sig')
        program = re.search(r"\$bundlePython = @'\n(.*?)\n'@", source, re.S).group(1)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            client = root / 'bridge with spaces.py'
            payload = b'# bridge\n# \xe6\xb5\x8b\xe8\xaf\x95\r\n'
            client.write_bytes(payload)
            manifest = dict(client_source=str(client), source_sha256='bad' if corrupt else hashlib.sha256(payload).hexdigest(), version='28', bridge_root=str(root))
            sender = root / 'sender with spaces.py'
            sender.write_text('import json\nprint(' + repr(json.dumps(manifest)) + ')\n')
            listeners = ''.join('tcp 0 0 0.0.0.0:%d 0.0.0.0:* LISTEN\n' % p for p in busy)
            import shlex
            config = dict(version_command='printf %s ' + shlex.quote(version), ports_command='printf %s ' + shlex.quote(listeners), sender=str(sender), start_port=11115, max_ports=10)
            code = program.replace('__CONFIG64__', base64.b64encode(json.dumps(config).encode()).decode())
            result = subprocess.run([sys.executable, '-c', code], capture_output=True, text=True, timeout=10)
            return result, payload

    def test_one_reply_contains_verified_bytes_and_first_free_port(self):
        result, payload = self.run_bundle()
        self.assertEqual(result.returncode, 0, result.stderr)
        bundle = json.loads(result.stdout)
        self.assertEqual(bundle['available_port'], 11117)
        self.assertEqual(base64.b64decode(bundle['client_base64']), payload)
        self.assertEqual(bundle['manifest']['source_sha256'], hashlib.sha256(payload).hexdigest())
        self.assertTrue(bundle['gateway'])

    def test_rejects_version_hash_and_exhausted_ports(self):
        for kwargs, message in [(dict(version='5.11.1'), 'version preflight failed'),
                                (dict(corrupt=True), 'bridge changed during preflight'),
                                (dict(busy=range(11115, 11125)), 'No available remote port')]:
            with self.subTest(kwargs=kwargs):
                result, _ = self.run_bundle(**kwargs)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertEqual(result.stdout, '')
