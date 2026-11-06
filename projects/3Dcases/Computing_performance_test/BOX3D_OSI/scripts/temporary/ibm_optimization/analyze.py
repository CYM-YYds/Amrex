#!/usr/bin/env python3
# 本次配对实验的复现分析，保留以复现报告。
import json, re, runpy, sys, statistics
from pathlib import Path
import numpy as np
root=Path(sys.argv[1]).resolve()
case=Path(__file__).resolve().parents[3]
fields=runpy.run_path(str(case/'scripts/temporary/ibm_switch/verify.py'),run_name='fab_reader')['fields']
rows=[]; comparisons=[]
for pair in range(1,4):
    data={}
    for variant in ('baseline','optimized'):
        path=root/f'{variant}_{pair}'
        assert (path/'completed.txt').exists(),path
        text=(path/'run.log').read_text(); assert 'finalized' in text
        record=re.findall(r'perf\(s\): (.*)',text)[-1]
        row={k:float(v) for k,v in re.findall(r'(\w+)=([\deE.+-]+)',record)}
        row.update(variant=variant,pair=pair); rows.append(row)
        data[variant]=fields(path/'chk00584032','f_old',require_finite=False)
    a,b=data['baseline'],data['optimized']; assert a.keys()==b.keys(),'topology differs'
    error=0.; count=0; covered_nonfinite=0
    for key,left in a.items():
        level=int(key[0].split('_')[1]); x0,y0,z0,x1,y1,z1=key[1:]
        mask=np.ones(left.shape[1:],dtype=bool)
        for fine in a:
            if fine[0]!=f'Level_{level+1}': continue
            fx0,fy0,fz0,fx1,fy1,fz1=fine[1:]
            low=(max(x0,fx0//2),max(y0,fy0//2),max(z0,fz0//2))
            high=(min(x1,fx1//2),min(y1,fy1//2),min(z1,fz1//2))
            if all(l<=h for l,h in zip(low,high)):
                mask[low[2]-z0:high[2]-z0+1,low[1]-y0:high[1]-y0+1,low[0]-x0:high[0]-x0+1]=False
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
report={'rows':rows,'comparisons':comparisons,'summary':summary,'scope':'single GPU; fixed mesh; 584000 to 584032; 3 paired samples; not long-run or multi-GPU certification'}
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['# IBM 核权重复用：配对测试','',report['scope'],'','|计时项|原实现中位数(s)|优化后中位数(s)|减少(%)|','|---|---:|---:|---:|']
for key,v in summary.items(): lines.append(f"|{key}|{v['baseline_median_s']:.6f}|{v['optimized_median_s']:.6f}|{v['reduction_percent']:.2f}|")
lines+=['',f"最终 DDF 最大逐值差：{max(c['ddf_linf'] for c in comparisons):.5g}",'','输入、程序 SHA 见 manifest.json；原始日志、命令、最终检查点保存在各配对目录。','比较仅覆盖未被细层覆盖的有效单元；粗层被覆盖区存在非有限存储值，已单独计数，不作为有效状态参与比较。', '本次没有修改 SumBoundary，不能把其波动归因为核权重优化。']
(root/'report.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(summary,indent=2)); print(comparisons)
