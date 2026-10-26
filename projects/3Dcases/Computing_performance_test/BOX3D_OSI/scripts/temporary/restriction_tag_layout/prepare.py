# 本次标签存储对照实验的快照生成器；保留作复现证据，实验不再需要时可删除。
from pathlib import Path
import shutil, subprocess
case = Path(__file__).resolve().parents[3]
root = case / 'runs/temporary/restriction_tag_layout'
root.mkdir(parents=True, exist_ok=False)
(root/'purpose.md').write_text('临时 A/B 实验：完整 block 标签与区域/block 分表。保留源快照、补丁、输入、命令、编译/运行日志、数值比较和报告；中间对象可删除。基线包含用户当前未提交修改。\n')
(root/'worktree.diff').write_bytes(subprocess.check_output(['git','diff'], cwd=case))
(root/'git_head.txt').write_bytes(subprocess.check_output(['git','rev-parse','HEAD'], cwd=case))
for variant in ('baseline','split'):
    dst=root/variant
    dst.mkdir()
    for name in ('src','config'):
        shutil.copytree(case/name,dst/name)
    shutil.copy2(case/'GNUmakefile',dst/'GNUmakefile')
    # 独立复制已有对象以复用未变更的 AMReX 编译，避免共享可写 inode。
    shutil.copytree(case/'tmp_build_dir',dst/'tmp_build_dir')
    p=dst/'src/AmrCoreLBM_amr.cpp'
    s=p.read_text()
    s=s.replace('Vector<RestrictionBlockTag> tags;', 'const double tag_build_start = amrex::second();\n                Vector<RestrictionBlockTag> tags;')
    needle='Gpu::copy(Gpu::hostToDevice, tags.begin(), tags.end(), device_tags.begin());'
    instrument='''
                Gpu::streamSynchronize();
                amrex::AllPrint() << "TAG_LAYOUT lev=" << lev
                    << " regions=" << result.local_size() << " blocks=" << tags.size()
                    << " block_size=" << sizeof(RestrictionBlockTag)
                    << " bytes=" << (tags.size()*sizeof(RestrictionBlockTag)EXTRA_BYTES)
                    << " build_s=" << (amrex::second()-tag_build_start) << '\\n';'''
    s=s.replace(needle,needle+instrument.replace('EXTRA_BYTES', '' if variant=='baseline' else '+region_tags.size()*sizeof(RestrictionRegionTag)'))
    if variant=='split':
        s=s.replace('Vector<RestrictionBlockTag> tags;', 'Vector<RestrictionBlockTag> tags;\n                Vector<RestrictionRegionTag> region_tags;')
        s=s.replace('const Box region = mfi.validbox();\n                    for (amrex::Long parent', '''const Box region = mfi.validbox();
                    const int region_index = static_cast<int>(region_tags.size());
                    region_tags.push_back({region, fab, mfi.LocalIndex(), local_fine, slot});
                    for (amrex::Long parent''')
        s=s.replace('tags.push_back({region, fab, parent, mfi.LocalIndex(), local_fine, slot});','tags.push_back({parent, region_index});')
        s=s.replace('auto& device_tags = average_down_cache.at(lev).block_tags;', '''auto& device_regions = average_down_cache.at(lev).region_tags;
                device_regions.resize(region_tags.size());
                Gpu::copy(Gpu::hostToDevice, region_tags.begin(), region_tags.end(), device_regions.begin());
                auto& device_tags = average_down_cache.at(lev).block_tags;''')
    p.write_text(s)
    if variant=='split':
        p=dst/'src/AmrCoreLBM.H'
        s=p.read_text().replace('struct RestrictionBlockTag {','struct RestrictionRegionTag {',1).replace('    amrex::Long first_parent;\n','',1)
        pos=s.index('// 按 staging Box')
        s=s[:pos]+'''// 每个 block 只记录区域索引与起始父单元；区域属性独立存储。
struct RestrictionBlockTag {
    amrex::Long first_parent;
    int region_index;
};
'''+s[pos:]
        s=s.replace('amrex::Gpu::DeviceVector<RestrictionBlockTag> block_tags;','amrex::Gpu::DeviceVector<RestrictionBlockTag> block_tags;\n    amrex::Gpu::DeviceVector<RestrictionRegionTag> region_tags;')
        s=s.replace('        block_tags.clear();','        block_tags.clear();\n        region_tags.clear();')
        p.write_text(s)
        p=dst/'src/AmrCoreLBM_osi.cpp'
        s=p.read_text().replace('const auto* tags = work.data();','const auto* tags = work.data();\n        const auto* regions = average_down_cache.at(coarse_lev).region_tags.data();',1)
        s=s.replace('const auto tag = tags[blockIdx.x];\n                const Long parent = tag.first_parent', 'const auto block = tags[blockIdx.x];\n                const auto tag = regions[block.region_index];\n                const Long parent = block.first_parent',1)
        p.write_text(s)
print(root)
