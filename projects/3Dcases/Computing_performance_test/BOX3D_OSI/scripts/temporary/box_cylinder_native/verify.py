#!/usr/bin/env python3
# 本任务原生 BOX 数值验证与冻结程序检查；复用既有 FAB 读取器，不调用旧求解器。
from pathlib import Path
import sys,runpy,json,hashlib,re
import numpy as np
root=Path(sys.argv[1]).resolve()
case=Path(__file__).resolve().parents[3]
if len(sys.argv)>2 and sys.argv[2]=='gate':
    data=json.loads((root/'validation.json').read_text())
    assert data['passed']
    assert hashlib.sha256((root/'app.ex').read_bytes()).hexdigest()==data['app_sha256']
    assert hashlib.sha256((root/'inputs').read_bytes()).hexdigest()==data['inputs_sha256']
    print('正式 BOX 初始化实验数值门槛与程序/输入哈希通过')
    raise SystemExit(0)
fields=runpy.run_path(str(case/'scripts/temporary/ibm_switch/verify.py'),run_name='fab_reader')['fields']
# 数值核为双精度，标记沿用 BOX 的单精度；同时报告实际误差，不称为双精度粒子。
report={'passed':False,'comparison_tolerance':1e-9,'source':'formal BOX3D_OSI, Qian/Guo + existing four-point IBM + cached boundary faces',
        'old_solver_code_used':False,'initialization_is_model_switch':True,'comparisons':{}}
def compare(a,b,step,tol=1e-9):
    left=fields(root/a/f'chk{step:08d}','f_old');right=fields(root/b/f'chk{step:08d}','f_old')
    assert left.keys()==right.keys(),(a,b,'topology mismatch')
    error=max(float(np.abs(left[k]-right[k]).max()) for k in left)
    assert error<=tol,(a,b,error,tol)
    report['comparisons'][a+' vs '+b]={'ddf_linf':error,'valid_values':sum(v.size for v in left.values()),'tolerance':tol}
for a,b,step,tol in [('small_ab','small_osi',32,1e-9),('small_osi','small_restart',32,1e-9),('cavity_implicit','cavity_explicit',32,0),('full_ab','full_osi',520032,1e-9),('full_aligned_osi','full_aligned_restart',520064,1e-9)]:
    compare(a,b,step,tol)
converted=fields(root/'converted/chk00520000','f_old')
initial=fields(root/'full_osi/plt_520000','Cell')
assert converted.keys()==initial.keys()
ex=np.array([0,1,-1,0,0,1,-1,1,-1])[:,None,None,None]
ey=np.array([0,0,0,1,-1,1,-1,-1,1])[:,None,None,None]
error=0
for key,v in converted.items():
    rho=v.sum(axis=0)
    error=max(error,float(np.abs((v*ex).sum(axis=0)/rho-initial[key][0]).max()),float(np.abs((v*ey).sum(axis=0)/rho-initial[key][1]).max()))
assert error<1e-14,error
report['initial_velocity_linf']=error
proof=json.loads((root/'native_manifest.json').read_text())
assert all(proof['code_reuse_checks'].values())
report['code_reuse_checks']=proof['code_reuse_checks']
for name in ('seed','small_ab','small_osi','small_restart','cavity_implicit','cavity_explicit','full_ab','full_osi','full_aligned_osi','full_aligned_restart'):
    assert (root/name/'completed.txt').exists(),name
    assert hashlib.sha256((root/name/'app.ex').read_bytes()).hexdigest()==proof['app_sha256'],name
    assert 'finalized' in (root/name/'run.log').read_text(),name
# 检查原生边界任务确实缩减了实际发射 cell，而非全 Fab 扫描。
log=(root/'full_osi/run.log').read_text()
row=re.findall(r'perf_count: (.+)',log)[-1]
counts={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',row)}
assert 0<counts['boundary_launch_cells']<counts['boundary_full_cells']*.05,counts
report['boundary_counts']=counts
report['app_sha256']=hashlib.sha256((root/'app.ex').read_bytes()).hexdigest()
report['inputs_sha256']=hashlib.sha256((root/'inputs').read_bytes()).hexdigest()
report['scope']='boundary kernel/parameter checks, 32-step full AB/OSI, output-aligned 32→64 restart; not a long-term equivalence certification'
report['supplementary_64_ab_osi']='Earlier 64-step independent AB/OSI runs had one-Fab topology difference and common-Fab linf 1.87e-7; unresolved, not certified equivalent.'
report['passed']=True
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
