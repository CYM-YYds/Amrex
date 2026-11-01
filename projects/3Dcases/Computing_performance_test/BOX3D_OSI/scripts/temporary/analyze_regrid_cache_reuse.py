"""本次缓存/补边优化临时分析；作为报告复现材料保留，之后可删除。"""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys
import numpy as np

root, suite = Path(sys.argv[1]), sys.argv[2]
rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
logs = {}
for row in rows:
    name = row['case']
    assert row['exit_code'] == '0', row
    log = (root / name / 'run.log').read_text()
    assert 'AMReX (26.06) finalized' in log, name
    assert 'osi_ab_stage_failure' not in log, name
    assert not re.search(r'\bfinite=0\b|\bmismatches=[1-9]', log), name
    logs[name] = log

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))

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

report = {'runs': len(rows), 'suite': suite}
if suite not in ('timing', 'steady'):
    files = 0
    nonidentical_data_files = []
    distribution_changes = []
    valid_fab_count = 0
    nonfinite_valid_norms = []
    for name, log in logs.items():
        if not name.endswith('_candidate') or name == 'restart_candidate':
            continue
        old = name.removesuffix('_candidate') + '_baseline'
        records = re.findall(r'^ddf_cell_norm_level: (.*)$', log, re.M)
        assert records, name
        for record in records:
            for key in ['active_linf', 'active_l1', 'active_l2']:
                value = float(fields(record)[key])
                assert math.isfinite(value) and value == 0, (name, key, value)
            for key in ['valid_linf', 'valid_l1', 'valid_l2']:
                value = float(fields(record)[key])
                if math.isfinite(value):
                    assert value == 0, (name, key, value)
                else:
                    # 真实涡量工况可保留旧 covered 非有限值；下方必须验证全 DDF 字节一致。
                    assert name.startswith('natural_'), (name, key, value)
                    nonfinite_valid_norms.append(dict(case=name, level=fields(record)['lev'], metric=key))
        sources = sorted((root / old).glob('chk*/Level_*/f_old_D_*'))
        assert sources, name
        for source in sources:
            target = root / name / source.relative_to(root / old)
            if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(target.read_bytes()).digest():
                nonidentical_data_files.append(str(target.relative_to(root)))
            files += 1
        for before in sorted((root / old).glob('chk*')):
            after = root / name / before.name
            old_header = (before / 'Header').read_text()
            new_header = (after / 'Header').read_text()
            pattern = r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'
            assert old_header.splitlines()[:5] == new_header.splitlines()[:5], after
            assert re.findall(pattern, old_header) == re.findall(pattern, new_header), after
            if old_header != new_header:
                distribution_changes.append(str(after.relative_to(root)))
            for level in sorted(before.glob('Level_*')):
                a, b = list(valid_fabs(level)), list(valid_fabs(after / level.name))
                assert len(a) == len(b), level
                for (box_a, data_a), (box_b, data_b) in zip(a, b):
                    assert box_a == box_b and np.array_equal(data_a.view('<u4'), data_b.view('<u4')), (name, before.name, level.name, box_a)
                    valid_fab_count += 1
        if name.startswith('fixture_'):
            for callback in ['MakeNewLevelFromCoarse', 'RemakeLevel', 'ClearLevel']:
                assert f'AMR_LIFECYCLE {callback}' in log, (name, callback)
            counts = [fields(line) for line in log.splitlines() if ' perf_count:' in line]
            # 第 96 步布局不变，应复用全部缓存。
            unchanged = next(c for line, c in zip([l for l in log.splitlines() if ' perf_count:' in l], counts) if line.startswith('step96 '))
            assert all(int(unchanged[k]) == 0 for k in ['regrid_mask_builds', 'regrid_boundary_builds', 'regrid_average_builds', 'interp_cache_builds']), (name, unchanged)
        else:
            # 判据准备的统计、tag 数量及生成网格必须保持一致。
            prefixes = ('REFINEMENT_VORT ', 'REFINEMENT_CDF ', 'REGRID_DIAG lev=')
            a = [l for l in logs[old].splitlines() if l.startswith(prefixes)]
            b = [l for l in log.splitlines() if l.startswith(prefixes)]
            assert a and a == b, name
    for name, log in logs.items():
        if name.startswith('lockstep_'):
            errors = [float(v) for v in re.findall(r'^osi_ab_stage:.*?linf=(\S+)', log, re.M)]
            assert errors and all(math.isfinite(v) and v == 0 for v in errors), name
    report['compared_ddf_files'] = files
    report['identical_ddf_files'] = files - len(nonidentical_data_files)
    report['identical_valid_fabs'] = valid_fab_count
    report['nonidentical_data_files'] = nonidentical_data_files
    report['distribution_mapping_changes'] = distribution_changes
    report['unchanged_nonfinite_valid_norms'] = nonfinite_valid_norms
    if 'detail' in logs:
        report['detail'] = [fields(l) for l in logs['detail'].splitlines() if ' perf_detail(s):' in l]
else:
    totals = {}
    workloads = []
    regrids = {}
    memory = {}
    program_total = {}
    cache_counts = {}
    for name, log in logs.items():
        if name == 'timing_detail':
            report['detail'] = [fields(l) for l in log.splitlines() if ' perf_detail(s):' in l]
            continue
        windows = [fields(l) for l in log.splitlines() if ' perf(s):' in l]
        width = 64 if suite == 'steady' else 160
        assert len(windows) == 4 and all(int(w['steps']) == width for w in windows), name
        totals[name] = sum(float(w['total']) for w in windows[1:])
        workloads.append([fields(l) for l in log.splitlines() if ' collide_level:' in l])
        regrids[name] = sum(float(v) / 1000 for v in re.findall(r'regrid_time: (\S+) ms', log)[1:])
        memory[name] = re.findall(r'\[The         Arena\].*?\[([\d.]+) \.\.\. ([\d.]+)\]', log)
        program_total[name] = float(re.search(r'^Total Time: (\S+)', log, re.M)[1])
        cache_counts[name] = [fields(l) for l in log.splitlines() if ' perf_count:' in l][1:]
    # 排除时间字段，检查所有运行的每窗碰撞工作量一致。
    reference = [[(v['lev'], v['calls'], v['launch_cells']) for v in w] for w in workloads]
    assert all(w == reference[0] for w in reference), 'workload changed'
    pairs = []
    names = list(totals)
    for i in range(0, 8, 2):
        pair = {n.rsplit('_', 1)[1]: totals[n] for n in names[i:i + 2]}
        pairs.append(dict(pair, reduction_pct=100 * (1 - pair['candidate'] / pair['baseline'])))
    report.update(totals_s=totals, regrid_s=regrids, pairs=pairs,
                  arena_reserved_then_used_mib=memory, program_total_s=program_total,
                  measured_cache_counts=cache_counts,
                  median_reduction_pct=statistics.median(p['reduction_pct'] for p in pairs))
(root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
(root / 'report.md').write_text('本次临时验证/计时结果，范围及原始数据见同目录命令、日志和 report.json。\n\n```json\n' + json.dumps(report, indent=2) + '\n```\n')
print(json.dumps(report, indent=2))
