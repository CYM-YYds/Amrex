"""原生 PowerShell 检查启动异常的退出码和错误留存；不建立真实 SSH 连接。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
PWSH = shutil.which('pwsh')


@unittest.skipUnless(PWSH, 'native PowerShell is unavailable')
class LauncherFailureTests(unittest.TestCase):
    def check_failure(self, blocked_log_directory=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            script = root / 'connect_paraview.ps1'
            shutil.copyfile(SCRIPTS / script.name, script)
            fallback = root / 'fallback'
            fallback.mkdir()
            if blocked_log_directory:
                (root / 'paraview_logs').write_text('not a directory')
            result = subprocess.run(
                [PWSH, '-NoProfile', '-NonInteractive', '-File', str(script),
                 '-BridgeStatus', '-UpdateBridge', '-NoPauseOnError'],
                capture_output=True, text=True, timeout=20,
                env=dict(os.environ, TMPDIR=str(fallback), TMP=str(fallback), TEMP=str(fallback)))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Choose either -BridgeStatus or -UpdateBridge.', result.stdout + result.stderr)
            transcripts = [p for p in root.rglob('launcher_*.log') if not p.name.startswith('launcher_error_')]
            self.assertEqual(len(transcripts), 1)
            self.assertIn('Launcher transcript:', transcripts[0].read_text(encoding='utf-8-sig'))
            logs = list(root.rglob('launcher_error_*.log'))
            self.assertEqual(len(logs), 1)
            self.assertIn('Choose either -BridgeStatus or -UpdateBridge.', logs[0].read_text())
            if blocked_log_directory:
                self.assertEqual(logs[0].parent, fallback)

    def test_failure_is_saved_and_returns_nonzero(self):
        self.check_failure()

    def test_unwritable_log_location_uses_temp_directory(self):
        self.check_failure(blocked_log_directory=True)
