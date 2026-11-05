#!/usr/bin/env python3
"""624717耗时饼图一次性分析；作为图表唯一复现入口保留，图表不再需要时可删除。"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

CASE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(CASE / 'data_post_processing'))
from plot_run_performance import parse_log, parse_values, PHASES
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
windows = parse_log(args.log)
assert [w.step for w in windows] == list(range(3200, 128001, 3200))
rows = [parse_values(line) for line in args.log.read_text().splitlines() if ' perf(s):' in line]
assert len(rows) == 40 and all(r['steps'] == 3200 for r in rows)
totals = {key: sum(w.phase[key] for w in windows) for key in PHASES}
totals['regrid'] = sum(w.regrid for w in windows)
keys = ['collide', 'boundary', 'interp', 'regrid', 'average', 'comm', 'stream', 'swap']
names = ['碰撞', '边界处理', '插值', '网格重构', '平均下传', '通信', '迁移', '数组交换']
items = [(key, name, totals[key]) for key, name in zip(keys, names) if totals[key] > 0]
denominator = sum(totals.values())
total = sum(r['total'] for r in rows)
font = '/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc'
font_manager.fontManager.addfont(font)
plt.rcParams.update({'font.family': font_manager.FontProperties(fname=font).get_name(), 'font.size': 12})
fig, ax = plt.subplots(figsize=(11, 6.5))
fig.subplots_adjust(left=.03, right=.65, top=.85, bottom=.18)
colors = ['#4C78A8', '#F58518', '#54A24B', '#B279A2', '#E4B64B', '#72B7B2', '#E45756', '#999999']
wedges, _, _ = ax.pie([v for _, _, v in items], startangle=90, counterclock=False,
    colors=colors[:len(items)], autopct=lambda p: f'{p:.2f}%' if p >= 5 else '',
    pctdistance=.73, wedgeprops={'edgecolor': 'white', 'linewidth': 1.5}, textprops={'fontsize': 12})
ax.legend(wedges, [f'{name}  {v:,.2f} 秒（{v / denominator:.2%}）' for _, name, v in items],
    loc='center left', bbox_to_anchor=(1.0, .5), frameon=False, labelspacing=1.25)
fig.suptitle('job624717 · OSI 耗时占比', fontsize=21, fontweight='bold', y=.97)
fig.text(.5, .89, f'1–128000 步 · t = 0–1000 · 分项累计 {denominator:,.2f} 秒', ha='center')
fig.text(.05, .10, '数据：runs/a5_C_t1000_gif/job624717/C/run.log；40 × 3200 步计时窗口。', fontsize=10)
fig.text(.05, .055, f'占比按分项之和归一化；日志 total = {total:,.2f} 秒，差异 {(denominator-total)/total:.3%}。无外部参考数据。', fontsize=10)
args.output.parent.mkdir(parents=True, exist_ok=True)
for suffix in ('.png', '.pdf', '.svg'):
    fig.savefig(args.output.with_suffix(suffix), dpi=220, facecolor='white')
svg = args.output.with_suffix('.svg')
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
with args.output.with_suffix('.csv').open('w', newline='') as f:
    writer = csv.writer(f, lineterminator='\n')
    writer.writerow(['项目', '累计秒数', '占比百分数'])
    writer.writerows((name, value, value / denominator * 100) for _, name, value in items)
args.output.with_suffix('.json').write_text(json.dumps({
    'input': str(args.log.resolve()), 'sha256': hashlib.sha256(args.log.read_bytes()).hexdigest(),
    'steps': [1, 128000], 'physical_time': [0, 1000], 'windows': 40,
    'timers_seconds': totals, 'total_seconds': total, 'denominator_seconds': denominator,
    'normalization': 'sum of phase timers plus regrid_time; milliseconds converted to seconds',
    'note': 'Zero swap omitted. Regrid includes checks and triggered work. Timers are not an exact wall-clock partition; total excludes some output/startup work.',
}, indent=2) + '\n')
print(args.output.with_suffix('.png'))
