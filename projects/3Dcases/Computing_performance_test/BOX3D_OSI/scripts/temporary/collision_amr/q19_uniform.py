# 本轮临时Q19同模型对照：AMReX和师兄框架均使用包含q0的标准密度BGK；正式Q27不变。
from pathlib import Path
import hashlib,json,math,os,re,shlex,shutil,subprocess,sys
CASE=Path(__file__).resolve().parents[3];REPO=CASE.parents[3]
EXP=CASE/'runs/temporary/q19_uniform/round1'

def values(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))

def run_job():
    out=EXP/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'q19_uniform.py');shutil.copy2(EXP/'inputs',out/'inputs');shutil.copy2(EXP/'q27.ex',out/'q27.ex')
    shutil.copytree(EXP/'build/src',out/'q19_src')
    (EXP/'build/src/LbmLattice.H').touch()
    with (out/'q19_compile.log').open('w') as log:subprocess.run(['make','-j8','DIM=3','PRECISION=FLOAT'],cwd=EXP/'build',stdout=log,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(EXP/'build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/'q19.ex')
    print('built AMReX standard Q19',flush=True)
    flags=[]
    for flag in shlex.split(subprocess.check_output(['mpicxx','--showme:compile'],text=True))+shlex.split(subprocess.check_output(['mpicxx','--showme:link'],text=True)):
        if flag=='-pthread':flags+=['-Xcompiler',flag]
        elif flag.startswith('-Wl,'):flags+=['-Xlinker',flag[4:]]
        else:flags.append(flag)
    for name,source in [('flat',EXP/'flat/main_q19.cu'),('flat_verify',EXP/'flat/verify.cu')]:
        command=['nvcc','-O3','--use_fast_math','-std=c++17','-arch=sm_80','-Xptxas=-v']+flags+[str(source),'-o',str(out/f'{name}.ex')]
        (out/f'{name}_compile.command.json').write_text(json.dumps(command,indent=2)+'\n')
        with (out/f'{name}_compile.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    (out/'executables.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.ex')},indent=2)+'\n')
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{s.split()[0]} slots={s.split()[1]}\n' for s in alloc if len(s.split())>=2))
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']));(out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    status=[]
    def run(name,mode,args):
        directory=out/name;directory.mkdir()
        command=[str(REPO/'.venv/bin/python'),str(CASE/'scripts/guard_run.py'),'--','mpirun','-hostfile',str(out/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart','/usr/bin/env','-u','BASH_ENV','CUDA_VISIBLE_DEVICES=0',str(out/f'{mode}.ex')]+args
        (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n')
        with (directory/'guard.log').open('w') as log:r=subprocess.run(command,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        status.append(dict(name=name,mode=mode,exit_code=r.returncode));(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
        assert r.returncode==0,(name,r.returncode)
        print('completed',name,flush=True)
        return directory
    run('flat_collision_reference','flat',['--verify'])
    run('flat_full_reference','flat_verify',[])
    probe=(out/'flat_full_reference/run.log').read_text();assert probe.count('mapping=exact')==6
    common=[str(out/'inputs'),'stop_time=2500000','checkpoint.begin_step=0','amr.n_cell=64','64','64','amr.max_level=0','amr.max_grid_size=16','amr.blocking_factor_x=16','amr.blocking_factor_y=16','amr.blocking_factor_z=16','amr.regrid_int=0',
            'max_step=160','amr.plot_int=-1','checkpoint.chk_int=-1','checkpoint.chk_prefix=chk','checkpoint.keep_latest_only=false','checkpoint.write_particles=false','verification.convergence_enabled=false','verification.final_state_check=true',
            'verification.osi_seed_pattern=true','verification.osi_ab_check=false','performance.report_int=80','performance.osi_detail_timing=0','lbm.reynolds_number=1000','lbm.collide_mode=1','lbm.stream_mode=1']
    comparisons=[]
    for periodic in [0,1]:
        geometry=[f'geometry.is_periodic={periodic}',str(periodic),str(periodic)]
        reference=run(f'q19_p{periodic}_ab','q19',common+geometry+['lbm.stream_mode=0','checkpoint.chk_int=160'])
        lattice=(reference/'chk00000160/Lattice').read_text();assert re.search(r'\b3\s+19\s+0\b',lattice),lattice
        candidate=run(f'q19_p{periodic}_osi','q19',common+geometry+[f'verification.ddf_reference_checkpoint={reference}/chk00000160'])
        lines=[line for line in (candidate/'run.log').read_text().splitlines() if line.startswith('ddf_cell_norm_level:')];assert len(lines)==1
        d=values(lines[0]);assert all(math.isfinite(float(d[key])) and float(d[key])==0 for key in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2'])
        comparisons.append(candidate.name)
    (out/'checks.json').write_text(json.dumps(dict(amrex_q19_ab_osi=comparisons,all_valid_active_norms_zero=True,teacher_double_reference_passed=True,teacher_full_grid_phases=6,q0_included=True),indent=2)+'\n')
    perf=common+['amr.n_cell=256','256','256','amr.max_grid_size=256','max_step=1200','performance.report_int=200','geometry.is_periodic=0','0','0','verification.osi_seed_pattern=false','verification.final_state_check=false']
    for repeat,order in enumerate([['q27','q19','flat'],['flat','q19','q27'],['q19','q27','flat']],1):
        for mode in order:run(f'perf_{repeat}_{mode}',mode,[] if mode=='flat' else perf)
    analyze(out)
    (out/'completed.txt').write_text('completed\n')

def analyze(out):
    status=json.loads((out/'status.json').read_text());assert all(row['exit_code']==0 for row in status)
    records=[];memory=[];work=None
    for repeat in [1,2,3]:
        for mode in ['q19','q27','flat']:
            directory=out/f'perf_{repeat}_{mode}';text=(directory/'run.log').read_text()
            if mode=='flat':
                windows=re.findall(r'total_time = ([\d.]+) ms',text);assert len(windows)==6
                seconds=sum(float(value) for value in windows[1:])/1000
                stages=re.findall(r'STAGE ms_per_step boundary=([\d.]+) collision=([\d.]+)',text);assert len(stages)==6
                boundary=sum(float(b) for b,c in stages[1:])/5;collision=sum(float(c) for b,c in stages[1:])/5
                assert 'FINITE rho_min=' in text
            else:
                lines=[values(line) for line in text.splitlines() if ' perf(s):' in line];assert len(lines)==6 and all(int(d['steps'])==200 for d in lines)
                seconds=sum(float(d['total']) for d in lines[1:])
                stages=[values(line) for line in text.splitlines() if 'uniform_stage_gpu(ms/step)' in line];assert len(stages)==6
                assert all(int(d['samples'])==200 for d in stages)
                collision=sum(float(d['collide_sum']) for d in stages[1:])/5;boundary=sum(float(d['boundary']) for d in stages[1:])/5
                shape=[(d['lev'],d['calls'],d['launch_cells']) for d in (values(line) for line in text.splitlines() if ' collide_level:' in line)]
                if work is None:work=shape
                else:assert work==shape,(mode,'lattice work differs')
                for d in lines:assert abs(float(d['MLUPS_total'])*float(d['total'])*1e6/(256**3*200)-1)<1e-6
                used=re.search(r'\[The\s+Arena\] max space \(MB\) used\s+spread across MPI: \[(\d+)',text);reserved=re.search(r'\[The\s+Arena\] max space \(MB\) allocated spread across MPI: \[(\d+)',text);assert used and reserved
                memory.append(dict(mode=mode,repeat=repeat,arena_peak_used_MB=int(used[1]),arena_reserved_MB=int(reserved[1])))
            records.append(dict(mode=mode,repeat=repeat,steps=1000,total_s=seconds,collision_ms=collision,boundary_ms=boundary))
    summary={}
    for mode in ['q19','q27','flat']:
        rows=[r for r in records if r['mode']==mode];seconds=sum(r['total_s'] for r in rows)
        summary[mode]=dict(MLUPS=256**3*3000/seconds/1e6,total_ms=seconds/3,collision_ms=sum(r['collision_ms'] for r in rows)/3,boundary_ms=sum(r['boundary_ms'] for r in rows)/3,min_ms=min(r['total_s'] for r in rows),max_ms=max(r['total_s'] for r in rows))
    (out/'summary.json').write_text(json.dumps(dict(summary=summary,records=records),indent=2)+'\n');(out/'memory.json').write_text(json.dumps(memory,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)

if __name__=='__main__':
    if sys.argv[1]=='run':run_job()
    elif sys.argv[1]=='analyze':analyze(Path(sys.argv[2]))
    else:raise SystemExit('run | analyze OUT')
