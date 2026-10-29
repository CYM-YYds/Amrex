#!/usr/bin/env python3
"""本次同步/无mask独立实验的临时汇总；复用上一轮的计时解析和rank聚合。"""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
from analyze_uniform_dispatch import fields, timing


def validate(root):
    rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
    checks = [r for r in rows if not r['case'].startswith('perf_')]
    assert len(checks) == 28, len(checks)
    comparisons = 0
    for row in checks:
        folder = root / row['case']
        assert row['exit_code'] == '0', row
        status = json.loads((folder / 'exit_status.json').read_text())
        assert status['exit_code'] == 0 and status['reason'] == 'launcher_exit', row
        for rank in range(int(row['ranks'])):
            assert json.loads((folder / f'rank_exit_{rank}.json').read_text())['exit_code'] == 0, row
        log = (folder / 'run.log').read_text()
        assert 'AMReX (26.06) finalized' in log and '[FinalState]' in log, row
        if row['case'].startswith('hooks_'):
            assert 'step=16 stage=StepEntryUncovered' in log, row
            assert not re.search(r'mismatches=[1-9]|(?:^|\s)finite=0', log), row
        elif not row['case'].endswith('_baseline'):
            norms = re.findall(r'^ddf_cell_norm_level: (.+)$', log, re.M)
            assert len(norms) == 1, row
            for key in ('valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2'):
                value = float(fields(norms[0])[key])
                assert math.isfinite(value) and value == 0, (row, key, value)
            assert 'ddf_norm_end' in log
            comparisons += 1
    assert comparisons == 18
    return {'runs': 28, 'checkpoint_comparisons': 18, 'zero_norms': True,
            'scope': '3D FLOAT; 64^3; 1/2 ranks; AB/OSI; cavity/periodic; two locksteps and two restarts'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--checks-only', action='store_true')
    args = parser.parse_args()
    root = args.root
    checks = validate(root)
    (root / 'checks.json').write_text(json.dumps(checks, indent=2) + '\n')
    print(json.dumps(checks), flush=True)
    if args.checks_only:
        return
    groups = {}
    for folder in sorted(root.glob('perf_*')):
        m = re.fullmatch(r'perf_n(\d+)_b(\d+)_r(\d+)_p(\d+)_(baseline|sync|nomask)', folder.name)
        assert m, folder
        n, box, ranks, block = map(int, m.groups()[:4])
        status = json.loads((folder / 'exit_status.json').read_text())
        assert status['exit_code'] == 0 and status['reason'] == 'launcher_exit', folder
        groups.setdefault((n, box, ranks), {}).setdefault(block, {})[m[5]] = timing(folder, ranks)
    assert len(groups) == 4
    summary = []
    lines = ['# 同步与无mask分别测试', '',
             '3D FLOAT D3Q27，OSI，非周期；固定单层，regrid=0，细分计时关闭。',
             '同节点六种执行顺序各一次；每组1200步，统计201–1200步。',
             '每窗取最慢rank的total再累加；候选分别与同一块的baseline对照，不叠加。',
             'sync的comm/stream/boundary/swap未测量；端到端时间含每步末尾GPU等待。', '',
             '| N | Box | ranks | 候选 | 耗时变化中位数 | 六对范围 | 每步节省中位数 |',
             '|---|---|---|---|---|---|---|']
    for config, blocks in sorted(groups.items()):
        assert set(blocks) == set(range(1, 7))
        for variant in ('sync', 'nomask'):
            deltas, saved, seconds = [], [], []
            for block in range(1, 7):
                variants = blocks[block]
                assert set(variants) == {'baseline', 'sync', 'nomask'}
                base, candidate = variants['baseline'], variants[variant]
                assert base['work'] == candidate['work'], (config, block, variant)
                a, b = base['total'], candidate['total']
                deltas.append((b/a - 1)*100)
                saved.append(a-b)  # 1000步的节省秒数与ms/step数值相同。
                seconds.append({'block': block, 'baseline': a, 'candidate': b})
            row = dict(zip(('cells', 'box', 'ranks'), config)) | {
                'variant': variant, 'change_pct': deltas, 'seconds': seconds,
                'median_change_pct': statistics.median(deltas),
                'range_change_pct': [min(deltas), max(deltas)],
                'median_saved_ms_per_step': statistics.median(saved)}
            summary.append(row)
            lines.append(f"| {config[0]} | {config[1]} | {config[2]} | {variant} | "
                         f"{row['median_change_pct']:+.3f}% | {min(deltas):+.3f}% 至 {max(deltas):+.3f}% | "
                         f"{row['median_saved_ms_per_step']:+.6f} ms |")
    result = {'checks': checks, 'timing': summary, 'node': (root / 'node.txt').read_text().strip()}
    (root / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    lines += ['', '负变化表示候选更快；跨零范围不据此宣称稳定收益。',
              '不代表AMR、二维、长期物理精度、其他GPU或多节点的验证。']
    (root / 'report.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
