# 本轮临时分析入口；保留结果期间作为复现依赖。
from pathlib import Path
import re,json,sys
out=Path(sys.argv[1]);rows=[]
status=(out/'status.tsv').read_text().strip().splitlines()[1:]
assert len(status)==11 and all(line.endswith('\t0') for line in status),status
for repeat in range(1,4):
 for mode in ['amrex','flat128','flat256']:
  p=out/f'perf_{repeat}_{mode}/run.log';txt=p.read_text()
  assert json.loads((p.parent/'exit_status.json').read_text())['exit_code']==0
  if mode=='amrex':
   lines=[l for l in txt.splitlines() if ' perf(s):' in l]
   assert len(lines)==6
   total=sum(float(re.search(r' total=([\d.]+)',l)[1]) for l in lines[1:])
   stages=[l for l in txt.splitlines() if 'uniform_stage_gpu(ms/step)' in l][1:]
   coll=sum(float(re.search(r'collide_sum=([\d.]+)',l)[1]) for l in stages)/5
   bc=sum(float(re.search(r'boundary=([\d.]+)',l)[1]) for l in stages)/5
  else:
   times=re.findall(r'total_time = ([\d.]+) ms',txt);assert len(times)==6
   total=sum(map(float,times[1:]))/1000
   stages=re.findall(r'STAGE ms_per_step boundary=([\d.]+) collision=([\d.]+)',txt);assert len(stages)==6
   bc=sum(float(b) for b,c in stages[1:])/5;coll=sum(float(c) for b,c in stages[1:])/5
   assert 'FINITE rho_min=' in txt
  rows.append({'repeat':repeat,'mode':mode,'total_s_1000_steps':total,'ms_per_step':total,'MLUPS':256**3*1000/total/1e6,'collision_ms':coll,'boundary_ms':bc})
summary={m:{'MLUPS':256**3*3000/sum(r['total_s_1000_steps'] for r in rows if r['mode']==m)/1e6,'ms_per_step':sum(r['ms_per_step'] for r in rows if r['mode']==m)/3,'collision_ms':sum(r['collision_ms'] for r in rows if r['mode']==m)/3,'boundary_ms':sum(r['boundary_ms'] for r in rows if r['mode']==m)/3} for m in ['amrex','flat128','flat256']}
(out/'summary.json').write_text(json.dumps({'rows':rows,'summary':summary},indent=2)+'\n')
print(json.dumps(summary,indent=2))
