"""验证启动器失败、退出挂起、孤儿日志写端和硬超时的资源回收。"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

GUARD = Path(__file__).resolve().parents[1] / 'scripts/guard_run.py'


class GuardTests(unittest.TestCase):
    def run_guard(self, command, wall=5, grace=0.4):
        with tempfile.TemporaryDirectory() as directory:
            started = time.monotonic()
            result = subprocess.run(
                [sys.executable, str(GUARD), '--wall-seconds', str(wall),
                 '--exit-grace-seconds', str(grace), '--', 'bash', '-c', command],
                cwd=directory, capture_output=True, text=True, timeout=12)
            status = json.loads((Path(directory) / 'exit_status.json').read_text())
            snapshot = Path(directory) / 'exit_processes.txt'
            return result.returncode, status, time.monotonic() - started, snapshot.exists()

    def test_normal_and_nonzero_exit(self):
        for expected in (0, 7):
            code, status, _, _ = self.run_guard(f'exit {expected}')
            self.assertEqual(code, expected)
            self.assertEqual(status['reason'], 'launcher_exit')

    def test_completed_application_hung_launcher(self):
        code, status, elapsed, snapshot = self.run_guard(
            "echo 'AMReX (26.06) finalized'; sleep 100")
        self.assertEqual(code, 124)
        self.assertEqual(status['reason'], 'completion_exit_timeout')
        self.assertLess(elapsed, 6)
        self.assertTrue(snapshot)

    def test_rank_return_without_final_log(self):
        code, status, _, _ = self.run_guard(
            '''echo '{"rank":0,"size":1,"exit_code":0}' > rank_exit_0.json; sleep 100''')
        self.assertEqual(code, 124)
        self.assertEqual(status['reason'], 'completion_exit_timeout')

    def test_incomplete_rank_set_does_not_finish(self):
        code, status, _, _ = self.run_guard(
            '''echo '{"rank":0,"size":2,"exit_code":0}' > rank_exit_0.json; sleep 100''', wall=0.8)
        self.assertEqual(code, 124)
        self.assertEqual(status['reason'], 'wall_timeout')

    def test_inherited_log_descriptor_does_not_hold_job(self):
        code, status, elapsed, _ = self.run_guard('sleep 100 & exit 0')
        self.assertEqual(code, 0)
        self.assertLess(elapsed, 5)
        self.assertEqual(status['reason'], 'launcher_exit')


class SchedulerWatchTests(unittest.TestCase):
    def test_only_completed_running_job_is_cancelled(self):
        import importlib.util
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('watch_job', GUARD.parent / 'watch_job.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / 'logs/submit').mkdir(parents=True)
            run = case / 'runs/test_job42'
            run.mkdir(parents=True)
            (run / 'exit_status.json').write_text('{"exit_code":0}')
            outputs = [subprocess.CompletedProcess([], 0, 'JOB_STATE RUNNING', ''),
                       subprocess.CompletedProcess([], 0, 'terminate accepted', ''),
                       subprocess.CompletedProcess([], 0, 'JOB_STATE FAILED', '')]
            with patch.object(sys, 'argv', ['watch_job', '--job', '42', '--case', str(case), '--grace-seconds', '1']), \
                 patch.object(module.subprocess, 'run', side_effect=outputs) as calls, \
                 patch.object(module.time, 'monotonic', side_effect=[0, 2]), \
                 patch.object(module.time, 'sleep'):
                module.main()
            self.assertEqual(calls.call_args_list[1].args[0][-2:], ['-T', '42'])
            receipt = json.loads((case / 'logs/submit/42-supervisor.json').read_text())
            self.assertEqual(receipt['action'], 'terminate_after_exit')
            self.assertEqual(receipt['final_state'], 'FAILED')

    def test_no_exit_receipt_does_not_cancel_running_job(self):
        import importlib.util
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('watch_job', GUARD.parent / 'watch_job.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / 'logs/submit').mkdir(parents=True)
            (case / 'runs').mkdir()
            outputs = [subprocess.CompletedProcess([], 0, 'JOB_STATE RUNNING', ''),
                       subprocess.CompletedProcess([], 0, 'JOB_STATE SUCCEEDED', '')]
            with patch.object(sys, 'argv', ['watch_job', '--job', '42', '--case', str(case)]), \
                 patch.object(module.subprocess, 'run', side_effect=outputs) as calls, \
                 patch.object(module.time, 'sleep'):
                module.main()
            self.assertTrue(all('-T' not in call.args[0] for call in calls.call_args_list))


if __name__ == '__main__':
    unittest.main()
