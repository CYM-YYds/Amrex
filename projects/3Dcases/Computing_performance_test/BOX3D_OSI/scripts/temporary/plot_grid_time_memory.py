#!/usr/bin/env python3
"""本次均匀/自适应网格图表的复现脚本；保留作为图表生成依据。"""
from pathlib import Path
import re
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/box3d-matplotlib')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties

case = Path(__file__).resolve().parents[2]
runs = case / 'runs'
out = case / 'data_post_processing/results/grid_time_memory_comparison'
font = FontProperties(fname='/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc')
plt.rcParams['font.family'] = font.get_name()
plt.rcParams['axes.unicode_minus'] = False
patterns = [
 ('均匀网格 512³\njob623012', sorted((runs/'uniform512_local_staging_t1000/job623012').glob('step*/run.log')), 40, 512000),
 ('自适应 B\njob622137', [runs/'a5_B_optimized_128000/job622137/B/run.log'], 40, 128000),
 ('自适应 C\njob624717', [runs/'a5_C_t1000_gif/job624717/C/run.log'], 40, 128000),
]
rows=[]
for name, paths, count, steps in patterns:
    timings=[]; peaks=[]; countsteps=0
    for path in paths:
        data=path.read_text(errors='replace')
        perf=re.findall(r'^step\d+ perf\(s\):.*?\btotal=([\d.eE+-]+) steps=(\d+)',data,re.M)
        timings.extend(float(t) for t,_ in perf)
        countsteps+=sum(int(n) for _,n in perf)
        matches=re.findall(r'\[The\s+Arena\] max space \(MB\) used\s+spread across MPI: \[(\d+) \.\.\. (\d+)\]',data)
        if len(matches)!=1: raise ValueError(f'内存数据异常: {path}')
        peaks.append(max(map(int,matches[0])))
    if len(timings)!=count or countsteps!=steps: raise ValueError(f'窗口/步数异常: {name}, {len(timings)}, {countsteps}')
    rows.append((name,sum(timings),max(peaks)))

fig, axes=plt.subplots(1,2,figsize=(12.8,6.4),layout='constrained')
colors=['#53687e','#168a83','#e39a42']
for ax, values, title, unit in [(axes[0],[r[1] for r in rows],'推进累计耗时（含重构）','s'),(axes[1],[r[2] for r in rows],'主 Arena 使用高水位','MiB')]:
    bars=ax.bar([r[0] for r in rows],values,color=colors,width=.63)
    ax.set_title(title,fontsize=15,weight='bold',pad=14); ax.set_ylabel(unit,fontsize=11)
    ax.set_ylim(0,max(values)*1.20); ax.grid(axis='y',alpha=.22); ax.set_axisbelow(True)
    ax.spines[['top','right']].set_visible(False)
    ax.bar_label(bars,labels=[f'{x:,.2f}' if unit=='s' else f'{x:,}' for x in values],padding=5,fontsize=11,weight='bold')
fig.suptitle('均匀网格与自适应网格：耗时和内存对比',fontsize=18,weight='bold')
fig.text(.5,-.025,'Re=1000，A100，FLOAT D3Q27；物理时间 0–1000（L=1）\n均匀网格：512000 步、40 段重启；B/C：128000 粗步、各 40 个窗口\n耗时为 perf(s).total 累加，不含初始化与输出；内存为主 Arena 已用空间高水位，并非整卡峰值\n数据源：job623012、job622137/B、job624717/C 运行日志；数值为原始值，未归一化\n程序版本、诊断和输出流程不同，耗时比不代表严格等精度加速比。',ha='center',va='top',fontsize=9,linespacing=1.55)
out.mkdir(parents=True,exist_ok=True)
for ext in ('png','pdf'):
 fig.savefig(out/f'grid_time_memory.{ext}',dpi=200,bbox_inches='tight',facecolor='white')
for row in rows: print(row)
