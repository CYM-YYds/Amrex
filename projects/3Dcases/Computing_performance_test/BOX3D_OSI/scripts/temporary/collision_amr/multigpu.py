# 本轮临时多GPU碰撞验证；二维只编译/回归。保留用于结果复现，不是正式运行入口。
from pathlib import Path
import hashlib,json,math,os,re,shutil,subprocess,sys
CASE=Path(__file__).resolve().parents[3]
REPO=CASE.parents[3]
EXP=CASE/'runs/temporary/collision_multigpu/round1'

def pairs(text):
    return dict(re.findall(r'(\w+)=([^\s]+)',text))

def assert_norms(directory,levels):
    text=(directory/'rank_0.log').read_text()
    lines=[line for line in text.splitlines() if line.startswith('ddf_cell_norm_level:')]
    assert len(lines)==levels,(directory.name,len(lines),levels)
    for line in lines:
        values=pairs(line)
        for key in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2']:
            assert math.isfinite(float(values[key])) and float(values[key])==0,(directory.name,key,values[key])

def run_job():
    out=EXP/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'multigpu.py')
    shutil.copy2(CASE/'scripts/temporary/collision_amr/submit.sh',out/'submit.sh')
    shutil.copy2(EXP/'baseline3d.ex',out/'baseline3d.ex')
    shutil.copy2(CASE/'config/inputs',out/'inputs3d');shutil.copy2(CASE/'config/inputs.2d',out/'inputs2d')
    build=EXP/'build'
    for label,dimension,source in [('candidate3d',3,EXP/'candidate_advance.cpp'),('baseline2d',2,EXP/'baseline_src/AmrCoreLBM_advance.cpp'),('candidate2d',2,EXP/'candidate_advance.cpp')]:
        shutil.copy2(source,build/'src/AmrCoreLBM_advance.cpp')
        (build/'src/AmrCoreLBM_advance.cpp').touch()
        with (out/f'{label}_compile.log').open('w') as log:
            subprocess.run(['make','-j8',f'DIM={dimension}','PRECISION=FLOAT'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        shutil.copy2(build/f'main{dimension}d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/f'{label}.ex')
        print('built',label,flush=True)
    assert hashlib.sha256((out/'baseline2d.ex').read_bytes()).digest()!=hashlib.sha256((out/'candidate2d.ex').read_bytes()).digest(), '2D candidate must be rebuilt'
    (out/'executables.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.ex')},indent=2)+'\n')
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{row.split()[0]} slots={row.split()[1]}\n' for row in alloc if len(row.split())>=2))
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']))
    (out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    (out/'topology.txt').write_bytes(subprocess.check_output(['nvidia-smi','topo','-m']))
    visible=os.environ.get('CUDA_VISIBLE_DEVICES','')
    devices=visible.split(',') if visible else ['0','1','2','3']
    assert len(devices)>=4,(visible,'four allocated GPUs required')
    (out/'allocation.json').write_text(json.dumps({'CUDA_VISIBLE_DEVICES':visible,'test_devices':devices[:4],'CCS_ALLOC_FILE_lines':alloc},indent=2)+'\n')
    wrapper=out/'rank.sh'
    wrapper.write_text('''#!/usr/bin/env bash
# 每rank独立日志和GPU绑定；不读取个人shell初始化文件。
set -uo pipefail
rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
local_rank=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}
IFS=, read -ra devices <<< "$BOX3D_TEST_GPU_LIST"
export CUDA_VISIBLE_DEVICES=${devices[$local_rank]}
printf '{"rank":%s,"local_rank":%s,"cuda_visible":"%s"}\\n' "$rank" "$local_rank" "$CUDA_VISIBLE_DEVICES" > "rank_${rank}_binding.json"
"$@" > "rank_${rank}.log" 2>&1
code=$?
printf '%s\\n' "$code" > "rank_${rank}.exit"
exit "$code"
''');wrapper.chmod(0o755)
    common=['stop_time=2500000','checkpoint.begin_step=0','checkpoint.chk_int=-1','checkpoint.chk_prefix=chk','checkpoint.keep_latest_only=false','checkpoint.write_particles=false',
            'amr.plot_int=-1','verification.convergence_enabled=false','verification.final_state_check=true','verification.osi_seed_pattern=true','verification.osi_ab_check=false',
            'performance.report_int=80','performance.osi_detail_timing=0','lbm.collide_mode=1','lbm.stream_mode=1','lbm.interp_mode=0','lbm.reynolds_number=1000',
            'lbm.osi_mpi_device_direct=0','lbm.osi_collide_comm_overlap=1','lbm.osi_mpi_pipeline_chunk_bytes=2097152']
    status=[]
    def run(name,mode,ranks,dimension,args):
        directory=out/name;directory.mkdir()
        executable=out/f'{mode}{dimension}d.ex'
        command=[str(REPO/'.venv/bin/python'),str(CASE/'scripts/guard_run.py'),'--','mpirun','-hostfile',str(out/'hostfile'),'-n',str(ranks),'-npernode',str(ranks),'-x','PATH','-x','LD_LIBRARY_PATH',
                 '--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart','/usr/bin/env','-u','BASH_ENV',f"BOX3D_TEST_GPU_LIST={','.join(devices[:4])}",'/bin/bash','--noprofile','--norc',str(wrapper),str(executable),str(out/f'inputs{dimension}d')]+common+args
        (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n')
        with (directory/'guard.log').open('w') as log:result=subprocess.run(command,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        row=dict(name=name,mode=mode,ranks=ranks,dimension=dimension,exit_code=result.returncode);status.append(row)
        (out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
        assert result.returncode==0,(name,result.returncode)
        for rank in range(ranks):assert (directory/f'rank_{rank}.exit').read_text().strip()=='0',(name,rank)
        assert len({json.loads((directory/f'rank_{rank}_binding.json').read_text())['cuda_visible'] for rank in range(ranks)})==ranks
        print('completed',name,flush=True)
        return directory
    uniform=['amr.n_cell=64','64','64','amr.max_level=0','amr.max_grid_size=16','amr.regrid_int=32','amr.blocking_factor_x=8','8','8','amr.blocking_factor_y=8','8','8','amr.blocking_factor_z=8','8','8','max_step=160']
    for ranks in [2,4]:
        for periodic in [0,1]:
            reference=CASE/f'runs/temporary/collision_factors/round1/job621730/check_n64_b16_p{periodic}_frozen/chk00000160'
            directory=run(f'uniform_r{ranks}_p{periodic}','candidate',ranks,3,uniform+[f'geometry.is_periodic={periodic}',str(periodic),str(periodic),f'verification.ddf_reference_checkpoint={reference}'])
            assert_norms(directory,1)
    source=CASE/'runs/a5_threshold_128000/job621436/B/chk00128000'
    real=['amr.n_cell=128','128','128','amr.max_level=2','amr.max_grid_size=128','256','512','amr.blocking_factor_x=16','16','16','amr.blocking_factor_y=16','16','16','amr.blocking_factor_z=16','16','16','amr.regrid_int=-1',
          'checkpoint.begin_step=128000',f'checkpoint.chk_prefix={source.parent}/chk','max_step=128032','verification.osi_seed_pattern=false','geometry.is_periodic=0','0','0','lbm.err=0.125','0.25','0.5','1','amr.plot_int=32','performance.report_int=16']
    reference=CASE/'runs/temporary/collision_amr/round1/job621733/local_restart_links/chk00128032'
    for ranks in [2,4]:
        directory=run(f'real_r{ranks}','candidate',ranks,3,real+[f'verification.ddf_reference_checkpoint={reference}']);assert_norms(directory,3)
    fixture=['amr.n_cell=32','32','32','amr.max_level=2','amr.max_grid_size=16','16','16','amr.blocking_factor_x=8','8','8','amr.blocking_factor_y=8','8','8','amr.blocking_factor_z=8','8','8','amr.regrid_int=8','verification.regrid_fixture=true','max_step=80','geometry.is_periodic=1','1','1','amr.plot_int=80']
    reference=CASE/'runs/temporary/collision_amr/round1/job621733/dynamic_p1_frozen/chk00000080'
    directory=run('dynamic_r4','candidate',4,3,fixture+[f'verification.ddf_reference_checkpoint={reference}']);assert_norms(directory,3)
    # 二维统一策略只检查数值，不做性能比较；覆盖无mask及动态AMR masked路径。
    for level in [0,1]:
        mesh=['amr.n_cell=32','32',f'amr.max_level={level}','amr.max_grid_size=16','16','amr.blocking_factor_x=8','8','amr.blocking_factor_y=8','8','amr.regrid_int=8','verification.regrid_fixture=true','geometry.is_periodic=1','1','max_step=80','amr.plot_int=80']
        baseline=run(f'2d_l{level}_baseline','baseline',2,2,mesh+['checkpoint.chk_int=80'])
        directory=run(f'2d_l{level}_candidate','candidate',2,2,mesh+[f'verification.ddf_reference_checkpoint={baseline}/chk00000080']);assert_norms(directory,level+1)
    (out/'checks.json').write_text(json.dumps({'full_ddf_comparisons':9,'3d_comparisons':7,'2d_comparisons':2,'all_valid_active_norms_zero':True,'2d_performance_tuned':False},indent=2)+'\n')
    # 新范围才做性能：2/4GPU、均匀/真实AMR，各三对交替。排除每次首窗。
    groups=[(2,'uniform'),(2,'amr'),(4,'uniform'),(4,'amr')]
    for repeat in [1,2,3]:
        for ranks,group in (groups if repeat!=2 else list(reversed(groups))):
            args=real+['max_step=128320','amr.plot_int=-1','performance.report_int=80'] if group=='amr' else uniform+['amr.n_cell=256','256','256','amr.max_grid_size=64','amr.regrid_int=0','max_step=1200','performance.report_int=200','geometry.is_periodic=0','0','0']
            args+=['verification.osi_seed_pattern=false','verification.final_state_check=false']
            for mode in (['baseline','candidate'] if repeat!=2 else ['candidate','baseline']):run(f'perf_{group}_r{ranks}_{repeat}_{mode}',mode,ranks,3,args)
    analyze(out)
    (out/'completed.txt').write_text('completed\n')

def analyze(out):
    status=json.loads((out/'status.json').read_text());assert all(row['exit_code']==0 for row in status)
    records=[];work={};memory=[]
    for row in status:
        name=row['name']
        if not name.startswith('perf_'):continue
        group=name.split('_')[1];ranks=row['ranks'];directory=out/name;steps_per=80 if group=='amr' else 200;expected=4 if group=='amr' else 6
        parsed=[];fingerprint=[]
        for rank in range(ranks):
            text=(directory/f'rank_{rank}.log').read_text()
            lines=[pairs(line) for line in text.splitlines() if ' perf(s):' in line];assert len(lines)==expected,(name,rank,len(lines))
            assert all(int(line['steps'])==steps_per for line in lines)
            parsed.append(lines)
            fingerprint.append([(d['lev'],d['calls'],d['launch_cells']) for d in (pairs(line) for line in text.splitlines() if ' collide_level:' in line)])
        key=(group,ranks)
        if key in work:assert work[key]==fingerprint,(name,'work counts differ')
        else:work[key]=fingerprint
        text=(directory/'rank_0.log').read_text()
        levels={int(lev):int(cells) for lev,cells in re.findall(r'Level\s+(\d+)\s+\d+ grids\s+(\d+) cells',text)};assert len(levels)==(3 if group=='amr' else 1),(name,levels)
        weighted=sum((1<<lev)*(cells-levels.get(lev+1,0)/8) for lev,cells in levels.items())
        for lines in parsed:
            for d in lines:assert abs(float(d['MLUPS_total'])*float(d['total'])*1e6/(weighted*steps_per)-1)<1e-6,(name,d)
        sums={key:sum(max(float(parsed[rank][window][key]) for rank in range(ranks)) for window in range(1,expected)) for key in ['total','collide','comm','average','interp','boundary']}
        records.append(dict(name=name,group=group,ranks=ranks,mode=row['mode'],steps=steps_per*(expected-1),updates=weighted*steps_per*(expected-1),**sums))
        # AMReX末尾已对rank给出min/max；记录整次allocator峰值，勿误作设备总显存。
        for label,pattern in [('arena_used_MB',r'\[The\s+Arena\] max space \(MB\) used\s+spread across MPI: \[(\d+) \.\.\. (\d+)\]'),('arena_reserved_MB',r'\[The\s+Arena\] max space \(MB\) allocated spread across MPI: \[(\d+) \.\.\. (\d+)\]')]:
            match=re.search(pattern,text);assert match,(name,label)
            memory.append(dict(name=name,metric=label,min_rank=int(match[1]),max_rank=int(match[2])))
    summary={}
    for group,ranks in [('uniform',2),('uniform',4),('amr',2),('amr',4)]:
        result={}
        for mode in ['baseline','candidate']:
            rows=[row for row in records if row['group']==group and row['ranks']==ranks and row['mode']==mode];assert len(rows)==3
            total=sum(row['total'] for row in rows);steps=sum(row['steps'] for row in rows)
            result[mode]=dict(MLUPS=sum(row['updates'] for row in rows)/total/1e6,total_ms=total/steps*1000,collide_ms=sum(row['collide'] for row in rows)/steps*1000,min_ms=min(row['total']/row['steps']*1000 for row in rows),max_ms=max(row['total']/row['steps']*1000 for row in rows))
        result['total_reduction_pct']=(1-result['candidate']['total_ms']/result['baseline']['total_ms'])*100
        summary[f'{group}_r{ranks}']=result
    (out/'summary.json').write_text(json.dumps({'summary':summary,'records':records},indent=2)+'\n');(out/'memory.json').write_text(json.dumps(memory,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)

def followup_2d():
    # 只修复二维增量构建；复用主轮基线checkpoint，不重做三维性能。
    source=EXP/f"job{os.environ['COORD_SOURCE_JOB']}"
    out=EXP/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'multigpu.py');shutil.copy2(source/'inputs2d',out/'inputs2d')
    shutil.copy2(source/'rank.sh',out/'rank.sh')
    build=EXP/'followup2d_build';assert not build.exists();shutil.copytree(EXP/'build',build)
    shutil.copy2(EXP/'candidate_advance.cpp',build/'src/AmrCoreLBM_advance.cpp');(build/'src/AmrCoreLBM_advance.cpp').touch()
    with (out/'candidate2d_compile.log').open('w') as log:
        subprocess.run(['make','-j8','DIM=2','PRECISION=FLOAT'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(build/'main2d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/'candidate2d.ex')
    checksum=hashlib.sha256((out/'candidate2d.ex').read_bytes()).hexdigest()
    assert checksum!=hashlib.sha256((source/'baseline2d.ex').read_bytes()).hexdigest()
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{r.split()[0]} slots={r.split()[1]}\n' for r in alloc if len(r.split())>=2))
    (out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']));(out/'node.txt').write_bytes(subprocess.check_output(['hostname']))
    visible=os.environ.get('CUDA_VISIBLE_DEVICES','');devices=visible.split(',') if visible else ['0','1'];assert len(devices)>=2
    checks=[]
    for level in [0,1]:
        directory=out/f'2d_l{level}_candidate';directory.mkdir()
        cmd=json.loads((source/f'2d_l{level}_candidate/command.json').read_text())
        mapping={str(source/'hostfile'):str(out/'hostfile'),str(source/'candidate2d.ex'):str(out/'candidate2d.ex'),str(source/'inputs2d'):str(out/'inputs2d'),str(source/'rank.sh'):str(out/'rank.sh')}
        cmd=[mapping.get(value,value) if not value.startswith('BOX3D_TEST_GPU_LIST=') else 'BOX3D_TEST_GPU_LIST='+','.join(devices[:2]) for value in cmd]
        (directory/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
        with (directory/'guard.log').open('w') as log:r=subprocess.run(cmd,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        assert r.returncode==0,(level,r.returncode)
        for rank in range(2):assert (directory/f'rank_{rank}.exit').read_text().strip()=='0'
        assert_norms(directory,level+1);checks.append(dict(level=level,all_valid_active_norms_zero=True))
    (out/'checks.json').write_text(json.dumps(dict(cases=checks,candidate_sha256=checksum,baseline_source_job=int(os.environ['COORD_SOURCE_JOB']),performance_tuned=False),indent=2)+'\n')
    (out/'completed.txt').write_text('completed\n');print('2D rebuilt and both comparisons passed',flush=True)

if __name__=='__main__':
    if sys.argv[1]=='run':run_job()
    elif sys.argv[1]=='analyze':analyze(Path(sys.argv[2]))
    elif sys.argv[1]=='2d':followup_2d()
    else:raise SystemExit('run | analyze OUT')
