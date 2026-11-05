"""AI 临时汇总与绘图；本图唯一生成入口，随实验材料保留。"""
from pathlib import Path
import argparse
import csv
import json
import math
import re
import statistics
import os
import subprocess
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run_dir',type=Path)
parser.add_argument('--output-dir',type=Path)
parser.add_argument('--collision-extra',action='store_true')
parser.add_argument('--independent',action='store_true')
parser.add_argument('--osi-last',action='store_true')
args=parser.parse_args()
assert sum((args.collision_extra,args.independent,args.osi_last))<=1
out=args.run_dir.resolve()
dest=args.output_dir.resolve() if args.output_dir else out
dest.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[7]
plan=out/'recheck_plan.json'
if args.independent and plan.exists() and not (out/'recheck_complete.txt').exists():
    # 只有计划指定的在分配作业内接续测量；普通离线重绘不启动应用。
    expected=json.loads(plan.read_text())['job_id']
    if os.environ.get('CCS_JOB_ID')==expected:
        subprocess.run([sys.executable,str(Path(__file__).with_name('run.py')),'--independent','--recheck'],check=True)
    else:
        raise RuntimeError('independent confirmation matrix is pending in the allocated job')
modes=json.loads((out/'modes.json').read_text())
rows=[]
windows=[]
runs=[(mode,out/f'perf_{mode["name"]}') for mode in modes]
if args.collision_extra:
    runs=[(mode,out/f'perf_pair{pair}_{mode["name"]}') for pair in (1,2) for mode in modes]
confirmed=args.independent and (out/'recheck_complete.txt').exists()
if confirmed:
    runs=[(mode,out/f'{prefix}_{mode["name"]}') for prefix in ('perf','perf_confirm') for mode in modes]
for mode,directory in runs:
    status=json.loads((directory/'exit_status.json').read_text())
    assert status['exit_code']==0 and status['launcher_returncode']==0, directory
    log=(directory/'run.log').read_text()
    # guard可能在最后一次轮询前观察到进程退出；以原始日志末尾终结标记复核。
    assert re.search(r'^AMReX \([^\r\n]*\) finalized\s*$',log,re.M),directory
    parsed=[]
    workload=[]
    work_counts=[]
    for line in log.splitlines():
        if ' perf(s): ' in line:
            values={k:float(v) for k,v in re.findall(r'(\w+)=([-+\deE.]+)',line)}
            step=int(re.match(r'step(\d+)',line).group(1))
            assert all(math.isfinite(v) for v in values.values()),line
            assert values['steps']==(100 if args.osi_last else 500), line
            values.update(step=step,mode=mode['name'])
            if args.collision_extra or args.independent: values['run']=directory.name
            values['weighted_updates']=values['MLUPS_total']*values['total']*1e6
            parsed.append(values)
        elif ' collide_level: ' in line:
            values={k:int(v) for k,v in re.findall(r'(lev|calls|launch_cells)=(\d+)',line)}
            values['step']=int(re.match(r'step(\d+)',line).group(1))
            workload.append(values)
        elif args.independent and ' perf_count: ' in line:
            counts={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
            # 薄面会有意减少无效候选格点，故不要求该启动量与厚面相同。
            counts.pop('boundary_launch_cells',None)
            counts['step']=int(re.match(r'step(\d+)',line).group(1))
            work_counts.append(counts)
    assert [p['step'] for p in parsed]==(list(range(128100,129001,100)) if args.osi_last else list(range(128500,133001,500))), directory
    total=sum(p['total'] for p in parsed)
    updates=sum(p['weighted_updates'] for p in parsed)
    perwindow=[p['MLUPS_total'] for p in parsed]
    row=dict(**mode,total_s=total,steps=sum(p['steps'] for p in parsed),weighted_updates=updates,
             MLUPS_total=updates/total/1e6,window_arithmetic_mean=statistics.mean(perwindow),
             window_std=statistics.stdev(perwindow),window_min=min(perwindow),window_max=max(perwindow),
             workload=workload,windows=parsed)
    if args.collision_extra or args.independent: row['run']=directory.name
    if args.independent: row['work_counts']=work_counts
    rows.append(row)
    windows.extend(parsed)
paired=[]
if confirmed:
    individual=rows;rows=[]
    for mode in modes:
        samples=[r for r in individual if r['name']==mode['name']]
        assert len(samples)==2 and samples[0]['workload']==samples[1]['workload'] and samples[0]['work_counts']==samples[1]['work_counts']
        combined=[]
        ranges=[]
        for a,b in zip(samples[0]['windows'],samples[1]['windows']):
            assert a['step']==b['step']
            average={k:(a[k]+b[k])/2 for k in a if k not in ('mode','run')}
            average.update(mode=mode['name'],run='mean_of_two',step=a['step'])
            average['MLUPS_total']=average['weighted_updates']/average['total']/1e6
            combined.append(average)
            ranges.append(dict(step=a['step'],minimum=min(a['total'],b['total']),maximum=max(a['total'],b['total'])))
        total=statistics.mean(r['total_s'] for r in samples)
        updates=statistics.mean(r['weighted_updates'] for r in samples)
        rates=[p['MLUPS_total'] for r in samples for p in r['windows']]
        rows.append(dict(**mode,total_s=total,steps=5000,weighted_updates=updates,MLUPS_total=updates/total/1e6,window_arithmetic_mean=statistics.mean(rates),window_std=statistics.stdev(rates),window_min=min(rates),window_max=max(rates),windows=combined,window_time_ranges=ranges,total_min_s=min(r['total_s'] for r in samples),total_max_s=max(r['total_s'] for r in samples),workload=samples[0]['workload'],work_counts=samples[0]['work_counts'],individual_runs=samples))
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
reference=rows[0] if args.collision_extra or args.independent or args.osi_last else rows[1]
equal_key='workload_equal_to_control' if args.collision_extra or args.independent or args.osi_last else 'workload_equal_to_OSI'
updates_key='weighted_updates_vs_control' if args.collision_extra or args.independent or args.osi_last else 'weighted_updates_vs_OSI'
phase_keys=['collide','boundary','interp','average','comm']
for index,row in enumerate(rows):
    row['vs_baseline']=row['MLUPS_total']/rows[0]['MLUPS_total']
    row['increment_percent']=0 if index==0 else (row['MLUPS_total']/rows[0 if args.independent else index-1]['MLUPS_total']-1)*100
    row[equal_key]=row['workload']==reference['workload']
    row[updates_key]=(row['weighted_updates']/reference['weighted_updates']-1)
    if args.osi_last:
        assert row[equal_key] and abs(row[updates_key])<1e-8,row['name']
    if args.independent:
        assert row[equal_key] and abs(row[updates_key])<1e-8, row['name']
        assert len(row['work_counts'])==10 and row['work_counts']==reference['work_counts'],row['name']
        row['time_ratio']=row['total_s']/reference['total_s']
        row['time_saved_percent']=(1-row['time_ratio'])*100
        for key in phase_keys: row[key+'_s']=sum(p[key] for p in row['windows'])
        row['other_s']=row['total_s']-sum(row[key+'_s'] for key in phase_keys)
        assert row['other_s']>=0,row
(dest/'summary.json').write_text(json.dumps(dict(rows=rows,metric='sum(weighted valid updates) / sum(perf.total seconds) / 1e6; numerator reconstructed from logged MLUPS_total*total; independent confirmed total_s is mean per5000step run',window='128001-129000 per run' if args.osi_last else '128001-133000 per run',repeats=2 if args.collision_extra or confirmed else 1,source='624717/C checkpoint step128000; frozen cumulative-optimization source',paired_comparison=paired),ensure_ascii=False,indent=2)+'\n')
fields=['name','label','MLUPS_total','total_s','steps','weighted_updates','vs_baseline','increment_percent','window_arithmetic_mean','window_std','window_min','window_max',equal_key,updates_key]
if args.independent: fields+=['time_ratio','time_saved_percent']+[key+'_s' for key in phase_keys]+['other_s']
if confirmed: fields+=['total_min_s','total_max_s']
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
if args.independent:
    phase_labels=['碰撞','边界','粗细插值','平均','通信','其余']
    colors=['#245ECC','#68A0DA','#009F9B','#7CBDB2','#FA9C40','#CBD8E9']
    components=phase_keys+['other']
    baseline=rows[0]['total_s']
    fig,ax=plt.subplots(figsize=(12.5,6.3))
    y=list(range(len(rows)));left=[0.0]*len(rows)
    for key,label,color in zip(components,phase_labels,colors):
        values=[r[key+'_s'] for r in rows]
        ax.barh(y,values,left=left,label=label,color=color,height=.62)
        left=[a+b for a,b in zip(left,values)]
    if confirmed:
        ax.errorbar([r['total_s'] for r in rows],y,xerr=[[r['total_s']-r['total_min_s'] for r in rows],[r['total_max_s']-r['total_s'] for r in rows]],fmt='none',ecolor='#263746',capsize=3,lw=1)
    ax.set_yticks(y,[r['label'] for r in rows]);ax.invert_yaxis()
    maximum=max(r['total_s'] for r in rows)
    for index,row in enumerate(rows):
        ax.text(row.get('total_max_s',row['total_s'])+maximum*.012,index,f'{row["total_s"]:.1f} s / {row["time_ratio"]*100:.1f}%',va='center',fontsize=10,color='#263746')
    ax.set_xlim(0,maximum*1.22)
    ax.set_xlabel('每次 5000 粗步平均推进耗时 / s（含重网格）' if confirmed else '5000 粗步累计推进耗时 / s（含重网格）')
    ax.set_title('单项优化与全部开启：分项耗时',loc='left',pad=18,fontsize=16)
    ax.xaxis.grid(True,color='#e3eaf0');ax.set_axisbelow(True)
    for name in ('top','right','left'): ax.spines[name].set_visible(False)
    ax.tick_params(axis='y',length=0,pad=12)
    ax.legend(ncol=6,loc='upper center',bbox_to_anchor=(.47,-.13),frameon=False)
    footer='输入：624717/C · checkpoint 128000 | step 128001–133000 | 单 A100 · FLOAT · D3Q27 · C 阈值\n每项独立相对 OSI 基线；标注百分比为总耗时 / 基线总耗时。其余含重网格未分列部分、相位推进等开销。'
    if confirmed: footer+='\n每组正/反顺序各一次5000步，取均值；误差线为两次结果范围，全部样本保留。'
    fig.text(.02,.02,footer,fontsize=9,color='#566675')
    fig.subplots_adjust(left=.25,right=.97,bottom=.25,top=.88)
    for extension in ('png','pdf','svg'): fig.savefig(dest/f'time_breakdown.{extension}',dpi=180)
    plt.close(fig)
    fig,ax=plt.subplots(figsize=(11.8,5.4))
    line_colors=['#245ECC','#009F9B','#68A0DA','#FA9C40','#9C68AE','#1B625B']
    for row,color in zip(rows,line_colors):
        ax.plot([p['step'] for p in row['windows']],[p['total'] for p in row['windows']],marker='o',markersize=4,label=row['label'],color=color,lw=2)
        if confirmed: ax.fill_between([p['step'] for p in row['windows']],[p['minimum'] for p in row['window_time_ranges']],[p['maximum'] for p in row['window_time_ranges']],color=color,alpha=.08)
    ax.set_xlabel('窗口结束 coarse step（每窗 500 步）');ax.set_ylabel('窗口推进耗时 / s')
    ax.set_title('相同 checkpoint：500 步窗口耗时曲线',loc='left',pad=16)
    ax.set_ylim(bottom=0);ax.ticklabel_format(axis='x',style='plain',useOffset=False)
    ax.grid(True,color='#e3eaf0');ax.legend(ncol=2,frameon=False,loc='lower right')
    footer='输入：624717/C step128000 | 单 A100 · FLOAT D3Q27 · C 阈值 | 动态 AMR 每 32 步重构\n'
    footer+=('每配置两次5000粗步；曲线为对应窗口均值，浅色带为两次范围，窗口不是独立重复。' if confirmed else '所有配置各运行一次 5000 粗步；曲线展示 10 个连续窗口，窗口不是独立重复。')
    fig.text(.02,.025,footer,fontsize=9,color='#566675')
    fig.subplots_adjust(left=.09,right=.97,bottom=.21,top=.88)
    for extension in ('png','pdf','svg'): fig.savefig(dest/f'window_times.{extension}',dpi=180)
    plt.close(fig)
    lines=['# 四项独立优化与全部开启：分项耗时','',f'原始材料：{out}。节点：{(out/"node.txt").read_text().strip()}。',
        '', '统一OSI、FLOAT/D3Q27、单rank/单A100、C阈值，从624717/C step128000恢复到133000。动态AMR每32步重构，关闭输出/收敛及嵌套OSI细分计时；通信重叠关闭，碰撞批量不采用。',
        '', '基线：平均与插值逐Box/任务发射、边界逐厚面任务发射、碰撞256线程、无碰撞坐标复用。四项分别独立相对同一基线开启，最后同时开启。仅坐标复用保持256线程；全开为128线程+坐标复用。',
        '', '平均仅切换跨Box批量启动与逐Box启动，覆盖界面平均和regrid完整平均；half-warp归约、block映射、GPU任务表构建、写回与同步不变。沿用前轮数值修复；不代表某一历史提交的完整复刻。',
        '', '每次total为十个500步窗口的推进墙钟和，含重网格、排除恢复/输出及窗口报告；若两轮已完成，图表为两次5000步耗时的均值。分项沿用perf(s)阶段墙钟；其余=total−碰撞−边界−插值−平均−通信，包含重网格未分列部分、OSI相位推进与未分列调度等。未进行分项百分比相加推导加速。',
        '', '| 配置 | 每次5000步平均total/s | 相对基线耗时 | 耗时减少 | 平均MLUPS_total |', '|---|---:|---:|---:|---:|']
    for row in rows: lines.append(f'| {row["label"]} | {row["total_s"]:.3f} | {row["time_ratio"]*100:.2f}% | {row["time_saved_percent"]:+.2f}% | {row["MLUPS_total"]:.2f} |')
    lines += ['', '各组先做真实checkpoint恢复后的64步动态AMR回归，逐文件核对同阶段完整valid DDF checkpoint，见validation.json。性能每窗口/每层calls与launch_cells及有效更新量一致；有效更新量由日志MLUPS_total×total恢复，受打印精度影响。',
        '', '不以小差异证明稳定收益，也不以64步回归证明长期物理等价或收敛。基线中保留未列出的现役优化。',
        '', '[累计分项图](time_breakdown.png) · [PDF](time_breakdown.pdf) · [窗口曲线](window_times.png) · [逐窗口CSV](windows.csv)',
        '', '复现（仓库根执行）：', '', '```bash', f'.venv/bin/python {Path(__file__).resolve().relative_to(repo)} {out} --independent --output-dir {dest}', '```']
    if confirmed:
        lines += ['', f'复测计划/原因：{json.loads(plan.read_text())["reason"]}。同分配内按反向顺序复测全部六组，记录GPU遥测。两轮全部保留，不剔除波动样本。表中total和分项为每次5000步的两轮均值，图中误差线/带为两次范围，不是置信区间。逐次结果见summary.json的individual_runs，逐窗口原始数值见windows.csv。',
            '', '每轮执行顺序：首轮基线→平均插值→边界→线程→坐标→全开；第二轮反向。离线绘图只读已完成结果；仅计划指定的CCS作业在检测到recheck_plan.json且未完成时，接续执行run.py --independent --recheck，保证复测在原节点/GPU，见recheck_plan.json、gpu_recheck.csv。']
    (dest/'report.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps([{k:r[k] for k in ('name','total_s','time_saved_percent','MLUPS_total')} for r in rows],indent=2))
    raise SystemExit(0)
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
if args.osi_last:
    footer='输入：624717/C checkpoint128000 | step128001–129000 | 单 A100 · FLOAT D3Q27 · 动态AMR每32步重构\n统计：10×100步累计更新量/累计推进耗时，含重网格；括号相对上一项。基线关闭平均/插值及边界批量。'
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
if args.osi_last:
    lines=['# 修订累加优化：OSI最后，1000步','',f'原始材料：{out}。节点：{(out/"node.txt").read_text().strip()}。',
        '', '每组从624717/C checkpoint128000恢复到129000，单A100/单rank、FLOAT/D3Q27、Re1000、C阈值；动态AMR每32步重构。各组仅一次1000步，十个100步窗口。稳定场出发不代表正式收敛，也不消除短测噪声。',
        '', '基线为A-B，关闭平均与插值批量、边界批量薄面，碰撞256线程。先累加平均插值、边界、碰撞128线程，最后切OSI。坐标复用只对OSI环形地址有效，最后一项合并OSI实现与坐标复用，不能把该增益全部归因于纯OSI切换。通信隐藏在单rank回退，本图不单列；碰撞批量仍作为历史额外实验，不采用。',
        '', '线程开关补齐A-B路径，掩码、计算公式和浮点运算顺序不变。沿用job626450冻结源码及数值修复，不混入工作区其他修改，不代表某一完整历史提交。平均两种路径共用同一归约和GPU任务表，只改变启动粒度。',
        '', '平均MLUPS=十窗口有效更新量之和/累计perf.total秒数/1e6，分子由打印MLUPS_total×total恢复；含重网格，排除初始化、恢复、输出和窗口报告。',
        '', '| 累加项 | MLUPS_total | 相邻变化 | 相对基线 | total/s | 碰撞工作量与基线一致 |','|---|---:|---:|---:|---:|---|']
    for row in rows:
        lines.append(f'| {row["label"]} | {row["MLUPS_total"]:.2f} | {row["increment_percent"]:+.2f}% | {row["vs_baseline"]:.3f}x | {row["total_s"]:.3f} | {row[equal_key]} |')
    lines += ['', '数值验证：每组另做64步动态AMR恢复，各组在64步和完整1000步后比较所有valid（含covered）及active DDF，结果见validation.json及各组run.log。A-B交换缓冲ghost存在残留差异，未宣称ghost全等；首轮全文件SHA失败证据另存job626485/checkpoint_byte_difference.json；同阶段完整valid比较见该目录same_stage_valid_comparison.json。checkpoint保存前在计时外统一执行完整平均和有限值检查，末尾不重复ComputeMacro；A-B与OSI的所有valid比较均在完整平均后进行。工程检查不等于长期物理等价或收敛证明。',
        '', '100步窗口不是独立重复，小幅差异不能证明稳定加速；不能把前轮5000步结果与本作业1000步结果直接作差归因。',
        '', '[主图PNG](optimization_ladder.png) · [PDF](optimization_ladder.pdf) · [逐窗口CSV](windows.csv)',
        '', '复现绘图（仓库根）：','','```bash',f'.venv/bin/python {Path(__file__).resolve().relative_to(repo)} {out} --osi-last --output-dir {dest}','```']
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
