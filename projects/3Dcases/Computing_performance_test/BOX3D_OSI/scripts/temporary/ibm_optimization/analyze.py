#!/usr/bin/env python3
# 本次配对实验的复现分析，保留以复现报告。
import json, re, runpy, sys, statistics
from pathlib import Path
import numpy as np
root=Path(sys.argv[1]).resolve()
local_sum='--local-sum' in sys.argv[2:]
label='局部力汇总' if local_sum else '核权重复用'
case=Path(__file__).resolve().parents[3]
fields=runpy.run_path(str(case/'scripts/temporary/ibm_switch/verify.py'),run_name='fab_reader')['fields']
rows=[]; comparisons=[]
for pair in range(1,4):
    data={}
    for variant in ('baseline','optimized'):
        path=root/f'{variant}_{pair}'
        assert (path/'completed.txt').exists(),path
        text=(path/'run.log').read_text(); assert 'finalized' in text
        records=[{k:float(v) for k,v in re.findall(r'(\w+)=([\deE.+-]+)',record)}
                 for record in re.findall(r'perf\(s\): (.*)',text)]
        assert records and all(r['steps']==32 for r in records)
        # 每个 rank 都打印计时；按阶段取 rank 最大值，不能随机取最后一行或跨 rank 求和。
        row={k:(min if k.startswith('MLUPS') else max)(r[k] for r in records)
             for k in records[0]}
        row['rank_records']=len(records)
        row.update(variant=variant,pair=pair); rows.append(row)
        data[variant]=fields(path/'chk00584032','f_old',require_finite=False)
    a,b=data['baseline'],data['optimized']; assert a.keys()==b.keys(),'topology differs'
    fine_by_level={}
    for key in a:
        level=int(key[0].split('_')[1])
        if level>0: fine_by_level.setdefault(level-1,[]).append(np.array(key[1:])//2)
    fine_by_level={level:np.array(boxes) for level,boxes in fine_by_level.items()}
    error=0.; count=0; covered_nonfinite=0
    for key,left in a.items():
        level=int(key[0].split('_')[1]); x0,y0,z0,x1,y1,z1=key[1:]
        mask=np.ones(left.shape[1:],dtype=bool)
        fine=fine_by_level.get(level)
        if fine is not None:
            low=np.maximum(fine[:,:3],np.array([x0,y0,z0]))
            high=np.minimum(fine[:,3:],np.array([x1,y1,z1]))
            overlap=np.all(low<=high,axis=1)
            for lo,hi in zip(low[overlap],high[overlap]):
                mask[lo[2]-z0:hi[2]-z0+1,lo[1]-y0:hi[1]-y0+1,lo[0]-x0:hi[0]-x0+1]=False
        right=b[key]
        covered_nonfinite+=int(np.count_nonzero(~np.isfinite(left[:,~mask])))
        assert np.isfinite(left[:,mask]).all() and np.isfinite(right[:,mask]).all(),('uncovered nonfinite',key)
        if mask.any(): error=max(error,float(np.abs(left[:,mask]-right[:,mask]).max()))
        count+=int(mask.sum())*left.shape[0]
    assert error<1e-9,error
    comparisons.append({'pair':pair,'ddf_linf':error,'uncovered_values':count,'covered_nonfinite_baseline':covered_nonfinite})
summary={}
for key in ('particle_force','particle_total','particle_sum','total'):
    old=statistics.median(r[key] for r in rows if r['variant']=='baseline')
    new=statistics.median(r[key] for r in rows if r['variant']=='optimized')
    summary[key]={'baseline_median_s':old,'optimized_median_s':new,'reduction_percent':100*(old-new)/old}
report={'rows':rows,'comparisons':comparisons,'summary':summary,'scope':'fixed mesh; 584000 to 584032; 3 paired samples; rank/GPU count follows frozen command and run log; not long-run certification', 'optimization':label,'rank_aggregation':'maximum per timer across ranks; minimum MLUPS; stage maxima are not additive'}
validation_tag=next((arg.split('=',1)[1] for arg in sys.argv[2:]
                     if arg.startswith('--validation-tag=')),None)
if validation_tag is not None:
    checks=[]
    for ranks in (1,2):
        for name in ('dynamic2d','periodic2d','dynamic3d','periodic3d','full_dynamic'):
            path=root/f'{name}_{ranks}gpu{validation_tag}'
            assert (path/'completed.txt').exists(),path
            command=(path/'command.txt').read_text()
            assert 'ibm.verify_local_sum=true' in command
            text=(path/'run.log').read_text()
            assert 'finalized' in text and '[FinalState]' in text
            assert 'Assertion' not in text and 'amrex::Abort' not in text
            sizes=[tuple(map(int,match)) for match in re.findall(
                r'\[IBM local sum\] level=(\d+) source_boxes=(\d+) selected_boxes=(\d+) full_boxes=(\d+)',text)]
            assert sizes and all(source<=selected<=full for _,source,selected,full in sizes)
            if name in ('dynamic3d','full_dynamic'): assert len(sizes)>1,('no cache rebuild',path)
            checks.append({'case':name,'ranks':ranks,'cache_builds':len(sizes),
                           'sizes_level_source_selected_full':sizes,'full_force_reference_passed':True})
    report['force_validation']=checks
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n')
lines=[f'# IBM {label}：配对测试','',report['scope'],'','|计时项|原实现中位数(s)|优化后中位数(s)|减少(%)|','|---|---:|---:|---:|']
for key,v in summary.items(): lines.append(f"|{key}|{v['baseline_median_s']:.6f}|{v['optimized_median_s']:.6f}|{v['reduction_percent']:.2f}|")
if 'force_validation' in report:
    lines+=['',f"整层力场逐次参考对照通过：{len(report['force_validation'])} 项，详情见 report.json。"]
lines+=['',f"最终 DDF 最大逐值差：{max(c['ddf_linf'] for c in comparisons):.5g}",'','输入、程序 SHA 见 manifest.json；原始日志、命令、最终检查点保存在各配对目录。','比较仅覆盖未被细层覆盖的有效单元；粗层被覆盖区存在非有限存储值，已单独计数，不作为有效状态参与比较。', ('本次改变 SumBoundary 工作集合，保持 AMReX 本地与 MPI 累加机制。' if local_sum else '本次没有修改 SumBoundary，不能把其波动归因为核权重优化。')]
(root/'report.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(summary,indent=2)); print(comparisons)
