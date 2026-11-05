"""AI 临时实验生成器；保留以复现优化累加图，任务结束且无复现依赖时可删除。"""
from pathlib import Path
import difflib
import json
import shutil
import subprocess
import sys

CASE = Path(__file__).resolve().parents[3]
EXTRA = '--collision-extra' in sys.argv
EXP = CASE / 'runs/temporary/optimization_ladder' / ('round2_collision' if EXTRA else 'round1')
BUILD = EXP / 'build'
EXP.mkdir(parents=True, exist_ok=False)
if EXTRA:
    # 扩展同一实验入口；从主图冻结源码恢复，避免后续正式源码变化混入。
    previous=CASE/'runs/temporary/optimization_ladder/round1/job626333'
    for directory in ('src','config'):
        shutil.copytree(previous/directory,BUILD/directory)
    shutil.copy2(CASE/'GNUmakefile',BUILD/'GNUmakefile')
    shutil.copytree(CASE/'tmp_build_dir',BUILD/'tmp_build_dir')
    for name in ('inputs','git_head.txt','worktree.diff'):
        shutil.copy2(previous/name,EXP/name)
    h=BUILD/'src/AmrCoreLBM.H'
    s=h.read_text()
    s=s.replace('    amrex::TagVector<BoundaryWorkTag> advance_tags;',
        '''    // 碰撞实验缓存由当前状态布局派生，仅存几何与索引；随原缓存一起失效。
    amrex::TagVector<BoundaryWorkTag> collision_tags;
    amrex::Long collision_cells = 0;
    amrex::TagVector<BoundaryWorkTag> advance_tags;''',1)
    s=s.replace('        advance_tags.undefine();','        collision_tags.undefine();\n        collision_cells = 0;\n        advance_tags.undefine();',1)
    h.write_text(s)
    p=BUILD/'src/AmrCoreLBM_amr.cpp';s=p.read_text()
    a=s.index('void AmrCoreLBM::BuildBoundaryWorkCache()')
    b=s.index('void AmrCoreLBM::BuildInterpolationCache()',a)
    part=s[a:b]
    part=part.replace('Vector<BoundaryWorkTag> advance_tags;', 'Vector<BoundaryWorkTag> collision_tags;\n        Vector<BoundaryWorkTag> advance_tags;',1)
    needle='            cache.full_cells += valid_box.numPts();'
    part=part.replace(needle,needle+'''
            if (ladder_option("LADDER_COLLISION_BATCH")) {
                const Box collision_box = amrex::grow(valid_box, nghost) & Geom(lev).growPeriodicDomain(nghost);
                collision_tags.push_back({collision_box, fab, mfi.LocalIndex(), phase_index});
                cache.collision_cells += collision_box.numPts();
            }
''',1)
    part=part.replace('        cache.advance_tags.define(advance_tags);','        cache.collision_tags.define(collision_tags);\n        cache.advance_tags.define(advance_tags);',1)
    p.write_text(s[:a]+part+s[b:])
    p=BUILD/'src/AmrCoreLBM_advance.cpp';s=p.read_text()
    a=s.index('template <bool UseMask>\nvoid AmrCoreLBM::CollideLayoutLevel')
    b=s.index('void AmrCoreLBM::Stream(',a)
    part=s[a:b]
    needle='    {\n        MFIter mfi(state_lev, collision_info);'
    replacement='''    if (ladder_option("LADDER_COLLISION_BATCH")) {
        // 仅实验单rank、OSI、256线程、无坐标缓存；数值公式与掩码集合沿用原路径。
        AMREX_ALWAYS_ASSERT(use_osi && n == nghost && send_subset == -1 &&
            ParallelDescriptor::NProcs() == 1 && !params_.ibm_enabled &&
            ladder_option("LADDER_THREADS",256) == 256 && !ladder_option("LADDER_COORD"));
        const auto& cache = boundary_work_cache.at(lev);
        const auto states = state_lev.arrays();
        const auto masks = has_fine_level ? covered_mask[lev].const_arrays() : MultiArray4<const int>{};
        const auto interfaces = has_fine_level ? interface_mask[lev].const_arrays() : MultiArray4<const int>{};
        const auto* shifts = osi_fab_phase_cache.at(lev).data(phase);
        amrex::ParallelFor(cache.collision_tags,
            [=] AMREX_GPU_DEVICE(int i,int j,int k,const BoundaryWorkTag& tag) {
                const int li=tag.local_index;
                if (has_fine_level && masks[li](i,j,k) != 0 && interfaces[li](i,j,k) == 0) { return; }
                collide_bgk_register_osi(i,j,k,states[li],tag.fab,shifts[tag.phase_index],omega_lev);
            });
        level_launch_cells = cache.collision_cells;
    } else {
        MFIter mfi(state_lev, collision_info);'''
    assert part.count(needle)==1
    part=part.replace(needle,replacement,1)
    p.write_text(s[:a]+part+s[b:])
    diff=''
    for name in ('AmrCoreLBM.H','AmrCoreLBM_amr.cpp','AmrCoreLBM_advance.cpp'):
        diff+=''.join(difflib.unified_diff((previous/'src'/name).read_text().splitlines(True),(BUILD/'src'/name).read_text().splitlines(True),fromfile='a/src/'+name,tofile='b/src/'+name))
    (EXP/'experiment.diff').write_text(diff)
    (EXP/'purpose.json').write_text(json.dumps(dict(purpose='碰撞批量另列：同节点插值批量对照与插值+碰撞批量实验',classification='temporary controlled benchmark',retention='保留源码、输入、程序、命令、日志、checkpoint、报告、图表与复现入口；构建对象可清理',source=str(previous/'src'),state='collision_tags/cells仅由当前布局和nghost派生；BuildBoundaryWorkCache构建，原cache.clear在布局重建/清层时失效；不保存数组地址或相位'),ensure_ascii=False,indent=2)+'\n')
    for p in (BUILD/'src').iterdir(): p.touch()
    print(EXP)
    raise SystemExit(0)
for directory in ('src', 'config'):
    shutil.copytree(CASE / directory, BUILD / directory)
shutil.copy2(CASE / 'GNUmakefile', BUILD / 'GNUmakefile')
shutil.copy2(CASE / 'config/inputs', EXP / 'inputs')
shutil.copytree(CASE / 'src', EXP / 'production_src')
subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=CASE, stdout=(EXP/'git_head.txt').open('w'), check=True)
subprocess.run(['git', 'diff', '--', '.'], cwd=CASE, stdout=(EXP/'worktree.diff').open('w'), check=True)
# 不变的 AMReX 对象复用；算例翻译单元全部更新时间并重编。
objects = CASE / 'tmp_build_dir/particles_float/o/3d.gnu.FLOAT.TPROF.MPI.CUDA.EXE'
shutil.copytree(objects, BUILD / objects.relative_to(CASE))
header = r'''#pragma once
#include <cstdlib>
#include <AMReX_TagParallelFor.H>
// 实验开关只读环境；正式源码和类状态不变。
inline int ladder_option(const char* name, int fallback=0) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}
// 复用相同缓存几何和数值函数，仅切换逐标签或批量发射。
template<class Tag, class F>
void ladder_for(const amrex::TagVector<Tag>& tags, bool batch, F f) {
    if (batch) { amrex::ParallelFor(tags, f); return; }
    const auto* host = reinterpret_cast<const Tag*>(tags.h_buffer);
    for (int t=0; t<tags.ntags; ++t) {
        const Tag tag = host[t];
        amrex::ParallelFor(tag.box(), [=] AMREX_GPU_DEVICE(int i,int j,int k) {
            f(i,j,k,tag);
        });
    }
}
'''
(BUILD/'src/LadderExperiment.H').write_text(header)
advance = BUILD/'src/AmrCoreLBM_advance.cpp'
s = advance.read_text()
s = '#include "LadderExperiment.H"\n' + s
s = s.replace('amrex::ParallelFor(\n            tags,', 'ladder_for(\n            tags, ladder_option("LADDER_BOUNDARY"),')
assert s.count('tags, ladder_option("LADDER_BOUNDARY")') == 2
# 同一掩码/数值函数，仅线程数与坐标缓存独立分派。
a = s.index('template <bool UseMask>\nvoid AmrCoreLBM::CollideLayoutLevel')
b = s.index('void AmrCoreLBM::Stream(', a)
part = s[a:b]
import re
pattern = r'amrex::ParallelFor<128>\((.*?)collide_bgk_register_impl<true, true>\((.*?)\n                        \}\);'
def collision(match):
    prefix, args = match.groups()
    branches=[]
    for index, (threads, cache) in enumerate(((128, True),(128,False),(256,False))):
        condition = ('ladder_option("LADDER_COORD")' if index == 0 else 'ladder_option("LADDER_THREADS",256)==128')
        begin = ('if' if index == 0 else 'else if') + f' ({condition}) {{' if index < 2 else 'else {'
        call = 'collide_bgk_register_impl<true, true>' if cache else 'collide_bgk_register_osi'
        branches.append(begin + f'\n                    amrex::ParallelFor<{threads}>(' + prefix + call + '(' + args + '\n                        });\n                    }')
    return '\n                    '.join(branches)
part, count = re.subn(pattern, collision, part, flags=re.S)
assert count == 2, count
s = s[:a]+part+s[b:]
advance.write_text(s)
amr = BUILD/'src/AmrCoreLBM_amr.cpp'
s = '#include "LadderExperiment.H"\n' + amr.read_text()
a = s.index('template <bool UseOsiAddress>\nvoid AmrCoreLBM::InterpolateDdfStageBatch')
b = s.index('void AmrCoreLBM::FillDdfGhostFromCoarse',a)
part=s[a:b].replace('amrex::ParallelFor(cache.scale_tags,','ladder_for(cache.scale_tags, ladder_option("LADDER_INTERP"),').replace('amrex::ParallelFor(cache.tags,','ladder_for(cache.tags, ladder_option("LADDER_INTERP"),')
assert part.count('ladder_for')==4
s=s[:a]+part+s[b:]
# 厚候选面保留当前完整物理面语义；只恢复薄面发射前的无效内层工作。
needle='                    physical_faces.push_back(face);'
replacement='''                    if (!ladder_option("LADDER_BOUNDARY")) {
                        if (side == domain.smallEnd(face_dir)) {
                            face.setBig(face_dir, std::min(boundary_source_box.bigEnd(face_dir), side+nghost));
                        } else {
                            face.setSmall(face_dir, std::max(boundary_source_box.smallEnd(face_dir), side-nghost));
                        }
                    }
'''+needle
assert s.count(needle)==1
s=s.replace(needle,replacement)
amr.write_text(s)
diff=''
for name in ('AmrCoreLBM_advance.cpp','AmrCoreLBM_amr.cpp'):
    diff+=''.join(difflib.unified_diff((CASE/'src'/name).read_text().splitlines(True),(BUILD/'src'/name).read_text().splitlines(True),fromfile='a/src/'+name,tofile='b/src/'+name))
(EXP/'experiment.diff').write_text(diff)
for p in (BUILD/'src').iterdir():
    p.touch()
(EXP/'purpose.json').write_text(json.dumps(dict(purpose='C128000 单GPU累加优化受控复测',classification='temporary performance experiment',retention='保留源码、输入、程序、命令、日志、验证、绘图材料；build对象可在完成后清理',baseline='current source with selected optimization switches disabled; numerical fixes retained',checkpoint=str(CASE/'runs/a5_C_t1000_gif/job624717/C/chk00128000')),ensure_ascii=False,indent=2)+'\n')
print(EXP)
