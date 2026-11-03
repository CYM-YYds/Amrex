#!/usr/bin/env python3
"""本次AMReX写入诊断临时分析器；作为完整性检查的复现材料保留。"""
import argparse
import json
import re
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
args = parser.parse_args()
reports = []
for job in sorted(args.root.glob('job*')):
    run = job / 'C_mode0'
    if not (run / 'exit_status.json').exists():
        continue
    status = json.loads((run / 'exit_status.json').read_text())
    errors, writes = [], 0
    for trace in sorted(run.glob('io_trace_rank*')):
        for line in trace.read_text(errors='replace').splitlines():
            if 'Cell_D_' in line and re.search(r'\b(?:write|writev|pwrite64)\(', line):
                writes += 1
            if '/plt_' in line and re.search(r'= -1 \w+', line):
                errors.append(line)
    plots = []
    for plot in sorted(run.glob('plt_*')):
        if not plot.is_dir():
            continue
        header = (plot / 'Header').read_text().splitlines()
        nvalues, fabs, ends = 0, 0, {}
        for level in sorted(plot.glob('Level_*')):
            text = (level / 'Cell_H').read_text()
            entries = re.findall(r'FabOnDisk: (\S+) (\d+)', text)
            assert entries, level
            for filename, offset in entries:
                path = level / filename
                with path.open('rb') as stream:
                    stream.seek(int(offset))
                    line = stream.readline().decode('ascii')
                    match = re.search(r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\) (\d+)\s*$', line)
                    assert match, line
                    lo, hi = [tuple(map(int, match[i].split(','))) for i in (1, 2)]
                    count = int(match[4]) * int(np.prod([b - a + 1 for a, b in zip(lo, hi)]))
                    assert '((8, (32 ' in line or '((8, (64 ' in line
                    dtype = '<f4' if '((8, (32 ' in line else '<f8'
                    data = np.fromfile(stream, dtype=dtype, count=count)
                    assert data.size == count and np.isfinite(data).all(), path
                    ends[str(path)] = max(ends.get(str(path), 0), stream.tell())
                    nvalues += count
                    fabs += 1
        for path, expected in ends.items():
            assert Path(path).stat().st_size == expected, path
        plots.append({'plot': plot.name, 'fabs': fabs, 'finite_values': nvalues,
                      'all_fabs_complete': True})
    reports.append({'job': job.name, 'node': (job / 'hostfile').read_text().strip(),
                    'exit_status': status, 'field_write_syscalls': writes,
                    'field_syscall_errors': errors, 'plots': plots})
assert len(reports) == 2
assert all(r['exit_status']['exit_code'] == 0 and not r['field_syscall_errors'] for r in reports)
(args.root / 'trace_validation.json').write_text(json.dumps(reports, indent=2) + '\n')
for r in reports:
    print(r['job'], 'writes', r['field_write_syscalls'], 'plots', len(r['plots']),
          'finite_values', sum(p['finite_values'] for p in r['plots']))
