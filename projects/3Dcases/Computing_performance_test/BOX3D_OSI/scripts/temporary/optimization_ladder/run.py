"""AI 临时实验驱动；保留以复现优化图，任务废弃且无复现依赖时可删除。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

CASE = Path(__file__).resolve().parents[3]
REPO = CASE.parents[3]
EXP = CASE/'runs/temporary/optimization_ladder/round1'
OUT = EXP/f'job{os.environ["CCS_JOB_ID"]}'
OUT.mkdir()
BUILD=EXP/'build'
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
source=CASE/'runs/a5_C_t1000_gif/job624717/C/chk00128000'
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
(OUT/'modes.json').write_text(json.dumps(MODES,ensure_ascii=False,indent=2)+'\n')
(OUT/'common.json').write_text(json.dumps(COMMON,indent=2)+'\n')

def run(mode, prefix, extra=()):
    directory=OUT/f'{prefix}_{mode["name"]}'
    directory.mkdir()
    (directory/'chk00128000').symlink_to(source,target_is_directory=True)
    envargs=[f'LADDER_INTERP={mode["interp"]}',f'LADDER_BOUNDARY={mode["boundary"]}',f'LADDER_THREADS={mode["threads"]}',f'LADDER_COORD={mode["coord"]}']
    command=['mpirun','-hostfile',str(OUT/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart',
             '/usr/bin/env','-u','BASH_ENV','CUDA_VISIBLE_DEVICES=0',*envargs,str(OUT/'experiment.ex'),str(OUT/'inputs'),*COMMON,
             f'checkpoint.chk_prefix={directory}/chk',f'lbm.stream_mode={mode["stream"]}',f'lbm.osi_collide_comm_overlap={mode["overlap"]}',*extra]
    (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (directory/'guard.log').open('w') as f:
        result=subprocess.run([sys.executable,str(CASE/'scripts/guard_run.py'),'--',*command],cwd=directory,stdout=f,stderr=subprocess.STDOUT)
    with (OUT/'status.jsonl').open('a') as f: f.write(json.dumps(dict(run=directory.name,exit_code=result.returncode))+'\n')
    if result.returncode: raise RuntimeError(f'{directory}: exit {result.returncode}')
    return directory

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
