"""分开核对阶段误差、active DDF 和物理 ghost 诊断，并复现修改前的已知差异。"""
import csv
import json
import math
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
control_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root

def status_rows(path):
    return list(csv.DictReader((path / 'status.tsv').open(), delimiter='\t'))

def source_rows(log):
    fields = ('lev', 'phase', 'targets', 'sources', 'source_valid', 'source_ghost',
              'source_physical_ghost', 'source_internal_ghost', 'source_interface',
              'source_covered', 'mismatches', 'mismatch_valid', 'mismatch_ghost',
              'mismatch_physical_ghost', 'mismatch_internal_ghost', 'mismatch_interface',
              'mismatch_covered', 'linf')
    result = []
    for line in log.splitlines():
        if not line.startswith('osi_ab_stream_sources:'):
            continue
        values = dict(re.findall(r'(\w+)=([^\s]+)', line))
        record = {'stage': values['stage']}
        for field in fields:
            record[field] = float(values[field]) if field == 'linf' else int(values[field])
        result.append(record)
    return result

def same_sources(left, right):
    if len(left) != len(right):
        return False
    return all(all(a[k] == b[k] if k != 'linf' else
                   math.isfinite(a[k]) and math.isfinite(b[k]) and abs(a[k] - b[k]) <= 1e-12
                   for k in a) for a, b in zip(left, right))

rows = status_rows(root)
controls = {}
for row in status_rows(control_root):
    name = row['case']
    if control_root == root and not name.endswith('_control'):
        continue
    key = name.removesuffix('_control')
    controls[key] = (row, (control_root / name / 'run.log').read_text(errors='replace'))
results = []
control_results = []
for row in rows:
    name = row['case']
    if name.endswith('_control'):
        continue
    log = (root / name / 'run.log').read_text(errors='replace')
    reasons = []
    if int(row['exit_code']):
        reasons.append('nonzero exit: ' + row['exit_code'])
    if '[FinalState]' not in log:
        reasons.append('missing finite-state check')
    is_stage = bool(re.fullmatch(r'd[23]_i[012]_r[12]_(cavity|periodic)', name))
    callbacks = ('MakeNewLevelFromCoarse',) if is_stage else ('MakeNewLevelFromCoarse', 'RemakeLevel', 'ClearLevel')
    for callback in callbacks:
        if f'AMR_LIFECYCLE {callback}' not in log:
            reasons.append('missing lifecycle: ' + callback)
    stage_errors = []
    sources = []
    baseline_sources_match = None
    if is_stage:
        highest = 2 if name.startswith('d2_') else 1
        if f'AMR_LIFECYCLE MakeNewLevelFromCoarse level={highest}' not in log:
            reasons.append('requested finest level was not created')
        for stage in ('Initial', 'Collision', 'CommunicationGhost', 'Stream', 'BoundaryUncovered', 'Swap'):
            if f'stage={stage}' not in log:
                reasons.append('missing stage: ' + stage)
        stage_errors = [float(v) for v in re.findall(
            r'^osi_ab_(?:stage|level0_pointwise):[^\n]*?\blinf=(\S+)', log, re.M)]
        if not stage_errors or any(not math.isfinite(v) or v > 1e-12 for v in stage_errors):
            reasons.append('missing or failed stage pointwise checks')
        sources = source_rows(log)
        if not sources or any(not math.isfinite(v['linf']) for v in sources):
            reasons.append('missing or nonfinite source diagnostics')
        if any(v[k] for v in sources for k in ('mismatch_valid', 'mismatch_internal_ghost', 'mismatch_interface', 'mismatch_covered')):
            reasons.append('nonphysical source mismatch')
        if name.endswith('_cavity'):
            if name not in controls:
                reasons.append('missing baseline source control')
            else:
                old_row, old_log = controls[name]
                old_sources = source_rows(old_log)
                old_stage = [float(v) for v in re.findall(
                    r'^osi_ab_(?:stage|level0_pointwise):[^\n]*?\blinf=(\S+)', old_log, re.M)]
                baseline_sources_match = same_sources(sources, old_sources)
                control_ok = (int(old_row['exit_code']) == 0 and '[FinalState]' in old_log and
                              bool(old_stage) and all(math.isfinite(v) and v <= 1e-12 for v in old_stage) and
                              baseline_sources_match)
                control_results.append(dict(case=name, passed=control_ok,
                                            source_rows=len(old_sources), unchanged_source_metrics=baseline_sources_match))
                if not control_ok:
                    reasons.append('baseline source control failed or changed')
        elif any(v['mismatches'] for v in sources):
            reasons.append('periodic source mismatch')
    active_errors = []
    if '_current_s' in name:
        active_errors = [float(v) for v in re.findall(r'^ddf_cell_norm_level:.*?active_linf=(\S+)', log, re.M)]
        if not active_errors or any(not math.isfinite(v) or v > 1e-12 for v in active_errors):
            reasons.append('missing or failed baseline active DDF comparison')
    physical = [v['linf'] for v in sources if v['mismatch_physical_ghost']]
    results.append(dict(case=name, passed=not reasons, reasons=reasons,
                        stage_rows=len(stage_errors), stage_linf=max(stage_errors, default=None),
                        active_linf=max(active_errors, default=None),
                        physical_ghost_diagnostic_linf=max(physical, default=0),
                        physical_source_metrics_unchanged=baseline_sources_match))
complete = len(results) == 42 and len({r['case'] for r in results}) == 42 and len(control_results) == 12
report = dict(complete=complete,
              passed=complete and all(r['passed'] for r in results) and all(r['passed'] for r in control_results),
              scope='stage-specific pointwise and active-DDF regression; physical ghost diagnostics reported separately',
              cases=results, baseline_controls=control_results)
(root / 'acceptance.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(report, ensure_ascii=False, indent=2))
sys.exit(0 if report['passed'] else 1)
