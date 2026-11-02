"""GPU restriction 建表实验分析；保留作报告复现入口，无后续依赖时可删除。"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import numpy as np

def valid_fabs(directory):
    # 复用 analyze_transfer_fusion.py 的 VisMF/FAB 解析方式，适配本次 FLOAT。
    header = (directory / 'f_old_H').read_text()
    assert header.splitlines()[:2] == ['1', '1'], directory
    components = int(header.splitlines()[2])
    pattern = r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'
    boxes = re.findall(pattern, header)
    records = re.findall(r'FabOnDisk: (\S+) (\d+)', header)
    assert len(boxes) == len(records), directory
    for box, (filename, offset) in zip(boxes, records):
        lo, hi = [tuple(map(int, v.split(','))) for v in box[:2]]
        with (directory / filename).open('rb') as stream:
            stream.seek(int(offset))
            text = stream.readline().decode('ascii').strip()
            assert text.startswith('FAB ((8, (32 8 23 0 1 9 0 127)),(4, (4 3 2 1)))'), text
            match = re.search(pattern + r' (\d+)$', text)
            assert match and int(match[4]) == components, text
            flo, fhi = [tuple(map(int, match[n].split(','))) for n in (1, 2)]
            shape = tuple(b - a + 1 for a, b in zip(flo, fhi))
            values = np.fromfile(stream, dtype='<f4', count=math.prod(shape) * components)
        values = values.reshape((components, *reversed(shape)))
        slices = tuple(slice(a - f, b - f + 1) for a, b, f in zip(lo, hi, flo))
        yield box, values[(slice(None), *reversed(slices))]

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))

def logs(directory):
    paths = sorted(directory.glob('rank*.log')) or [directory / 'run.log']
    texts = [p.read_text() for p in paths]
    # AMReX 的全局结束信息只由 IO rank 输出；每 rank 均须有末窗口。
    assert any('AMReX (26.06) finalized' in t for t in texts), directory
    assert all(' perf(s):' in t for t in texts), directory
    return texts

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--regression-only', action='store_true')
    args = parser.parse_args()
    root = args.root
    status = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
    assert all(r['exit_code'] == '0' for r in status), status
    result = {'job': str(root), 'executable_sha256': hashlib.sha256((root/'candidate.ex').read_bytes()).hexdigest()}
    a, b = [root / f'regression_{n}' / 'chk00000192' for n in ('cpu', 'gpu')]
    assert (a/'Header').read_text() == (b/'Header').read_text()
    fabs = values = 0
    for level in sorted(a.glob('Level_*')):
        aa, bb = list(valid_fabs(level)), list(valid_fabs(b/level.name))
        assert len(aa) == len(bb)
        for (box,x),(box2,y) in zip(aa,bb):
            assert box == box2 and np.isfinite(x).all() and np.isfinite(y).all(), level
            assert np.array_equal(x.view('<u4'),y.view('<u4')), level
            fabs += 1
            values += x.size
    assert fabs > 0
    for name in ('cpu','gpu'):
        texts = logs(root/f'regression_{name}')
        assert any('ddf_norm_global:' in t for t in texts) if name == 'gpu' else True
        for text in texts:
            for line in text.splitlines():
                if line.startswith('ddf_cell_norm_level:'):
                    f=fields(line)
                    for k in ('valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2'):
                        assert math.isfinite(float(f[k])) and float(f[k]) == 0, line
    result['regression'] = {'finite_bitwise_identical_valid_fabs': fabs, 'values_per_run': values}
    if not args.regression_only:
        runs={}; workloads={}
        for row in status:
            name=row['case']
            if not name.startswith('timing_'): continue
            texts=logs(root/name)
            windows=[]; counts=[]; levels=[]
            for text in texts:
                ws=[fields(l) for l in text.splitlines() if ' perf(s):' in l]
                assert len(ws)==4 and all(int(w['steps'])==160 for w in ws),name
                windows.append(ws)
                counts.append([fields(l) for l in text.splitlines() if ' perf_count:' in l])
                levels.append([{k:v for k,v in fields(l).items() if k in ('lev','calls','launch_cells')} for l in text.splitlines() if ' collide_level:' in l])
            # 每个窗口先取较慢 rank，再累加后 480 步；初始化和首 160 步不计。
            sums={k:sum(max(float(w[i][k]) for w in windows) for i in range(1,4)) for k in ('total','solv','comm','collide','interp','average','boundary')}
            runs[name]=sums
            workloads[name]={'counts':counts,'levels':levels}
        assert len(runs) in (8,16),len(runs)
        assert all(w == next(iter(workloads.values())) for w in workloads.values()), 'workload mismatch'
        comparisons={}
        for detail in ('false','true'):
            cpu=[v['total'] for n,v in runs.items() if n.startswith(f'timing_{detail}_') and n.endswith('_false')]
            gpu=[v['total'] for n,v in runs.items() if n.startswith(f'timing_{detail}_') and n.endswith('_true')]
            if not cpu: continue
            a,b=statistics.mean(cpu),statistics.mean(gpu)
            comparisons[detail]={'cpu_s':cpu,'gpu_s':gpu,'cpu_mean_s':a,'gpu_mean_s':b,'reduction_percent':100*(a-b)/a,'cpu_stdev_s':statistics.stdev(cpu),'gpu_stdev_s':statistics.stdev(gpu)}
        result.update(runs=runs,comparisons=comparisons,workloads=workloads,workload_equal=True)
    dest=root/('regression_report.json' if args.regression_only else 'report.json')
    dest.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('runs','workloads')},indent=2))

if __name__ == '__main__':
    main()
