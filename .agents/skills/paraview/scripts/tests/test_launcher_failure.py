"""原生 PowerShell 检查启动异常的退出码和错误留存；不建立真实 SSH 连接。"""
import json
import sys
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


@unittest.skipUnless(PWSH, 'native PowerShell is unavailable')
class LauncherRetryTests(unittest.TestCase):
    def test_pre_auth_retry_is_bounded_and_preserves_output(self):
        source = (SCRIPTS / 'connect_paraview.ps1').read_text(encoding='utf-8-sig')
        helper = source[source.index('function Invoke-StartupNative'):source.index('if ($BridgeStatus -and $UpdateBridge)')]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fake = root / 'native.py'
            fake.write_text("""import sys,pathlib,json
p=pathlib.Path(sys.argv[1]); n=int(p.read_text())+1 if p.exists() else 1; p.write_text(str(n))
if sys.argv[2]=='auth' or (sys.argv[2]=='busy' and n<3) or sys.argv[2]=='always':
 print('Permission denied (publickey)' if sys.argv[2]=='auth' else 'Exceeded MaxStartups',file=sys.stderr);sys.exit(255)
print(json.dumps(sys.argv[3:]))
""")
            for mode, expected, delays in [('busy', 3, [4, 8]), ('auth', 1, []), ('always', 6, [4, 8, 16, 32, 60])]:
                count = root / (mode + '.count')
                def quoted(value):
                    return "'" + str(value).replace("'", "''") + "'"
                harness = root / 'check.ps1'
                harness.write_text("$ErrorActionPreference='Stop'\n" + helper + "\n" +
                    "function Start-Sleep { param($Seconds) $script:delays += [int]$Seconds }\n$script:delays=@()\n" +
                    "$out = Invoke-StartupNative -Program " + quoted(sys.executable) +
                    " -NativeArguments @(" + ','.join(quoted(x) for x in [fake, count, mode, 'a b', 'last']) + ")\n" +
                    "$code=$LASTEXITCODE\n[ordered]@{code=$code;delays=@($script:delays);output=@($out)} | ConvertTo-Json -Compress\n")
                result = subprocess.run([PWSH, '-NoProfile', '-NonInteractive', '-File', str(harness)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                response = json.loads(result.stdout.splitlines()[-1])
                self.assertEqual(int(count.read_text()), expected)
                self.assertEqual(response['delays'], delays)
                self.assertEqual(response['code'], 0 if mode == 'busy' else 255)
                if mode == 'busy':
                    self.assertEqual(json.loads(response['output'][0]), ['a b', 'last'])
