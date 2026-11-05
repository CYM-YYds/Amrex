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
parser.add_argument('--collision-extra',action='store_true')
args=parser.parse_args()
out=args.run_dir.resolve()
dest=args.output_dir.resolve() if args.output_dir else out
dest.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[7]
modes=json.loads((out/'modes.json').read_text())
rows=[]
windows=[]
runs=[(mode,out/f'perf_{mode["name"]}') for mode in modes]
if args.collision_extra:
    runs=[(mode,out/f'perf_pair{pair}_{mode["name"]}') for pair in (1,2) for mode in modes]
for mode,directory in runs:
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
            if args.collision_extra: values['run']=directory.name
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
    if args.collision_extra: row['run']=directory.name
    rows.append(row)
    windows.extend(parsed)
paired=[]
if args.collision_extra:
    individual=rows
    for pair in (1,2):
        control,candidate=[next(r for r in individual if r['run']==f'perf_pair{pair}_{mode["name"]}') for mode in modes]
        assert control['workload']==candidate['workload']
        assert abs(control['weighted_updates']/candidate['weighted_updates']-1)<1e-8
        paired.append(dict(pair=pair,control_MLUPS=control['MLUPS_total'],candidate_MLUPS=candidate['MLUPS_total'],MLUPS_change_percent=(candidate['MLUPS_total']/control['MLUPS_total']-1)*100,total_change_percent=(candidate['total_s']/control['total_s']-1)*100))
    rows=[]
    for mode in modes:
        samples=[r for r in individual if r['name']==mode['name']]
        seconds=sum(r['total_s'] for r in samples)
        updates=sum(r['weighted_updates'] for r in samples)
        perwindow=[p['MLUPS_total'] for r in samples for p in r['windows']]
        rows.append(dict(**mode,total_s=seconds,steps=sum(r['steps'] for r in samples),weighted_updates=updates,MLUPS_total=updates/seconds/1e6,window_arithmetic_mean=statistics.mean(perwindow),window_std=statistics.stdev(perwindow),window_min=min(perwindow),window_max=max(perwindow),workload=[p for r in samples for p in r['workload']],windows=[p for r in samples for p in r['windows']],individual_runs=samples))
reference=rows[0] if args.collision_extra else rows[1]
equal_key='workload_equal_to_control' if args.collision_extra else 'workload_equal_to_OSI'
updates_key='weighted_updates_vs_control' if args.collision_extra else 'weighted_updates_vs_OSI'
for index,row in enumerate(rows):
    row['vs_baseline']=row['MLUPS_total']/rows[0]['MLUPS_total']
    row['increment_percent']=0 if index==0 else (row['MLUPS_total']/rows[index-1]['MLUPS_total']-1)*100
    row[equal_key]=row['workload']==reference['workload']
    row[updates_key]=(row['weighted_updates']/reference['weighted_updates']-1)
(dest/'summary.json').write_text(json.dumps(dict(rows=rows,metric='sum(weighted valid updates) / sum(perf.total seconds) / 1e6; numerator reconstructed from logged MLUPS_total*total',window='128001-133000 per run',repeats=2 if args.collision_extra else 1,source='624717/C checkpoint step128000; frozen cumulative-optimization source',paired_comparison=paired),ensure_ascii=False,indent=2)+'\n')
fields=['name','label','MLUPS_total','total_s','steps','weighted_updates','vs_baseline','increment_percent','window_arithmetic_mean','window_std','window_min','window_max',equal_key,updates_key]
with (dest/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');writer.writeheader()
    writer.writerows({k:r[k] for k in fields} for r in rows)
with (dest/'windows.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(windows[0]),lineterminator='\n');writer.writeheader();writer.writerows(windows)
preferred_font=Path('/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc')
font=str(preferred_font) if preferred_font.exists() else next((f.fname for f in font_manager.fontManager.ttflist if 'Sans CJK' in f.name or 'WenQuanYi' in f.name),None)
if font:
    font_manager.fontManager.addfont(font)
    plt.rcParams['font.family']=font_manager.FontProperties(fname=font).get_name()
plt.rcParams['axes.unicode_minus']=False
plt.rcParams['svg.fonttype']='none'
fig,ax=plt.subplots(figsize=(12.5,4.1 if args.collision_extra else 5.9))
colors=['#8faabd']+['#269c8f']*(len(rows)-2)+['#1e7068']
if args.collision_extra: colors=['#8faabd','#c57b65']
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
ax.set_title('碰撞批量提交：额外实验' if args.collision_extra else '单 GPU 累加优化性能',loc='left',pad=18,fontsize=16)
ax.xaxis.grid(True,color='#e3eaf0');ax.set_axisbelow(True)
for name in ('top','right','left'):ax.spines[name].set_visible(False)
ax.tick_params(axis='y',length=0,pad=12)
footer='输入：624717/C · checkpoint 128000（t=1000） | step 128001–133000 | 单 A100 · FLOAT · D3Q27 · C 阈值\n统计：10 × 500 步窗口累计更新量 / 累计推进耗时；括号为相对上一项变化。通信隐藏单 rank 无远端传输。'
if args.collision_extra:
    footer='输入：624717/C · checkpoint 128000 | 单 A100 · FLOAT · D3Q27 · C 阈值 · 256 线程 · 无坐标复用\n每组 2 × 5000 步，同节点换序；累计有效更新量 / 累计推进时间（含重网格）。括号相对本作业对照。'
fig.text(.02,.025,footer,fontsize=9,color='#566675')
fig.subplots_adjust(left=.32,right=.96,bottom=.25 if args.collision_extra else .18,top=.82 if args.collision_extra else .87)
figure_name='collision_batch_extra' if args.collision_extra else 'optimization_ladder'
for extension in ('png','pdf','svg'):
    fig.savefig(dest/f'{figure_name}.{extension}',dpi=180)
plt.close(fig)
lines=['# 单 GPU 优化累加实验','',
       f'来源：{out}。冻结程序 SHA256 见 executable.sha256；源码及差异、每次实际命令、日志、退出状态均保留。',
       '', '每配置从 624717/C step128000/t1000 恢复并推进5000粗步，单 rank/单 A100、FLOAT、D3Q27、Re1000、C阈值。动态AMR每32步重构，关闭输出和收敛检查，所有配置相同。',
       '', '平均 MLUPS = 10个500步窗口有效更新量之和 / 推进 total 耗时之和 / 1e6。日志未输出独立更新总数，分子从各窗口已打印的 MLUPS_total × total 恢复，受打印精度影响；不是最后窗口或启动器整次耗时。',
       '', '| 累加项 | 平均 MLUPS | 相邻变化 | 相对基线 | total/s | 与OSI逐窗口碰撞工作量一致 |',
       '|---|---:|---:|---:|---:|---|']
for row in rows:
    lines.append(f'| {row["label"]} | {row["MLUPS_total"]:.2f} | {row["increment_percent"]:+.2f}% | {row["vs_baseline"]:.3f}x | {row["total_s"]:.3f} | {row[equal_key]} |')
lines += ['', '基线为当前源码中受控撤回指定优化的版本，保留所有数值修复和未列出的优化，不等于某个完整历史提交。边界撤回采用每面厚度nghost+1的候选区并逐标签发射，仍覆盖修复后的完整物理面；插值复用同一缓存/任务区域，仅切换逐任务和批量启动。',
          '', '现役正式碰撞仍逐Fab提交；碰撞批量实验曾退化，不能将本表插值收益标为碰撞批量收益。线程数和坐标复用分别列出。单rank通信隐藏回退，不把这一行的波动归因为通信收益。',
          '', '各配置只有一次5000步测量，500步窗口不是独立重复；相邻小差异不能证明稳定加速。64步动态AMR优化回归见validation.json；本次A-B对OSI的最终DDF比较valid/active全0，详见check_00_AB/run.log。它不证明长期数值等价或收敛。',
          '', 'job626333全部14次应用退出0；最初汇总对guard completion_seen的判断过严，作业末尾报错。现以应用/启动器退出码0、原始AMReX finalized标记及完整窗口复核，已从既有日志恢复，无重跑。原始失败日志与postprocess_recovery.json保留。',
          '', '复现绘图（仓库根目录执行）：', '', '```bash', f'.venv/bin/python {Path(__file__).resolve().relative_to(repo)} {out} --output-dir {dest}', '```']
if args.collision_extra:
    lines=['# 碰撞批量提交：额外实验','',f'原始材料：{out}。节点：{(out/"node.txt").read_text().strip()}。程序、源码差异、输入、实际命令、原始日志和退出状态均保留。',
        '', '从主图job626333冻结源码扩展，正式源码不改。仅比较已启用插值批量时逐Fab碰撞与跨Fab批量碰撞；均为256线程、无坐标复用、边界优化关闭、通信重叠关闭，其余参数与主图一致。',
        '', '同一程序、同一节点、同一GPU，先对照→实验，再实验→对照。每次独立从624717/C step128000恢复并推进5000粗步，共两对。平均MLUPS按每组20个500步窗口累计有效更新量 / 累计perf.total秒数计算，含重网格；分子由日志MLUPS_total×total恢复，受打印精度影响。',
        '', '| 方案 | 平均MLUPS_total | 相对本作业对照 | 累计total/s |', '|---|---:|---:|---:|']
    for row in rows:
        lines.append(f'| {row["label"]} | {row["MLUPS_total"]:.2f} | {row["increment_percent"]:+.2f}% | {row["total_s"]:.3f} |')
    lines += ['', '逐对结果：', '', '| 对照顺序 | 对照MLUPS | 实验MLUPS | MLUPS变化 | total变化 |', '|---|---:|---:|---:|---:|']
    for pair in paired:
        lines.append(f'| {"对照→实验" if pair["pair"]==1 else "实验→对照"} | {pair["control_MLUPS"]:.2f} | {pair["candidate_MLUPS"]:.2f} | {pair["MLUPS_change_percent"]:+.2f}% | {pair["total_change_percent"]:+.2f}% |')
    lines += ['', '两组每窗口/逐层碰撞calls、launch_cells及有效更新量一致。64步真实checkpoint动态AMR回归：本作业对照与主图冻结对照checkpoint一致，批量实验与本作业对照checkpoint一致，见validation.json。',
        '', '碰撞任务只存当前布局派生的区域/Fab几何/索引，沿现有布局缓存的重建和clear失效，不保存状态数组地址或相位。执行时获取当前状态/掩码/phase；数值函数、掩码与ghost范围相同，阶段末同步保留。批量任务建表成本计入重网格计时。',
        '', '只有两对本配置测量，不外推其他网格、精度、线程数、多GPU或长期物理精度。作为额外实验单列，不混入已采用优化主图；不将不同节点作业的绝对MLUPS直接作差。',
        '', '复现绘图（仓库根执行）：', '', '```bash', f'.venv/bin/python {Path(__file__).resolve().relative_to(repo)} {out} --collision-extra --output-dir {dest}', '```']
if not args.collision_extra and (dest/'collision_batch_extra/report.md').exists():
    lines += ['', '用户确认碰撞批量作为额外实验单列，未纳入上述已采用优化累加。见[额外实验报告](collision_batch_extra/report.md)与[对照图](collision_batch_extra/collision_batch_extra.png)，不同节点作业的绝对MLUPS不直接作差。']
(dest/'report.md').write_text('\n'.join(lines)+'\n')
print(json.dumps([{k:r[k] for k in ('name','MLUPS_total','increment_percent',equal_key)} for r in rows],indent=2))
