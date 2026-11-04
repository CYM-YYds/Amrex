# 本次Re1000双模型AMR/均匀方腔实验入口；保留为科研复现材料，实验废弃后可删除。
from pathlib import Path
import hashlib, json, math, os, re, shutil, subprocess, sys, tempfile, time, threading
import numpy as np
CASE=Path(__file__).resolve().parents[3]
REPO=CASE.parents[3]
ROOT=CASE/'runs/temporary/cavity_models/round1'
FROZEN=CASE/'runs/temporary/collision_threads/round1/job626009'
BOX=r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'

def save(path,data):path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))

def prepare():
    ROOT.mkdir(parents=True,exist_ok=False)
    config={'purpose':'Re1000 AMR versus uniform cavity; equal finest dx and physical time', 'temporary':True, 'retain':'all inputs, commands, logs, fields, checkpoints, sources, executables and reports', 'short_time':25,'long_time':1000,'models':{
        'D3Q19':dict(dim=3,base=128,finest=512,threads=128,thresholds=[0.25,0.5,1,2],blocking=16,grid_eff=0.7,regrid=32,reference='D3Q27 C/job624717'),
        'D2Q9':dict(dim=2,base=512,finest=2048,threads=256,thresholds=[0.64,1.28,2.56,5.12],blocking=16,grid_eff=0.7,regrid=32,reference='2D Re1000 cavity; selected by this task')}}
    save(ROOT/'settings.json',config)
    for model in config['models']:
        shutil.copy2(FROZEN/f'{model}.ex',ROOT/f'{model}.ex')
        shutil.copy2(FROZEN/f'{model}.inputs',ROOT/f'{model}.inputs')
        shutil.copytree(FROZEN/f'{model}_src',ROOT/f'{model}_src')
        shutil.copy2(FROZEN/f'{model}_compile.log',ROOT/f'{model}_compile.log')
        assert sha(ROOT/f'{model}.ex')==json.loads((FROZEN/'executables.json').read_text())[f'{model}.ex']
    shutil.copytree(CASE/'config',ROOT/'config')
    save(ROOT/'manifest.json',dict(source_job=626009,git_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),executables={m:sha(ROOT/f'{m}.ex') for m in config['models']},build='Reuse freshly validated frozen model binaries; current solver sources unchanged since thread scan; no formula changes',selection='runtime test.collision_threads selects a compiled launch instance'))
    (ROOT/'worktree.patch').write_bytes(subprocess.check_output(['git','diff']))
    print(ROOT)

def publish(local,out,name):
    # 沿用已有uniform512本地落盘协议；旧入口绑定Q27历史作业且导入即执行，不能直接调用。
    partial=out/f'.{name}.partial';partial.mkdir()
    manifest={}
    for source in sorted(local.rglob('*')):
        if source.is_symlink() or not source.is_file():continue
        dest=partial/source.relative_to(local);dest.parent.mkdir(parents=True,exist_ok=True)
        for attempt in range(3):
            try:
                with source.open('rb') as reader,dest.open('wb',buffering=0) as writer:
                    while block:=reader.read(8*1024**2):
                        view=memoryview(block)
                        while view:
                            n=writer.write(view)
                            if not n:raise OSError('no copy progress')
                            view=view[n:]
                    os.fsync(writer.fileno())
                checksum=sha(source)
                assert dest.stat().st_size==source.stat().st_size and sha(dest)==checksum
                manifest[str(source.relative_to(local))]=dict(bytes=source.stat().st_size,sha256=checksum)
                break
            except (OSError,AssertionError):
                if attempt==2:raise
                time.sleep(2)
    save(partial/'transfer_manifest.json',manifest)
    final=out/name;partial.rename(final)
    shutil.rmtree(local)
    return final

def sample(plot,base,points):
    # 复用checkpoint_io/compare_checkpoints.py的VisMF头解析及memmap方式，适配速度和2D。
    header=(plot/'Header').read_text().splitlines();components=int(header[1]);dim=int(header[2+components]);finest=int(header[4+components])
    assert components==dim and dim==points.shape[1]
    result=np.full((len(points),components),np.nan)
    cells=[]
    for lev in range(finest+1):
        folder=plot/f'Level_{lev}';text=(folder/'Cell_H').read_text()
        boxes=re.findall(BOX,text);entries=re.findall(r'FabOnDisk: (\S+) (\d+)',text)
        assert len(boxes)==len(entries)>0
        cells.append(sum(math.prod(int(b)-int(a)+1 for a,b in zip(box[0].split(','),box[1].split(','))) for box in boxes))
        indices=np.floor(points*base*2**lev).astype(int)
        for box,(name,offset) in zip(boxes,entries):
            lo,hi=[np.array(list(map(int,v.split(',')))) for v in box[:2]]
            hit=np.all((indices>=lo)&(indices<=hi),axis=1)
            if not hit.any():continue
            path=folder/name
            with path.open('rb') as f:
                f.seek(int(offset));h=f.readline().decode('ascii');raw=re.findall(BOX,h)[0]
                flo,fhi=[np.array(list(map(int,v.split(',')))) for v in raw[:2]]
                assert '((8, (32 ' in h and int(h.split()[-1])==components
                shape=(components,)+tuple((fhi-flo+1)[::-1])
                data=np.memmap(path,mode='r',dtype='<f4',offset=f.tell(),shape=shape)
            take=indices[hit]-flo
            result[hit]=data[(slice(None),)+tuple(take[:,axis] for axis in range(dim-1,-1,-1))].T
    assert np.isfinite(result).all()
    return result,cells

def compare(out,model,settings,physical_time=25,short_source=None):
    cfg=settings['models'][model];n=cfg['finest'];dim=cfg['dim']
    # 固定最细格中心采样，逐层覆盖得到leaf值；不是全场积分误差，也不是插值中心线。
    coordinates=(np.linspace(0,n-1,33).round()+0.5)/n
    mesh=np.meshgrid(*([coordinates]*dim),indexing='ij');points=np.stack([v.ravel() for v in mesh],axis=1)
    suffix='' if physical_time==25 else f'_t{physical_time}'
    a=out/f'{model}_amr{suffix}'/f"plt_{cfg['base']*physical_time:06d}"
    u=out/f'{model}_uniform{suffix}'/f"plt_{cfg['finest']*physical_time:06d}"
    av,counts=sample(a,cfg['base'],points);uv,_=sample(u,cfg['finest'],points)
    delta=(av-uv)/0.05
    rows={}
    for kind in ['amr','uniform']:
        folders=[out/f'{model}_{kind}'] if physical_time==25 else [short_source/f'{model}_{kind}']+sorted(out.glob(f'{model}_{kind}_t*'),key=lambda p:int(p.name.rsplit('_t',1)[1]))
        text='\n'.join((folder/'run.log').read_text() for folder in folders)
        perf=[fields(line) for line in text.splitlines() if ' perf(s):' in line]
        assert len(perf)==(5 if physical_time==25 else 5+(physical_time-25)//25)
        assert sum(int(r['steps']) for r in perf)==(cfg['base'] if kind=='amr' else cfg['finest'])*physical_time
        rows[kind]=dict(total_s=sum(float(r['total']) for r in perf),solv_s=sum(float(r['solv']) for r in perf),updates=sum(float(r['total'])*float(r['MLUPS_total'])*1e6 for r in perf),steps=(cfg['base'] if kind=='amr' else cfg['finest'])*physical_time)
    row=dict(model=model,physical_time=physical_time,sample_points=len(points),sampling='finest-grid cell centers on 33-per-axis regular lattice; leaf piecewise constant values; sampled discrepancy, not full-field norm',velocity_rms_over_U=float(np.sqrt(np.mean(delta**2))),velocity_linf_over_U=float(abs(delta).max()),amr_valid_cells_per_level=counts,valid_cell_saving_fraction=1-sum(counts)/n**dim,performance=rows,uniform_over_amr_time=rows['uniform']['total_s']/rows['amr']['total_s'])
    row['short_gate']=dict(finite_and_regrid_checks=True,amr_faster=row['uniform_over_amr_time']>1.1,sampled_rms_under_2percent=row['velocity_rms_over_U']<0.02,sampled_linf_under_15percent=row['velocity_linf_over_U']<0.15)
    # 固定格中心剖面用于相同采样规则下对比；不冒充ParaView插值中心线。
    line=np.full((n,dim),0.5);line[:,-1]=(np.arange(n)+0.5)/n
    al,_=sample(a,cfg['base'],line);ul,_=sample(u,cfg['finest'],line)
    profile=np.column_stack([line[:,-1],al[:,0]/0.05,ul[:,0]/0.05])
    np.savetxt(out/f'{model}_profile.csv',profile,delimiter=',',header='axis_over_H,AMR_ux_over_U,uniform_ux_over_U',comments='')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(1,2,figsize=(10,4))
    for col,label in [(1,'AMR'),(2,'Uniform')]:axes[0].plot(profile[:,col],profile[:,0],label=label)
    axes[0].set(xlabel='ux / U_lid',ylabel=('z' if dim==3 else 'y')+' / H',title=f'{model} Re=1000, t={physical_time}');axes[0].legend();axes[0].grid(alpha=.3)
    axes[1].bar(['AMR','Uniform'],[rows[k]['total_s'] for k in ['amr','uniform']]);axes[1].set(ylabel='Accumulated advance time (s)',title=f'Equal finest dx; time ratio {row["uniform_over_amr_time"]:.2f}')
    fig.text(.01,.01,f'Input: {out.name}/{model}_*; steps AMR={cfg["base"]*physical_time}, uniform={n*physical_time}. U_lid=0.05, H=1.\nLeaf cell samples; uniform numerical comparison; no external steady reference. Cost excludes initialization/output/transfers.',fontsize=7)
    fig.tight_layout(rect=(0,.12,1,1));fig.savefig(out/f'{model}_comparison.png',dpi=160);plt.close(fig)
    save(out/f'{model}_comparison.json',row)
    lines=[f'# {model} Re=1000 AMR / uniform cavity', '', f'Physical time: {physical_time}; finest grid: {n}^{dim}; collision threads/block: {cfg["threads"]}.', '', '| Method | Steps | Accumulated advance seconds |', '|---|---:|---:|']
    for kind in ['amr','uniform']:lines.append(f'| {kind} | {rows[kind]["steps"]} | {rows[kind]["total_s"]:.6f} |')
    lines += ['', f'Uniform / AMR advance time: {row["uniform_over_amr_time"]:.4f}; valid-cell saving: {row["valid_cell_saving_fraction"]:.4%}.', f'Sampled velocity RMS / U_lid: {row["velocity_rms_over_U"]:.6g}; maximum / U_lid: {row["velocity_linf_over_U"]:.6g}.', 'Cell count includes covered coarse valid cells, excludes ghosts; it is not a memory saving measurement. Accuracy values are leaf-cell sampled discrepancies at fixed finest-cell centers, not full-field norms or external-reference errors.', 'Advance time includes regridding, excludes initialization, checkpoint/plot output and transfers. One trajectory pair is screening evidence, not a repeated performance confidence interval. Same node/GPU per pair; finite-state and regrid checks enabled.', 'Long runs reuse the t=25 checkpoint. D2Q9 continues to t=1000; D3Q19 AMR uses t=250 endpoints, uniform uses t=25 segments for disk capacity. Restarts reset convergence snapshots. Formal completion is not convergence. Preserve all segment logs, outputs and SHA256 transfer manifests.', '', f'Sources: {out}; model settings and program hashes: settings.json / source_manifest.json; comparison details: {model}_comparison.json; sampled profile: {model}_profile.csv.']
    (out/f'{model}_report.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps(row,ensure_ascii=False),flush=True)
    return row

def run(stage):
    settings=json.loads((ROOT/'settings.json').read_text())
    selected=os.environ.get('CAVITY_MODELS','D3Q19 D2Q9').split()
    assert set(selected)<=set(settings['models'])
    parent=ROOT if stage=='short' else CASE/'runs/cavity_models_t1000'
    out=parent/f"job{os.environ['CCS_JOB_ID']}";out.mkdir(parents=True,exist_ok=False)
    save(out/'purpose.json',dict(stage=stage,temporary=stage=='short',formal=stage=='long',purpose='Re1000 equal-finest-resolution AMR/uniform cavity comparison',retain='all source, inputs, commands, logs, checkpoints, fields, checksums, reports',models=selected))
    shutil.copy2(__file__,out/'experiment.py');shutil.copy2(Path(__file__).with_name('submit.sh'),out/'submit.sh');shutil.copy2(ROOT/'settings.json',out/'settings.json');shutil.copy2(ROOT/'manifest.json',out/'source_manifest.json')
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']));(out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines();(out/'hostfile').write_text(''.join(f'{r.split()[0]} slots={r.split()[1]}\n' for r in alloc if len(r.split())>=2))
    local_root=max([Path('/var/tmp'),Path('/tmp')],key=lambda p:shutil.disk_usage(p).free)
    filesystem=subprocess.check_output(['stat','-f','-c','%T',str(local_root)],text=True).strip();assert filesystem not in ['nfs','nfs4','dpc','autofs']
    local=Path(tempfile.mkdtemp(prefix=f'cavity_{os.environ["CCS_JOB_ID"]}_',dir=local_root));save(out/'local_disk.json',dict(path=str(local),filesystem=filesystem,free_bytes=shutil.disk_usage(local_root).free))
    status=[]
    short=Path(os.environ['CAVITY_SHORT_SOURCE']) if stage=='long' else None
    for model in selected:
        cfg=settings['models'][model];dim=cfg['dim']
        for name in [f'{model}.ex',f'{model}.inputs']:shutil.copy2(ROOT/name,out/name)
        assert sha(out/f'{model}.ex')==json.loads((ROOT/'manifest.json').read_text())['executables'][model]
        if stage=='long':
            gate=json.loads((short/f'{model}_comparison.json').read_text())
            assert all(gate['short_gate'].values()),gate['short_gate']
            save(out/f'{model}_short_gate.json',gate)
        for kind in ['amr','uniform']:
            cells=cfg['base'] if kind=='amr' else cfg['finest'];levels=2 if kind=='amr' else 0
            previous=None if stage=='short' else short/f'{model}_{kind}'/f'chk{cells*25:08d}'
            if stage=='short':times=[(0,25)]
            elif dim==2:times=[(25,1000)]
            elif kind=='amr':times=[(25,250),(250,500),(500,750),(750,1000)]
            else:times=[(end-25,end) for end in range(50,1001,25)]
            for begin,end in times:
                required_gib=14 if dim==3 else (1 if stage=='short' else 5)
                assert shutil.disk_usage(local_root).free>required_gib*1024**3, f'local disk free bytes: {shutil.disk_usage(local_root).free}; need {required_gib} GiB'
                name=f'{model}_{kind}' if stage=='short' else f'{model}_{kind}_t{end}'
                directory=local/name;directory.mkdir()
                if previous is not None:(directory/f'chk{cells*begin:08d}').symlink_to(previous,target_is_directory=True)
                args=[str(out/f'{model}.inputs'),f'test.collision_threads={cfg["threads"]}',f'max_step={cells*end}',f'stop_time={end}',f'checkpoint.begin_step={cells*begin}',f'checkpoint.chk_int={cells*(25 if stage=="short" or (dim==3 and kind=="uniform") else 250)}','checkpoint.chk_prefix=chk','checkpoint.keep_latest_only=false','checkpoint.write_particles=false',f'amr.plot_int={cells*25}',f'performance.report_int={cells*5 if stage=="short" else cells*25}','performance.osi_detail_timing=0','lbm.reynolds_number=1000','lbm.collide_mode=1','lbm.stream_mode=1','lbm.interp_mode=0',f'amr.n_cell={cells}']+[str(cells)]*(dim-1)+[f'amr.max_level={levels}',f'amr.max_grid_size={cells}']+[str(cells*2**lev) for lev in range(1,levels+1)]
                for axis in 'xyz'[:dim]:args+=[f'amr.blocking_factor_{axis}={cfg["blocking"]}']+[str(cfg['blocking'])]*levels
                args+=['geometry.is_periodic=0']+['0']*(dim-1)+[f'amr.regrid_int={cfg["regrid"] if kind=="amr" else 0}',f'amr.grid_eff={cfg["grid_eff"]}','amr.n_error_buf=1']+['1']*levels+['lbm.err='+str(cfg['thresholds'][0])]+list(map(str,cfg['thresholds'][1:]))+['verification.osi_seed_pattern=false','verification.osi_ab_check=false','verification.final_state_check=true','verification.regrid_valid_check=true','verification.refinement_diagnostics=false','verification.convergence_enabled=true','verification.convergence_stop=false',f'verification.convergence_trend_int={cells*5}',f'verification.convergence_check_int={cells*25}','verification.convergence_tolerance=0.0001','verification.convergence_required=3','amrex.the_arena_init_size=0','amrex.the_arena_release_threshold=0','lbm.uniform_nomask_collision=1','performance.uniform_reduce_sync=1']
                command=[str(REPO/'.venv/bin/python'),str(CASE/'scripts/guard_run.py'),'--wall-seconds','21600','--','mpirun','-hostfile',str(out/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart','/usr/bin/env','-u','BASH_ENV',str(out/f'{model}.ex')]+args
                save(directory/'command.json',command);shutil.copy2(out/f'{model}.inputs',directory/'inputs');(directory/'overrides.txt').write_text(' '.join(args[1:])+'\n');save(directory/'manifest.json',dict(model=model,kind=kind,threads=cfg['threads'],begin_time=begin,end_time=end,executable_sha256=sha(out/f'{model}.ex')))
                started=time.monotonic()
                finished=threading.Event()
                def progress():
                    while not finished.wait(2):
                        path=directory/'run.log'
                        if path.exists():
                            with path.open('rb') as f:
                                f.seek(max(0,path.stat().st_size-32768));tail=f.read().decode(errors='replace')
                            steps=re.findall(r'STEP (\d+)starts',tail)
                            save(out/'progress.json',dict(name=name,begin_time=begin,end_time=end,last_step_started=int(steps[-1]) if steps else None,elapsed_seconds=time.monotonic()-started,local_run_log=str(path)))
                watcher=threading.Thread(target=progress,daemon=True);watcher.start()
                try:
                    with (out/f'{name}-guard.log').open('w') as log:result=subprocess.run(command,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
                finally:
                    finished.set();watcher.join()
                row=dict(name=name,returncode=result.returncode,wall_seconds=time.monotonic()-started,local=str(directory));status.append(row);save(out/'status.json',status)
                assert result.returncode==0,(name,result.returncode)
                text=(directory/'run.log').read_text();assert ') finalized' in text
                assert not re.search(r'(?i)(?<![a-z])(?:nan|inf)(?![a-z])',text)
                checkpoint=directory/f'chk{cells*end:08d}';h=(checkpoint/'Header').read_text().splitlines();assert int(h[2])==cells*end and abs(float(h[3])-end)<1e-8
                assert (checkpoint/'Lattice').read_text().split()[1:3]==[str(dim),str(19 if dim==3 else 9)]
                transfer_started=time.monotonic()
                final=publish(directory,out,name);row.update(published=str(final),sha256_verified=True,transfer_seconds=time.monotonic()-transfer_started);save(out/'status.json',status)
                previous=final/checkpoint.name
                print('completed and published',name,flush=True)
        compare(out,model,settings,physical_time=25 if stage=='short' else 1000,short_source=short)
    (out/'completed.txt').write_text(f'{stage} completed; all output transfers SHA256 verified\n')

if __name__=='__main__':
    if sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='analyze':
        directory=Path(sys.argv[2]);settings=json.loads((directory/'settings.json').read_text())
        for model in settings['models']:
            if (directory/f'{model}_uniform').is_dir():compare(directory,model,settings)
    else:
        try:run(sys.argv[1])
        except Exception as error:
            parent=ROOT if sys.argv[1]=='short' else CASE/'runs/cavity_models_t1000'
            directory=parent/f"job{os.environ['CCS_JOB_ID']}"
            if directory.is_dir():save(directory/'failure.json',dict(error=repr(error),status=str(directory/'status.json'),local_disk=str(directory/'local_disk.json')))
            raise
