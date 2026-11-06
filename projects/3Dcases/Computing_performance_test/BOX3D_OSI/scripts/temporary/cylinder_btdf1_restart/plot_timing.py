#!/usr/bin/env python3
# 本次圆柱续算的耗时占比图；保留脚本与数据快照以复现，任务后勿删除唯一生成入口。
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

os.environ.setdefault('MPLCONFIGDIR', '/tmp/box3d-cylinder-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties


def main():
    parser = argparse.ArgumentParser(description='从圆柱续算的已完成统计窗口生成耗时占比图')
    parser.add_argument('run_directory', type=Path)
    parser.add_argument('--job-id', default='')
    args = parser.parse_args()
    run = args.run_directory.resolve()
    verifier = Path(__file__).with_name('verify.py')
    result = subprocess.run([sys.executable, str(verifier), str(run.parent), 'production'], check=True, capture_output=True, text=True)
    report = json.loads(result.stdout)
    with (run/'timing_windows.csv').open() as stream:
        windows = list(csv.DictReader(stream))
    if not windows:
        raise SystemExit('尚无完整统计窗口')
    start = int(float(windows[0]['step'])) - int(float(windows[0]['steps']))
    end = int(float(windows[-1]['step']))
    out = run/'plots'/f'timing_share_{start}_{end}'
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(run/'timing_windows.csv', out/'timing_windows.csv')
    (out/'summary_snapshot.json').write_text(json.dumps(report, indent=2)+'\n')
    shutil.copy2(run/'run.log', out/'run_log_snapshot.txt')
    total = report['total_seconds']
    particle = report['particle_all_seconds']
    overall = [
        ('颗粒相关路径', particle),
        ('边界处理', report['boundary_seconds']),
        ('碰撞', report['collide_seconds']),
        ('通信', report['comm_seconds']),
        ('插值', report['interp_seconds']),
        ('平均下传', report['average_seconds']),
        ('迁移 Stream', report['stream_seconds']),
    ]
    detail = [
        ('力汇总 SumBoundary', report['particle_sum_seconds']),
        ('宏观量准备', report['particle_macro_seconds']),
        ('插值与施力', report['particle_force_seconds']),
        ('受力归约与写文件', report['particle_reduce_seconds']),
        ('力补边', report['particle_ghost_seconds']),
        ('颗粒迁移', report['particle_redistribute_seconds']),
    ]
    remainder = report['particle_total_seconds'] - sum(report[f'particle_{k}_seconds'] for k in ('macro','force','sum','ghost'))
    if remainder < -1e-6:
        raise ValueError('颗粒子项大于父计时，不能绘制内部占比')
    detail.append(('颗粒路径其余开销', max(remainder, 0)))
    font = FontProperties(fname='/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc')
    plt.rcParams['font.family'] = font.get_name()
    plt.rcParams['axes.unicode_minus'] = False
    fig, axes = plt.subplots(1, 2, figsize=(15, 6.4), gridspec_kw={'wspace': .62})
    colors = ['#2274A5','#EA8C55','#4C956C','#8E6C88','#4F9DA6','#D8A12B','#8892A0']
    rows = []
    for ax, entries, denominator, title in zip(axes, [overall, detail], [total, particle],
            ['各阶段累计 / 计算总耗时', '颗粒相关路径内部占比']):
        entries = sorted(entries, key=lambda item: item[1], reverse=True)
        percentages = [100*v/denominator for _,v in entries]
        ax.barh(range(len(entries)), percentages, color=colors, height=.62)
        ax.set_yticks(range(len(entries)), [name for name,_ in entries], fontsize=11)
        ax.invert_yaxis()
        ax.set_xlim(0, max(percentages)*1.43)
        for i, ((name, seconds), percent) in enumerate(zip(entries, percentages)):
            ax.text(percent+max(percentages)*.025, i, f'{percent:.2f}%  |  {seconds:.2f} s', va='center', fontsize=10)
            rows.append({'panel': title, 'item': name, 'seconds': seconds, 'denominator_seconds': denominator, 'percent': percent})
        ax.set_title(f'{title}\n分母 {denominator:.2f} 秒', fontsize=13, pad=16)
        ax.set_xlabel('占比（%）', fontsize=11)
        ax.xaxis.grid(True, alpha=.2)
        ax.set_axisbelow(True)
        for side in ('top','right','left'): ax.spines[side].set_visible(False)
    state = '完整运行' if report['complete'] else '运行中快照'
    fig.suptitle(f'二维圆柱 OSI 耗时占比 · 作业 {args.job_id or "未指定"} · {state}\n'
                 f'步骤 {start} → {end}，{report["statistical_steps"]:,} 步 / {report["windows"]} 个完整窗口', fontsize=16, y=.98)
    fig.text(.02, .085, '左图按各计时器 / compute total 计算，非严格互斥的 100% 分解；平均计时可能包含输出阶段的平均下传。', fontsize=10)
    fig.text(.02, .052, '颗粒父计时不与子项重复相加；受力归约项含文件输出。compute total 不含流场/checkpoint I/O，细分计时包含 GPU 同步开销。', fontsize=10)
    fig.text(.02, .019, '来源：本图目录的 timing_windows.csv、summary_snapshot.json 与 run_log_snapshot.txt；无参考数据，未做额外物理归一化。', fontsize=9, color='#555555')
    fig.subplots_adjust(left=.15, right=.96, bottom=.23, top=.74)
    for suffix in ('png','svg'):
        fig.savefig(out/f'timing_share.{suffix}', dpi=180, facecolor='white')
    plt.close(fig)
    # 各点对应一个完整统计窗口；按实际步数换算单步耗时。
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    x = [int(float(w['step'])) for w in windows]
    series = [('total', '计算总耗时'), ('particle_all', '颗粒相关路径'),
              ('collide', '碰撞'), ('particle_sum', '力汇总 SumBoundary')]
    for key, label in series:
        axes[0].plot(x, [1000*float(w[key])/float(w['steps']) for w in windows],
                     label=label, linewidth=1.6)
    for key, label in [('boundary', '边界处理'), ('comm', '通信'),
                       ('interp', '插值'), ('average', '平均下传')]:
        axes[1].plot(x, [1000*float(w[key])/float(w['steps']) for w in windows],
                     label=label, linewidth=1.6)
    for ax in axes:
        ax.set_ylabel('窗口平均耗时（毫秒 / 粗层步）')
        ax.grid(alpha=.25)
        ax.legend(ncol=2)
        ax.ticklabel_format(axis='x', style='plain', useOffset=False)
    axes[1].set_xlabel('统计窗口结束步数')
    fig.suptitle(f'二维圆柱 OSI 耗时曲线 · 作业 {args.job_id or "未指定"}\n步骤 {start} → {end}，{len(windows)} 个完整窗口')
    fig.text(.02, .025, '来源：同目录 timing_windows.csv；按各窗口实际步数归一化，无参考数据。\ncompute total 不含流场/checkpoint I/O；细分计时包含同步开销，父子项不可相加。', fontsize=9)
    fig.tight_layout(rect=(0, .075, 1, .92))
    for suffix in ('png', 'svg'):
        fig.savefig(out/f'timing_curve.{suffix}', dpi=180, facecolor='white')
    plt.close(fig)
    with (out/'chart_data.csv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    manifest = {'job_id': args.job_id, 'start_step': start, 'end_step': end, 'windows': len(windows),
                'source_csv_sha256': hashlib.sha256((out/'timing_windows.csv').read_bytes()).hexdigest(),
                'command': [sys.executable, str(Path(__file__).resolve()), str(run), '--job-id', args.job_id]}
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(out/'timing_share.png')


if __name__ == '__main__':
    main()
