#!/usr/bin/env python3
"""本次阶段一诊断构建准备；仅在实验副本修改调度，不修改生产源码或历史冻结源。"""
from pathlib import Path
import difflib
import shutil

CASE = Path(__file__).resolve().parents[3]
BASE = CASE / 'runs/archive/history_20261101/a5_threshold_128000/frozen_20261029'
OUT = CASE / 'runs/temporary/C_AB_OSI_stage_review/phase1_build'
OUT.mkdir()
for name in ('src', 'config'):
    shutil.copytree(BASE / name, OUT / name)
for name in ('inputs', 'git_head.txt'):
    shutil.copy2(BASE / name, OUT / name)
shutil.copy2(CASE / 'GNUmakefile', OUT / 'GNUmakefile')

def replace_function(text, name, replacement):
    start = text.index('void ' + name + '(')
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[:start] + replacement + text[end:]

p = OUT / 'src/AmrCoreLBM_advance.cpp'
s = p.read_text()
start = s.index('    if (run_mode == RunMode::OsiLockstep) {', s.index('void AmrCoreLBM::AdvanceLevel('))
brace = s.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
s = s[:start] + '''    if (run_mode == RunMode::OsiLockstep) {
        // 阶段一只锁步推进，在完整粗步末按重构间隔检查 uncovered valid。
        // 不进行逐阶段范数、ghost/source 主机遍历或全 valid 重构比较。
        AdvanceOneLayout(lev, DdfLayout::Osi);
        AdvanceOneLayout(lev, DdfLayout::Canonical);
        SwapLevel(lev, nghost, DdfLayout::Osi);
        SwapLevel(lev, nghost, DdfLayout::Canonical);
        return;
    }''' + s[end:]
p.write_text(s)
p = OUT / 'src/OsiAbVerification.cpp'
s = p.read_text()
s = replace_function(s, 'OsiAbVerification::BeforeRegrid', '''void OsiAbVerification::BeforeRegrid(AmrCoreLBM& lid, int step) const {
    if (lid.osiReferenceEnabled() && step == lid.params().begin_step + 1) {
        lid.CheckOsiReferenceUncovered(step - 1, "InitialUncovered");
    }
}''')
for name, signature in (
    ('AfterAverageDown', 'AmrCoreLBM&, int'), ('AfterRepair', 'AmrCoreLBM&, int'),
    ('AfterRefineMesh', 'AmrCoreLBM&, int'), ('AfterFirstFillGhost', 'AmrCoreLBM&, int')):
    s = replace_function(s, 'OsiAbVerification::' + name,
                         'void OsiAbVerification::' + name + '(' + signature + ')' + (' const' if name != 'AfterFirstFillGhost' else '') + ' {}')
s = replace_function(s, 'OsiAbVerification::AfterAdvance', '''void OsiAbVerification::AfterAdvance(AmrCoreLBM& lid, int lev, int step) const {
    if (lev == 0 && lid.osiReferenceEnabled() && IsTargetStep(step)) {
        lid.CheckOsiReferenceUncovered(step, "StepEndUncovered");
    }
}''')
p.write_text(s)
p = OUT / 'src/AmrCoreLBM_diagnostics.cpp'
s = p.read_text()
s = replace_function(s, 'AmrCoreLBM::CheckOsiReferenceUncovered', '''void AmrCoreLBM::CheckOsiReferenceUncovered(int step, const char* stage) {
    if (!osiReferenceEnabled()) { return; }
    amrex::Print() << "osi_ab_uncovered_begin: step=" << step
                   << " stage=" << stage << " finest_level=" << finest_level << '\\n';
    for (int lev = 0; lev <= finest_level; ++lev) {
        const auto& reference = f_old.at(lev);
        // 锁步共享拓扑和mask；记录来源，不将共享误写成独立生成后验证一致。
        amrex::Print() << "PHASE1_MESH step=" << step << " lev=" << lev
                       << " phase=" << osi_phase.at(lev)
                       << " boxes=" << boxArray(lev).size()
                       << " valid_cells=" << boxArray(lev).numPts()
                       << " covered=" << covered_cell_counts.at(lev)
                       << " interface=" << interface_cell_counts.at(lev)
                       << " shared_topology=1 shared_masks=1" << '\\n';
        CompareOsiReferenceStage(lev, stage, reference, 0, false, true);
    }
}''')
s = s.replace('const bool comparison_failed =\n        !std::isfinite(linf) || linf > tolerance;',
'''const bool comparison_failed =
        difference.contains_nan(0, Q, 0) || difference.contains_inf(0, Q, 0) ||
        !std::isfinite(linf) || linf > tolerance;
    amrex::Print() << "PHASE1_VALUES step=" << probe_.coarse_advance_count
                   << " lev=" << lev << " phase=" << phase
                   << " nonfinite=" << (difference.contains_nan(0, Q, 0) || difference.contains_inf(0, Q, 0))
                   << " scope=uncovered_valid" << '\\n';''')
s = s.replace('output << "osi_ab_stage_failure: stage=" << stage',
              'output << "osi_ab_stage_failure: step=" << probe_.coarse_advance_count << " stage=" << stage')
p.write_text(s)
patch = ''
for filename in ('AmrCoreLBM_advance.cpp', 'OsiAbVerification.cpp', 'AmrCoreLBM_diagnostics.cpp'):
    patch += ''.join(difflib.unified_diff((BASE / 'src' / filename).read_text().splitlines(True),
                     (OUT / 'src' / filename).read_text().splitlines(True),
                     fromfile='frozen/src/' + filename, tofile='phase1/src/' + filename))
(OUT / 'phase1.patch').write_text(patch)
(OUT / 'source.diff').write_text((BASE / 'source.diff').read_text() + '\n' + patch)
print(OUT)
