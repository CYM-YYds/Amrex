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
    totals.update({name: sum(row[name] for row in rows) for name in ('solv', 'total')})
    return totals

ab = summarize(args.ab_logs)
osi = summarize([args.osi_log])
phases = ['interp', 'average', 'collide', 'stream', 'comm', 'boundary', 'swap', 'regrid']
labels = ['Interpolation', 'Average', 'Collision', 'Stream', 'Communication', 'Boundary', 'Swap', 'Regrid']
colors = ['#3676AB', '#E68A32']
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(1, 2, figsize=(14, 6.8), gridspec_kw={'width_ratios': [2.35, 1]})
fig.subplots_adjust(left=.065, right=.98, top=.79, bottom=.26, wspace=.24)
fig.suptitle('A-B vs OSI: recorded computation time', fontsize=19, fontweight='bold', y=.97)
fig.text(.5, .905, 'C thresholds | Re = 1000 | FLOAT D3Q27 | single A100 | t = 0-1000', ha='center', fontsize=11)

def bars(ax, keys, names):
    positions = np.arange(len(keys))
    for offset, values, color, name in zip((-.19, .19), (ab, osi), colors, ('A-B', 'OSI')):
        rects = ax.bar(positions + offset, [values[k] for k in keys], .36, label=name, color=color, zorder=3)
        ax.bar_label(rects, labels=[f'{values[k]:,.0f}' if len(keys) > 2 and values[k] >= 10 else f'{values[k]:,.1f}' for k in keys], padding=4, fontsize=8, zorder=5)
    ax.set_xticks(positions, names)
    ax.set_ylabel('Accumulated time (s)')
    ax.set_ylim(0, max(max(ab[k], osi[k]) for k in keys) * 1.22)
    ax.grid(axis='y', alpha=.20, zorder=0)
    ax.tick_params(axis='x', length=0)

bars(axes[0], phases, labels)
axes[0].tick_params(axis='x', labelrotation=28)
axes[0].set_title('Phase timers', loc='left', fontweight='bold', pad=15)
bars(axes[1], ['solv', 'total'], ['Solver', 'Compute total'])
axes[1].set_title('Aggregate timers', loc='left', fontweight='bold', pad=15)
handles, names = axes[0].get_legend_handles_labels()
fig.legend(handles, names, loc='upper center', bbox_to_anchor=(.5,.87), ncol=2, frameon=False)
fig.text(.065, .145, 'Sources: A-B jobs 625774 + 625855; OSI job 624717. Sum of 40 x 3200-step windows (128000 steps); no normalization.', fontsize=9)
fig.text(.065, .112, 'Compute total includes AMR work; excludes initialization, field/checkpoint output and transfer. Phase bars are not an additive partition of total.', fontsize=9)
fig.text(.065, .079, 'Same frozen solver; A-B has one restart and I/O tracing. Different nodes/output paths and evolving AMR states: descriptive comparison.', fontsize=9, color='#555555')
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=220, facecolor='white')
fig.savefig(args.output.with_suffix('.svg'), facecolor='white')
# 清除绘图库生成的行尾空白，不改变SVG路径中的换行分隔。
svg = args.output.with_suffix('.svg')
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
with args.output.with_suffix('.csv').open('w', newline='') as stream:
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(['timer', 'AB_seconds', 'OSI_seconds'])
    writer.writerows((key, ab[key], osi[key]) for key in phases + ['solv', 'total'])
metadata = {'inputs': [{'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in args.ab_logs + [args.osi_log]],
            'steps': [1, 128000], 'physical_time': [0, 1000], 'windows': 40, 'window_steps': 3200,
            'unit': 'seconds', 'normalization': 'none; cumulative perf(s) timers, regrid_time converted ms to s',
            'AB': ab, 'OSI': osi, 'note': 'A-B one restart and tracing; different nodes/output paths and evolving grids; not a controlled speedup benchmark'}
args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(args.output)
