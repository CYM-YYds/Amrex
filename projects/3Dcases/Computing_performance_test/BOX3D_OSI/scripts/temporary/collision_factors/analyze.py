# 本轮临时检查与分析；数值回归为性能矩阵的先决条件。
from pathlib import Path
import sys,re,json,math,hashlib,collections
out=Path(sys.argv[1])
if '--sass' in sys.argv:
    source=Path(sys.argv[sys.argv.index('--sass')+1]).read_text()
    data={};saved=[]
    for section in source.split('Function : ')[1:]:
        name=section.splitlines()[0].strip()
        if 'CollideLayoutLevelILb0' not in name:continue
        match=re.search(r'DdfLayoutEiEUliii(E[0-2]?_)Li3',name)
        if not match:continue
        mode={'E_':'0','E0_':'1','E1_':'2','E2_':'3'}[match[1]]
        instructions=re.findall(r'/\*[0-9a-f]+\*/\s+([^;]+);',section)
        normalized=[i.strip() for i in instructions]
        counts=collections.Counter()
        for instruction in normalized:
            m=re.match(r'(?:@\S+\s+)?([A-Z][A-Z0-9.]*)',instruction)
            if m:counts[m[1]]+=1
        data[mode]={'kernel':name,'static_instructions':len(normalized),'instruction_sha256':hashlib.sha256('\n'.join(normalized).encode()).hexdigest(),'opcodes':dict(counts)}
        saved.append('Function : '+section)
    assert set(data)=={'0','1','2','3'},data
    (out/'collision_kernels.sass').write_text('\n'.join(saved))
    (out/'sass_summary.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({k:{q:v for q,v in d.items() if q not in ['kernel','opcodes']} for k,d in data.items()},indent=2));sys.exit(0)
rows=(out/'status.tsv').read_text().strip().splitlines()[1:]
checks=[]
for row in rows:
 name,mode,code=row.split('\t');assert code=='0',(name,code)
 if name.startswith(('check_','restart_')):
  p=out/name;txt=(p/'run.log').read_text()
  assert 'AMReX (26.06) finalized' in txt,name
  if mode!='frozen':
   lines=[l for l in txt.splitlines() if l.startswith('ddf_cell_norm_level:')]
   assert len(lines)==1,(name,lines)
   d=dict(re.findall(r'(\w+)=([^ ]+)',lines[0]))
   for k in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2']:
    assert math.isfinite(float(d[k])) and float(d[k])==0,(name,k,d[k])
   checks.append(name)
assert len(checks)==20,len(checks)
(out/'checks.json').write_text(json.dumps({'checkpoint_comparisons':len(checks),'all_valid_active_norms_zero':True,'cases':checks},indent=2)+'\n')
if '--checks-only' in sys.argv:print('20 checkpoint comparisons: all valid/active norms exactly zero');sys.exit(0)
perf=[]
for repeat in range(1,6):
 for mode in ['frozen','0','1','2','3']:
  name=f'perf_{repeat}_m{mode}';txt=(out/name/'run.log').read_text()
  timings=[l for l in txt.splitlines() if ' perf(s):' in l]
  stages=[l for l in txt.splitlines() if ' uniform_stage_gpu(ms/step):' in l]
  calls=[l for l in txt.splitlines() if ' collide_level:' in l]
  assert len(timings)==len(stages)==len(calls)==6,name
  for line in calls:
   assert 'calls=200 ' in line and 'launch_cells=3355443200 ' in line,name
  assert all('samples=200 ' in l for l in stages),name
  total=sum(float(re.search(r' total=([\d.]+)',l)[1]) for l in timings[1:])
  collision=sum(float(re.search(r'collide_sum=([\d.]+)',l)[1]) for l in stages[1:])/5
  boundary=sum(float(re.search(r'boundary=([\d.]+)',l)[1]) for l in stages[1:])/5
  perf.append({'repeat':repeat,'mode':mode,'seconds_1000_steps':total,'MLUPS':256**3*1000/total/1e6,'collision_ms':collision,'boundary_ms':boundary})
summary={}
for mode in ['frozen','0','1','2','3']:
 group=[r for r in perf if r['mode']==mode]
 summary[mode]={'MLUPS':256**3*5000/sum(r['seconds_1000_steps'] for r in group)/1e6,'ms_step':sum(r['seconds_1000_steps'] for r in group)/5,'collision_ms':sum(r['collision_ms'] for r in group)/5,'boundary_ms':sum(r['boundary_ms'] for r in group)/5,'min_ms_step':min(r['seconds_1000_steps'] for r in group),'max_ms_step':max(r['seconds_1000_steps'] for r in group)}
(out/'summary.json').write_text(json.dumps({'rows':perf,'summary':summary},indent=2)+'\n');print(json.dumps(summary,indent=2))
