"""验证批处理节点路由、参数引用和环境隔离，不启动 ParaView。"""
import json
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


class RoutingTests(unittest.TestCase):
    def make_command(self, directory, name, body):
        path = directory/name
        path.write_text('#!/bin/bash\n'+body+'\n')
        path.chmod(0o755)
        return path

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
