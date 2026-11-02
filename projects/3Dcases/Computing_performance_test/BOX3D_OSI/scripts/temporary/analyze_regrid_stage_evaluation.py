"""重构内部成本临时分析器；保留作为本任务复现入口，无依赖后可删除。"""
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
assert len(rows) == 11, rows
logs = {}
for row in rows:
    assert row['exit_code'] == '0', row
    log = (root / row['case'] / 'run.log').read_text()
    assert 'AMReX (26.06) finalized' in log, row
    logs[row['case']] = log

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))

regression = {}
for name in ('regression_detail', 'regression_off'):
    norms = re.findall(r'^ddf_cell_norm_level: (.*)$', logs[name], re.M)
    assert len(norms) == 2, name
    for row in norms:
        values = fields(row)
        for key in ('valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2'):
            assert float(values[key]) == 0, (name, values)
    source = root / 'regression_baseline/chk00000192'
    count = 0
    for p in source.glob('Level_*/f_old_D_*'):
        q = root / name / 'chk00000192' / p.relative_to(source)
        assert hashlib.sha256(p.read_bytes()).digest() == hashlib.sha256(q.read_bytes()).digest(), q
        count += 1
    assert count > 0
    regression[name] = {'norms': norms, 'identical_ddf_files': count}

runs = {}
updates = []
for name, log in logs.items():
    if not name.startswith('timing_'):
        continue
    windows = [fields(l) for l in log.splitlines() if ' perf(s):' in l]
    assert len(windows) == 4 and all(int(w['steps']) == 160 for w in windows)
    updates.append([float(w['MLUPS_total']) * float(w['total']) for w in windows])
    details = [fields(l) for l in log.splitlines() if ' perf_detail(s):' in l][1:]
    sums = {k: sum(float(w[k]) for w in details) for k in details[0] if k != 'regrid_detail_timing'}
    runs[name] = {'total_s':sum(float(w['total']) for w in windows[1:]),
                  'regrid_s':sum(float(x) / 1000 for x in re.findall(r'regrid_time: (\S+) ms', log)[1:]),
                  'per_regrid_ms':{k:v / 15 * 1000 for k,v in sums.items()},
                  'counts':[fields(l) for l in log.splitlines() if 'perf_count:' in l][1:],
                  'levels':[{k:v for k,v in fields(l).items() if k in ('lev','calls','launch_cells')} for l in log.splitlines() if 'collide_level:' in l]}
assert all(all(math.isclose(a,b,rel_tol=1e-8) for a,b in zip(v,updates[0])) for v in updates)
assert all(v['levels'] == next(iter(runs.values()))['levels'] and v['counts'] == next(iter(runs.values()))['counts'] for v in runs.values()), 'level work or counts differ'
on = [v for k,v in runs.items() if k.endswith('_true')]
off = [v for k,v in runs.items() if k.endswith('_false')]
means = {k:statistics.mean(v['per_regrid_ms'][k] for v in on) for k in on[0]['per_regrid_ms']}
means['eval_avg_unassigned'] = means['regrid_average_valid'] - means['regrid_repair'] - sum(means[k] for k in ('eval_avg_alloc','eval_avg_tags','eval_avg_kernel','eval_avg_plan','eval_avg_copy'))
means['eval_remake_unassigned'] = means['regrid_remake'] - sum(v for k,v in means.items() if k.startswith('eval_remake_'))
result = {'regression':regression,'runs':runs,'workload_updates_match':True,'mean_profile_ms':means,
          'mean_off_total_s':statistics.mean(v['total_s'] for v in off),'mean_on_total_s':statistics.mean(v['total_s'] for v in on)}
(root/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('runs','regression')},indent=2))
