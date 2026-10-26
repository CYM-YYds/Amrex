# 分析本次标签 A/B 实验；保留作复现证据，实验不再需要时可删除。
import json, re, statistics, sys
from pathlib import Path
run = Path(sys.argv[1]).resolve()
rows=[]
for d in sorted(run.glob('perf_*')):
    records={}
    text=(d/'run.log').read_text()
    for line in text.splitlines():
        m=re.search(r'step(\d+) perf\(s\): (.*)',line)
        if m:
            step=int(m[1]); vals={k:float(v) for k,v in re.findall(r'(\w+)=([\d.eE+\-]+)',m[2])}
            if step>64:
                dest=records.setdefault(step,{})
                for k,v in vals.items(): dest[k]=max(dest.get(k,0),v)
    if set(records)!={128,192,256}: raise ValueError(f'incomplete {d}: {records.keys()}')
    _,rank,trial,variant=d.name.split('_')
    row=dict(ranks=int(rank[1:]),trial=int(trial),variant=variant)
    for metric in ('average','total','solv'):
        row[metric]=sum(r[metric] for r in records.values())
    row['windows']=records
    rows.append(row)
summary={}
for ranks in sorted({r['ranks'] for r in rows}):
    summary[ranks]={}
    for metric in ('average','total','solv'):
        a=[r[metric] for r in rows if r['ranks']==ranks and r['variant']=='baseline']
        b=[r[metric] for r in rows if r['ranks']==ranks and r['variant']=='split']
        summary[ranks][metric]={'baseline':a,'split':b,'baseline_mean':statistics.mean(a),'split_mean':statistics.mean(b),'delta_pct':100*(statistics.mean(b)/statistics.mean(a)-1)}
(run/'analysis.json').write_text(json.dumps({'runs':rows,'summary':summary},indent=2))
print(json.dumps(summary,indent=2))
# 每层每 rank 的区域/block 数、逻辑标签字节数与构建时间，保留全部重构事件。
layout=[]
for d in sorted(run.glob('perf_*')):
    for line in (d/'run.log').read_text().splitlines():
        if 'TAG_LAYOUT' in line:
            vals=dict(re.findall(r'(\w+)=([\d.eE+\-]+)',line))
            layout.append({'run':d.name,'raw':line,**vals})
(run/'layout.json').write_text(json.dumps(layout,indent=2))
# 确认同一 rank 数下每轮的工作量一致，包含各层碰撞单元量。
workloads={}
for d in sorted(run.glob('perf_*')):
    lines=(d/'run.log').read_text().splitlines()
    signature=sorted(re.sub(r' (?:seconds|launch_MLUPS)=[^ ]+', '', x) for x in lines if re.search(r'step(?:128|192|256) (?:perf_count:|collide_level:)',x))
    rank=d.name.split('_')[1]
    if rank in workloads and signature!=workloads[rank]: raise ValueError(f'workload mismatch {d}')
    workloads[rank]=signature
(run/'workload.json').write_text(json.dumps(workloads,indent=2))
report=['# Restriction 标签布局 A/B 实测', '', '窗口：排除前 64 步，统计 65–256 步；每窗口取较慢 rank。average 为完整平均阶段，含结果写回；total 含重构，不含初始化。', '', '| ranks | metric | baseline mean (s) | split mean (s) | delta |', '|---:|---|---:|---:|---:|']
for ranks, metrics in summary.items():
    for metric, v in metrics.items():
        report.append(f"| {ranks} | {metric} | {v['baseline_mean']:.6f} | {v['split_mean']:.6f} | {v['delta_pct']:+.2f}% |")
report += ['', '## 每层每 rank 的标签存储范围', '', '| level | baseline bytes 范围 | split bytes 范围 |', '|---:|---:|---:|']
for lev in sorted({int(x['lev']) for x in layout}):
    values=[]
    for variant in ('baseline','split'):
        nums=[int(x['bytes']) for x in layout if int(x['lev'])==lev and x['run'].endswith(variant)]
        values.append(f'{min(nums)}–{max(nums)}')
    report.append(f'| {lev} | {values[0]} | {values[1]} |')
report += ['', '逐次原始值见 layout.json，重复运行的完整分布见 analysis.json；字节为逻辑标签载荷，不含分配器预留。构建时间为生成及上传标签并同步的耗时，含分配成本，未分离 GPU kernel 时间。', '', '仅覆盖本次固定输入、64³ 基网格、三层 AMR、每 32 步重构、当前 D3Q27 线程映射；不能推广为所有生产布局的结果。数值比较见 check_* 目录。']
(run/'report.md').write_text('\n'.join(report)+'\n')
