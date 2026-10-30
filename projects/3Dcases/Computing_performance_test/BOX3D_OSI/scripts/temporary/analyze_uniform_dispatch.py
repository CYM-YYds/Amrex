#!/usr/bin/env python3
"""本次单层调度实验的临时汇总；随结果保留，用于复核逐值检查与配对计时。"""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def validate(root):
    rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
    checks = [row for row in rows if not row['case'].startswith('perf_')]
    assert len(checks) == 21, len(checks)
    for row in checks:
        name = row['case']
        folder = root / name
        log = (folder / 'run.log').read_text()
        assert row['exit_code'] == '0', row
        status = json.loads((folder / 'exit_status.json').read_text())
        assert status['exit_code'] == 0 and status['reason'] == 'launcher_exit', name
        assert 'AMReX (26.06) finalized' in log and '[FinalState]' in log, name
        for rank in range(int(row['ranks'])):
            marker = json.loads((folder / f'rank_exit_{rank}.json').read_text())
            assert marker['exit_code'] == 0, (name, rank)
        if name.startswith('new_') or name == 'restart_check':
            norms = re.findall(r'^ddf_cell_norm_level: (.+)$', log, re.M)
            assert len(norms) == (2 if name == 'new_amr' else 1), (name, norms)
            for line in norms:
                values = fields(line)
                for key in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2']:
                    assert math.isfinite(float(values[key])) and float(values[key]) == 0, (name, key, values)
            assert 'ddf_norm_end' in log, name
            assert f'[Advance] path={"amr" if name == "new_amr" else "uniform"}' in log, name
        if name.endswith('_amr'):
            assert 'AMR_LIFECYCLE MakeNewLevelFromCoarse level=1' in log, name
    hooks = (root / 'hooks/run.log').read_text()
    assert 'step=16 stage=StepEntryUncovered' in hooks
    assert not re.search(r'mismatches=[1-9]|(?:^|\s)finite=0', hooks)
    for step in [32, 64]:
        assert (root / f'restart_seed/chk{step:08d}/Header').is_file()
        assert (root / f'restart_seed/plt_{step:06d}/Header').is_file()
    return {'runs': len(checks), 'checkpoint_comparisons': 10, 'zero_norms': True,
            'hooks': '16 seeded periodic steps, 2 ranks, OSI/AB lockstep',
            'amr': 'level 1 created at step 32; old/new checkpoint comparison at step 40'}


def timing(folder, ranks):
    log = (folder / 'run.log').read_text()
    assert 'AMReX (26.06) finalized' in log, folder
    perf = {}
    for step, line in re.findall(r'^step(\d+) perf\(s\): (.+)$', log, re.M):
        perf.setdefault(int(step), []).append(fields(line))
    assert set(perf) == set(range(200, 1201, 200)), (folder, perf.keys())
    total = solver = 0.0
    for step, rows in perf.items():
        assert len(rows) == ranks, (folder, step, rows)
        assert all(row['steps'] == '200' for row in rows), folder
        if step > 200:
            total += max(float(row['total']) for row in rows)
            solver += max(float(row['solv']) for row in rows)
    # 每个 rank 的每个窗口都必须具有相同的工作量，忽略首窗后仍检查全部计数。
    work = sorted(tuple(sorted(fields(line).items())) for line in
                  re.findall(r'^step\d+ collide_level: (.+)$', log, re.M)
                  if line)
    # seconds 和 launch_MLUPS 是测量值；仅比较计数与层号。
    work = sorted(tuple((key, value) for key, value in row
                        if key in ('lev', 'calls', 'launch_cells')) for row in work)
    assert len(work) == 6 * ranks, (folder, len(work))
    return {'total': total, 'solver': solver, 'work': work}


def analyze_sampling(root):
    """本轮事件采样验证：检查完成、逐值对照、窗口归属、CPU分账及开销。"""
    statuses = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
    comparisons = 0
    for status in statuses:
        folder = root / status['case']
        assert status['exit_code'] == '0', status
        assert json.loads((folder / 'exit_status.json').read_text())['exit_code'] == 0
        log = (folder / 'run.log').read_text()
        assert 'AMReX (26.06) finalized' in log
        norms = re.findall(r'^ddf_cell_norm_level: (.+)$', log, re.M)
        for line in norms:
            d = fields(line)
            for key in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2']:
                assert float(d[key]) == 0 and math.isfinite(float(d[key])), (folder, d)
        comparisons += bool(norms)
        gpu = [fields(l) for l in log.splitlines() if 'uniform_stage_gpu(ms/step):' in l]
        cpu = [fields(l) for l in log.splitlines() if 'uniform_stage_cpu(ms/step):' in l]
        assert len(gpu) == len(cpu)
        for g, h in zip(gpu, cpu):
            assert g['samples'] == h['samples']
            for values in [g, h]:
                assert all(math.isfinite(float(v)) and float(v) >= 0 for v in values.values())
            # 输出为ms平均值，分项和与step允许浮点打印的舍入误差。
            exclusive = sum(float(h[k]) for k in ['collide_submit', 'collide_finalize', 'comm',
                'phase_submit', 'boundary_submit', 'step_wait', 'other'])
            assert math.isclose(exclusive, float(h['step']), rel_tol=1e-4, abs_tol=1e-5), h
            if int(g['samples']):
                assert all(float(g[k]) > 0 for k in ['collide_sum', 'phase']), g
        # 支持路径每步采样：每窗样本数等于该窗实际推进步数。
        if status['case'] in ['interval7', 'restart_sample']:
            expected = [7]*9 if status['case'] == 'interval7' else [6] + [7]*8
            assert [int(g['samples']) for g in gpu] == expected, gpu
            reported = [int(step) for step in re.findall(r'^step(\d+) uniform_stage_gpu', log, re.M)]
            assert reported == (list(range(7, 64, 7)) if status['case'] == 'interval7' else list(range(70, 127, 7))), reported
        if status['case'].endswith('_on') and 'r2' not in status['case'] and not status['case'].startswith('amr'):
            assert [int(g['samples']) for g in gpu] == [32, 32], gpu
        if 'r2' in status['case'] or status['case'] in ['amr_on', 'hooks_sample']:
            if status['case'].endswith('_off'):
                assert not gpu
            else:
                assert gpu and all(g['eligible'] == '0' and g['samples'] == '0' for g in gpu)
    assert comparisons == 5, comparisons
    assert 'AMR_LIFECYCLE MakeNewLevelFromCoarse level=1' in (root / 'amr_on/run.log').read_text()
    hooks = (root / 'hooks_sample/run.log').read_text()
    assert 'stage=StepEntryUncovered' in hooks
    assert not re.search(r'mismatches=[1-9]|(?:^|\s)finite=0', hooks)
    results = []
    for cells, box in [(128, 128), (128, 32), (256, 256)]:
        deltas = []
        measurements = []
        for pair in range(1, 6):
            off_result = timing(root / f'perf_n{cells}_b{box}_p{pair}_base', 1)
            on_result = timing(root / f'perf_n{cells}_b{box}_p{pair}_new', 1)
            assert off_result['work'] == on_result['work']
            off, on = off_result['total'], on_result['total']
            deltas.append((on/off-1)*100)
            log = (root / f'perf_n{cells}_b{box}_p{pair}_new/run.log').read_text()
            # 排除首200步；各行已是窗口每步平均ms，按样本数加权合并为201–1200步平均。
            for kind in ['gpu', 'cpu']:
                rows = [fields(line) for step, line in re.findall(
                    rf'^step(\d+) uniform_stage_{kind}\(ms/step\): (.+)$', log, re.M) if int(step) > 200]
                count = sum(int(row['samples']) for row in rows)
                assert count == 1000, count
                measurements.append({'pair': pair, 'kind': kind, 'samples': count,
                    'ms_per_step': {k: sum(float(row[k])*int(row['samples']) for row in rows)/count
                     for k in rows[0] if k not in ['rank', 'eligible', 'samples']}})
        results.append({'cells': cells, 'box': box, 'change_pct': deltas,
                        'median_change_pct': statistics.median(deltas),
                        'range_change_pct': [min(deltas), max(deltas)], 'measurements': measurements})
    summary = {'runs': len(statuses), 'checkpoint_comparisons': comparisons,
               'zero_norms': True, 'node': (root/'node.txt').read_text().strip(),
               'timing': results, 'scope': 'steps 201-1200; 5 alternating pairs; same node; every-step sampling vs frozen no-sampling executable; report_int=200'}
    (root/'sampling_summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--checks-only', action='store_true')
    parser.add_argument("--sampling", action="store_true")
    args = parser.parse_args()
    if args.sampling:
        analyze_sampling(args.root)
        return
    root = args.root
    checks = validate(root)
    (root / 'checks.json').write_text(json.dumps(checks, indent=2) + '\n')
    print(json.dumps(checks))
    if args.checks_only:
        return
    groups = {}
    for folder in sorted(root.glob('perf_*')):
        match = re.fullmatch(r'perf_n(\d+)_b(\d+)_r(\d+)_g(\d+)_p(\d+)_v([01])', folder.name)
        assert match, folder
        cells, box, ranks, regrid, pair, enabled = map(int, match.groups())
        groups.setdefault((cells, box, ranks, regrid), {}).setdefault(pair, {})[enabled] = timing(folder, ranks)
    assert len(groups) == 5, groups.keys()
    summary = []
    for config, pairs in groups.items():
        assert set(pairs) == set(range(1, 6)), config
        changes = []
        baseline = []
        candidate = []
        for pair, variants in sorted(pairs.items()):
            assert set(variants) == {0, 1}, (config, pair)
            assert variants[0]['work'] == variants[1]['work'], (config, pair)
            a, b = variants[0]['total'], variants[1]['total']
            baseline.append(a)
            candidate.append(b)
            changes.append((b / a - 1) * 100)
        summary.append(dict(zip(('cells', 'box', 'ranks', 'regrid_int'), config)) | {
            'baseline_seconds': baseline, 'candidate_seconds': candidate,
            'paired_change_pct': changes, 'median_change_pct': statistics.median(changes),
            'median_saved_ms_per_step': statistics.median((a-b) for a,b in zip(baseline,candidate)),
            'range_change_pct': [min(changes), max(changes)]})
    result = {'checks': checks, 'timing': summary,
              'metric': 'sum of per-window slowest-rank total; steps 201-1200; 5 alternating pairs',
              'node': (root / 'node.txt').read_text().strip()}
    (root / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    lines = ['# 单层调度验证与性能', '',
             f"节点：{result['node']}；三维 FLOAT D3Q27，固定单层，非周期，OSI，嵌套细分计时关闭。",
             '', '数值检查21组；10次checkpoint对照的各层valid/active差异范数均为0。',
             '性能采用同程序开关，5组交替AB/BA；排除首200步，统计201–1200步。',
             '多rank每窗取较慢rank的total，随后累加；负变化表示候选耗时下降。', '',
             '| N | Box | ranks | regrid | 配对变化中位数 | 配对范围 | 每步节省中位数 |',
             '|---|---|---|---|---|---|---|']
    for row in summary:
        low, high = row['range_change_pct']
        lines.append(f"| {row['cells']} | {row['box']} | {row['ranks']} | {row['regrid_int']} | "
                     f"{row['median_change_pct']:+.3f}% | {low:+.3f}% 至 {high:+.3f}% | "
                     f"{row['median_saved_ms_per_step']:+.6f} ms |")
    lines += ['', '每步节省按1000个统计步数折算；表内ms/step数值与整段节省秒数数值相同。',
              '本轮仅验证调度变更，不代表长程物理精度、跨节点或其他GPU上的性能。']
    (root / 'report.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
