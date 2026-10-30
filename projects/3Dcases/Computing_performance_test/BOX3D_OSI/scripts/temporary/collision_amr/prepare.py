# 本轮临时AMR因素试验生成器，正式接入只改固定单层，AMR候选只在隔离源码中生成。
from pathlib import Path
import shutil,subprocess,json
case=Path(__file__).resolve().parents[3];exp=case/'runs/temporary/collision_amr/round1';build=exp/'build'
assert not build.exists();build.mkdir(parents=True)
for d in ['src','config']:shutil.copytree(case/d,build/d)
shutil.copy2(case/'GNUmakefile',build/'GNUmakefile')
shutil.copy2(case/'config/inputs',exp/'inputs')
shutil.copy2(case/'runs/uniform_256_lev0_t1000/application.ex',exp/'frozen.ex')
for d in ['o','s']:
 source=case/f'runs/temporary/collision_factors/round1/build/tmp_build_dir/particles_float/{d}/3d.gnu.FLOAT.TPROF.MPI.CUDA.EXE'
 if source.exists():shutil.copytree(source,build/f'tmp_build_dir/particles_float/{d}/3d.gnu.FLOAT.TPROF.MPI.CUDA.EXE')
for p in (build/'src').iterdir():
 if p.is_file():p.touch()
s=(build/'src/AmrCoreLBM_advance.cpp').read_text()
a=s.index('        } else {\n            if (use_osi) {',s.index('void AmrCoreLBM::CollideLayoutLevel'))
b=s.index('            } else if (collide_mode == 0)',a)
original=s[a:b];assert 'covered(i, j, k)' in original
new='''        } else {
            if (use_osi) {
                const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
                // AMR试验仅host分派；每种候选保持covered/interface筛选。
                static const int mode = []() {
                    const char* value = std::getenv("BOX3D_AMR_COLLISION_TEST_MODE");
                    return value ? std::atoi(value) : 0;
                }();
                AMREX_ALWAYS_ASSERT(mode >= 0 && mode <= 3);
'''
for mode,block,cache in [(0,256,False),(1,128,False),(2,256,True),(3,128,True)]:
 call='collide_bgk_register_impl<true, true>' if cache else 'collide_bgk_register_osi'
 new+=('                if' if mode==0 else '                else if')+f' (mode == {mode}) {{\n'
 new+=f'''                    amrex::ParallelFor<{block}>(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {{
                            if (has_fine_level && covered(i, j, k) != 0 &&
                                interface(i, j, k) == 0) {{
                                return;
                            }}
                            {call}(i, j, k, state, fab, phase_shift, omega_lev);
                        }});
                }}
'''
variant='#include <cstdlib>\n'+s[:a]+new+s[b:]
(exp/'amr_candidate_advance.cpp').write_text(variant)
(exp/'production.diff').write_bytes(subprocess.check_output(['git','-C',str(case),'diff','--','src/Kernels.H','src/AmrCoreLBM_advance.cpp']))
r=subprocess.run(['diff','-u','--label','a/src/AmrCoreLBM_advance.cpp','--label','b/src/AmrCoreLBM_advance.cpp',str(build/'src/AmrCoreLBM_advance.cpp'),str(exp/'amr_candidate_advance.cpp')],capture_output=True);assert r.returncode==1
(exp/'amr_candidate.patch').write_bytes(r.stdout)
(exp/'manifest.json').write_text(json.dumps({'modes':{'0':'original256','1':'original128','2':'cached256','3':'cached128'},'checkpoint':'runs/a5_threshold_128000/job621436/B/chk00128000','production_scope':'CUDA 3D FLOAT single rank fixed lev0 OSI nomask','amr':'experiment only; covered/interface retained'},indent=2)+'\n')
print(exp)
