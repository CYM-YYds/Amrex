# 本轮临时扩展：正式AMR接入及其他阶段坐标复用筛选；保留此文件用于复现结果。
from pathlib import Path
import hashlib,json,math,os,re,shutil,subprocess,sys
CASE=Path(__file__).resolve().parents[3]
REPO=CASE.parents[3]
EXP=CASE/'runs/temporary/collision_amr/round2'
MODES=['production','interp','average','macro']

def write(path,text):
    content=text.encode().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n') if path.name=='Kernels.H' else text.encode()
    if not path.exists() or path.read_bytes()!=content:
        path.write_bytes(content)

def variants(original):
    # 三线性插值：coarse staging为canonical，只有fine目标地址需要复用。
    s=original;a=s.index('AMREX_GPU_DEVICE AMREX_FORCE_INLINE void interp_bilinear_ddf_impl(');b=s.index('\nAMREX_GPU_DEVICE',a+10)
    body=s[a:b];pos=body.index('    const int ic =')
    body=body[:pos]+'''    OSI::CellCoordinates coordinates{};
    if constexpr (UseOsiAddress) {
        coordinates = OSI::osi_cell_coordinates({i, j, k}, fine_fab, fine_shift);
    }
'''+body[pos:]
    old='''        store_interpolated_ddf<UseOsiAddress>(
            i, j, k, q, value, fine, fine_fab, fine_shift);'''
    assert old in body
    body=body.replace(old,'''        store_ddf_cached<UseOsiAddress>(
            i, j, k, q, value, fine, coordinates);''')
    result={'interp':s[:a]+body+s[b:]}
    # 半warp每lane只有两个分量；只在当前子单元内复用，不缓存8个子单元。
    a=s.index('AMREX_GPU_DEVICE AMREX_FORCE_INLINE void average_down_to_canonical_half_warp(');b=s.index('\n#endif',a)
    body=s[a:b];old='''                const Real f0 = load_ddf<UseOsiAddress>(
                    2 * i + ir, 2 * j + jr, 2 * k + kr, q0,
                    fine, fine_fab, fine_shift);''';assert old in body
    body=body.replace(old,'''                OSI::CellCoordinates coordinates{};
                if constexpr (UseOsiAddress) {
                    coordinates = OSI::osi_cell_coordinates(
                        {2 * i + ir, 2 * j + jr, 2 * k + kr}, fine_fab, fine_shift);
                }
                const Real f0 = load_ddf_cached<UseOsiAddress>(
                    2 * i + ir, 2 * j + jr, 2 * k + kr, q0, fine, coordinates);''')
    body=body.replace('''f1 = load_ddf<UseOsiAddress>(
                        2 * i + ir, 2 * j + jr, 2 * k + kr, q1,
                        fine, fine_fab, fine_shift);''','''f1 = load_ddf_cached<UseOsiAddress>(
                        2 * i + ir, 2 * j + jr, 2 * k + kr, q1, fine, coordinates);''')
    result['average']=s[:a]+body+s[b:]
    result['average']=result['average'].replace(
        'template <bool UseOsiAddress>\nAMREX_GPU_DEVICE AMREX_FORCE_INLINE amrex::Real load_ddf_cached(',
        'template <bool UseOsiAddress, typename T>\nAMREX_GPU_DEVICE AMREX_FORCE_INLINE amrex::Real load_ddf_cached(')
    start=result['average'].index('amrex::Real load_ddf_cached(')
    stop=result['average'].index('\ntemplate <',start)
    result['average']=result['average'][:start]+result['average'][start:stop].replace('amrex::Array4<amrex::Real>','amrex::Array4<T>')+result['average'][stop:]
    a=s.index('void compute_macro_osi(');b=s.index('\ntemplate <bool UseOsiAddress>',a)
    body=s[a:b];old='''        const auto raw = OSI::osi_address(
            {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, fab, phase_shift);''';assert old in body
    body=body.replace('    Real rhot =','    const auto coordinates = OSI::osi_cell_coordinates({i, j, k}, fab, phase_shift);\n    Real rhot =',1)
    body=body.replace(old,'''        const auto raw = OSI::osi_address(
            coordinates, {e[q][0], e[q][1], lattice_ez(q)});''')
    # 独立参考函数只用于数值模式，逐单元检查rho和三个速度分量。
    result['macro']=s[:a]+body+'\nAMREX_GPU_DEVICE AMREX_FORCE_INLINE\n'+s[a:b].replace('compute_macro_osi(','compute_macro_osi_reference(',1)+s[b:]
    return result

def prepare():
    assert not EXP.exists(),EXP
    EXP.mkdir(parents=True)
    (EXP/'README.md').write_text('临时实验：正式AMR碰撞128线程及坐标复用接入；独立筛选三线性插值、Q27半warp平均、宏观量计算。保留输入、源快照、程序、命令、日志、报告及生成脚本；历史checkpoint只读。\n')
    build=EXP/'build';build.mkdir()
    for name in ['src','config']:shutil.copytree(CASE/name,build/name)
    shutil.copy2(CASE/'GNUmakefile',build/'GNUmakefile')
    shutil.copy2(CASE/'config/inputs',EXP/'inputs')
    # 增量复用同配置对象；本次源码全部touch确保不错误复用旧kernel。
    cache=CASE/'runs/temporary/collision_amr/round1/build/tmp_build_dir'
    shutil.copytree(cache,build/'tmp_build_dir')
    for p in (build/'src').iterdir():
        if p.is_file():p.touch()
    shutil.copy2(CASE/'main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',EXP/'baseline.ex')
    shutil.copytree(build/'src',EXP/'production_src')
    kernel=(build/'src/Kernels.H').read_text()
    for mode,s in variants(kernel).items():
        d=EXP/mode;d.mkdir();write(d/'Kernels.H',s)
    # 宏观量候选的逐值参考检查不进入正式源码，也不进入性能模式。
    p=build/'src/AmrCoreLBM.cpp';s=p.read_text();point=s.index('        return;',s.index('void AmrCoreLBM::ComputeMacroLevel'))
    check='''        if (std::getenv("BOX3D_MACRO_VERIFY")) {
            MultiFab reference_rho(rho_lev.boxArray(), rho_lev.DistributionMap(), 1, 0);
            MultiFab reference_u(u_lev.boxArray(), u_lev.DistributionMap(), AMREX_SPACEDIM, 0);
            for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                const auto context = OSI::MakeOsiFabContext(
                    amrex::grow(mfi.validbox(), state.nGrowVect()), phase);
                const auto ddf = state.const_array(mfi);
                const auto rr = reference_rho.array(mfi);
                const auto ru = reference_u.array(mfi);
                amrex::ParallelFor(mfi.validbox(), [=] AMREX_GPU_DEVICE(int i,int j,int k) {
                    compute_macro_osi_reference(i,j,k,ddf,rr,ru,context.fab,context.phase_shift);
                });
            }
            MultiFab::Subtract(reference_rho,rho_lev,0,0,1,0);
            MultiFab::Subtract(reference_u,u_lev,0,0,AMREX_SPACEDIM,0);
            Real error = reference_rho.norm0(0);
            for (int component=0;component<AMREX_SPACEDIM;++component) error=amrex::max(error,reference_u.norm0(component));
            amrex::Print() << "macro_reference lev=" << lev << " linf=" << error << "\\n";
            AMREX_ALWAYS_ASSERT(std::isfinite(error) && error == Real(0));
            static bool measured[3] = {false,false,false};
            if (phase > 0 && !measured[lev]) {
                measured[lev] = true;
                for (int cached : {0,1,1,0}) {
                    Gpu::synchronize();
                    const double start = ParallelDescriptor::second();
                    for (int repeat=0;repeat<20;++repeat) {
                        for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                            const auto context = OSI::MakeOsiFabContext(
                                amrex::grow(mfi.validbox(), state.nGrowVect()), phase);
                            const auto ddf = state.const_array(mfi);
                            const auto rr = reference_rho.array(mfi);
                            const auto ru = reference_u.array(mfi);
                            if (cached) {
                                amrex::ParallelFor(mfi.validbox(), [=] AMREX_GPU_DEVICE(int i,int j,int k) {
                                    compute_macro_osi(i,j,k,ddf,rr,ru,context.fab,context.phase_shift);
                                });
                            } else {
                                amrex::ParallelFor(mfi.validbox(), [=] AMREX_GPU_DEVICE(int i,int j,int k) {
                                    compute_macro_osi_reference(i,j,k,ddf,rr,ru,context.fab,context.phase_shift);
                                });
                            }
                        }
                    }
                    Gpu::synchronize();
                    amrex::Print() << "macro_probe lev=" << lev << " phase=" << phase
                        << " cached=" << cached << " seconds=" << (ParallelDescriptor::second()-start)
                        << " calls=20\\n";
                }
            }

        }
'''
    write(EXP/'macro/AmrCoreLBM.cpp','#include <cstdlib>\n'+s[:point]+check+s[point:])
    (EXP/'worktree.diff').write_bytes(subprocess.check_output(['git','-C',str(CASE),'diff']))
    (EXP/'manifest.json').write_text(json.dumps({'modes':MODES,'scope':'CUDA 3D FLOAT single rank','source_head':subprocess.check_output(['git','-C',str(CASE),'rev-parse','HEAD'],text=True).strip(),'user_worktree_preserved':True,'interpolation_scope':'mode0 only; mode1/2 unchanged','average_scope':'CUDA Q27 half-warp, scaled and unscaled','macro_scope':'rho and velocity; convergence cadence separate'},indent=2)+'\n')
    print(EXP,flush=True)

def run_job():
    out=EXP/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'stages.py');shutil.copy2(EXP/'inputs',out/'inputs')
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{s.split()[0]} slots={s.split()[1]}\n' for s in alloc if len(s.split())>=2))
    for cmd,name in [(['nvidia-smi'],'gpu.txt'),(['hostname'],'node.txt')]:
        (out/name).write_bytes(subprocess.check_output(cmd))
    build=EXP/'build'
    shutil.copy2(CASE/'scripts/temporary/collision_amr/submit.sh',out/'submit.sh')
    for mode in MODES:
        # 仅复用本轮失败作业中已完整生成的独立程序，统一冻结SHA256。
        previous=EXP/f"job{os.environ.get('COORD_RESUME_JOB','none')}"
        if (previous/f'{mode}.ex').exists():
            shutil.copy2(previous/f'{mode}.ex',out/f'{mode}.ex')
            shutil.copy2(previous/f'{mode}_compile.log',out/f'{mode}_compile.log')
            print('reused completed build',mode,previous.name,flush=True)
            continue
        write(build/'src/Kernels.H',(EXP/'production_src/Kernels.H' if mode=='production' else EXP/mode/'Kernels.H').read_text())
        write(build/'src/AmrCoreLBM.cpp',(EXP/'macro/AmrCoreLBM.cpp' if mode=='macro' else EXP/'production_src/AmrCoreLBM.cpp').read_text())
        with (out/f'{mode}_compile.log').open('w') as log:
            subprocess.run(['make','-j8','DIM=3','PRECISION=FLOAT'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        shutil.copy2(build/'main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/f'{mode}.ex')
        with (out/f'{mode}_resources.txt').open('w') as log:
            subprocess.run(['cuobjdump','--dump-resource-usage',str(out/f'{mode}.ex')],stdout=log,stderr=subprocess.STDOUT,check=True)
        print('built',mode,flush=True)
    shutil.copy2(EXP/'baseline.ex',out/'baseline.ex')
    (out/'executables.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.ex')},indent=2)+'\n')
    status=[]
    common=['max_step=80','stop_time=2500000','checkpoint.begin_step=0','amr.n_cell=32','32','32','amr.max_level=2','amr.max_grid_size=16','16','16',
        'amr.blocking_factor_x=8','8','8','amr.blocking_factor_y=8','8','8','amr.blocking_factor_z=8','8','8',
        'amr.regrid_int=8','verification.regrid_fixture=true','amr.plot_int=80','checkpoint.chk_int=-1','checkpoint.chk_prefix=chk','checkpoint.keep_latest_only=false','checkpoint.write_particles=false',
        'verification.convergence_enabled=false','verification.final_state_check=true','verification.osi_seed_pattern=true','performance.report_int=40','performance.osi_detail_timing=0',
        'lbm.reynolds_number=1000','lbm.collide_mode=1','lbm.stream_mode=1','lbm.interp_mode=0','geometry.is_periodic=0','0','0']
    def run(name,mode,args,verify=False):
        directory=out/name;directory.mkdir()
        env=['/usr/bin/env','-u','BASH_ENV','CUDA_VISIBLE_DEVICES=0']
        if verify and mode=='macro':env+=['BOX3D_MACRO_VERIFY=1']
        cmd=[str(REPO/'.venv/bin/python'),str(CASE/'scripts/guard_run.py'),'--','mpirun','-hostfile',str(out/'hostfile'),'-n','1','-npernode','1','-x','PATH','-x','LD_LIBRARY_PATH','--mca','plm_rsh_agent','/opt/batch/agent/tools/dstart']+env+[str(out/f'{mode}.ex'),str(out/'inputs')]+common+args
        (directory/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
        with (directory/'guard.log').open('w') as log:r=subprocess.run(cmd,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        status.append({'name':name,'mode':mode,'code':r.returncode});(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
        assert r.returncode==0,(name,r.returncode)
        print('completed',name,flush=True)
    prior=CASE/'runs/temporary/collision_amr/round1/job621733'
    # 当前正式合入与三种候选，全部对同一个未优化碰撞的独立checkpoint比较。
    for periodic in [0,1]:
        for mode in MODES:
            run(f'dynamic_p{periodic}_{mode}',mode,[f'geometry.is_periodic={periodic}',str(periodic),str(periodic),f'verification.ddf_reference_checkpoint={prior}/dynamic_p{periodic}_frozen/chk00000080'],True)
    source=CASE/'runs/a5_threshold_128000/job621436/B/chk00128000'
    real=['amr.n_cell=128','128','128','amr.max_grid_size=128','256','512','amr.blocking_factor_x=16','16','16','amr.blocking_factor_y=16','16','16','amr.blocking_factor_z=16','16','16',
        'amr.regrid_int=-1','verification.regrid_fixture=false','checkpoint.begin_step=128000',f'checkpoint.chk_prefix={source.parent}/chk','max_step=128032',
        'verification.osi_seed_pattern=false','lbm.err=0.125','0.25','0.5','1','amr.plot_int=32','performance.report_int=16']
    for mode in MODES:
        run(f'real_{mode}',mode,real+[f'verification.ddf_reference_checkpoint={prior}/local_restart_links/chk00128032'],True)
    analyze(out,checks_only=True)
    # 完整推进矩阵只比较单因素；宏观量probe在数值运行内单独计时，不混用total。
    perf=real+['max_step=128320','amr.plot_int=-1','verification.final_state_check=false','performance.report_int=80']
    for repeat,order in enumerate([['baseline','production','interp','average'],['average','interp','production','baseline'],['average','baseline','production','interp']],1):
        for mode in order:run(f'perf_{repeat}_{mode}',mode,perf)
    analyze(out)
    (out/'completed.txt').write_text('completed\n')

def analyze(out,checks_only=False):
    status=json.loads((out/'status.json').read_text());assert all(r['code']==0 for r in status)
    checks=[];macro_checks=0
    for r in status:
        if not r['name'].startswith(('dynamic_','real_')):continue
        txt=(out/r['name']/'run.log').read_text()
        lines=[l for l in txt.splitlines() if l.startswith('ddf_cell_norm_level:')];assert len(lines)==3,(r['name'],lines)
        for l in lines:
            d=dict(re.findall(r'(\w+)=([^ ]+)',l))
            for key in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2']:
                assert math.isfinite(float(d[key])) and float(d[key])==0,(r['name'],key,d[key])
        if r['mode']=='macro':
            norms=re.findall(r'macro_reference lev=(\d+) linf=([^\s]+)',txt);assert norms
            assert all(float(v)==0 for lev,v in norms);macro_checks+=len(norms)
        checks.append(r['name'])
    assert len(checks)==12,len(checks)
    (out/'checks.json').write_text(json.dumps({'full_ddf_comparisons':checks,'all_valid_active_norms_zero':True,'macro_field_comparisons':macro_checks},indent=2)+'\n')
    print('checks passed',len(checks),macro_checks,flush=True)
    if checks_only:return
    records=[];work={}
    for r in status:
        if not r['name'].startswith('perf_'):continue
        txt=(out/r['name']/'run.log').read_text();group='advance'
        lines=[dict(re.findall(r'(\w+)=([^ ]+)',l)) for l in txt.splitlines() if ' perf(s):' in l];assert len(lines)==4
        assert all(int(d['steps'])==80 for d in lines)
        levels={int(lev):int(n) for lev,n in re.findall(r'Level\s+(\d+)\s+\d+ grids\s+(\d+) cells',txt)};assert len(levels)==3
        weighted=sum((1<<lev)*(n-levels.get(lev+1,0)/8) for lev,n in levels.items())
        counts=tuple(re.findall(r'collide_level:.*',txt))
        # 只比较工作计数，不把阶段时间并入工作指纹。
        shape=[tuple((k,v) for k,v in re.findall(r'(lev|calls|launch_cells)=([^ ]+)',l)) for l in counts]
        if group in work:assert work[group]==shape,(r['name'],'work mismatch')
        else:work[group]=shape
        for d in lines:assert abs(float(d['MLUPS_total'])*float(d['total'])*1e6/(weighted*80)-1)<1e-6
        sums={key:sum(float(d[key]) for d in lines[1:]) for key in ['total','collide','interp','average','boundary','comm']}
        records.append(dict(name=r['name'],group=group,mode=r['mode'],steps=240,updates=weighted*240,**sums))
    summary={}
    for group in ['advance']:
        summary[group]={}
        for mode in ['baseline','production','interp','average']:
            rows=[r for r in records if r['group']==group and r['mode']==mode];assert len(rows)==3
            steps=sum(r['steps'] for r in rows);total=sum(r['total'] for r in rows)
            summary[group][mode]={'MLUPS':sum(r['updates'] for r in rows)/total/1e6,**{key+'_ms':1000*sum(r[key] for r in rows)/steps for key in ['total','collide','interp','average','boundary','comm']},'min_ms':min(r['total']/r['steps']*1000 for r in rows),'max_ms':max(r['total']/r['steps']*1000 for r in rows)}
    (out/'summary.json').write_text(json.dumps({'summary':summary,'records':records},indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)
    probes=[];memory=[]
    for r in status:
        txt=(out/r['name']/'run.log').read_text()
        for lev,phase,cached,seconds,calls in re.findall(r'macro_probe lev=(\d+) phase=(\d+) cached=(\d+) seconds=([^ ]+) calls=(\d+)',txt):
            probes.append({'case':r['name'],'lev':int(lev),'phase':int(phase),'cached':int(cached),'ms_per_call':float(seconds)/int(calls)*1000})
        if r['name'].startswith('perf_'):
            used=re.search(r'\[The\s+Arena\] max space \(MB\) used\s+spread across MPI: \[(\d+)',txt)
            reserve=re.search(r'\[The\s+Arena\] max space \(MB\) allocated spread across MPI: \[(\d+)',txt)
            assert used and reserve,r['name']
            memory.append({'case':r['name'],'mode':r['mode'],'arena_peak_used_MB':int(used[1]),'arena_peak_reserved_MB':int(reserve[1])})
    assert any(p['case']=='real_macro' for p in probes)
    (out/'macro_probes.json').write_text(json.dumps(probes,indent=2)+'\n')
    (out/'memory.json').write_text(json.dumps(memory,indent=2)+'\n')
    resources={}
    for mode in MODES:
        text=(out/f'{mode}_compile.log').read_text();items=[]
        pattern=r"Function properties for ([^\n]+)\n\s*(\d+) bytes stack frame, (\d+) bytes spill stores, (\d+) bytes spill loads\nptxas info\s*: Used (\d+) registers"
        for match in re.finditer(pattern,text):
            if any(key in match[1] for key in ['CollideLayoutLevel','RestrictDdfToCoarse','ComputeMacroLevel','InterpolateDdfStageBatch']):
                items.append(dict(symbol=match[1],stack=int(match[2]),spill_stores=int(match[3]),spill_loads=int(match[4]),registers=int(match[5])))
        resources[mode]=items
    (out/'resources.json').write_text(json.dumps(resources,indent=2)+'\n')

def seam_job():
    # 复用主轮命令与逐值参考，仅改变本地ghost复制核；新的节点上重新交替计时。
    source_out=EXP/f"job{os.environ['COORD_SOURCE_JOB']}"
    out=EXP/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'stages.py')
    shutil.copy2(CASE/'scripts/temporary/collision_amr/submit.sh',out/'submit.sh')
    shutil.copy2(EXP/'inputs',out/'inputs')
    shutil.copy2(source_out/'production.ex',out/'production.ex')
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{s.split()[0]} slots={s.split()[1]}\n' for s in alloc if len(s.split())>=2))
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']))
    (out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    build=EXP/'seam_build';assert not build.exists()
    shutil.copytree(EXP/'build',build)
    for p in (EXP/'production_src').iterdir():
        if p.is_file():write(build/'src'/p.name,p.read_text())
    path=build/'src/AmrCoreLBM_osi.cpp';s=path.read_text()
    old="""                const auto dst_shift = phase_shifts[tag.dst_phase_index];
                for (int q = 0; q < Q; ++q) {"""
    new="""                const auto dst_shift = phase_shifts[tag.dst_phase_index];
                const auto source_coordinates = OSI::osi_cell_coordinates(
                    {si, sj, sk}, tag.src_fab, src_shift);
                const auto destination_coordinates = OSI::osi_cell_coordinates(
                    {i, j, k}, tag.dst_fab, dst_shift);
                for (int q = 0; q < Q; ++q) {"""
    assert old in s;s=s.replace(old,new,1)
    for before,after in [("""{si, sj, sk}, {e[q][0], e[q][1], lattice_ez(q)},
                        tag.src_fab, src_shift""","""source_coordinates, {e[q][0], e[q][1], lattice_ez(q)}"""),("""{i, j, k}, {e[q][0], e[q][1], lattice_ez(q)},
                        tag.dst_fab, dst_shift""","""destination_coordinates, {e[q][0], e[q][1], lattice_ez(q)}""")]:
        assert before in s;s=s.replace(before,after,1)
    write(path,s);shutil.copy2(path,out/path.name)
    with (out/'seam_compile.log').open('w') as log:
        subprocess.run(['make','-j8','DIM=3','PRECISION=FLOAT'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(build/'main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/'seam.ex')
    (out/'executables.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.ex')},indent=2)+'\n')
    status=[]
    def run(name,mode,template):
        directory=out/name;directory.mkdir()
        cmd=json.loads((source_out/template/'command.json').read_text())
        cmd=[v.replace(str(source_out),str(out)) for v in cmd]
        cmd=[str(out/f'{mode}.ex') if v==str(out/'production.ex') else v for v in cmd]
        (directory/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
        with (directory/'guard.log').open('w') as log:
            r=subprocess.run(cmd,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        status.append(dict(name=name,mode=mode,code=r.returncode));(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
        assert r.returncode==0,(name,r.returncode)
        print('completed',name,flush=True)
    for template in ['dynamic_p0_production','dynamic_p1_production','real_production']:
        run(template.replace('production','seam'),'seam',template)
    # gate逐层DDF范数，不能以正常退出代替逐值检查。
    for item in status:
        lines=[l for l in (out/item['name']/'run.log').read_text().splitlines() if l.startswith('ddf_cell_norm_level:')];assert len(lines)==3
        for line in lines:
            d=dict(re.findall(r'(\w+)=([^ ]+)',line))
            assert all(math.isfinite(float(d[k])) and float(d[k])==0 for k in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2'])
    (out/'checks.json').write_text(json.dumps({'comparisons':3,'all_valid_active_norms_zero':True},indent=2)+'\n')
    for repeat,order in enumerate([['production','seam'],['seam','production'],['production','seam']],1):
        for mode in order:run(f'perf_{repeat}_{mode}',mode,'perf_1_production')
    records=[];work=None
    for item in status:
        if not item['name'].startswith('perf_'):continue
        txt=(out/item['name']/'run.log').read_text()
        lines=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in txt.splitlines() if ' perf(s):' in line];assert len(lines)==4
        shape=[tuple(re.findall(r'(lev|calls|launch_cells)=([^ ]+)',line)) for line in txt.splitlines() if ' collide_level:' in line]
        if work is None:work=shape
        else:assert work==shape
        for line in lines:assert int(line['steps'])==80
        record=dict(name=item['name'],mode=item['mode'],steps=240)
        for key in ['total','collide','comm','interp','average','boundary']:
            record[key]=sum(float(d[key]) for d in lines[1:])
        levels={int(lev):int(n) for lev,n in re.findall(r'Level\s+(\d+)\s+\d+ grids\s+(\d+) cells',txt)}
        assert len(levels)==3
        weighted=sum((1<<lev)*(n-levels.get(lev+1,0)/8) for lev,n in levels.items())
        for d in lines:assert abs(float(d['MLUPS_total'])*float(d['total'])*1e6/(weighted*80)-1)<1e-6
        record['updates']=weighted*240
        records.append(record)
    summary={}
    for mode in ['production','seam']:
        rows=[r for r in records if r['mode']==mode];assert len(rows)==3
        steps=sum(r['steps'] for r in rows);total=sum(r['total'] for r in rows)
        summary[mode]={'MLUPS':sum(r['updates'] for r in rows)/total/1e6,**{key+'_ms':1000*sum(r[key] for r in rows)/steps for key in ['total','collide','comm','interp','average','boundary']},'min_ms':min(r['total']/r['steps']*1000 for r in rows),'max_ms':max(r['total']/r['steps']*1000 for r in rows)}
    (out/'summary.json').write_text(json.dumps({'summary':summary,'records':records},indent=2)+'\n')
    (out/'completed.txt').write_text('completed\n');print(json.dumps(summary,indent=2),flush=True)

def integration_job():
    # 接入验收仅编译一次、复用一个真实AMR逐值用例；不重复性能矩阵。
    exp=CASE/'runs/temporary/collision_amr/round3'
    out=exp/f"job{os.environ['CCS_JOB_ID']}";out.mkdir()
    shutil.copy2(__file__,out/'stages.py')
    shutil.copy2(CASE/'scripts/temporary/collision_amr/submit.sh',out/'submit.sh')
    build=exp/'build'
    with (out/'compile.log').open('w') as log:
        subprocess.run(['make','-j8','DIM=3','PRECISION=FLOAT'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(build/'main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex',out/'production.ex')
    previous=EXP/'job621802'
    shutil.copy2(previous/'inputs',out/'inputs')
    alloc=Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
    (out/'hostfile').write_text(''.join(f'{row.split()[0]} slots={row.split()[1]}\n' for row in alloc if len(row.split())>=2))
    (out/'node.txt').write_bytes(subprocess.check_output(['hostname']))
    (out/'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    test=out/'real_integration';test.mkdir()
    cmd=json.loads((previous/'real_production/command.json').read_text())
    cmd=[value.replace(str(previous),str(out)) for value in cmd]
    (test/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    with (test/'guard.log').open('w') as log:
        result=subprocess.run(cmd,cwd=test,stdout=log,stderr=subprocess.STDOUT)
    assert result.returncode==0,result.returncode
    lines=[line for line in (test/'run.log').read_text().splitlines() if line.startswith('ddf_cell_norm_level:')];assert len(lines)==3
    for line in lines:
        values=dict(re.findall(r'(\w+)=([^ ]+)',line))
        assert all(math.isfinite(float(values[k])) and float(values[k])==0 for k in ['valid_linf','valid_l1','valid_l2','active_linf','active_l1','active_l2'])
    def digest(path):
        h=hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(8*1024*1024),b''):h.update(block)
        return h.hexdigest()
    payloads=[]
    for plot in ['plt_128032','plt_vort_128032']:
        old=previous/'real_production'/plot;new=test/plot
        files=sorted(path.relative_to(old) for path in old.glob('Level_*/Cell_D_*'))
        assert files and files==sorted(path.relative_to(new) for path in new.glob('Level_*/Cell_D_*'))
        for relative in files:
            value=digest(new/relative);assert value==digest(old/relative),(plot,str(relative))
            payloads.append(dict(plot=plot,file=str(relative),sha256=value))
    report=dict(exit_code=0,ddf_levels=3,all_valid_active_norms_zero=True,velocity_vorticity_payloads=payloads,performance_matrix_repeated=False,executable_sha256=digest(out/'production.ex'))
    (out/'checks.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'completed.txt').write_text('completed\n');print(json.dumps(report,indent=2),flush=True)

if __name__=='__main__':
    if sys.argv[1]=='run':run_job()
    elif sys.argv[1]=='analyze':analyze(Path(sys.argv[2]))
    elif sys.argv[1]=='seam':seam_job()
    elif sys.argv[1]=='integrate':integration_job()
    else:raise SystemExit('run | analyze OUT')
