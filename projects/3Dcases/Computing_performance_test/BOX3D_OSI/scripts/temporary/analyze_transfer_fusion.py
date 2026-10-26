"""本次融合实验的临时分析器；保留为逐值回归和交替计时报告的复现材料。"""
import csv
import json
import math
from pathlib import Path
import re
import statistics
import sys
import numpy as np

root = Path(sys.argv[1]).resolve()
rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
production_only = bool(rows) and all(r['case'].startswith('prod_') for r in rows)


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def errors(log, prefix, field):
    return [float(fields(line)[field]) for line in log.splitlines()
            if line.startswith(prefix) and field in fields(line)]


def checkpoint_fabs(directory):
    """只支持本实验的 AMReX little-endian double FAB，逐块提取 valid 区。"""
    header = (directory / 'f_old_H').read_text()
    lines = header.splitlines()
    if lines[:2] != ['1', '1']:
        raise ValueError('unsupported VisMF format: ' + str(directory))
    components = int(lines[2])
    box_pattern = r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'
    boxes = re.findall(box_pattern, header)
    records = re.findall(r'FabOnDisk: (\S+) (\d+)', header)
    if len(boxes) != len(records):
        raise ValueError('FAB/valid box count mismatch: ' + str(directory))
    for box, (filename, offset) in zip(boxes, records):
        lo, hi = [tuple(map(int, x.split(','))) for x in box[:2]]
        with (directory / filename).open('rb') as stream:
            stream.seek(int(offset))
            fab_header = stream.readline().decode('ascii').strip()
            if not fab_header.startswith('FAB ((8, (64 11 52 0 1 12 0 1023)),(8, (8 7 6 5 4 3 2 1)))'):
                raise ValueError('unsupported real format: ' + fab_header)
            match = re.search(box_pattern + r' (\d+)$', fab_header)
            if not match or int(match[4]) != components:
                raise ValueError('invalid FAB header: ' + fab_header)
            fab_lo, fab_hi = [tuple(map(int, match[n].split(','))) for n in (1, 2)]
            lengths = tuple(b-a+1 for a, b in zip(fab_lo, fab_hi))
            count = math.prod(lengths)*components
            data = np.fromfile(stream, dtype='<f8', count=count)
            if data.size != count:
                raise ValueError('truncated FAB: ' + str(directory / filename))
        data = data.reshape((components, *reversed(lengths)))
        slices = tuple(slice(a-f, b-f+1) for a, b, f in zip(lo, hi, fab_lo))
        yield box, data[(slice(None), *reversed(slices))]


def compare_checkpoints(before, after, allow_nonfinite_covered=False):
    old = (before / 'Header').read_text().splitlines()
    new = (after / 'Header').read_text().splitlines()
    if old != new or (before / 'Lattice').read_bytes() != (after / 'Lattice').read_bytes():
        raise ValueError('same-layout checkpoint metadata changed')
    result = []
    for lev in range(int(old[4]) + 1):
        maximum = 0
        active_maximum = 0
        values = 0
        covered_nonfinite = 0
        finer_boxes = []
        if lev < int(old[4]):
            header = (after / f'Level_{lev+1}' / 'f_old_H').read_text()
            for box in re.findall(r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)', header):
                finer_boxes.append([tuple(int(v)//2 for v in x.split(',')) for x in box[:2]])
        for (a_box, a), (b_box, b) in zip(checkpoint_fabs(before / f'Level_{lev}'), checkpoint_fabs(after / f'Level_{lev}'), strict=True):
            if a_box != b_box or a.shape != b.shape:
                raise ValueError('checkpoint valid layout mismatch')
            lo, hi = [tuple(map(int, x.split(','))) for x in a_box[:2]]
            active = np.ones(a.shape[1:], dtype=bool)
            for fine_lo, fine_hi in finer_boxes:
                lower = tuple(max(v, f) for v, f in zip(lo, fine_lo))
                upper = tuple(min(v, f) for v, f in zip(hi, fine_hi))
                if all(x <= y for x, y in zip(lower, upper)):
                    slices = tuple(slice(x-l, y-l+1) for x, y, l in zip(lower, upper, lo))
                    active[tuple(reversed(slices))] = False
            finite = np.isfinite(a) & np.isfinite(b)
            if not finite.all():
                if not allow_nonfinite_covered:
                    raise ValueError('nonfinite saved valid DDF')
                for classify in (np.isnan, np.isposinf, np.isneginf):
                    if not np.array_equal(classify(a), classify(b)):
                        raise ValueError('nonfinite patterns differ')
                if not finite[:, active].all():
                    raise ValueError('nonfinite active DDF')
                covered_nonfinite += int(np.count_nonzero(~finite))
            delta = np.zeros_like(a)
            np.subtract(a, b, out=delta, where=finite)
            if finite.any():
                maximum = max(maximum, float(np.max(np.abs(delta))))
            if active.any():
                active_maximum = max(active_maximum, float(np.max(np.abs(delta[:, active]))))
            values += a.size
        result.append(dict(level=lev, values=values, valid_linf=maximum,
                           active_linf=active_maximum, covered_nonfinite_components=covered_nonfinite,
                           valid_all_finite=covered_nonfinite == 0))
    return result


numerics = []
for row in rows:
    name = row['case']
    if not name.startswith(('stage_', 'reg_', 'prod_')):
        continue
    log = (root / name / 'run.log').read_text(errors='replace')
    reasons = []
    if int(row['exit_code']):
        reasons.append('nonzero exit')
    if '[FinalState]' not in log:
        reasons.append('missing final finite check')
    dim = int(re.search(r'_d([23])_', name)[1])
    highest = 2 if dim == 2 or '_deep_' in name or name.startswith('prod_') else 1
    if not production_only and f'AMR_LIFECYCLE MakeNewLevelFromCoarse level={highest}' not in log:
        reasons.append('requested finest level missing')
    stage = errors(log, 'osi_ab_stage:', 'linf')
    valid = errors(log, 'ddf_cell_norm_level:', 'valid_linf')
    active = errors(log, 'ddf_cell_norm_level:', 'active_linf')
    checkpoint = []
    if name.startswith('stage_'):
        for lev in range(highest + 1):
            for label in ('Initial', 'Collision', 'CommunicationGhost', 'Stream', 'BoundaryUncovered', 'Swap'):
                if not any(line.startswith('osi_ab_stage:') and
                           fields(line).get('lev') == str(lev) and fields(line).get('stage') == label
                           for line in log.splitlines()):
                    reasons.append(f'missing stage {lev}/{label}')
        if not stage or any(not math.isfinite(v) or v > 1e-12 for v in stage):
            reasons.append('stage pointwise error')
        sources = [fields(line) for line in log.splitlines() if line.startswith('osi_ab_stream_sources:')]
        if not sources:
            reasons.append('missing source diagnostics')
        for s in sources:
            if any(int(s[k]) for k in ('mismatch_valid', 'mismatch_internal_ghost', 'mismatch_interface', 'mismatch_covered')):
                reasons.append('nonphysical stream source mismatch')
                break
        if name.endswith('_candidate'):
            old_log = (root / name.replace('_candidate', '_baseline') / 'run.log').read_text(errors='replace')
            old = [fields(line) for line in old_log.splitlines() if line.startswith('osi_ab_stream_sources:')]
            if len(old) != len(sources):
                reasons.append('source record count changed')
            elif any(any(a.get(k) != b.get(k) for k in a if k != 'linf') or
                     abs(float(a['linf']) - float(b['linf'])) > 1e-12
                     for a, b in zip(old, sources)):
                reasons.append('baseline source diagnostics changed')
    else:
        for callback in (() if production_only else ('RemakeLevel', 'ClearLevel')):
            if f'AMR_LIFECYCLE {callback}' not in log:
                reasons.append('missing dynamic lifecycle ' + callback)
        if name.endswith('_candidate'):
            checkpoint_name = 'chk00000128' if production_only else 'chk00000192'
            before = root / name.replace('_candidate', '_baseline') / checkpoint_name
            after = root / name / checkpoint_name
            checkpoint = compare_checkpoints(before, after, allow_nonfinite_covered=production_only)
            levels = len(checkpoint)
            if production_only and levels != 3:
                reasons.append('production checkpoint must retain three levels')
            if not production_only and (len(valid) != levels or len(active) != levels):
                reasons.append('missing per-level checkpoint comparisons')
            # live valid 已经过额外 ComputeMacro/AverageDownValid，只检查其中 active 范围。
            if any(not math.isfinite(v) or v > 1e-12 for v in active):
                reasons.append('same-layout checkpoint error')
            if any(r['valid_linf'] > 1e-12 for r in checkpoint):
                reasons.append('saved valid checkpoint error')
            for key in (() if production_only else ('valid_l1', 'valid_l2', 'active_l1', 'active_l2')):
                values = errors(log, 'ddf_cell_norm_level:', key)
                if len(values) != levels or any(not math.isfinite(v) for v in values):
                    reasons.append('nonfinite or missing checkpoint norm ' + key)
    numerics.append(dict(case=name, passed=not reasons, reasons=sorted(set(reasons)),
                         stage_linf=max(stage, default=None), checkpoint=checkpoint,
                         valid_linf=max((r['valid_linf'] for r in checkpoint), default=None),
                         post_macro_live_valid_linf=max(valid, default=None),
                         active_linf=max(active + [r['active_linf'] for r in checkpoint], default=None)))


def timing(name):
    log = (root / name / 'run.log').read_text(errors='replace')
    windows = {}
    counts = []
    levels = {}
    for line in log.splitlines():
        if ' perf(s):' in line:
            step = int(re.match(r'step(\d+)', line)[1])
            if step <= 128:
                continue
            f = fields(line)
            if int(f['steps']) != 64:
                raise ValueError('unexpected window length: ' + name)
            record = windows.setdefault(step, {})
            for k in ('total', 'interp', 'average', 'collide', 'comm'):
                record[k] = max(record.get(k, 0), float(f[k]))
        elif ' collide_level:' in line:
            f = fields(line)
            step = int(re.match(r'step(\d+)', line)[1])
            lev = int(f['lev'])
            counts.append((step, lev, int(f['calls']), int(f['launch_cells'])))
            if step > 128:
                levels[(step, lev)] = max(levels.get((step, lev), 0), float(f['seconds']))
    if len(windows) != 3:
        raise ValueError('missing timing windows: ' + name)
    result = {k: sum(w[k] for w in windows.values()) for k in next(iter(windows.values()))}
    for lev in sorted({lev for _, lev in levels}):
        result[f'collide_L{lev}'] = sum(t for (_, level), t in levels.items() if level == lev)
    return result, sorted(counts)


perf = []
status = {r['case']: int(r['exit_code']) for r in rows}
for ranks in (1, 2):
    for storage in (0, 1):
        pairs = []
        for repeat in range(1, 5):
            names = [f'perf_r{ranks}_s{storage}_{repeat}_{v}' for v in ('baseline', 'candidate')]
            if any(n not in status or status[n] for n in names):
                continue
            a, ac = timing(names[0])
            b, bc = timing(names[1])
            if ac != bc:
                raise ValueError('collision workload changed: ' + str(names))
            pairs.append(dict(repeat=repeat, baseline=a, candidate=b,
                              changes_percent={k: 100 * (b[k] / a[k] - 1) for k in a}))
        perf.append(dict(ranks=ranks, storage=storage, pairs=pairs,
                         median_changes_percent={k: statistics.median(p['changes_percent'][k] for p in pairs)
                                                 for k in ('total', 'interp', 'average')} if pairs else {}))

units = []
for dim in (() if production_only else (2, 3)):
    name = f'unit_d{dim}'
    log = (root / name / 'run.log').read_text(errors='replace') if (root / name / 'run.log').exists() else ''
    units.append(dict(dimension=dim, passed=status.get(name, 1) == 0 and
                      f'PASS restriction GPU DIM={dim} cases=80 ' in log, log=log.strip()))
expected_cases = 4 if production_only else 108
report = dict(numerical_complete=len(numerics) == expected_cases,
              numerical_passed=len(numerics) == expected_cases and all(r['passed'] for r in numerics) and all(r['passed'] for r in units),
              validation_scope='finite active and finite valid values; identical covered nonfinite patterns' if production_only else 'finite saved valid and active values; stage and source diagnostics',
              gpu_units=units,
              max_stage_linf=max((r['stage_linf'] or 0 for r in numerics), default=0),
              max_valid_linf=max((r['valid_linf'] or 0 for r in numerics), default=0),
              max_active_linf=max((r['active_linf'] or 0 for r in numerics), default=0),
              valid_all_finite=all(r['valid_all_finite'] for row in numerics for r in row['checkpoint']),
              failures=[r for r in numerics if not r['passed']], numerics=numerics,
              performance=perf, timing_scope='step 129-320; maximum rank per window, then sum; four alternating pairs')
report['strict_full_valid_passed'] = report['numerical_passed'] and report['valid_all_finite']
(root / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
valid_label = '有限 valid 分量' if production_only else 'valid DDF'
lines = ['# AB/OSI 插值与界面平均融合验证', '',
         f"数值案例：{len(numerics)}/{expected_cases}；通过：{report['numerical_passed']}。", '',
         f"推进阶段最大 Linf：{report['max_stage_linf']:.10g}；同阶段保存的{valid_label}最大 Linf：{report['max_valid_linf']:.10g}。", '',
         '保存的 checkpoint 逐块按 valid 区比较；AB checkpoint 中的 ghost 不纳入 valid 范数。',
         'final_state_check 在写出 checkpoint 后还会刷新 covered 粗单元，其 live 全 valid 范数不能当作同阶段比较。', '']
if production_only:
    lines += [f"分区等价核对：{report['numerical_passed']}；严格全 valid 有限性验收：{report['strict_full_valid_passed']}。", '',
              '本组从固定三层 checkpoint 重启至 step 128，关闭 regrid，分别对照修改前后的 AB 和 OSI。', '',
              '本组 active 区必须有限；covered 区另逐点核对 NaN/+Inf/-Inf 类型及位置一致，有限部分按数值比较。',
              '若下表非有限计数非零，valid Linf 只指有限部分，不宣称完整 valid 数值正确。', '',
              '| 模式 | 层级 | valid DDF 分量数 | 有限 valid Linf | active Linf | covered 非有限分量 |', '|---|---:|---:|---:|---:|---:|']
    for row in numerics:
        if row['case'].endswith('_candidate'):
            mode = 'OSI' if '_s1_' in row['case'] else 'AB'
            lines += [f"| {mode} | {r['level']} | {r['values']} | {r['valid_linf']:.10g} | {r['active_linf']:.10g} | {r['covered_nonfinite_components']} |" for r in row['checkpoint']]
else:
    lines += ['动态 fixture 的最高层与最终层数不同；核对实际 MakeNew/Remake/Clear 日志及 report.json 中逐层 checkpoint 记录。', '',
              '数值矩阵中的两个 MPI rank 若只分配一张 GPU，则共享 GPU，仅证明该配置的 MPI/数值行为。', '',
              '性能：同节点、同一 step 64 checkpoint、固定 64³ 基网格与三层布局，关闭 regrid、输出和细分计时；',
              '每轮丢弃 step 65–128，比较 step 129–320。每窗口先取较慢 rank，再求和；下表为四轮配对变化中位数。', '',
              '| ranks | 模式 | 配对数 | interp 变化 | average 变化 | total 变化 |', '|---:|---|---:|---:|---:|---:|']
    for row in perf:
        if row['pairs']:
            d = row['median_changes_percent']
            lines.append(f"| {row['ranks']} | {'OSI' if row['storage'] else 'AB'} | {len(row['pairs'])} | {d['interp']:+.3f}% | {d['average']:+.3f}% | {d['total']:+.3f}% |")
    lines += ['', '各轮原始秒数、L0/L1/L2 碰撞耗时和工作量一致性记录见 report.json；未出现的 rank 配置未测量。',
              '该短测不证明生产 128³ 布局、动态重网格性能或物理精度。']
(root / 'report.md').write_text('\n'.join(lines) + '\n')
print(json.dumps({k: v for k, v in report.items() if k != 'numerics'}, ensure_ascii=False, indent=2))
sys.exit(0 if report['numerical_passed'] else 1)
