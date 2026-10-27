"""验证批处理节点路由、参数引用和环境隔离，不启动 ParaView。"""
import json
import base64
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import re
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


class RoutingTests(unittest.TestCase):
    def make_command(self, directory, name, body):
        path = directory/name
        path.write_text('#!/bin/bash\n'+body+'\n')
        path.chmod(0o755)
        return path

    def server_environment(self, directory, mpi=True):
        self.make_command(directory, 'hostname', 'echo whshare-agent-1')
        prefix = directory/'paraview install'
        binaries = prefix/'bin'
        binaries.mkdir(parents=True)
        config = prefix/'lib/cmake/paraview-6.2'
        config.mkdir(parents=True)
        (config/'paraview-config.cmake').write_text('set(PARAVIEW_USE_MPI "' + ('ON' if mpi else 'OFF') + '")\n')
        self.make_command(binaries, 'pvserver',
                          'printf "%s\\n" "$@" > "$CAPTURE"\n'
                          'test -z "${LD_LIBRARY_PATH:-}" && test -z "${PYTHONPATH:-}" && '
                          'test -z "${DISPLAY:-}" && test "$QT_QPA_PLATFORM" = offscreen')
        self.make_command(binaries, 'mpiexec',
                          'printf "%s\\n" "$@" > "$MPI_CAPTURE"\nshift 2\nexec "$@"')
        env = dict(os.environ, PATH=str(directory)+':'+os.environ['PATH'],
                   CAPTURE=str(directory/'server-args'), MPI_CAPTURE=str(directory/'mpi-args'),
                   PARAVIEW_SERVER_LOG_DIR=str(directory/'server logs'),
                   LD_LIBRARY_PATH='/invalid/libs', PYTHONPATH='/invalid/python', DISPLAY=':invalid')
        return prefix, env

    def test_server_log_keeps_both_streams_and_failure_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            prefix, env = self.server_environment(directory)
            self.make_command(prefix/'bin', 'pvserver',
                              'echo server-output; echo server-error >&2; exit 23')
            result = subprocess.run(['bash', str(SCRIPTS/'run_paraview_server.sh'),
                                     '--prefix', str(prefix), '--processes', '4'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23)
            logs = list((directory/'server logs').glob('*.log'))
            self.assertEqual(len(logs), 1)
            contents = logs[0].read_text()
            self.assertIn('server-output', contents)
            self.assertIn('server-error', contents)
            self.assertIn('server-output', result.stdout)
            self.assertIn('server-error', result.stdout)

    def test_parallel_server_uses_matching_launcher_and_preserves_arguments(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            prefix, env = self.server_environment(directory)
            result = subprocess.run(['bash', str(SCRIPTS/'run_paraview_server.sh'), '--prefix', str(prefix),
                                     '--processes', '4', '--server-port=12345', 'argument with spaces'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((directory/'mpi-args').read_text().splitlines(),
                             ['-n', '4', str(prefix/'bin/pvserver'), '--force-offscreen-rendering',
                              '--server-port=12345', 'argument with spaces'])
            self.assertEqual((directory/'server-args').read_text().splitlines()[-1], 'argument with spaces')

    def test_serial_build_rejects_parallel_launch_before_starting_server(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            prefix, env = self.server_environment(directory, mpi=False)
            result = subprocess.run(['bash', str(SCRIPTS/'run_paraview_server.sh'), '--prefix', str(prefix),
                                     '--processes', '4', '--version'], env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('built without MPI', result.stderr)
            self.assertFalse((directory/'mpi-args').exists())
            self.assertFalse((directory/'server-args').exists())

    def test_parallel_preflight_does_not_launch_multiple_version_probes(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            prefix, env = self.server_environment(directory)
            result = subprocess.run(['bash', str(SCRIPTS/'run_paraview_server.sh'), '--prefix', str(prefix),
                                     '--processes', '4', '--version'], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((directory/'mpi-args').exists())
            self.assertEqual((directory/'server-args').read_text().strip(), '--version')
            self.assertFalse((directory/'server logs').exists())

    def test_single_process_retains_serial_compatibility_and_rejects_bad_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            prefix, env = self.server_environment(directory, mpi=False)
            (prefix/'bin/mpiexec').unlink()
            base = ['bash', str(SCRIPTS/'run_paraview_server.sh'), '--prefix', str(prefix)]
            result = subprocess.run([*base, '--processes', '1', '--version'], env=env, capture_output=True)
            self.assertEqual(result.returncode, 0)
            (directory/'server-args').unlink()
            for value in ['0', '-2', '1.5', '257', '4;echo injected']:
                result = subprocess.run([*base, '--processes', value], env=env, capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertFalse((directory/'server-args').exists())

    def test_gui_server_refuses_login_node_even_for_version_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.make_command(directory, 'hostname', 'echo whshare-ccs-cli-test')
            environment = dict(os.environ, PATH=str(directory)+':'+os.environ['PATH'])
            result = subprocess.run(['bash', str(SCRIPTS/'run_paraview_server.sh'), '--version'],
                                    env=environment, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('refusing this node', result.stderr)

    def test_gui_command_routes_without_ssh_to_itself(self):
        source = (SCRIPTS/'connect_paraview.ps1').read_text(encoding='utf-8-sig')
        template = re.search(r'return "(if hostname .*?)"', source).group(1)
        payload = 'printf "%s" "payload with spaces" > "$CAPTURE"'
        command = template.replace('${SERVER_HOST}', 'whshare-agent-1').replace(
            '$SERVER_HOST', 'whshare-agent-1').replace(
            '$encoded', base64.b64encode(payload.encode()).decode())
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.make_command(directory, 'ssh', 'exit 19')
            capture = directory/'result'
            environment = dict(os.environ, PATH=str(directory)+':'+os.environ['PATH'],
                               CAPTURE=str(capture))
            self.make_command(directory, 'hostname', 'echo whshare-agent-1')
            result = subprocess.run(['bash', '-c', command], env=environment)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(capture.read_text(), 'payload with spaces')
            capture.unlink()
            # 登录节点必须转发；模拟转发失败时不得本地执行载荷。
            self.make_command(directory, 'hostname', 'echo whshare-ccs-cli-test')
            result = subprocess.run(['bash', '-c', command], env=environment)
            self.assertEqual(result.returncode, 19)
            self.assertFalse(capture.exists())

    def test_failed_ssh_never_falls_back_to_local_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            self.make_command(directory,'hostname','echo whshare-ccs-cli-test')
            self.make_command(directory,'ssh','printf "%s\\n" "$@" > "$CAPTURE"; exit 17')
            executable=self.make_command(directory,'fake pvbatch','touch "$FORBIDDEN"')
            environment=dict(os.environ,PATH=str(directory)+':'+os.environ['PATH'],
                PARAVIEW_BATCH_EXECUTABLE=str(executable),CAPTURE=str(directory/'args'),
                FORBIDDEN=str(directory/'ran-local'))
            arg='input with spaces; $(touch SHOULD_NOT_EXIST)'
            result=subprocess.run(['bash',str(SCRIPTS/'run_paraview_batch.sh'),'--input',arg],
                                  env=environment,capture_output=True)
            self.assertEqual(result.returncode,17)
            self.assertFalse((directory/'ran-local').exists())
            arguments=(directory/'args').read_text()
            self.assertIn('whshare-agent-1',arguments)
            self.assertIn(r'input\ with\ spaces\;',arguments)
            self.assertIn(r'\$\(',arguments)
            # 安装入口也必须先跳转，不能因为本地已有环境就直接启动 ParaView。
            result=subprocess.run(['bash',str(SCRIPTS/'install_paraview_batch.sh')],
                                  env=environment,capture_output=True)
            self.assertEqual(result.returncode,17)
            self.assertFalse((directory/'ran-local').exists())

    def test_environment_lock_checks_encoded_filenames_and_hashes(self):
        script=(SCRIPTS/'install_paraview_batch.sh').read_text()
        check=script.split("<<'CHECK_ENV'\n",1)[1].split('\nCHECK_ENV',1)[0]
        with tempfile.TemporaryDirectory() as tmp:
            prefix=Path(tmp);metadata=prefix/'conda-meta';metadata.mkdir()
            record=metadata/'package.json';lock=prefix/'explicit.lock'
            record.write_text(json.dumps({'fn':'pkg-1!1.0-0.conda','sha256':'a'*64}))
            lock.write_text('@EXPLICIT\nhttps://example.org/pkg-1%211.0-0.conda#'+'a'*64+'\n')
            command=[sys.executable,'-c',check,str(prefix),str(lock)]
            self.assertEqual(subprocess.run(command).returncode,0)
            record.write_text(json.dumps({'fn':'pkg-1!1.0-0.conda','sha256':'b'*64}))
            self.assertEqual(subprocess.run(command).returncode,1)

    def test_execution_only_on_compile_node_with_clean_environment(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            self.make_command(directory,'hostname','echo whshare-agent-1')
            executable=self.make_command(directory,'fake pvbatch',
                'printf "%s\\n" "$@" > "$CAPTURE"\n'
                'test -z "${PYTHONPATH:-}" && test -z "${DISPLAY:-}" && '
                'test -z "${LD_LIBRARY_PATH:-}" && test "$QT_QPA_PLATFORM" = offscreen')
            environment=dict(os.environ,PATH=str(directory)+':'+os.environ['PATH'],
                PARAVIEW_BATCH_EXECUTABLE=str(executable),CAPTURE=str(directory/'args'),
                PYTHONPATH='/invalid/python',DISPLAY=':invalid',LD_LIBRARY_PATH='/invalid/lib')
            result=subprocess.run(['bash',str(SCRIPTS/'run_paraview_batch.sh'),
                '--input','/remote/data with spaces'],env=environment,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr.decode())
            arguments=(directory/'args').read_text().splitlines()
            self.assertEqual(arguments[0],'--force-offscreen-rendering')
            self.assertEqual(arguments[-1],'/remote/data with spaces')
            self.assertEqual(arguments[1],str(SCRIPTS/'paraview_batch_export.py'))


if __name__ == '__main__':
    unittest.main()
