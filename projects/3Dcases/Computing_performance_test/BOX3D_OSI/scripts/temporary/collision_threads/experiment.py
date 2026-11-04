# 本任务临时碰撞线程数扫描；保留为结果复现入口，实验废弃后可删除。
from pathlib import Path
import hashlib, json, math, os, re, shutil, statistics, subprocess, sys
CASE = Path(__file__).resolve().parents[3]
REPO = CASE.parents[3]
EXP = CASE / 'runs/temporary/collision_threads/round1'
THREADS = [64, 128, 256, 512]

def save(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')

def prepare():
    EXP.mkdir(parents=True, exist_ok=False)
    for name in ['src', 'config']:
        shutil.copytree(CASE/name, EXP/name)
    shutil.copy2(CASE/'GNUmakefile', EXP/'GNUmakefile')
    save(EXP/'purpose.json', dict(purpose='D3Q19/D2Q9 FLOAT OSI collision block-size comparison', temporary=True, retain='source, build logs, executables, inputs, commands, checkpoints, raw logs and reports', threads=THREADS))
    (EXP/'git_head').write_bytes(subprocess.check_output(['git','rev-parse','HEAD']))
    (EXP/'worktree.patch').write_bytes(subprocess.check_output(['git','diff']))
    # 只复制已有框架/维度构建缓存；本轮源时间戳统一刷新，确保应用重新编译。
    build = EXP/'build'
    build.mkdir()
    shutil.copytree(CASE/'tmp_build_dir', build/'tmp_build_dir')
    shutil.copy2(EXP/'GNUmakefile', build/'GNUmakefile')
    shutil.copytree(EXP/'config', build/'config')
    shutil.copytree(EXP/'src', build/'src')
    source = (EXP/'src/AmrCoreLBM_advance.cpp').read_text()
    pattern = r'amrex::ParallelFor<128>\(\s*bx, \[=\] AMREX_GPU_DEVICE\(int i, int j, int k\) \{.*?\n                        \}\);'
    matches = list(re.finditer(pattern, source, re.S))
    assert len(matches) == 2
    # 展开原启动语句，不在泛型lambda中定义CUDA扩展lambda；128分支保持原闭包与公式。
    def expand(match):
        call = match.group()
        branches = []
        for index, threads in enumerate(THREADS):
            branches.append(('if' if index == 0 else 'else if') + f' (test_threads == {threads}) {{\n                    ' + call.replace('<128>', f'<{threads}>') + '\n                    }')
        return '\n                    '.join(branches) + '\n                    else { amrex::Abort("invalid test collision threads"); }'
    source = re.sub(pattern, expand, source, flags=re.S)
    marker = '            // host 侧编译期分派使无 mask 闭包不捕获掩码参数；碰撞公式仍共用。'
    assert source.count(marker) == 1
    source = source.replace(marker, '            static const int test_threads = [] {\n                int value = 128;\n                amrex::ParmParse pp("test");\n                pp.query("collision_threads", value);\n                amrex::Print() << "TEST_COLLISION_THREADS=" << value << "\\n";\n                return value;\n            }();\n' + marker)
    (EXP/'candidate_advance.cpp').write_text(source)
    print(EXP)

def run_job():
    assert (EXP/'candidate_advance.cpp').is_file() and (EXP/'build/src').is_dir(), 'prepare must finish before submission'
    out = EXP/f"job{os.environ['CCS_JOB_ID']}"
    out.mkdir()
    shutil.copy2(__file__, out/'experiment.py')
    shutil.copy2(Path(__file__).with_name('submit.sh'), out/'submit.sh')
    for name, cmd in [('node',['hostname']),('gpu',['nvidia-smi']),('modules',['bash','-c','module list'])]:
        result = subprocess.run(cmd, capture_output=True)
        (out/f'{name}.txt').write_bytes(result.stdout + result.stderr)
    alloc = Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{r.split()[0]} slots={r.split()[1]}\n' for r in alloc if len(r.split())>=2))
    build = EXP/'build'
    status=[]
    for model, dim in [('D3Q19',3), ('D2Q9',2)]:
        shutil.rmtree(build/'src')
        shutil.copytree(EXP/'src',build/'src')
        shutil.copy2(EXP/'candidate_advance.cpp', build/'src/AmrCoreLBM_advance.cpp')
        lattice = build/'src/LbmLattice.H'
        if dim == 3:
            original = lattice.read_text()
            assert original.count('#define LBM_Q 27') == 1
            lattice.write_text(original.replace('#define LBM_Q 27','#define LBM_Q 19'))
        for p in (build/'src').iterdir():
            if p.is_file(): p.touch()
        shutil.copytree(build/'src',out/f'{model}_src')
        command=['make','-j8',f'DIM={dim}','PRECISION=FLOAT','CUDA_ARCH=80']
        save(out/f'{model}_build_command.json',command)
        if model == 'D3Q19':
            frozen = EXP/'job626000'
            for p in (build/'src').iterdir():
                if p.is_file():
                    assert p.read_bytes() == (frozen/f'{model}_src'/p.name).read_bytes()
            shutil.copy2(frozen/f'{model}.ex',build/f'main{dim}d.gnu.FLOAT.TPROF.MPI.CUDA.ex')
            shutil.copy2(frozen/f'{model}_compile.log',out/f'{model}_compile.log')
            (out/'D3Q19_build_reuse.txt').write_text('Reused job626000 executable; candidate sources byte-identical; only numerical-check output-stage alignment changed.\n')
        else:
            with (out/f'{model}_compile.log').open('w') as log:
                subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        executable=out/f'{model}.ex'
        shutil.copy2(build/f'main{dim}d.gnu.FLOAT.TPROF.MPI.CUDA.ex',executable)
        save(out/'executables.json',{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.ex')})
        shutil.copy2(EXP/'config'/('inputs' if dim==3 else 'inputs.2d'),out/f'{model}.inputs')
        print('built',model,flush=True)
        def run(name,threads,args):
            directory=out/f'{model}_{name}_t{threads}'
            directory.mkdir()
            command=[str(REPO/'.venv/bin/python'),str(CASE/'scripts/guard_run.py'),'--wall-seconds','1200','--','mpirun','-hostfile',str(out/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart','/usr/bin/env','-u','BASH_ENV',str(executable),str(out/f'{model}.inputs'),f'test.collision_threads={threads}']+args
            save(directory/'command.json',command)
            with (directory/'guard.log').open('w') as log:
                result=subprocess.run(command,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
            status.append(dict(model=model,name=name,threads=threads,exit_code=result.returncode))
            save(out/'status.json',status)
            assert result.returncode==0,(directory,result.returncode)
            assert f'TEST_COLLISION_THREADS={threads}' in (directory/'run.log').read_text()
            print('completed',directory.name,flush=True)
            return directory
        common=['stop_time=2500000','checkpoint.begin_step=0','checkpoint.chk_int=-1','checkpoint.chk_prefix=chk','checkpoint.keep_latest_only=false','checkpoint.write_particles=false','amr.plot_int=-1','verification.convergence_enabled=false','verification.osi_ab_check=false','verification.final_state_check=true','verification.osi_seed_pattern=false','performance.osi_detail_timing=0','lbm.collide_mode=1','lbm.stream_mode=1','lbm.reynolds_number=1000','geometry.is_periodic=0']+['0']*(dim-1)
        def mesh(cells,box,level):
            result=[f'amr.n_cell={cells}']+[str(cells)]*(dim-1)+[f'amr.max_level={level}',f'amr.max_grid_size={box}']+[str(box)]*level
            for axis in 'xyz'[:dim]: result += [f'amr.blocking_factor_{axis}=8']+['8']*level
            return result
        # 两类启动分支均逐值验证：动态AMR覆盖mask、均匀单层覆盖nomask。
        for kind in ['uniform','amr']:
            check=common+mesh(32,16,2 if kind=='amr' else 0)+['max_step=80','performance.report_int=40','verification.osi_seed_pattern=true',f'verification.regrid_fixture={str(kind=="amr").lower()}','amr.regrid_int=8','amr.plot_int=80','performance.uniform_grid_path=1','performance.uniform_reduce_sync=1','lbm.uniform_nomask_collision=1']
            ref=run(f'check_{kind}',128,check+['checkpoint.chk_int=80'])
            for threads in [64,256,512]:
                directory=run(f'check_{kind}',threads,check+[f'verification.ddf_reference_checkpoint={ref}/chk00000080'])
                norms=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in (directory/'run.log').read_text().splitlines() if line.startswith('ddf_cell_norm_level:')]
                assert len(norms)==(3 if kind=='amr' else 1),(directory,len(norms))
                assert all(math.isfinite(float(n[k])) and float(n[k])==0 for n in norms for k in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2']),directory
        for kind in ['uniform','amr']:
            perf=common+['verification.final_state_check=false']
            if kind=='uniform':
                n=256 if dim==3 else 2048
                perf+=mesh(n,n,0)+['amr.regrid_int=-1','max_step=600','performance.report_int=100','performance.uniform_grid_path=1','performance.uniform_reduce_sync=1','lbm.uniform_nomask_collision=1']
            else:
                n=64 if dim==3 else 256
                perf+=mesh(n,n,2)+['amr.regrid_int=32','max_step=320','performance.report_int=80','lbm.err=0.125','0.25','0.5','1','performance.uniform_grid_path=0']
            for repeat,order in enumerate([[64,128,256,512],[512,256,128,64],[128,512,64,256]],1):
                for threads in order:
                    run(f'perf_{kind}_r{repeat}',threads,perf)
    analyze(out)
    (out/'completed.txt').write_text('completed\n')

def analyze(out):
    records=[]
    work={}
    for directory in sorted(out.glob('*_perf_*')):
        match=re.fullmatch(r'(D3Q19|D2Q9)_perf_(uniform|amr)_r(\d+)_t(\d+)',directory.name)
        if not match: continue
        model,kind,repeat,threads=match.groups()
        text=(directory/'run.log').read_text()
        def rows(marker):
            return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if marker in line]
        windows=rows(' perf(s):')
        assert len(windows)==(6 if kind=='uniform' else 4),directory
        workload=[{k:r[k] for k in ['lev','calls','launch_cells']} for r in rows(' collide_level:')]
        counts=[{k:v for k,v in r.items() if k!='regrid_mask_builds'} for r in rows(' perf_count:')]
        key=(model,kind)
        current=(workload,counts)
        if key in work: assert work[key]==current,(directory,'workload mismatch')
        else: work[key]=current
        seconds=sum(float(r['total']) for r in windows[1:])
        collide=sum(float(r['collide']) for r in windows[1:])
        updates=sum(float(r['MLUPS_total'])*float(r['total']) for r in windows[1:])
        stages=rows(' uniform_stage_gpu(ms/step):')
        record=dict(model=model,kind=kind,repeat=int(repeat),threads=int(threads),total_s=seconds,collide_s=collide,MLUPS=updates/seconds)
        if kind=='uniform':
            assert all(int(r['samples'])==100 for r in stages)
            record['collision_gpu_ms']=statistics.mean(float(r['collide_sum']) for r in stages[1:])
        records.append(record)
    assert len(records)==48,len(records)
    summary=[]
    for model in ['D3Q19','D2Q9']:
        for kind in ['uniform','amr']:
            for threads in THREADS:
                selected=[r for r in records if (r['model'],r['kind'],r['threads'])==(model,kind,threads)]
                row=dict(model=model,kind=kind,threads=threads)
                for metric in ['total_s','collide_s','MLUPS']+(['collision_gpu_ms'] if kind=='uniform' else []):
                    values=[r[metric] for r in selected]
                    row[metric]=dict(median=statistics.median(values),min=min(values),max=max(values))
                summary.append(row)
    save(out/'summary.json',dict(records=records,summary=summary,workload_equivalent=True,ddf_norms_zero=True,warmup='first reporting window excluded',timing='perf(s).total excludes initialization, includes regrid; single rank/GPU'))
    lines=['# D3Q19 / D2Q9 碰撞线程数实测','', 'CUDA FLOAT，单节点单GPU；每组3轮换序，排除首个统计窗口。只改变碰撞block线程数，其他核不变。', '', '|模型|网格|线程/block|total秒 中位数 [范围]|MLUPS中位数|', '|---|---|---:|---:|---:|']
    for r in summary:
        t=r['total_s']; lines.append(f"|{r['model']}|{r['kind']}|{r['threads']}|{t['median']:.6f} [{t['min']:.6f}, {t['max']:.6f}]|{r['MLUPS']['median']:.2f}|")
    lines+=['','uniform: D3Q19 256³ / D2Q9 2048²，600步排除前100步；AMR: 64³ / 256²，最高level=2，重网格间隔32，320步排除前80步。', '检查同模型同网格所有轮次每层calls/launch_cells及perf_count一致；uniform与动态三层AMR相对128线程逐值DDF范数为零。', 'total包括重网格、不含初始化；均匀网格另保留CUDA event碰撞时间。短测不证明长期精度或收敛；不外推DOUBLE、多GPU及其他GPU。', '', '原始数据：本目录summary.json、status.json、gpu.txt、各次command.json和run.log；源码与可执行文件指纹同目录保留。']
    (out/'report.md').write_text('\n'.join(lines)+'\n')


def confirm():
    source = EXP/'job626009'
    out = EXP/f"job{os.environ['CCS_JOB_ID']}_confirm"
    out.mkdir()
    shutil.copy2(__file__,out/'experiment.py')
    shutil.copy2(source/'D2Q9.ex',out/'D2Q9.ex')
    shutil.copy2(source/'D2Q9.inputs',out/'D2Q9.inputs')
    (out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']))
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{r.split()[0]} slots={r.split()[1]}\n' for r in alloc if len(r.split())>=2))
    save(out/'manifest.json',dict(source_job=626009,executable_sha256=hashlib.sha256((out/'D2Q9.ex').read_bytes()).hexdigest(),purpose='D2Q9 128/256 longer-window confirmation',temporary=True,retain='all reproduction materials'))
    records=[]
    work={}
    for kind in ['uniform','amr']:
        for repeat in range(1,6):
            for threads in ([128,256] if repeat%2 else [256,128]):
                directory=out/f'D2Q9_perf_{kind}_r{repeat}_t{threads}'
                directory.mkdir()
                command=json.loads((source/f'D2Q9_perf_{kind}_r1_t{threads}/command.json').read_text())
                mapping={str(source/'hostfile'):str(out/'hostfile'),str(source/'D2Q9.ex'):str(out/'D2Q9.ex'),str(source/'D2Q9.inputs'):str(out/'D2Q9.inputs')}
                command=[mapping.get(arg,arg) for arg in command]
                command=[{'max_step=600':'max_step=6000','performance.report_int=100':'performance.report_int=1000','max_step=320':'max_step=3200','performance.report_int=80':'performance.report_int=800'}.get(arg,arg) for arg in command]
                save(directory/'command.json',command)
                with (directory/'guard.log').open('w') as log:
                    result=subprocess.run(command,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
                assert result.returncode==0,(directory,result.returncode)
                text=(directory/'run.log').read_text()
                assert 'AMReX (' in text and ') finalized' in text
                assert not re.search(r'(?i)(?<![a-z])(?:nan|inf)(?![a-z])',text),directory
                def rows(marker):
                    return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if marker in line]
                workload=[{k:r[k] for k in ['lev','calls','launch_cells']} for r in rows(' collide_level:')]
                counts=rows(' perf_count:')
                if kind in work: assert work[kind]==(workload,counts),(directory,'workload differs')
                else: work[kind]=(workload,counts)
                windows=rows(' perf(s):')
                assert len(windows)==(6 if kind=='uniform' else 4)
                total=sum(float(r['total']) for r in windows[1:])
                record=dict(kind=kind,repeat=repeat,threads=threads,total_s=total,collide_s=sum(float(r['collide']) for r in windows[1:]),MLUPS=sum(float(r['MLUPS_total'])*float(r['total']) for r in windows[1:])/total,exit_code=result.returncode)
                if kind=='uniform': record['collision_gpu_ms']=statistics.mean(float(r['collide_sum']) for r in rows(' uniform_stage_gpu(ms/step):')[1:])
                records.append(record)
                save(out/'records.json',records)
                print('completed',directory.name,flush=True)
    summary=[]
    for kind in ['uniform','amr']:
        for threads in [128,256]:
            selected=[r for r in records if (r['kind'],r['threads'])==(kind,threads)]
            row=dict(kind=kind,threads=threads)
            for metric in ['total_s','collide_s','MLUPS']+(['collision_gpu_ms'] if kind=='uniform' else []):
                values=[r[metric] for r in selected]
                row[metric]=dict(median=statistics.median(values),min=min(values),max=max(values))
            summary.append(row)
    save(out/'summary.json',dict(records=records,summary=summary,workload_equivalent=True,source_job=626009,warmup='first window excluded'))
    (out/'completed.txt').write_text('completed\n')

if __name__=='__main__':
    if sys.argv[1]=='prepare': prepare()
    elif sys.argv[1]=='run': run_job()
    elif sys.argv[1]=='analyze': analyze(Path(sys.argv[2]))
    elif sys.argv[1]=='confirm': confirm()
