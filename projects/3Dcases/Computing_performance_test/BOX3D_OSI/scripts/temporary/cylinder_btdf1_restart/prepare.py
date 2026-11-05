#!/usr/bin/env python3
# 本次 BTDF1→OSI 续算的实验源码适配器；保留以复现迁移，不自动用于生产算例。
from pathlib import Path
import shutil, sys, hashlib, json, difflib
repo = Path(__file__).resolve().parents[7]
case = repo / 'projects/3Dcases/Computing_performance_test/BOX3D_OSI'
donor = repo / 'projects/Cylindertest/Cylinder2D_BTDF1'
out = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else case / 'runs/cylinder_btdf1_restart_520000'
if out.exists():
    raise SystemExit(f'拒绝覆盖已有实验目录: {out}')
out.mkdir(parents=True)
for name in ('src', 'config'):
    shutil.copytree(case/name, out/'build'/name)
shutil.copy2(case/'GNUmakefile', out/'build/GNUmakefile')
shutil.copytree(donor/'src', out/'donor_src')
shutil.copy2(donor/'config/inputs', out/'donor_inputs')
(out/'README.md').write_text('正式迁移实验：从 BTDF1 的 520000 步状态续算至 584000。保留源码、补丁、输入、命令、日志、检查点和流场。短测验证通过前不得启动生产续算。\n')
src = out/'build/src'
def edit(name, old, new, count=1):
    p=src/name; s=p.read_text()
    if s.count(old) != count: raise RuntimeError((name, old[:90], s.count(old), count))
    p.write_text(s.replace(old,new))
# 压力型分布与参考参数：直接保持旧 DDF 数值，只重排方向。
edit('LbmLattice.H', '#define rho0 (1.0_rt)', '#define rho0 (3.0_rt)')
edit('LbmLattice.H', '#define p0 (rho0 * cs2)', '#define p0 (1.0_rt)')
edit('LbmLattice.H', '#define U0 (0.05_rt)', '#define U0 (0.03_rt)')
edit('LbmLattice.H', 'const Real LP_dr = 1.0_rt;', 'const Real LP_dr = 1.9_rt;')
edit('LbmLattice.H', 'feqt = w[i] * rhot * (1.0_rt + 3.0_rt * eu + 4.5_rt * eu * eu - 1.5_rt * uv);', 'feqt = w[i] * (rhot + p0 * (3.0_rt * eu + 4.5_rt * eu * eu - 1.5_rt * uv));')
# 所有压力型宏观重建/非平衡缩放统一使用参考压力。
p=src/'Kernels.H'; s=p.read_text()
for a,b in [('uxt / rhot','uxt / p0'),('uyt / rhot','uyt / p0'),('uzt / rhot','uzt / p0'),('1.0_rt / rhot','1.0_rt / p0'),('1.0_rt / rho;','1.0_rt / p0;')]: s=s.replace(a,b)
s=s.replace('const Real feq = w[q] * rho *\n                         (equilibrium_common + 3.0_rt * eu + 4.5_rt * eu * eu);','const Real feq = feqHeLuo(rho, {AMREX_D_DECL(ux,uy,uz)}, q);')
s=s.replace('(mx + Real(0.5)*dt*F[0])/rho','mx/p0 + Real(0.5)*dt*F[0]/rho0').replace('(my + Real(0.5)*dt*F[1])/rho','my/p0 + Real(0.5)*dt*F[1]/rho0').replace('(mz + Real(0.5)*dt*F[2])/rho','mz/p0 + Real(0.5)*dt*F[2]/rho0')
s=s.replace('(Real(1)-Real(0.5)*omega)*dt*forceGuo(u,F,q);','(Real(1)-Real(0.5)*omega)*dt*forceGuo(u,F,q)*cs2;')
# 平滑三点核直接移植，避免同名 delta3p 所代表的四点核混淆。
old=(donor/'src/Kernels.H').read_text(); fun=old[old.index('Real delta3pSmoothed(Real r) {'):old.index('\nAMREX_GPU_HOST_DEVICE',old.index('Real delta3pSmoothed(Real r) {'))]
s=s.replace('#if AMREX_SPACEDIM == 2\ntemplate <typename P>\nAMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void force_interp_extrap(', 'AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE\n'+fun+'\n#if AMREX_SPACEDIM == 2\ntemplate <typename P>\nAMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void force_interp_extrap(')
a=s.index('#if AMREX_SPACEDIM == 2\ntemplate <typename P>\nAMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void force_interp_extrap('); b=s.index('#else',a)
part=s[a:b].replace('delta3p(', 'delta3pSmoothed(').replace('/ grid.dt_min','/ cs2 / grid.dt_min')
s=s[:a]+part+s[b:]; p.write_text(s)
edit('LagrangeParticleContainer.cpp', 'std::ceil(2 * PI * radius / ds0)', '(2 * PI * radius / ds0)', count=2)
# 粒子双精度保留旧标记坐标；由解析静止几何重新生成 502 点，无运动历史丢失。
p=out/'build/config/GNUmakefile'; s=p.read_text().replace('USE_SINGLE_PRECISION_PARTICLES = TRUE','USE_SINGLE_PRECISION_PARTICLES = FALSE'); p.write_text(s)
edit('main.cpp','U0 * x_length / input.reynolds_number','U0 * (Real(20) * dx) / input.reynolds_number')
# 旧 checkpoint 明确白名单导入；本实验读入源由独立的 ImportBTDF1 标志文件指定。
edit('AmrCoreLBM.cpp', '    ValidateCheckpointLattice(chkname);','    const bool import_btdf1 = amrex::FileExists(chkname + "/ImportBTDF1");\n    if (!import_btdf1) { ValidateCheckpointLattice(chkname); }\n    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(!import_btdf1 || (AMREX_SPACEDIM == 2 && Geom(0).Domain().length(0) == 800 && max_level == 3), "BTDF1 import requires 800 squared L3");')
edit('AmrCoreLBM.cpp','stored_enabled == params_.ibm_enabled,','(import_btdf1 || stored_enabled == params_.ibm_enabled),')
needle='        // 拷贝完成后先释放读入缓冲，避免大网格恢复时三份 DDF 同时驻留。'
insert='''        if (import_btdf1) {
            for (MFIter mfi(canonical); mfi.isValid(); ++mfi) {
                const auto a = canonical.array(mfi);
                amrex::ParallelFor(mfi.validbox(), [=] AMREX_GPU_DEVICE(int i,int j,int k) {
                    const int map[9] = {0,1,3,2,4,5,7,8,6};
                    Real v[9]; for (int q=0;q<9;++q) v[q]=a(i,j,k,map[q]);
                    for (int q=0;q<9;++q) a(i,j,k,q)=v[q];
                });
            }
        }
'''
edit('AmrCoreLBM.cpp',needle,insert+needle)
edit('AmrCoreLBM.cpp','            particles[i]->Restart(chkname, pname);','            if (import_btdf1) { particles[i]->InitParticle(finest_level); }\n            else { particles[i]->Restart(chkname, pname); }')
edit('AmrCoreLBM.cpp','const bool correct_force = params_.ibm_enabled && include_ibm_force;','const bool correct_force = false; // 原 BTDF1 输出与打标使用原始压力矩，不加半步力。\n    (void)include_ibm_force;')
# 分项计时：作用域结束等待 GPU，避免只统计异步发射时间。
edit('AmrCoreLBM.H','        double collide = 0.0;','        double collide = 0.0;\n        double particle_total=0, particle_macro=0, particle_force=0, particle_sum=0, particle_ghost=0;')
edit('AmrCoreLBM.cpp','    ComputeMacroLevel(lev, false);\n    density[lev].FillBoundary(Geom(lev).periodicity());\n    velocity[lev].FillBoundary(Geom(lev).periodicity());\n    InterpForce(lev);\n    SumForce(lev);\n    FillForceGhostLevel(lev, 0.0);','''    ScopedPerfTimer total(perf_stats.particle_total);
    { ScopedPerfTimer t(perf_stats.particle_macro);
      ComputeMacroLevel(lev, false);
      density[lev].FillBoundary(Geom(lev).periodicity());
      velocity[lev].FillBoundary(Geom(lev).periodicity()); }
    { ScopedPerfTimer t(perf_stats.particle_force); InterpForce(lev); }
    { ScopedPerfTimer t(perf_stats.particle_sum); SumForce(lev); }
    { ScopedPerfTimer t(perf_stats.particle_ghost); FillForceGhostLevel(lev, 0.0); }''')
edit('main.cpp','                  << " collide=" << perf.collide','                  << " particle_total=" << perf.particle_total\n                  << " particle_macro=" << perf.particle_macro\n                  << " particle_force=" << perf.particle_force\n                  << " particle_sum=" << perf.particle_sum\n                  << " particle_ghost=" << perf.particle_ghost\n                  << " collide=" << perf.collide')
# 与旧算法一样，先用步入口分布计算并保存边界，再碰撞/迁移，最后覆盖新边界。
# 每次推进分配临时边界值缓冲，随本次推进释放；分配开销计入总时间。
edit('AmrCoreLBM_advance.cpp','void AmrCoreLBM::AdvanceOneLayout(int lev, DdfLayout layout) {','''void AmrCoreLBM::AdvanceOneLayout(int lev, DdfLayout layout) {
    MultiFab boundary_values(boxArray(lev), DistributionMap(lev), Q, 0);
    const auto hi = Geom(lev).Domain().bigEnd();
    auto& input_state = layout == DdfLayout::Osi ? osi_state[lev] : f_old[lev];
    for (MFIter mfi(input_state); mfi.isValid(); ++mfi) {
        if (!mfi.validbox().intersects(Geom(lev).Domain().bdryLo(0))) { /* 下方逐点筛选 */ }
        const auto a=input_state.array(mfi); const auto b=boundary_values.array(mfi);
        const auto context=OSI::MakeOsiFabContext(amrex::grow(mfi.validbox(),input_state.nGrowVect()),osi_phase[lev]);
        const auto fab=context.first; const auto phase=context.second;
        const bool osi=layout==DdfLayout::Osi;
        amrex::ParallelFor(mfi.validbox(),[=] AMREX_GPU_DEVICE(int i,int j,int k) {
            if (i!=0 && i!=hi[0] && j!=0 && j!=hi[1]) return;
            int x=i,y=j;
            if(i==0)x=i+1; if(i==hi[0])x=i-1;
            // 与原代码四个独立 if 的末次覆盖顺序相同，角点采用 y 面。
            if(j==0){x=i;y=j+1;} if(j==hi[1]){x=i;y=j-1;}
            Real f[Q],p=0,ux=0,uy=0;
            for(int q=0;q<Q;++q){f[q]=osi?load_ddf<true>(x,y,k,q,a,fab,phase):a(x,y,k,q);p+=f[q];ux+=f[q]*e[q][0];uy+=f[q]*e[q][1];}
            ux/=p0;uy/=p0;
            const bool inlet=i==0 && j!=0 && j!=hi[1];
            const RealVect u{inlet?U0:ux,inlet?Real(0):uy};
            for(int q=0;q<Q;++q)b(i,j,k,q)=feqHeLuo(p,u,q)+f[q]-feqHeLuo(p,{ux,uy},q);
        });
    }
    Gpu::streamSynchronize();''')
# 上面不需要 Box 的边界构造（避免依赖特殊 AMReX API）。
edit('AmrCoreLBM_advance.cpp','        if (!mfi.validbox().intersects(Geom(lev).Domain().bdryLo(0))) { /* 下方逐点筛选 */ }\n','')
edit('AmrCoreLBM_advance.cpp','    Boundary(lev, layout);','''    // 恢复步入口计算的外推边界；此时 OSI phase 已推进。
    auto& output_state=layout==DdfLayout::Osi?osi_state[lev]:f_new[lev];
    for(MFIter mfi(output_state);mfi.isValid();++mfi){
        const auto a=output_state.array(mfi);const auto b=boundary_values.const_array(mfi);
        const auto context=OSI::MakeOsiFabContext(amrex::grow(mfi.validbox(),output_state.nGrowVect()),osi_phase[lev]);
        const auto fab=context.first;const auto phase=context.second;const bool osi=layout==DdfLayout::Osi;
        amrex::ParallelFor(mfi.validbox(),[=] AMREX_GPU_DEVICE(int i,int j,int k){
            if(i!=0&&i!=hi[0]&&j!=0&&j!=hi[1])return;
            for(int q=0;q<Q;++q){if(osi)store_ddf<true>(i,j,k,q,b(i,j,k,q),a,fab,phase);else a(i,j,k,q)=b(i,j,k,q);}
        });
    }
    Gpu::streamSynchronize();''')
# 重网格修补边界采用同样入口/外推条件（此处无旧时刻暂存）。
p=src/'Kernels.H';s=p.read_text();a=s.index('    int x1 = i, y1 = j, z1 = k;',s.index('void fill_boundary_impl'));b=s.index('\n}\n',a)
s=s[:a]+'''    if(i!=0&&i!=hi[0]&&j!=0&&j!=hi[1])return;
    int x=i,y=j;
    if(i==0)x=i+1;if(i==hi[0])x=i-1;
    if(j==0){x=i;y=j+1;}if(j==hi[1]){x=i;y=j-1;}
    Real f[Q],p=0,ux=0,uy=0;
    for(int q=0;q<Q;++q){f[q]=load_ddf<UseOsiAddress>(x,y,k,q,state,fab,phase_shift);p+=f[q];ux+=f[q]*e[q][0];uy+=f[q]*e[q][1];}
    ux/=p0;uy/=p0;
    const bool inlet=i==0&&j!=0&&j!=hi[1];
    for(int q=0;q<Q;++q){const Real value=feqHeLuo(p,{inlet?U0:ux,inlet?Real(0):uy},q)+f[q]-feqHeLuo(p,{ux,uy},q);store_ddf<UseOsiAddress>(i,j,k,q,value,state,fab,phase_shift);}
'''+s[b:];p.write_text(s)
# 原标记条件读取涡量分量 1（BOX 的分量0为 Q）。
edit('AmrCoreLBM_amr.cpp','    if (params_.ibm_enabled) {\n        // 将完整物体','    if (false) {\n        // 原 BTDF1 使用圆形距离带，替换默认矩形支撑标记。\n        // 将完整物体')
a='                state_error_3(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);'
b='''                const Real dx=geomdata.CellSize(0);
                const Real cx=Real(0.5)/dx, cy=Real(0.5)/dx;
                const Real r=Real(0.0125)/dx;
                const Real dist=std::sqrt((i-cx)*(i-cx)+(j-cy)*(j-cy));
                const Real w=vort(i,j,k,1);
                if(dist<=r+10 || (lev==0&&w>=Real(0.1)) || (lev==1&&w>=Real(0.4)) ||
                   (lev==2&&w>=Real(0.6)&&w<=Real(4)&&j>=Real(0.4625)/dx&&j<=Real(0.5375)/dx))tagfab(i,j,k)=tagval;'''
# 同时替换未编译的 3D 分支，副本只允许二维构建。
edit('AmrCoreLBM_amr.cpp',a,b,count=2)
# 默认重网格初始新暴露区采用原均匀来流平衡态。
p=src/'AmrCoreLBM_amr.cpp';s=p.read_text().replace('feqQian(rho0, {AMREX_D_DECL(0.0, 0.0, 0.0)}, q)','feqHeLuo(p0, {AMREX_D_DECL(U0, 0.0, 0.0)}, q)');p.write_text(s)
# 双数组/OSI 必须分别运行比较；禁止锁步参考调用绕开本实验步入口边界暂存。
edit('AmrCoreLBM_advance.cpp','    if (run_mode == RunMode::OsiLockstep) {','    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(run_mode != RunMode::OsiLockstep,"BTDF1 profile requires separate AB/OSI regression");\n    if (run_mode == RunMode::OsiLockstep) {')
# nvcc 设备 lambda 需要公开 host 启动函数；只改变本实验副本的可见性。
edit('AmrCoreLBM.H','    void AdvanceOneLayout(int lev, DdfLayout layout);','  public:\n    void AdvanceOneLayout(int lev, DdfLayout layout);\n  private:')
p=src/'AmrCoreLBM_advance.cpp';p.write_text(p.read_text().replace('context.first','context.fab').replace('context.second','context.phase_shift'))
p=src/'Kernels.H';s=p.read_text()
for c in ('ux','uy','uz'):s=s.replace(c+' /= rho;',c+' /= p0;')
p.write_text(s)
# 原 Boundary 遍历 grown-Fab 的域内物理面，包含 patch ghost；不能只修 valid。
p=src/'AmrCoreLBM_advance.cpp';s=p.read_text();a=s.index('void AmrCoreLBM::AdvanceOneLayout(');b=s.index('\nvoid AmrCoreLBM::AdvanceLevel(',a);part=s[a:b].replace('DistributionMap(lev), Q, 0);','DistributionMap(lev), Q, nghost);').replace('const auto hi = Geom(lev).Domain().bigEnd();','const Box domain=Geom(lev).Domain();\n    const auto hi = domain.bigEnd();').replace('amrex::ParallelFor(mfi.validbox(),','amrex::ParallelFor(mfi.growntilebox(nghost) & domain,');s=s[:a]+part+s[b:];p.write_text(s)
# 颗粒归约/输出与重网格后的迁移另列，避免混入插值施力耗时。
edit('AmrCoreLBM.H','double particle_total=0, particle_macro=0, particle_force=0, particle_sum=0, particle_ghost=0;','double particle_total=0, particle_macro=0, particle_force=0, particle_sum=0, particle_ghost=0, particle_reduce=0, particle_redistribute=0;')
edit('AmrCoreLBM.cpp','void AmrCoreLBM::ReduceFxy(int lev, int step) {','void AmrCoreLBM::ReduceFxy(int lev, int step) {\n    ScopedPerfTimer timer(perf_stats.particle_reduce);')
edit('AmrCoreLBM.cpp','void AmrCoreLBM::RedistributeParticle() {','void AmrCoreLBM::RedistributeParticle() {\n    ScopedPerfTimer timer(perf_stats.particle_redistribute);')
edit('main.cpp','                  << " particle_total=" << perf.particle_total','                  << " particle_reduce=" << perf.particle_reduce\n                  << " particle_redistribute=" << perf.particle_redistribute\n                  << " particle_total=" << perf.particle_total')
edit('main.cpp','    InitializeSimulation(lid, cur_time);','    InitializeSimulation(lid, cur_time);\n    // 在独立输出目录保存导入状态的初始流场。\n    lid.WriteVelocityFile(begin_step, cur_time);')
# 边界计时覆盖准备及恢复；暂存数组分配仍包含于求解器总时间。
p=src/'AmrCoreLBM_advance.cpp';s=p.read_text();s=s.replace('    auto& input_state = layout == DdfLayout::Osi ? osi_state[lev] : f_old[lev];','    { ScopedPerfTimer boundary_timer(perf_stats.boundary);\n    auto& input_state = layout == DdfLayout::Osi ? osi_state[lev] : f_old[lev];');s=s.replace('    Gpu::streamSynchronize();\n    // 支持路径','    Gpu::streamSynchronize(); }\n    // 支持路径');s=s.replace('    auto& output_state=layout==DdfLayout::Osi?osi_state[lev]:f_new[lev];','    { ScopedPerfTimer boundary_timer(perf_stats.boundary);\n    auto& output_state=layout==DdfLayout::Osi?osi_state[lev]:f_new[lev];');s=s.replace('    Gpu::streamSynchronize();\n    if (sample)', '    Gpu::streamSynchronize(); }\n    if (sample)');p.write_text(s)
# 最终补丁包括以上所有修订。

# 初始场直接从已导入 valid DDF 重建，不额外平均或改变原始状态。
edit('main.cpp', '    lid.WriteVelocityFile(begin_step, cur_time);', '    for (int lev=0; lev<=lid.finestLevel(); ++lev) { lid.ComputeMacroLevel(lev, false); }\n    lid.WriteVelocityFile(begin_step, cur_time);')
edit('main.cpp', '        lid.ComputeVorticity(cur_time);', '        lid.ComputeVorticity(cur_time);\n        lid.ValidateInitializedState("OutputState");')
manifest={str(p.relative_to(donor)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (donor/'chk00520000').rglob('*') if p.is_file()}
(out/'source_checkpoint_sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
patch=''.join(''.join(difflib.unified_diff((case/'src'/p.name).read_text().splitlines(True),p.read_text().splitlines(True),fromfile='a/src/'+p.name,tofile='b/src/'+p.name)) for p in src.iterdir() if p.is_file() and (case/'src'/p.name).is_file())
(out/'adaptation.patch').write_text(patch)
print(out)
# 默认同时准备独立导入副本；--source-only 用于源码重建验证，不复制大数据。
if '--source-only' not in sys.argv:
    import re
    shutil.copy2(Path(__file__).with_name('inputs'), out/'inputs')
    imported=out/'import/chk00520000'
    imported.parent.mkdir()
    shutil.copytree(donor/'chk00520000', imported)
    (imported/'ImportBTDF1').write_text('Cylinder2D_BTDF1 520000 DIM2 Q9 pressure-HeLuo smoothed3p NF1\n')
    legacy=out/'import_donor/chk00520000'
    legacy.parent.mkdir()
    shutil.copytree(donor/'chk00520000',legacy)
    p=legacy/'Header'
    def remap(match):
        lines=match.group().splitlines()
        count=int(lines[0][1:])
        assert len(lines[1:-1])==count
        return f'({count}\n'+'0\n'*count+')\n'
    converted,n=re.subn(r'^\(\d+\n(?:\d+\n)+\)\n',remap,p.read_text(),flags=re.M)
    assert n==4
    p.write_text(converted)
    shutil.copy2(donor/'main2d.gnu.MPI.CUDA.ex',out/'donor.ex')
