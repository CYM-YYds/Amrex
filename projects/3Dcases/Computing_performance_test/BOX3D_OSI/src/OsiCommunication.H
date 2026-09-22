#ifndef OSI_COMMUNICATION_H_
#define OSI_COMMUNICATION_H_

#include <AMReX_Array4.H>
#include <AMReX_Box.H>
#include <AMReX_GpuQualifiers.H>
#include <AMReX_GpuContainers.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_Vector.H>

#include <cstddef>
#include <utility>

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

// 插值阶段沿用同一套 pack 描述；保留别名便于区分调用语义。
using RawPackTag = MpiPackTag;

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

// 插值阶段从跨 rank 缓冲区解包到 canonical coarse stencil 的描述。
struct CanonicalUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};

// 一次 CPC 交换的聚合缓冲布局。插值和平均只需分别构造自己的
// pack/unpack tag，peer 计数与 offset 统一由这里生成。
struct MpiPlan {
    amrex::Vector<std::size_t> send_counts;
    amrex::Vector<std::size_t> recv_counts;
    amrex::Vector<std::size_t> send_offsets;
    amrex::Vector<std::size_t> recv_offsets;
    std::size_t send_total = 0;
    std::size_t recv_total = 0;
};

// CPC 交换使用的 GPU 聚合缓冲和可选 pinned host staging 缓冲。
struct MpiBuffers {
    amrex::Gpu::DeviceVector<amrex::Real> send_device;
    amrex::Gpu::DeviceVector<amrex::Real> recv_device;
    amrex::Gpu::PinnedVector<amrex::Real> send_host;
    amrex::Gpu::PinnedVector<amrex::Real> recv_host;

    void resize(const MpiPlan& plan, bool device_direct)
    {
        send_device.resize(plan.send_total);
        recv_device.resize(plan.recv_total);
        if (device_direct) {
            send_host.clear();
            recv_host.clear();
        } else {
            send_host.resize(plan.send_total);
            recv_host.resize(plan.recv_total);
        }
    }
};

// 按 peer 提交非阻塞接收，保持与本地 GPU 操作的重叠。
template <typename MessageVector, typename BufferPtr>
void post_receives(MessageVector& receives,
                   const amrex::Vector<std::size_t>& recv_counts,
                   const amrex::Vector<std::size_t>& recv_offsets,
                   BufferPtr recv_buffer, int nprocs, int mpi_tag)
{
    for (int peer = 0; peer < nprocs; ++peer) {
        if (recv_counts[peer] != 0) {
            receives.push_back(amrex::ParallelDescriptor::Arecv(
                recv_buffer + recv_offsets[peer], recv_counts[peer], peer,
                mpi_tag));
        }
    }
}

template <typename MessageVector, typename BufferPtr>
void post_sends(MessageVector& sends,
                const amrex::Vector<std::size_t>& send_counts,
                const amrex::Vector<std::size_t>& send_offsets,
                BufferPtr send_buffer, int nprocs, int mpi_tag)
{
    for (int peer = 0; peer < nprocs; ++peer) {
        if (send_counts[peer] != 0) {
            sends.push_back(amrex::ParallelDescriptor::Asend(
                send_buffer + send_offsets[peer], send_counts[peer], peer,
                mpi_tag));
        }
    }
}

template <typename MessageVector>
void wait_messages(MessageVector& messages)
{
    for (auto& message : messages) {
        message.wait();
    }
}

template <typename SendTagMap, typename RecvTagMap>
MpiPlan make_mpi_plan(const SendTagMap& send_tags,
                      const RecvTagMap& recv_tags,
                      int nprocs, int ncomp)
{
    MpiPlan plan;
    plan.send_counts.assign(nprocs, 0);
    plan.recv_counts.assign(nprocs, 0);
    plan.send_offsets.assign(nprocs, 0);
    plan.recv_offsets.assign(nprocs, 0);

    for (const auto& [peer, tags] : send_tags) {
        for (const auto& tag : tags) {
            plan.send_counts[peer] +=
                static_cast<std::size_t>(tag.sbox.numPts()) * ncomp;
        }
    }
    for (const auto& [peer, tags] : recv_tags) {
        for (const auto& tag : tags) {
            plan.recv_counts[peer] +=
                static_cast<std::size_t>(tag.dbox.numPts()) * ncomp;
        }
    }
    for (int peer = 0; peer < nprocs; ++peer) {
        plan.send_offsets[peer] = plan.send_total;
        plan.recv_offsets[peer] = plan.recv_total;
        plan.send_total += plan.send_counts[peer];
        plan.recv_total += plan.recv_counts[peer];
    }
    return plan;
}

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
