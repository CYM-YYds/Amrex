"""AI 临时汇总与绘图；本图唯一生成入口，随实验材料保留。"""
from pathlib import Path
import argparse
import csv
import json
import math
import re
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run_dir',type=Path)
parser.add_argument('--output-dir',type=Path)
args=parser.parse_args()
out=args.run_dir.resolve()
dest=args.output_dir.resolve() if args.output_dir else out
dest.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[7]
modes=json.loads((out/'modes.json').read_text())
rows=[]
windows=[]
for mode in modes:
    directory=out/f'perf_{mode["name"]}'
    status=json.loads((directory/'exit_status.json').read_text())
    assert status['exit_code']==0 and status['launcher_returncode']==0, directory
    log=(directory/'run.log').read_text()
    # guard可能在最后一次轮询前观察到进程退出；以原始日志末尾终结标记复核。
    assert re.search(r'^AMReX \([^\r\n]*\) finalized\s*$',log,re.M),directory
    parsed=[]
    workload=[]
    for line in log.splitlines():
        if ' perf(s): ' in line:
            values={k:float(v) for k,v in re.findall(r'(\w+)=([-+\deE.]+)',line)}
            step=int(re.match(r'step(\d+)',line).group(1))
            assert all(math.isfinite(v) for v in values.values()),line
            assert values['steps']==500, line
            values.update(step=step,mode=mode['name'])
            values['weighted_updates']=values['MLUPS_total']*values['total']*1e6
            parsed.append(values)
        elif ' collide_level: ' in line:
            values={k:int(v) for k,v in re.findall(r'(lev|calls|launch_cells)=(\d+)',line)}
            values['step']=int(re.match(r'step(\d+)',line).group(1))
            workload.append(values)
    assert [p['step'] for p in parsed]==list(range(128500,133001,500)), directory
    total=sum(p['total'] for p in parsed)
    updates=sum(p['weighted_updates'] for p in parsed)
    perwindow=[p['MLUPS_total'] for p in parsed]
    row=dict(**mode,total_s=total,steps=sum(p['steps'] for p in parsed),weighted_updates=updates,
             MLUPS_total=updates/total/1e6,window_arithmetic_mean=statistics.mean(perwindow),
             window_std=statistics.stdev(perwindow),window_min=min(perwindow),window_max=max(perwindow),
             workload=workload,windows=parsed)
    rows.append(row)
    windows.extend(parsed)
for index,row in enumerate(rows):
    row['vs_baseline']=row['MLUPS_total']/rows[0]['MLUPS_total']
    row['increment_percent']=0 if index==0 else (row['MLUPS_total']/rows[index-1]['MLUPS_total']-1)*100
    row['workload_equal_to_OSI']=row['workload']==rows[1]['workload']
    row['weighted_updates_vs_OSI']=(row['weighted_updates']/rows[1]['weighted_updates']-1)
(dest/'summary.json').write_text(json.dumps(dict(rows=rows,metric='sum(weighted valid updates) / sum(perf.total seconds) / 1e6; numerator reconstructed from logged MLUPS_total*total',window='128001-133000',repeats=1,source='624717/C checkpoint step128000; controlled current-source optimization ablation'),ensure_ascii=False,indent=2)+'\n')
fields=['name','label','MLUPS_total','total_s','steps','weighted_updates','vs_baseline','increment_percent','window_arithmetic_mean','window_std','window_min','window_max','workload_equal_to_OSI','weighted_updates_vs_OSI']
with (dest/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
    writer.writerows({k:r[k] for k in fields} for r in rows)
with (dest/'windows.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(windows[0]));writer.writeheader();writer.writerows(windows)
font=next((f.fname for f in font_manager.fontManager.ttflist if 'CJK' in f.name or 'WenQuanYi' in f.name),None)
if font:
    font_manager.fontManager.addfont(font)
    plt.rcParams['font.family']=font_manager.FontProperties(fname=font).get_name()
plt.rcParams['axes.unicode_minus']=False
plt.rcParams['svg.fonttype']='none'
fig,ax=plt.subplots(figsize=(12.5,5.9))
colors=['#8faabd']+['#269c8f']*(len(rows)-2)+['#1e7068']
y=list(range(len(rows)))
ax.barh(y,[r['MLUPS_total'] for r in rows],color=colors,height=.62)
ax.set_yticks(y,[r['label'] for r in rows]);ax.invert_yaxis()
maximum=max(r['MLUPS_total'] for r in rows)
for index,row in enumerate(rows):
    value=row['MLUPS_total']
    suffix='' if index==0 else f'  ({row["increment_percent"]:+.1f}%)'
    ax.text(value+maximum*.012,index,f'{value:.0f}{suffix}',va='center',fontsize=10,color='#263746')
ax.set_xlim(0,maximum*1.22)
ax.set_xlabel('MLUPS（有效格点更新量 / 墙钟秒，含重网格）')
ax.set_title('单 GPU 累加优化性能',loc='left',pad=18,fontsize=16)
ax.xaxis.grid(True,color='#e3eaf0');ax.set_axisbelow(True)
for name in ('top','right','left'):ax.spines[name].set_visible(False)
ax.tick_params(axis='y',length=0,pad=12)
fig.text(.02,.025,'输入：624717/C · checkpoint 128000（t=1000） | step 128001–133000 | 单 A100 · FLOAT · D3Q27 · C 阈值\n统计：10 × 500 步窗口累计更新量 / 累计推进耗时；括号为相对上一项变化。通信隐藏单 rank 无远端传输。',fontsize=9,color='#566675')
fig.subplots_adjust(left=.32,right=.96,bottom=.18,top=.87)
for extension in ('png','pdf','svg'):
    fig.savefig(dest/f'optimization_ladder.{extension}',dpi=180)
plt.close(fig)
lines=['# 单 GPU 优化累加实验','',
       f'来源：{out}。冻结程序 SHA256 见 executable.sha256；源码及差异、每次实际命令、日志、退出状态均保留。',
       '', '每配置从 624717/C step128000/t1000 恢复并推进5000粗步，单 rank/单 A100、FLOAT、D3Q27、Re1000、C阈值。动态AMR每32步重构，关闭输出和收敛检查，所有配置相同。',
       '', '平均 MLUPS = 10个500步窗口有效更新量之和 / 推进 total 耗时之和 / 1e6。日志未输出独立更新总数，分子从各窗口已打印的 MLUPS_total × total 恢复，受打印精度影响；不是最后窗口或启动器整次耗时。',
       '', '| 累加项 | 平均 MLUPS | 相邻变化 | 相对基线 | total/s | 与OSI逐窗口碰撞工作量一致 |',
       '|---|---:|---:|---:|---:|---|']
for row in rows:
    lines.append(f'| {row["label"]} | {row["MLUPS_total"]:.2f} | {row["increment_percent"]:+.2f}% | {row["vs_baseline"]:.3f}x | {row["total_s"]:.3f} | {row["workload_equal_to_OSI"]} |')
lines += ['', '基线为当前源码中受控撤回指定优化的版本，保留所有数值修复和未列出的优化，不等于某个完整历史提交。边界撤回采用每面厚度nghost+1的候选区并逐标签发射，仍覆盖修复后的完整物理面；插值复用同一缓存/任务区域，仅切换逐任务和批量启动。',
          '', '现役正式碰撞仍逐Fab提交；碰撞批量实验曾退化，不能将本表插值收益标为碰撞批量收益。线程数和坐标复用分别列出。单rank通信隐藏回退，不把这一行的波动归因为通信收益。',
          '', '各配置只有一次5000步测量，500步窗口不是独立重复；相邻小差异不能证明稳定加速。64步动态AMR优化回归见validation.json；本次A-B对OSI的最终DDF比较valid/active全0，详见check_00_AB/run.log。它不证明长期数值等价或收敛。',
          '', 'job626333全部14次应用退出0；最初汇总对guard completion_seen的判断过严，作业末尾报错。现以应用/启动器退出码0、原始AMReX finalized标记及完整窗口复核，已从既有日志恢复，无重跑。原始失败日志与postprocess_recovery.json保留。',
          '', '复现绘图（仓库根目录执行）：', '', '```bash', f'.venv/bin/python {Path(__file__).resolve().relative_to(repo)} {out} --output-dir {dest}', '```']
(dest/'report.md').write_text('\n'.join(lines)+'\n')
print(json.dumps([{k:r[k] for k in ('name','MLUPS_total','increment_percent','workload_equal_to_OSI')} for r in rows],indent=2))
