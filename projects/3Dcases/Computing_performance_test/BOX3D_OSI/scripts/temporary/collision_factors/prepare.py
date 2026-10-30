# 本轮临时实验生成器；不修改正式源码，保留结果期间用于复现。
from pathlib import Path
import shutil,hashlib,json,subprocess
case=Path(__file__).resolve().parents[3];exp=case/'runs/temporary/collision_factors/round1';build=exp/'build'
assert not build.exists(),'不要覆盖已有构建/运行现场'
build.mkdir(parents=True)
for name in ['src','config']:shutil.copytree(case/name,build/name)
shutil.copy2(case/'GNUmakefile',build/'GNUmakefile')
shutil.copy2(case/'config/inputs',exp/'inputs')
shutil.copy2(case/'runs/uniform_256_lev0_t1000/application.ex',exp/'frozen_baseline.ex')
(exp/'git_head.txt').write_text(subprocess.check_output(['git','-C',str(case),'rev-parse','HEAD'],text=True))
(exp/'worktree.diff').write_bytes(subprocess.check_output(['git','-C',str(case),'diff','--','src','config']))
# 复用AMReX构建缓存但强制重编本算例源，避免将旧算例对象误当本次构建。
for category in ['o','s']:
 src=case/f'tmp_build_dir/particles_float/{category}/3d.gnu.FLOAT.TPROF.MPI.CUDA.EXE'
 dest=build/f'tmp_build_dir/particles_float/{category}/3d.gnu.FLOAT.TPROF.MPI.CUDA.EXE'
 if src.exists():shutil.copytree(src,dest)
for p in (build/'src').iterdir():
 if p.is_file():p.touch()
p=build/'src/Kernels.H';s=p.read_bytes().decode();nl='\r\n' if '\r\n' in s else '\n';s=s.replace('\r\n','\n')
a=s.index('// A-B 与 OSI 共用 BGK');b=s.index('AMREX_GPU_DEVICE\nAMREX_FORCE_INLINE\nvoid collide_bgk_register(',a)
part=s[a:b].replace('template <bool UseOsiAddress>','template <bool UseOsiAddress, bool CacheCoordinates = false>')
part=part.replace('    Real fq[Q];','    OSI::CellCoordinates coordinates{};\n    if constexpr (CacheCoordinates) {\n        coordinates = OSI::osi_cell_coordinates({i, j, k}, fab, phase_shift);\n    }\n    Real fq[Q];')
part=part.replace('''        const Real ft = load_ddf<UseOsiAddress>(
            i, j, k, q, state, fab, phase_shift);''','''        Real ft;
        if constexpr (CacheCoordinates) {
            ft = load_ddf_cached<UseOsiAddress>(i, j, k, q, state, coordinates);
        } else {
            ft = load_ddf<UseOsiAddress>(i, j, k, q, state, fab, phase_shift);
        }''')
part=part.replace('''        store_ddf<UseOsiAddress>(
            i, j, k, q, value, state, fab, phase_shift);''','''        if constexpr (CacheCoordinates) {
            store_ddf_cached<UseOsiAddress>(i, j, k, q, value, state, coordinates);
        } else {
            store_ddf<UseOsiAddress>(i, j, k, q, value, state, fab, phase_shift);
        }''')
assert part!=s[a:b];s=s[:a]+part+s[b:];p.write_bytes(s.replace('\n',nl).encode())
p=build/'src/AmrCoreLBM_advance.cpp';s=p.read_text();a=s.index('        if constexpr (!UseMask) {',s.index('void AmrCoreLBM::CollideLayoutLevel'));b=s.index('            } else if (collide_mode == 0)',a)
old=s[a:b]
new='''        if constexpr (!UseMask) {
            if (use_osi) {
                const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
                // 仅实验副本使用环境选择；各选项在host分派，GPU不执行模式判断。
                static const int mode = []() {
                    const char* value = std::getenv("BOX3D_COLLISION_TEST_MODE");
                    return value ? std::atoi(value) : 0;
                }();
                AMREX_ALWAYS_ASSERT(mode >= 0 && mode <= 3);
'''
for mode,block,cache in [(0,256,False),(1,128,False),(2,256,True),(3,128,True)]:
 new+=('                if' if mode==0 else '                else if')+f' (mode == {mode}) {{\n'
 call='collide_bgk_register_impl<true, true>' if cache else 'collide_bgk_register_osi'
 new+=f'''                    amrex::ParallelFor<{block}>(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {{
                            {call}(i, j, k, state, fab, phase_shift, omega_lev);
                        }});
                }}
'''
s=s[:a]+new+s[b:];s='#include <cstdlib>\n'+s;p.write_text(s)
# 保存限定补丁与文件指纹，实验副本含用户当时已有修改，不能称干净HEAD。
diffs=[]
for name in ['Kernels.H','AmrCoreLBM_advance.cpp']:
 r=subprocess.run(['diff','-u','--label','a/src/'+name,'--label','b/src/'+name,str(case/'src'/name),str(build/'src'/name)],capture_output=True)
 assert r.returncode==1;diffs.append(r.stdout)
(exp/'candidate.patch').write_bytes(b''.join(diffs))
(exp/'manifest.json').write_text(json.dumps({'modes':{'0':'original256','1':'original128','2':'cached256','3':'cached128'},'baseline_sha256':hashlib.sha256((exp/'frozen_baseline.ex').read_bytes()).hexdigest(),'source_scope':'current worktree including existing user changes'},indent=2)+'\n')
print(build)
