# 本次实验的日志审计与计时汇总；保留为结果的复现入口。
import json
import re
import sys
from pathlib import Path
root = Path(sys.argv[1])
results = {}
for folder in sorted(root.iterdir()):
    if not folder.is_dir() or not (folder / 'run.log').exists():
        continue
    text = (folder / 'run.log').read_text()
    row = {'finalized': 'AMReX (26.06) finalized' in text}
    if folder.name.startswith('check_'):
        sources = re.findall(r'osi_ab_stream_sources:.*', text)
        stages = re.findall(r'osi_ab_stage:.*', text)
        row.update(source_checks=len(sources), stage_checks=len(stages),
                   internal_mismatches=sum(int(x) for line in sources for x in re.findall(r'\bmismatch_internal_ghost=(\d+)', line)),
                   physical_mismatches=sum(int(x) for line in sources for x in re.findall(r'\bmismatch_physical_ghost=(\d+)', line)),
                   source_mismatches=sum(int(x) for line in sources for x in re.findall(r'\bmismatches=(\d+)', line)),
                   max_stage_linf=max((float(x) for line in stages for x in re.findall(r'\blinf=([^ ]+)', line)), default=None),
                   regrids=text.count('REFINEMENT_REGRID'))
    row['ddf_norm'] = re.findall(r'(?:ddf_norm_global|ddf_cell_norm_level):.*', text)
    row['work_tables'] = re.findall(r'COLLISION_WORK.*', text)
    if folder.name.startswith('timing_'):
        row['windows'] = []
        for line in text.splitlines():
            if ' perf(s):' in line:
                fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
                row['windows'].append({k: float(fields[k]) for k in ['collide','comm','total','MLUPS_total']})
        row['collision_levels'] = re.findall(r'collide_level:.*', text)
    results[folder.name] = row
# 每次运行跳过首窗口，避免将首次工作表构建混入稳态比较。
summary = {}
for mode in ['0','1','2','off','on']:
    prefix = f'timing_b{mode}_' if mode.isdigit() else f'timing_{mode}_'
    runs = [v for k, v in results.items() if k.startswith(prefix) and v['finalized']]
    if not runs:
        continue
    rows = [w for run in runs for w in run['windows'][1:]]
    summary[str(mode)] = {
        'runs': len(runs), 'steady_windows': len(rows),
        'mean': {f: sum(w[f] for w in rows)/len(rows) for f in ['total','collide','comm','MLUPS_total']},
        'total_range': [min(w['total'] for w in rows), max(w['total'] for w in rows)],
        'run_mean_total': [sum(w['total'] for w in run['windows'][1:])/len(run['windows'][1:]) for run in runs]
    }
    levels = {}
    for lev in range(3):
        samples = [line for run in runs for line in
                   [x for x in run['collision_levels'] if f'lev={lev} ' in x][1:]]
        if samples:
            levels[str(lev)] = {
                'mean_seconds': sum(float(re.search(r'seconds=([^ ]+)', line)[1]) for line in samples)/len(samples),
                'calls': sorted(set(int(re.search(r'calls=(\d+)', line)[1]) for line in samples)),
                'launch_cells': sorted(set(int(re.search(r'launch_cells=(\d+)', line)[1]) for line in samples))
            }
    summary[str(mode)]['levels'] = levels
(root / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
(root / 'audit.json').write_text(json.dumps(results, indent=2)+'\n')
print(json.dumps(results, indent=2))
# 可选第二参数是 cuobjdump --dump-sass 输出；仅统计静态指令，不推断动态瓶颈。
if len(sys.argv) > 2:
    from collections import Counter
    sass = Path(sys.argv[2])
    counts = []
    for block in sass.read_text().split('Function : ')[1:]:
        name = block.splitlines()[0]
        if 'Collide' not in name:
            continue
        ops = re.findall(r'/\*[0-9a-f]+\*/\s+(?:@!?P\d+\s+)?([A-Z][A-Z0-9.]*)', block)
        counts.append({'name': name, 'instructions': len(ops), 'opcodes': dict(Counter(ops))})
    sass.with_name('instruction_counts.json').write_text(json.dumps(counts, indent=2)+'\n')
