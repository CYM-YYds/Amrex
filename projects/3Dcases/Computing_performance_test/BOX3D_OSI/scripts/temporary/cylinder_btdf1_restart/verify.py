#!/usr/bin/env python3
# 本任务的阶段验证与最终报告；复用既有 FAB 解码器，保留以复现实验结论。
from pathlib import Path
import sys,runpy,json,re,hashlib,csv
import numpy as np
root=Path(sys.argv[1]).resolve()
case=Path(__file__).resolve().parents[3]
fields=runpy.run_path(str(case/'scripts/temporary/ibm_switch/verify.py'),run_name='fab_reader')['fields']
def compare(a,b,step,strict=True):
 left=fields(root/a/f'chk{step:08d}','f_old');right=fields(root/b/f'chk{step:08d}','f_old')
 assert left.keys()==right.keys(),(a,b,'topology mismatch')
 report={f'level_{lev}':max(float(np.abs(left[k]-right[k]).max()) for k in left if k[0]==f'Level_{lev}') for lev in range(4)}
 if strict: assert max(report.values())<=1e-12,report
 return report
if len(sys.argv)>2 and sys.argv[2]=='production':
 d=root/'production';log=(d/'run.log').read_text() if (d/'run.log').exists() else ''
 rows=[]
 for step,line in re.findall(r'^step(\d+) perf\(s\): (.+)$',log,re.M):
  vals={k:float(v) for k,v in re.findall(r'(\w+)=([-+\d.eE]+)',line)};vals['step']=int(step)
  vals['particle_all']=sum(vals.get(k,0) for k in ('particle_total','particle_reduce','particle_redistribute'))
  # total 与 solv 已包含 particle_total；子项不得再次相加。
  vals['particle_compute_share']=vals['particle_all']/vals['total'] if vals.get('total',0)>0 else 0
  rows.append(vals)
 if rows:
  with (d/'timing_windows.csv').open('w') as f:
   writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
 complete=(d/'completed.txt').exists() and 'AMReX (26.06) finalized' in log and 'STEP 584000starts' in log
 report={'complete':complete,'target_step':584000,'last_started_step':max(map(int,re.findall(r'^STEP (\d+)starts',log,re.M)),default=520000),'windows':len(rows),'statistical_steps':int(sum(r.get('steps',0) for r in rows)),'times_are_window_sums_not_per_step':True,'io_excluded_from_perf_total':True}
 for key in ('total','solv','particle_total','particle_macro','particle_force','particle_sum','particle_ghost','particle_reduce','particle_redistribute','boundary','collide','stream','average','interp','comm'):
  report[key+'_seconds']=sum(r.get(key,0) for r in rows)
 if rows:
  report['particle_all_seconds']=sum(r['particle_all'] for r in rows)
  report['particle_compute_share']=report['particle_all_seconds']/report['total_seconds']
 total=re.findall(r'^Total Time: (\S+)',log,re.M)
 if total: report['simulation_wall_including_init_output_seconds']=float(total[-1])
 report['flow_fields']=sorted(p.name for p in d.glob('plt_*') if p.is_dir() and (p/'Header').exists())
 if complete:
  final=fields(d/'plt_584000','Cell')
  assert all(np.isfinite(v).all() for v in final.values())
  assert report['statistical_steps']==64000
  assert len(re.findall(r'^\[FinalState\] level=',log,re.M))==4
  report['final_ddf_finite_check']='application FinalState checks all valid DDF on four levels'
 (d/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
 (d/'summary.md').write_text('二维圆柱 OSI 续算状态\n\n'+f"- 完成：{complete}\n- 最近启动步：{report['last_started_step']}；目标：584000\n- 统计窗口：{len(rows)}\n- 计算累计：{report['total_seconds']:.6f} s（不含流场与 checkpoint I/O）\n- 颗粒计算：{report['particle_total_seconds']:.6f} s\n- 颗粒归约/受力输出：{report['particle_reduce_seconds']:.6f} s\n- 颗粒迁移：{report['particle_redistribute_seconds']:.6f} s\n\nparticle_total 已包含 macro/force/sum/ghost；不可重复累加。计时包含 GPU 等待，细分同步及边界暂存分配影响性能，不能视作正式方腔实现的性能结论。\n")
 print(json.dumps(report,indent=2))
 sys.exit(0)
report={'ab_osi_64':compare('smoke_ab_accepted','smoke_osi_accepted',520064),'restart_32_to_64':compare('smoke_osi_accepted','native_restart_accepted',520064),'ab_osi_tolerance':1e-12,'native_restart_exact':True,'native_restart_diagnostic_only':False,'production_starts_from_original_520000_not_native_restart':True,'scope':'all valid DDF at each level, including covered; 64 coarse steps with dynamic regrid every 10','executable_sha256':hashlib.sha256((root/'app.ex').read_bytes()).hexdigest()}
# 用原始方向表直接恢复源检查点的速度，检查导入初始流场的所有 valid cell。
original=fields(root/'import/chk00520000','f_old')
initial=fields(root/'smoke_osi_accepted/plt_520000','Cell')
assert original.keys()==initial.keys()
ex=np.array([0,1,0,-1,0,1,-1,-1,1])[:,None,None,None]
ey=np.array([0,0,1,0,-1,1,1,-1,-1])[:,None,None,None]
import_error=max(max(float(np.abs((original[k]*ex).sum(axis=0)-initial[k][0]).max()),float(np.abs((original[k]*ey).sum(axis=0)-initial[k][1]).max())) for k in original)
assert import_error<=1e-14,import_error
report['import_velocity_linf']=import_error
for name in ('smoke_ab_accepted','smoke_osi_accepted','native_restart_accepted'):
 log=(root/name/'run.log').read_text();assert 'AMReX (26.06) finalized' in log and '[FinalState] level=3' in log,name
 assert hashlib.sha256((root/name/'app.ex').read_bytes()).hexdigest()==report['executable_sha256']
report['native_restart_exact']=max(report['restart_32_to_64'].values())==0
report['input_sha256']=hashlib.sha256((root/'inputs').read_bytes()).hexdigest()
(root/'validation_passed.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
