#!/usr/bin/env python3
"""AI 临时工具：汇总本次续跑显存采样；运行目录保留副本以复现峰值。"""
import argparse
import csv
import json
from pathlib import Path
import time

parser = argparse.ArgumentParser()
parser.add_argument('run', type=Path)
parser.add_argument('--watch', action='store_true')
args = parser.parse_args()
run = args.run.resolve()
job = run.name.rsplit('job', 1)[1]
receipt = run.parents[1] / 'logs' / 'submit' / f'{job}-supervisor.json'
if args.watch:
    while not receipt.exists():
        time.sleep(10)
    # 等待采样入口完成退出清理，随后读取已刷新的尾部。
    time.sleep(2)
gpus = {}
with (run / 'gpu-memory.csv').open() as stream:
    for row in csv.reader(stream):
        if len(row) != 5:
            continue
        stamp, index, uuid, used, total = [value.strip() for value in row]
        try:
            used, total = float(used), float(total)
        except ValueError:
            continue
        item = gpus.setdefault(uuid, {'index': index, 'uuid': uuid,
                                     'samples': 0, 'peak_used_MiB': -1})
        item['samples'] += 1
        item['last_sample'] = stamp
        item['last_used_MiB'] = used
        item['total_MiB'] = total
        if used > item['peak_used_MiB']:
            item['peak_used_MiB'] = used
            item['peak_time'] = stamp
summary = {'job': job, 'nominal_sample_interval_ms': 200,
           'scope': 'device-wide sampled usage; includes CUDA context and allocator pool',
           'scheduler_finished': receipt.exists(),
           'gpus': sorted(gpus.values(), key=lambda item: int(item['index']))}
target = run / 'gpu-memory-summary.json'
temp = target.with_suffix('.json.tmp')
temp.write_text(json.dumps(summary, indent=2) + '\n')
temp.replace(target)
print(json.dumps(summary, indent=2))
