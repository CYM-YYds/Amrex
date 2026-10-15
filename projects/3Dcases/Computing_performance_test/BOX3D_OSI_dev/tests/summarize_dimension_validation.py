#!/usr/bin/env python3
"""核对维度回归的退出状态、逐值误差和实际 AMR 生命周期证据。"""
import csv
import json
import math
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
results = []
for row in csv.DictReader((root / 'status.tsv').open(), delimiter='\t'):
    name, status = row['case'], int(row['exit_code'])
    log = (root / name / 'run.log').read_text(errors='replace')
    reasons = []
    if name.startswith('reject_'):
        expected = 'Legacy checkpoint' if 'legacy' in name else 'Lattice mismatch'
        if status == 0 or expected not in log:
            reasons.append('missing expected checkpoint rejection')
    else:
        if status:
            reasons.append(f'exit={status}')
        if not name.startswith('d3_baseline_') and '[FinalState]' not in log:
            reasons.append('missing finite-state check')
        if name.startswith(('d2_resume_', 'd3_current_', 'd3_legacy_restart', 'd2_production_compare')):
            norms = re.findall(r'^ddf_cell_norm_level:.*?active_linf=(\S+)', log, re.M)
            if not norms or any(not math.isfinite(float(v)) or float(v) > 1.e-12 for v in norms):
                reasons.append('missing or failed active DDF checkpoint comparison')
        if name == 'd2_output_smoke':
            header = root / name / 'plt_000001' / 'Header'
            lines = header.read_text().splitlines() if header.exists() else []
            if len(lines) < 5 or lines[1:5] != ['2', 'ux', 'uy', '2']:
                reasons.append('invalid 2D PlotFile header')
        match = re.fullmatch(r'd2_l(\d)_i\d_r\d_\w+', name)
        if match:
            level = int(match[1])
            for stage in ['Initial', 'Collision', 'CommunicationGhost', 'Stream', 'BoundaryUncovered', 'Swap']:
                if f'stage={stage}' not in log:
                    reasons.append(f'missing stage {stage}')
            if level:
                for callback in ['MakeNewLevelFromCoarse', 'RemakeLevel', 'ClearLevel']:
                    if f'AMR_LIFECYCLE {callback}' not in log:
                        reasons.append(f'missing callback {callback}')
                if f'AMR_LIFECYCLE MakeNewLevelFromCoarse level={level}' not in log:
                    reasons.append('requested finest level was not created')
    results.append({'case': name, 'passed': not reasons, 'reasons': reasons})
report = {'run_dir': str(root), 'passed': sum(r['passed'] for r in results),
          'total': len(results), 'expected_total': 66,
          'complete': len(results) == 66 and len({r['case'] for r in results}) == 66,
          'cases': results}
(root / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(f"DIMENSION_VALIDATION passed={report['passed']} total={report['total']} run_dir={root}")
for row in results:
    if not row['passed']:
        print(row['case'] + ': ' + '; '.join(row['reasons']))
if not report['complete']:
    print('FAIL incomplete validation matrix: expected 66 distinct cases')
sys.exit(0 if report['complete'] and report['passed'] == report['total'] else 1)
