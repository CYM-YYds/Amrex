#!/usr/bin/env python3
"""监督本算例启动器的生命周期，完成或超时后及时回收进程组。"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wall-seconds', type=float, default=10800)
    parser.add_argument('--exit-grace-seconds', type=float, default=60)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.wall_seconds <= 0 or args.exit_grace_seconds <= 0:
        parser.error('command and positive time limits are required')
    started = time.monotonic()
    reason = 'launcher_exit'
    completion = None
    interrupted = []
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda number, frame: interrupted.append(number))
    # 直接写文件，不让继承 stdout 的辅助进程延长 tee 管道生命周期。
    with open('run.log', 'wb', buffering=0) as output, open('run.log', 'rb') as reader:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        Path('launcher.pid').write_text(str(process.pid) + '\n')
        pending = b''
        while process.poll() is None:
            pending += reader.read(1024 * 1024)
            lines = pending.split(b'\n')
            pending = lines.pop()
            # finalized 表明应用进入退出末尾，但不替代启动器退出码。
            if completion is None and any(re.fullmatch(rb'AMReX \([^\r\n]*\) finalized\r?', line)
                                          for line in lines):
                completion = time.monotonic()
            if completion is None:
                markers = []
                for path in Path('.').glob('rank_exit_*.json'):
                    markers.append(json.loads(path.read_text()))
                if markers and len(markers) == markers[0]['size']:
                    if {m['rank'] for m in markers} == set(range(markers[0]['size'])):
                        completion = time.monotonic()
            now = time.monotonic()
            if interrupted:
                reason = 'signal'
                break
            if now - started >= args.wall_seconds:
                reason = 'wall_timeout'
                break
            if completion is not None and now - completion >= args.exit_grace_seconds:
                reason = 'completion_exit_timeout'
                break
            time.sleep(0.2)
        if reason != 'launcher_exit':
            # 保存当前作业进程组供复查，不扫描或终止其他作业。
            with open('exit_processes.txt', 'w') as snapshot:
                try:
                    listing = subprocess.run(['ps', '-eo', 'pid,ppid,pgid,etime,stat,args'],
                                             capture_output=True, text=True, timeout=5)
                    for line in listing.stdout.splitlines():
                        fields = line.split(None, 5)
                        if len(fields) > 2 and fields[2] == str(process.pid):
                            snapshot.write(line + '\n')
                except subprocess.TimeoutExpired:
                    snapshot.write('process snapshot timed out\n')
        code = process.poll()
        # 无论正常退出还是异常，都清理继承启动器进程组的残留子进程。
        try:
            os.killpg(process.pid, signal.SIGTERM)
            for _ in range(15):
                if process.poll() is not None:
                    break
                time.sleep(0.2)
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    result = (code if code is not None else 1) if reason == 'launcher_exit' else (
        128 + interrupted[0] if interrupted else 124)
    status_file = Path('exit_status.json.tmp')
    status_file.write_text(json.dumps({
        'reason': reason, 'exit_code': result, 'launcher_returncode': process.returncode,
        'elapsed_seconds': time.monotonic() - started,
        'completion_seen': completion is not None,
        'wall_seconds': args.wall_seconds, 'exit_grace_seconds': args.exit_grace_seconds,
    }, indent=2) + '\n')
    status_file.replace('exit_status.json')
    print(f'BOX3D_OSI finished: reason={reason} exit_code={result}', flush=True)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
