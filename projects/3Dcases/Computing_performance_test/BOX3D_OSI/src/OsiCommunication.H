#ifndef OSI_COMMUNICATION_H_
#define OSI_COMMUNICATION_H_

#include <AMReX_Array4.H>
#include <AMReX_Box.H>
#include <AMReX_GpuQualifiers.H>

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
