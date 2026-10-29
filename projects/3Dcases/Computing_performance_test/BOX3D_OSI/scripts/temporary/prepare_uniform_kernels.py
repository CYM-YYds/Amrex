#!/usr/bin/env python3
"""本次单层同步/无mask实验的临时源码生成器；随实验结果保留供复现。"""
from pathlib import Path
import difflib
import shutil

case = Path(__file__).resolve().parents[2]
root = case / 'runs/temporary/uniform_kernels/round1'
root.mkdir(parents=True, exist_ok=False)
for variant in ('baseline', 'sync', 'nomask'):
    dest = root / variant
    dest.mkdir()
    for name in ('src', 'config'):
        shutil.copytree(case / name, dest / name, copy_function=shutil.copy2)

for variant in ('sync', 'nomask'):
    dest = root / variant
    p = dest / 'src/AmrCoreLBM_advance.cpp'
    text = p.read_text()
    old = 'void AmrCoreLBM::AdvanceLevel(int lev) {\n'
    assert text.count(old) == 1
    text = text.replace(old, old + '    // 实验程序只验证固定单层，禁止将候选误用于 AMR。\n'
                        '    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(max_level == 0, "Uniform-only experiment");\n')
    if variant == 'nomask':
        old = '''                    if (has_fine_level && covered(i, j, k) != 0 &&
                        interface(i, j, k) == 0) {
                        return;
                    }
'''
        assert text.count(old) == 3
        text = text.replace(old, '')
    else:
        # 保留 MFIter 的多stream等待和MPI内部依赖；阶段计时不再隐式同步。
        for name in ('comm', 'stream', 'swap'):
            old = f'ScopedPerfTimer timer(perf_stats.{name});'
            assert old in text
            text = text.replace(old, f'ScopedPerfTimer timer(perf_stats.{name}, false);')
        text = text.replace('ScopedPerfTimer comm_timer(perf_stats.comm);',
                            'ScopedPerfTimer comm_timer(perf_stats.comm, false);')
        old = 'void AmrCoreLBM::Boundary(int lev, DdfLayout layout) {\n    ScopedPerfTimer timer(perf_stats.boundary);'
        assert old in text
        text = text.replace(old, old.replace('perf_stats.boundary);', 'perf_stats.boundary, false);'))
        old = '''    // 延后子集留在 stream 0，由通信后的同 stream 消费和阶段同步保证完成。
    if (send_subset != 0) {
        amrex::Gpu::streamSynchronize();
    }
'''
        assert text.count(old) == 1
        text = text.replace(old, '    // 非延后子集已由 MFIter 析构等待其全部stream；延后子集仍由通信消费。\n')
        old = '    Stream(lev, nghost, layout);\n    Boundary(lev, layout);\n}'
        assert text.count(old) == 1
        text = text.replace(old, '    Stream(lev, nghost, layout);\n    Boundary(lev, layout);\n'
                            '    // 每步末尾等待边界，保持外层端到端计时及下步数据依赖。\n'
                            '    amrex::Gpu::streamSynchronize();\n}')
        osi = dest / 'src/AmrCoreLBM_osi.cpp'
        src = osi.read_text()
        old = '''    if (ParallelDescriptor::NProcs() == 1) {
        launch_local_copy();
        amrex::Gpu::streamSynchronize();
        return;
    }'''
        assert src.count(old) == 1
        src = src.replace(old, '''    if (ParallelDescriptor::NProcs() == 1) {
        // 本地复制、phase更新及边界使用同一stream，统一在推进末尾等待。
        launch_local_copy();
        return;
    }''')
        osi.write_text(src)
    p.write_text(text)
    patch = []
    for name in ('AmrCoreLBM_advance.cpp', 'AmrCoreLBM_osi.cpp'):
        a = (root / 'baseline/src' / name).read_text().splitlines(keepends=True)
        b = (dest / 'src' / name).read_text().splitlines(keepends=True)
        patch.extend(difflib.unified_diff(a, b, fromfile=f'a/src/{name}', tofile=f'b/src/{name}'))
    (root / f'{variant}.patch').write_text(''.join(patch))

build = root / 'build'
build.mkdir()
(build / 'scripts').mkdir()
shutil.copy2(case / 'GNUmakefile', build / 'GNUmakefile')
shutil.copy2(case / 'scripts/compile.sh', build / 'scripts/compile.sh')
shutil.copytree(case / 'tmp_build_dir', build / 'tmp_build_dir', copy_function=shutil.copy2)
(root / 'README.md').write_text('''# 同步与无mask独立实验

临时短测。baseline/sync/nomask 保存独立源码和配置；两个候选分别改变同步或碰撞mask，禁止AMR。
保留源码、patch、程序、输入、构建命令/日志、哈希、运行命令、日志、检查点和分析报告。
build/tmp_build_dir 是本次复制的可删除编译缓存，验证完成后清理，其他复现材料保留。
生产源码不因实验改写。sync 的 comm/stream/boundary/swap 为未测量，不能解释为零成本。
''')
print(root)
