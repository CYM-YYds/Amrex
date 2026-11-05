"""AI 临时实验生成器；保留以复现优化累加图，任务结束且无复现依赖时可删除。"""
from pathlib import Path
import difflib
import json
import shutil
import subprocess

CASE = Path(__file__).resolve().parents[3]
EXP = CASE / 'runs/temporary/optimization_ladder/round1'
BUILD = EXP / 'build'
EXP.mkdir(parents=True, exist_ok=False)
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
