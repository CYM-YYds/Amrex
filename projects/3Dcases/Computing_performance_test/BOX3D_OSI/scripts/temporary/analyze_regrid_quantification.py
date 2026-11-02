"""本次完整重构归因临时分析器；保留输入、日志与本脚本供复现。"""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys

root = Path(sys.argv[1])
rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
assert len(rows) == 14, rows
logs = {}
for row in rows:
    assert row['exit_code'] == '0', row
    log = (root / row['case'] / 'run.log').read_text()
    assert 'AMReX (26.06) finalized' in log, row
    logs[row['case']] = log

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))

for name in ['regression_detail', 'regression_probe']:
    log = logs[name]
    norms = re.findall(r'^ddf_cell_norm_level: (.*)$', log, re.M)
    assert len(norms) == 2, name
    for row in norms:
        values = fields(row)
        for key in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2']:
            assert float(values[key]) == 0, (name, values)
    before = root / 'regression_baseline/chk00000192'
    for source in before.glob('Level_*/f_old_D_*'):
        target = root / name / 'chk00000192' / source.relative_to(before)
        assert hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest(), target

totals, regrids, profiles, checks, updates = {}, {}, {}, {}, []
for name, log in logs.items():
    if name.startswith('regression_'):
        continue
    windows = [fields(l) for l in log.splitlines() if ' perf(s):' in l]
    assert len(windows) == 4 and all(int(w['steps']) == 160 for w in windows), name
    totals[name] = sum(float(w['total']) for w in windows[1:])
    regrids[name] = sum(float(v) / 1000 for v in re.findall(r'regrid_time: (\S+) ms', log)[1:])
    updates.append([float(w['MLUPS_total']) * float(w['total']) for w in windows])
    checks[name] = sum(float(v) for v in re.findall(r'^benchmark_legacy_check .*seconds=(\S+)', log, re.M)[5:])
    if name.startswith('profile_'):
        windows = [fields(l) for l in log.splitlines() if ' perf_detail(s):' in l][1:]
        sums = {k: sum(float(w[k]) for w in windows) for k in windows[0] if k != 'regrid_detail_timing'}
        groups = {
            'full_average': sums['regrid_average_valid'] - sums['regrid_repair'],
            'repair': sums['regrid_repair'],
            'macro': sums['regrid_macro'], 'macro_ghost': sums['regrid_macro_ghost'],
            'vorticity': sums['regrid_vorticity'], 'tag': sums['regrid_tag'],
            'error_est_other': sums['regrid_error_est'] - sum(sums[k] for k in ['regrid_macro', 'regrid_macro_ghost', 'regrid_vorticity', 'regrid_tag']),
            'remake': sums['regrid_remake'], 'new_level': sums['regrid_new_level'],
            'clear_level': sums['regrid_clear_level'],
            'layout_planning': sums['regrid_amrex'] - sum(sums[k] for k in ['regrid_error_est', 'regrid_remake', 'regrid_new_level', 'regrid_clear_level']),
            'caches': sums['regrid_cache_build'],
        }
        groups['outer_other'] = regrids[name] - sum(groups.values())
        assert all(v >= -1e-4 for v in groups.values()), (name, groups)
        profiles[name] = dict(raw=sums, exclusive_s=groups, regrid_s=regrids[name], total_s=totals[name])
assert all(all(math.isclose(a, b, rel_tol=1e-8) for a, b in zip(v, updates[0])) for v in updates), 'workload changed'
pairs = []
for i in range(0, 8, 2):
    keys = [k for k in totals if k.startswith(f'legacy_{i}_') or k.startswith(f'legacy_{i+1}_')]
    pair = {k.rsplit('_', 1)[1]: k for k in keys}
    off, on = pair['false'], pair['true']
    pairs.append(dict(off=totals[off], on=totals[on], extra_s=totals[on] - totals[off],
                      extra_pct=100 * (totals[on] / totals[off] - 1),
                      off_regrid=regrids[off], on_regrid=regrids[on], measured_check_s=checks[on]))
mean_groups = {k: statistics.mean(v['exclusive_s'][k] for v in profiles.values()) for k in next(iter(profiles.values()))['exclusive_s']}
report = dict(runs=len(rows), totals_s=totals, regrid_s=regrids, legacy_pairs=pairs,
              median_legacy_extra_pct=statistics.median(v['extra_pct'] for v in pairs),
              median_legacy_extra_s=statistics.median(v['extra_s'] for v in pairs),
              profiles=profiles, mean_exclusive_s=mean_groups)
(root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
(root / 'report.md').write_text('完整阶段计时及独立旧检查回放；嵌套计时相减后归一化，范围见实际命令。\n\n```json\n' + json.dumps(report, indent=2) + '\n```\n')
print(json.dumps(report, indent=2))
