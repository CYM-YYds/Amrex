# 本轮临时验收与性能分析入口；逐值检查通过后才允许性能矩阵。
from pathlib import Path
import json,re,sys,math
out=Path(sys.argv[1]);status=[l.split('\t') for l in (out/'status.tsv').read_text().splitlines()[1:]]
assert all(s[2]=='0' for s in status),status
compared=[]
for name,mode,code in status:
 if name.startswith(('uniform_perf_','amr_perf_')) or name=='uniform_lockstep' or mode=='frozen':continue
 txt=(out/name/'run.log').read_text();lines=[l for l in txt.splitlines() if l.startswith('ddf_cell_norm_level:')]
 levels=1 if name.startswith('uniform_') else 3
 assert len(lines)==levels,(name,len(lines))
 for line in lines:
  d=dict(re.findall(r'(\w+)=([^ ]+)',line))
  for key in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2']:
   assert math.isfinite(float(d[key])) and float(d[key])==0,(name,key,d[key])
 compared.append(name)
assert len(compared)==20,len(compared)
lock=(out/'uniform_lockstep/run.log').read_text();assert 'AMReX (26.06) finalized' in lock
assert not re.search(r'\bmismatch\b.*[1-9]',lock,re.I),lock[-1000:]
(out/'checks.json').write_text(json.dumps({'comparisons':compared,'count':len(compared),'valid_active_all_zero':True,'uniform_lockstep_completed':True},indent=2)+'\n')
if '--checks-only' in sys.argv:print('20 full-DDF comparisons zero; uniform lockstep completed');sys.exit(0)
records=[];fingerprints={}
for group,modes in [('uniform',['frozen','production']),('amr',['production','0','1','2','3'])]:
 for repeat in [1,2,3]:
  for mode in modes:
   name=f'{group}_perf_{repeat}_{mode}' if group=='uniform' else f'amr_perf_{repeat}_m{mode}'
   txt=(out/name/'run.log').read_text();steps_per=200 if group=='uniform' else 80
   lines=[l for l in txt.splitlines() if ' perf(s):' in l];expected=6 if group=='uniform' else 4
   assert len(lines)==expected,(name,len(lines))
   levels={int(lev):int(n) for lev,n in re.findall(r'Level\s+(\d+)\s+\d+ grids\s+(\d+) cells',txt)}
   assert len(levels)==(1 if group=='uniform' else 3),(name,levels)
   weighted=sum((1<<lev)*(n-(levels.get(lev+1,0)/8)) for lev,n in levels.items())
   parsed=[dict(re.findall(r'(\w+)=([^ ]+)',l)) for l in lines]
   assert all(int(d['steps'])==steps_per for d in parsed)
   for d in parsed:
    assert abs(float(d['MLUPS_total'])*float(d['total'])*1e6/(weighted*steps_per)-1)<1e-6,(name,d)
   total=sum(float(d['total']) for d in parsed[1:]);solv=sum(float(d['solv']) for d in parsed[1:]);collide=sum(float(d['collide']) for d in parsed[1:]);boundary=sum(float(d['boundary']) for d in parsed[1:])
   calls=[dict(re.findall(r'(\w+)=([^ ]+)',l)) for l in txt.splitlines() if ' collide_level:' in l]
   for d in calls:assert int(d['calls'])==steps_per*(1<<int(d['lev'])),(name,d)
   shape=tuple((d['lev'],d['calls'],d['launch_cells']) for d in calls)
   if group not in fingerprints:fingerprints[group]=shape
   else:assert fingerprints[group]==shape,(name,'collision work counts differ')
   records.append({'group':group,'mode':mode,'repeat':repeat,'steps':steps_per*(expected-1),'updates':weighted*steps_per*(expected-1),'total_s':total,'solv_s':solv,'collide_s':collide,'boundary_s':boundary,'mesh_cells':levels,'weighted_updates_per_coarse_step':weighted})
summary={}
for group,modes in [('uniform',['frozen','production']),('amr',['production','0','1','2','3'])]:
 summary[group]={}
 for mode in modes:
  rows=[r for r in records if r['group']==group and r['mode']==mode];total=sum(r['total_s'] for r in rows);steps=sum(r['steps'] for r in rows)
  summary[group][mode]={'MLUPS':sum(r['updates'] for r in rows)/total/1e6,'ms_per_coarse_step':total/steps*1000,'collide_ms_per_coarse_step':sum(r['collide_s'] for r in rows)/steps*1000,'boundary_ms_per_coarse_step':None if group=='uniform' else sum(r['boundary_s'] for r in rows)/steps*1000,'min_ms':min(r['total_s']/r['steps']*1000 for r in rows),'max_ms':max(r['total_s']/r['steps']*1000 for r in rows)}
(out/'summary.json').write_text(json.dumps({'records':records,'summary':summary},indent=2)+'\n');print(json.dumps(summary,indent=2))
