"""临时精度回归汇总；保留用于复现 precision_switch 报告。"""
import csv
import json
import math
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
assert len(rows) == 15, len(rows)
assert all(int(row['exit_code']) == 0 for row in rows), rows
report = {'job': root.name, 'cases': len(rows), 'modes': []}
for mode in range(3):
    logs = {kind: (root / f'{kind}_i{mode}' / 'run.log').read_text()
            for kind in ('baseline', 'double', 'float', 'compare', 'float_stage')}
    for kind, log in logs.items():
        states = re.findall(r'^\[FinalState\].*$', log, re.M)
        assert states, (mode, kind)
        for state in states:
            values = [float(v) for v in re.findall(r'=([^\s]+)', state)]
            assert all(math.isfinite(v) for v in values), state
    def norm(log):
        line = re.search(r'^ddf_norm_global: (.*)$', log, re.M)
        assert line
        result = {k: float(v) for k, v in re.findall(r'(\w+)=([^\s]+)', line[1])}
        assert all(math.isfinite(v) for v in result.values()), result
        return result
    before_after = norm(logs['double'])
    assert before_after['linf'] == before_after['l2'] == 0, before_after
    stages = re.findall(r'^osi_ab_stage: stage=(\S+).*?linf=(\S+)', logs['float_stage'], re.M)
    assert stages and all(math.isfinite(float(e)) and float(e) == 0 for _, e in stages)
    assert {'Collision', 'CommunicationGhost', 'Stream', 'BoundaryUncovered', 'Swap'} <= {s for s, _ in stages}
    for kind, precision in [('double', 'DOUBLE'), ('float', 'FLOAT')]:
        assert f'[Precision] Real={precision}' in logs[kind]
    # 核对初始化创建细层的证据；不将该 fixture 扩大为任意 regrid 生命周期验收。
    assert 'AMR_LIFECYCLE MakeNewLevelFromCoarse level=1' in logs['float']
    report['modes'].append({'interpolation': mode, 'double_baseline_active_ddf': before_after,
                            'float_vs_double_active_ddf': norm(logs['compare']),
                            'float_periodic_stage_checks': len(stages), 'stage_linf': 0})
(root / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
