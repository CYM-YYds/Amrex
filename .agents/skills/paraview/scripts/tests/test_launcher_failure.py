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
mode=sys.argv[2]
if mode in ('auth','key','always') or (mode in ('busy','closed') and n<3):
 message={'auth':'Permission denied (publickey)','key':'REMOTE HOST IDENTIFICATION HAS CHANGED','closed':'Connection closed by host port 6022'}.get(mode,'Exceeded MaxStartups')
 print(message,file=sys.stderr);sys.exit(255)
print('Authorized users only. All activities may be monitored and reported.',file=sys.stderr)
print(json.dumps(sys.argv[3:]))
""")
            for mode, read_only, expected, delays in [
                ('busy', False, 3, [4, 8]), ('auth', True, 1, []),
                ('key', True, 1, []), ('always', False, 6, [4, 8, 16, 32, 60]),
                ('closed', True, 3, [4, 8]), ('closed', False, 1, [])]:
                count = root / (mode + str(read_only) + '.count')
                def quoted(value):
                    return "'" + str(value).replace("'", "''") + "'"
                harness = root / 'check.ps1'
                harness.write_text("$ErrorActionPreference='Stop'\n" + helper + "\n" +
                    "function Start-Sleep { param($Seconds) $script:delays += [int]$Seconds }\n$script:delays=@()\n" +
                    "$out = Invoke-StartupNative " + ("-ReadOnly " if read_only else "") + "-Program " + quoted(sys.executable) +
                    " -NativeArguments @(" + ','.join(quoted(x) for x in [fake, count, mode, 'a b', 'last']) + ")\n" +
                    "$code=$LASTEXITCODE\n[ordered]@{code=$code;delays=@($script:delays);output=@($out)} | ConvertTo-Json -Compress\n")
                result = subprocess.run([PWSH, '-NoProfile', '-NonInteractive', '-File', str(harness)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                response = json.loads(result.stdout.splitlines()[-1])
                self.assertEqual(int(count.read_text()), expected)
                self.assertEqual(response['delays'], delays)
                success = mode == 'busy' or (mode == 'closed' and read_only)
                self.assertEqual(response['code'], 0 if success else 255)
                if success:
                    self.assertEqual(json.loads(response['output'][0]), ['a b', 'last'])


@unittest.skipUnless(PWSH, 'native PowerShell is unavailable')
class DiagnosticBundleTests(unittest.TestCase):
    def test_server_ssh_failure_is_recorded_and_archived(self):
        import zipfile
        source = (SCRIPTS / 'connect_paraview.ps1').read_text(encoding='utf-8-sig')
        helper = source[source.index('function Export-LaunchDiagnostics'):source.index('# 崩溃后可随时重打包')]
        runner = source.split("$runnerTemplate = @'\n", 1)[1].split("\n'@", 1)[0]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            launch = 'a' * 32
            stem = 'pvserver_' + launch
            command = "& '" + sys.executable.replace("'", "''") + "' -c \"import sys; print('server output'); print('Connection closed by host', file=sys.stderr); sys.exit(255)\""
            script = root / (stem + '.ps1')
            script.write_text(runner.replace('__EXPORT_FUNCTION__', helper).replace('__LAUNCH_ID__', launch).replace('__STEM__', stem).replace('__SSH_COMMAND__', command))
            result = subprocess.run([PWSH, '-NoProfile', '-NonInteractive', '-File', str(script)], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            record = json.loads((root / (stem + '.server-exit.json')).read_text(encoding='utf-8-sig'))
            self.assertEqual(record['exit_code'], 255, result.stdout + result.stderr)
            with zipfile.ZipFile(root / ('diagnostics_' + launch + '_server-exit.zip')) as archive:
                log = archive.read(stem + '.server.log').decode('utf-8-sig')
                self.assertIn('server output', log)
                self.assertIn('Connection closed by host', log)
                self.assertIn(stem + '.server-exit.json', archive.namelist())
