#!/usr/bin/env python3
"""在提交节点检查作业完成凭证与调度状态，回收已完成但未释放的分配。"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job', required=True, type=int)
    parser.add_argument('--case', required=True, type=Path)
    parser.add_argument('--grace-seconds', type=float, default=60)
    args = parser.parse_args()
    if args.job <= 0 or args.grace_seconds <= 0:
        parser.error('positive job id and grace are required')
    env = dict(os.environ, CCS_CLI_HOME=os.environ.get('CCS_CLI_HOME', '/opt/batch/cli'))
    djob = '/opt/batch/cli/bin/djob'
    completed_at = None
    (args.case / 'logs/submit' / f'{args.job}-supervisor.pid').write_text(str(os.getpid()) + '\n')
    # 完成凭证只由 guard_run 在启动器退出或清理后原子发布。
    receipt = args.case / 'logs/submit' / f'{args.job}-supervisor.json'
    while True:
        try:
            result = subprocess.run([djob, '-L', str(args.job)], env=env,
                                    capture_output=True, text=True, timeout=20)
            match = re.search(r'^JOB_STATE\s+(\S+)', result.stdout, re.M)
            state = match[1] if match else 'UNKNOWN'
            runs = list((args.case / 'runs').glob(f'*_job{args.job}'))
            exit_file = runs[0] / 'exit_status.json' if len(runs) == 1 else None
            if exit_file and exit_file.exists() and completed_at is None:
                json.loads(exit_file.read_text())
                completed_at = time.monotonic()
            if state in ('SUCCEEDED', 'FAILED'):
                receipt.write_text(json.dumps({'job': args.job, 'state': state,
                                                'action': 'finished'}, indent=2) + '\n')
                return
            if (completed_at is not None and state == 'RUNNING' and
                    time.monotonic() - completed_at >= args.grace_seconds):
                action = subprocess.run([djob, '-T', str(args.job)], env=env,
                                        capture_output=True, text=True, timeout=20)
                receipt.write_text(json.dumps({
                    'job': args.job, 'state': state, 'action': 'terminate_after_exit',
                    'command_returncode': action.returncode,
                    'message': action.stdout + action.stderr,
                }, indent=2) + '\n')
                if action.returncode == 0:
                    # 继续确认终态；保留回收动作，不能以正常结束覆盖异常证据。
                    for _ in range(12):
                        time.sleep(2)
                        check = subprocess.run([djob, '-L', str(args.job)], env=env,
                                               capture_output=True, text=True, timeout=20)
                        end = re.search(r'^JOB_STATE\s+(\S+)', check.stdout, re.M)
                        if end and end[1] in ('SUCCEEDED', 'FAILED'):
                            record = json.loads(receipt.read_text())
                            record['final_state'] = end[1]
                            receipt.write_text(json.dumps(record, indent=2) + '\n')
                            return
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            print(f'supervisor retry: {error}', flush=True)
        time.sleep(5)


if __name__ == '__main__':
    main()
