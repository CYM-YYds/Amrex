#!/usr/bin/env python3
"""本次C阈值A-B/OSI耗时对照图；保留为图表唯一生成入口及复现材料。"""
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
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ab-logs', nargs='+', type=Path, required=True)
parser.add_argument('--osi-log', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()

def summarize(paths):
    windows = [window for path in paths for window in parse_log(path)]
    assert [w.step for w in windows] == list(range(3200, 128001, 3200)), '必须覆盖40个连续3200步窗口'
    totals = {phase: sum(w.phase[phase] for w in windows) for phase in PHASES}
    totals['regrid'] = sum(w.regrid for w in windows)
    rows = [parse_values(line) for path in paths for line in path.read_text().splitlines() if ' perf(s):' in line]
    assert len(rows) == 40 and all(row['steps'] == 3200 for row in rows)
    totals.update({name: sum(row[name] for row in rows) for name in ('total',)})
    return totals

ab = summarize(args.ab_logs)
osi = summarize([args.osi_log])
phases = ['interp', 'average', 'collide', 'stream', 'comm', 'boundary', 'swap', 'regrid']
labels = ['插值', '平均下传', '碰撞', '迁移', '通信', '边界处理', '数组交换', '网格重构']
colors = ['#3676AB', '#E68A32']
font_manager.fontManager.addfont('/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc')
chinese_font = font_manager.FontProperties(fname='/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc').get_name()
plt.rcParams.update({'font.family': chinese_font, 'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(1, 2, figsize=(13, 5.4), gridspec_kw={'width_ratios': [3, 1]})
fig.subplots_adjust(left=.065, right=.98, top=.77, bottom=.15, wspace=.24)
fig.suptitle('A-B模式与OSI模式耗时对比', fontsize=19, fontweight='bold', y=.97)

def bars(ax, keys, names):
    positions = np.arange(len(keys))
    for offset, values, color, name in zip((-.19, .19), (ab, osi), colors, ('A-B模式', 'OSI模式')):
        rects = ax.bar(positions + offset, [values[k] for k in keys], .36, label=name, color=color, zorder=3)
        ax.bar_label(rects, labels=[f'{values[k]:,.0f}' if len(keys) > 2 and values[k] >= 10 else f'{values[k]:,.1f}' for k in keys], padding=4, fontsize=8, zorder=5)
    ax.set_xticks(positions, names)
    ax.set_ylabel('累计耗时（秒）')
    ax.set_ylim(0, max(max(ab[k], osi[k]) for k in keys) * 1.22)
    ax.grid(axis='y', alpha=.20, zorder=0)
    ax.tick_params(axis='x', length=0)

bars(axes[0], phases, labels)
axes[0].tick_params(axis='x', labelrotation=0)
axes[0].set_title('分项耗时', loc='left', fontweight='bold', pad=15)
bars(axes[1], ['total'], ['总耗时'])
axes[1].set_title('推进总耗时', loc='left', fontweight='bold', pad=15)
handles, names = axes[0].get_legend_handles_labels()
fig.legend(handles, names, loc='upper center', bbox_to_anchor=(.5,.87), ncol=2, frameon=False)
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=220, facecolor='white')
fig.savefig(args.output.with_suffix('.svg'), facecolor='white')
# 清除绘图库生成的行尾空白，不改变SVG路径中的换行分隔。
svg = args.output.with_suffix('.svg')
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
with args.output.with_suffix('.csv').open('w', newline='') as stream:
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(['计时项目', 'A-B模式耗时（秒）', 'OSI模式耗时（秒）'])
    writer.writerows((label, ab[key], osi[key]) for key, label in list(zip(phases, labels)) + [('total', '总耗时')])
metadata = {'inputs': [{'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in args.ab_logs + [args.osi_log]],
            'steps': [1, 128000], 'physical_time': [0, 1000], 'windows': 40, 'window_steps': 3200,
            'unit': 'seconds', 'normalization': 'none; cumulative perf(s) timers, regrid_time converted ms to s',
            'AB': ab, 'OSI': osi, 'note': 'A-B one restart and tracing; different nodes/output paths and evolving grids; not a controlled speedup benchmark'}
args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(args.output)
