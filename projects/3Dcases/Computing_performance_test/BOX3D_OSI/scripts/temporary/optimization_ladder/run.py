"""AI 临时实验驱动；保留以复现优化图，任务废弃且无复现依赖时可删除。"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

CASE = Path(__file__).resolve().parents[3]
REPO = CASE.parents[3]
EXTRA = '--collision-extra' in sys.argv
INDEPENDENT = '--independent' in sys.argv
OSI_LAST = '--osi-last' in sys.argv
assert sum((EXTRA, INDEPENDENT, OSI_LAST)) <= 1
EXP = CASE/'runs/temporary/optimization_ladder'/('round4_osi_last_1000' if OSI_LAST else 'round3_independent' if INDEPENDENT else 'round2_collision' if EXTRA else 'round1')
OUT = EXP/f'job{os.environ["CCS_JOB_ID"]}'
RECHECK = '--recheck' in sys.argv
BUILD=EXP/'build'
source=CASE/'runs/a5_C_t1000_gif/job624717/C/chk00128000'
if not RECHECK:
    OUT.mkdir()
    for name in ('inputs','git_head.txt','worktree.diff','experiment.diff','purpose.json'):
        shutil.copy2(EXP/name,OUT/name)
    shutil.copytree(BUILD/'src',OUT/'src')
    shutil.copytree(BUILD/'config',OUT/'config')
    shutil.copytree(Path(__file__).parent,OUT/'driver')
    for command, name in [(['hostname'],'node.txt'),(['nvidia-smi'],'gpu.txt')]:
        with (OUT/name).open('w') as f: subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True)
    with (OUT/'environment.json').open('w') as f:
        json.dump({k:os.environ.get(k) for k in ('PATH','LD_LIBRARY_PATH','CUDA_VISIBLE_DEVICES','CCS_ALLOC_FILE','LOADEDMODULES')},f,indent=2)
    with (OUT/'compile.log').open('w') as f:
        subprocess.run(['make','-j8','DIM=3','PRECISION=FLOAT'],cwd=BUILD,stdout=f,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(BUILD/'main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',OUT/'experiment.ex')
    (OUT/'executable.sha256').write_text(hashlib.sha256((OUT/'experiment.ex').read_bytes()).hexdigest()+'  experiment.ex\n')
    # 原始 checkpoint 只读，新输出写本作业目录。
    (OUT/'checkpoint_source.json').write_text(json.dumps({str(p.relative_to(source)):dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in source.rglob('*') if p.is_file()},indent=2)+'\n')
    with (OUT/'hostfile').open('w') as f:
        for line in Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines():
            fields=line.split()
            if len(fields)>=2: f.write(f'{fields[0]} slots={fields[1]}\n')
COMMON=['max_step=133000','stop_time=2500000','checkpoint.begin_step=128000',
    'amr.n_cell=128','128','128','amr.max_level=2','amr.max_grid_size=128','256','512',
    'amr.blocking_factor_x=16','16','16','amr.blocking_factor_y=16','16','16','amr.blocking_factor_z=16','16','16',
    'amr.n_error_buf=1','1','1','amr.grid_eff=0.7','amr.regrid_int=32',
    'lbm.err=0.25','0.5','1','2','lbm.reynolds_number=1000','lbm.collide_mode=1','lbm.interp_mode=0',
    'amr.plot_int=-1','checkpoint.chk_int=-1','checkpoint.keep_latest_only=false','checkpoint.write_particles=false',
    'verification.convergence_enabled=false','verification.convergence_stop=false','verification.final_state_check=false',
    'verification.regrid_valid_check=false','performance.report_int=500','performance.osi_detail_timing=0','ibm.enabled=false',
    'lbm.osi_mpi_device_direct=0','lbm.osi_mpi_pipeline_chunk_bytes=2097152']
MODES=[
    dict(name='00_AB',label='A-B 基线',stream=0,interp=0,boundary=0,overlap=0,threads=256,coord=0),
    dict(name='01_OSI',label='+ OSI 实现',stream=1,interp=0,boundary=0,overlap=0,threads=256,coord=0),
    dict(name='02_interp',label='+ 插值批量提交',stream=1,interp=1,boundary=0,overlap=0,threads=256,coord=0),
    dict(name='03_boundary',label='+ 边界批量薄面 / 直接构面',stream=1,interp=1,boundary=1,overlap=0,threads=256,coord=0),
    dict(name='04_overlap',label='+ 通信隐藏（单卡回退）',stream=1,interp=1,boundary=1,overlap=1,threads=256,coord=0),
    dict(name='05_threads',label='+ 碰撞线程 256 → 128',stream=1,interp=1,boundary=1,overlap=1,threads=128,coord=0),
    dict(name='06_coordinates',label='+ 碰撞坐标复用（现役策略）',stream=1,interp=1,boundary=1,overlap=1,threads=128,coord=1),
]
if EXTRA:
    control=dict(MODES[2],name='interp_control',label='仅插值批量（对照）',collision_batch=0)
    MODES=[control,dict(control,name='interp_collision_batch',label='插值＋碰撞批量（实验）',collision_batch=1)]
if INDEPENDENT:
    base=dict(name='00_baseline',label='OSI 基线',stream=1,interp=0,average=0,boundary=0,overlap=0,threads=256,coord=0)
    MODES=[base,
        dict(base,name='01_average_interp',label='仅平均＋插值批量',interp=1,average=1),
        dict(base,name='02_boundary',label='仅边界批量薄面',boundary=1),
        dict(base,name='03_threads',label='仅碰撞128线程',threads=128),
        dict(base,name='04_coordinates',label='仅碰撞坐标复用',coord=1),
        dict(base,name='05_all',label='全部优化开启',interp=1,average=1,boundary=1,threads=128,coord=1)]
if OSI_LAST:
    COMMON=[value.replace('max_step=133000','max_step=129000').replace('performance.report_int=500','performance.report_int=100') for value in COMMON]
    base=dict(name='00_AB',label='A-B 基线',stream=0,interp=0,average=0,boundary=0,overlap=0,threads=256,coord=0)
    MODES=[base,
        dict(base,name='01_batch',label='+ 平均与插值批量',interp=1,average=1),
        dict(base,name='02_boundary',label='+ 边界批量薄面、直接构面',interp=1,average=1,boundary=1),
        dict(base,name='03_threads',label='+ 碰撞256→128线程',interp=1,average=1,boundary=1,threads=128)]
    # 最后一步的OSI坐标策略在提交前根据展示方案确定。
    MODES.append(dict(MODES[-1],name='04_OSI',label='+ OSI（含坐标复用）',stream=1,coord=1))
if RECHECK:
    assert json.loads((OUT/'modes.json').read_text())==MODES
    assert json.loads((OUT/'common.json').read_text())==COMMON
else:
    (OUT/'modes.json').write_text(json.dumps(MODES,ensure_ascii=False,indent=2)+'\n')
    (OUT/'common.json').write_text(json.dumps(COMMON,indent=2)+'\n')

def run(mode, prefix, extra=()):
    directory=OUT/f'{prefix}_{mode["name"]}'
    directory.mkdir()
    (directory/'chk00128000').symlink_to(source,target_is_directory=True)
    envargs=[f'LADDER_INTERP={mode["interp"]}',f'LADDER_AVERAGE={mode.get("average",1)}',f'LADDER_BOUNDARY={mode["boundary"]}',f'LADDER_THREADS={mode["threads"]}',f'LADDER_COORD={mode["coord"]}',f'LADDER_COLLISION_BATCH={mode.get("collision_batch",0)}']
    command=['mpirun','-hostfile',str(OUT/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart',
             '/usr/bin/env','-u','BASH_ENV','CUDA_VISIBLE_DEVICES=0',*envargs,str(OUT/'experiment.ex'),str(OUT/'inputs'),*COMMON,
             f'checkpoint.chk_prefix={directory}/chk',f'lbm.stream_mode={mode["stream"]}',f'lbm.osi_collide_comm_overlap={mode["overlap"]}',*extra]
    (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (directory/'guard.log').open('w') as f:
        result=subprocess.run([sys.executable,str(CASE/'scripts/guard_run.py'),'--',*command],cwd=directory,stdout=f,stderr=subprocess.STDOUT)
    with (OUT/'status.jsonl').open('a') as f: f.write(json.dumps(dict(run=directory.name,exit_code=result.returncode))+'\n')
    if result.returncode: raise RuntimeError(f'{directory}: exit {result.returncode}')
    return directory

if RECHECK:
    assert INDEPENDENT and (OUT/'recheck_plan.json').exists()
    assert hashlib.sha256((OUT/'experiment.ex').read_bytes()).hexdigest()==(OUT/'executable.sha256').read_text().split()[0]
    # 当前分配内反向接续整组；保留首轮异常，不筛掉不利样本。
    with (OUT/'gpu_recheck.csv').open('w') as gpu, (OUT/'gpu_recheck.err').open('w') as err:
        monitor=subprocess.Popen(['nvidia-smi','--query-gpu=timestamp,index,uuid,temperature.gpu,utilization.gpu,power.draw,power.limit,clocks.current.sm,clocks.current.memory,clocks_throttle_reasons.active','--format=csv','--loop-ms=1000'],stdout=gpu,stderr=err)
        try:
            for mode in reversed(MODES):
                with (OUT/f'gpu_before_{mode["name"]}.txt').open('w') as f:
                    subprocess.run(['nvidia-smi'],stdout=f,stderr=subprocess.STDOUT,check=False)
                run(mode,'perf_confirm')
        finally:
            monitor.terminate();monitor.wait(timeout=15)
    (OUT/'recheck_complete.txt').write_text('reverse matrix completed; first matrix retained\n')
    raise SystemExit(0)

if OSI_LAST:
    # checkpoint写在ComputeMacro之前；关闭末尾额外平均，确保比较处于同一阶段。
    validation=[]
    reference=None
    for mode in MODES:
        checks=['max_step=128064','performance.report_int=32','checkpoint.chk_int=64','verification.final_state_check=false','verification.regrid_valid_check=true']
        if reference is not None:
            checks.append(f'verification.ddf_reference_checkpoint={reference}')
        directory=run(mode,'check',checks)
        candidate=directory/'chk00128064'
        if reference is not None:
            # A-B交换缓冲的ghost可带无效残留；用实际valid/active逐值比较，不能用全文件SHA冒充数值比较。
            log=(directory/'run.log').read_text()
            norms=re.findall(r'ddf_cell_norm_level: lev=(\d+).*?valid_linf=([^ ]+).*?active_linf=([^ ]+)',log)
            assert len(norms)==3 and all(float(valid)==0 and float(active)==0 for _,valid,active in norms),directory
            validation.append(dict(mode=mode['name'],scope='all valid including covered; active separately',levels=norms,exact_zero=True))
            (OUT/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
        if mode['stream']==0: reference=candidate
    reference=None
    for mode in MODES:
        final=['checkpoint.chk_int=1000','verification.final_state_check=false']
        if reference is not None: final.append(f'verification.ddf_reference_checkpoint={reference}')
        directory=run(mode,'perf',final)
        if reference is not None:
            norms=re.findall(r'ddf_cell_norm_level: lev=(\d+).*?valid_linf=([^ ]+).*?active_linf=([^ ]+)',(directory/'run.log').read_text())
            assert len(norms)==3 and all(float(valid)==0 and float(active)==0 for _,valid,active in norms),directory
            validation.append(dict(mode=mode['name'],step=129000,scope='all valid including covered; active separately',levels=norms,exact_zero=True))
            (OUT/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
        if mode['stream']==0: reference=directory/'chk00129000'
    subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),str(OUT),'--osi-last'],check=True)
    (OUT/'completed.txt').write_text('completed\n')
    raise SystemExit(0)

if EXTRA or INDEPENDENT:
    validation=[]
    prior=CASE/'runs/temporary/optimization_ladder/round1/job626333/check_02_interp/chk00128064'
    reference=prior
    for mode in MODES:
        directory=run(mode,'check',['max_step=128064','performance.report_int=32','checkpoint.chk_int=64','verification.final_state_check=true','verification.regrid_valid_check=true'])
        candidate=directory/'chk00128064'
        files=sorted(p.relative_to(reference) for p in reference.rglob('*') if p.is_file())
        mismatches=[str(p) for p in files if not (candidate/p).is_file() or hashlib.sha256((reference/p).read_bytes()).digest()!=hashlib.sha256((candidate/p).read_bytes()).digest()]
        validation.append(dict(mode=mode['name'],reference=str(reference),files=len(files),mismatches=mismatches))
        (OUT/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
        if mismatches: raise RuntimeError(f'optimization regression mismatch: {validation[-1]}')
        reference=candidate
    if INDEPENDENT:
        for mode in MODES: run(mode,'perf')
        (OUT/'recheck_plan.json').write_text(json.dumps(dict(job_id=os.environ['CCS_JOB_ID'],reason='preplanned forward/reverse independent matrix; retain all samples',action='reverse six modes with telemetry; report mean and range of two5000step runs'),indent=2)+'\n')
        subprocess.run([sys.executable,str(Path(__file__)),'--independent','--recheck'],check=True)
        subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),str(OUT),'--independent'],check=True)
        (OUT/'completed.txt').write_text('completed\n')
        raise SystemExit(0)
    # 同节点两对换序，每次均独立从同一checkpoint推进5000步。
    for pair,order in enumerate((MODES,list(reversed(MODES))),1):
        for mode in order: run(mode,f'perf_pair{pair}')
    subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),str(OUT),'--collision-extra'],check=True)
    (OUT/'completed.txt').write_text('completed\n')
    raise SystemExit(0)

# 独立于性能的真实 checkpoint 64步动态 AMR 回归；同阶段输出逐文件比对。
reference=run(MODES[1],'check',['max_step=128064','performance.report_int=32','checkpoint.chk_int=64','verification.final_state_check=true','verification.regrid_valid_check=true'])
validation=[]
for mode in MODES[2:]:
    directory=run(mode,'check',['max_step=128064','performance.report_int=32','checkpoint.chk_int=64','verification.final_state_check=true','verification.regrid_valid_check=true'])
    a=reference/'chk00128064'; b=directory/'chk00128064'
    files=sorted(p.relative_to(a) for p in a.rglob('*') if p.is_file())
    mismatches=[str(p) for p in files if not (b/p).is_file() or hashlib.sha256((a/p).read_bytes()).digest()!=hashlib.sha256((b/p).read_bytes()).digest()]
    validation.append(dict(mode=mode['name'],files=len(files),mismatches=mismatches))
    (OUT/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    if mismatches: raise RuntimeError(f'optimization regression mismatch: {validation[-1]}')
# A-B/OSI 数值差异单独输出，不以既有多层 A-B 差异冒充工程优化差异。
run(MODES[0],'check',['max_step=128064','performance.report_int=32','verification.final_state_check=true',f'verification.ddf_reference_checkpoint={reference}/chk00128064'])
for mode in MODES:
    run(mode,'perf')
subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),str(OUT)],check=True)
(OUT/'completed.txt').write_text('completed\n')
