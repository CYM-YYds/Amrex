#pragma once
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
