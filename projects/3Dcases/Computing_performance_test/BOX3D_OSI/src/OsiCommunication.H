#ifndef OSI_COMMUNICATION_H_
#define OSI_COMMUNICATION_H_

#include <AMReX_Array4.H>
#include <AMReX_Box.H>
#include <AMReX_GpuQualifiers.H>

#include <cstddef>

#include "OsiIndex.H"

namespace OSI {

// 同一 rank 内直接进行 OSI raw-to-raw 拷贝的通信描述。
struct LocalCopyTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Array4<amrex::Real> dst;
    amrex::Box src_box;
    amrex::Box dst_box;
    FabGeometry src_fab;
    FabGeometry dst_fab;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return dst_box; }
};

// 跨 MPI rank 的 logical 通信配对，供后续 pack/unpack 复用。
struct RemoteCopyTag {
    int src_rank = -1;
    int dst_rank = -1;
    int src_index = -1;
    int dst_index = -1;
    amrex::Box src_box;
    amrex::Box dst_box;
    amrex::IntVect periodic_shift{0};
};

// 跨 rank 通信的 GPU 融合打包描述；offset 指向聚合缓冲中的首元素。
struct MpiPackTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};

// 跨 rank 通信的 GPU 融合解包描述。
struct MpiUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};

// 平均下传专用的 canonical 源打包描述。源数据来自 restriction 结果，
// 因此不经过 OSI 地址解码。
struct CanonicalPackTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Box region;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};

// 平均下传专用的 raw 目标解包描述。收到 canonical 值后按目标 phase 写入 raw。
struct CanonicalRawUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::IntVect source_cell(LocalCopyTag const& tag, int i, int j, int k) noexcept
{
    return tag.src_box.smallEnd() + amrex::IntVect(
        AMREX_D_DECL(i - tag.dst_box.smallEnd(0),
                     j - tag.dst_box.smallEnd(1),
                     k - tag.dst_box.smallEnd(2)));
}

} // namespace OSI

#endif
