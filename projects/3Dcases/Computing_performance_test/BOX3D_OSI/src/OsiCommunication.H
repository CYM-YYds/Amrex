#ifndef OSI_COMMUNICATION_H_
#define OSI_COMMUNICATION_H_

#include <AMReX_Array4.H>
#include <AMReX_Box.H>
#include <AMReX_GpuQualifiers.H>
#include <AMReX_GpuContainers.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_TagParallelFor.H>
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

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return dst_box;
    }
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

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
};

// 插值阶段沿用同一套 pack 描述；保留别名便于区分调用语义。
using RawPackTag = MpiPackTag;

// 跨 rank 通信的 GPU 融合解包描述。
struct MpiUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
};

// 平均下传专用的 canonical 源打包描述。源数据来自 restriction 结果，
// 因此不经过 OSI 地址解码。
struct CanonicalPackTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Box region;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
};

// 平均下传专用的 raw 目标解包描述。收到 canonical 值后按目标 phase 写入 raw。
struct CanonicalRawUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
};

// 插值阶段从跨 rank 缓冲区解包到 canonical coarse stencil 的描述。
struct CanonicalUnpackTag {
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    std::size_t offset = 0;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
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

    void resize(const MpiPlan& plan, bool device_direct) {
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

struct CopyTiming {
    double* pack = nullptr;
    double* wait = nullptr;
    double* unpack = nullptr;
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
std::size_t buffer_cell_index(const amrex::Box& region,
                              int i, int j, int k) noexcept {
    const auto lo = region.smallEnd();
    return static_cast<std::size_t>(
        ((k - lo[2]) * region.length(1) + (j - lo[1])) *
            region.length(0) + (i - lo[0]));
}

// 按 peer 提交非阻塞接收，保持与本地 GPU 操作的重叠。
template <typename MessageVector, typename BufferPtr>
void post_receives(MessageVector& receives,
                   const amrex::Vector<std::size_t>& recv_counts,
                   const amrex::Vector<std::size_t>& recv_offsets,
                   BufferPtr recv_buffer, int nprocs, int mpi_tag) {
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
                BufferPtr send_buffer, int nprocs, int mpi_tag) {
    for (int peer = 0; peer < nprocs; ++peer) {
        if (send_counts[peer] != 0) {
            sends.push_back(amrex::ParallelDescriptor::Asend(
                send_buffer + send_offsets[peer], send_counts[peer], peer,
                mpi_tag));
        }
    }
}

template <typename MessageVector>
void wait_messages(MessageVector& messages) {
    for (auto& message : messages) {
        message.wait();
    }
}

// 统一执行 OSI CPC 的 rank-local 部分。调用方只负责提供一个设备可调用的
// value_copy，决定 logical cell 如何映射到源和目标 raw 地址。
template <typename Tag, typename CopyValue>
void parallel_copy_local(const amrex::TagVector<Tag>& tags, int ncomp,
                         CopyValue copy_value) {
    amrex::ParallelFor(
        tags, ncomp,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int n,
                             const Tag& tag) noexcept {
            copy_value(i, j, k, n, tag);
        });
}

// 统一执行 OSI CPC 的 MPI 部分。pack_value/unpack_value 将 logical cell
// 映射到各自的 raw 地址；这里集中处理聚合 buffer、host staging 和 MPI
// 请求生命周期，避免各个 AMR 阶段重复实现相同的传输骨架。
template <typename PackTag, typename UnpackTag, typename PackValue,
          typename UnpackValue>
void parallel_copy_mpi(const amrex::TagVector<PackTag>& pack_tags,
                       const amrex::TagVector<UnpackTag>& unpack_tags,
                       const MpiPlan& plan, bool device_direct, int ncomp,
                       int nprocs, PackValue pack_value,
                       UnpackValue unpack_value,
                       CopyTiming timing = {}) {
    MpiBuffers buffers;
    buffers.resize(plan, device_direct);
    auto& send_device = buffers.send_device;
    auto& recv_device = buffers.recv_device;
    auto& send_host = buffers.send_host;
    auto& recv_host = buffers.recv_host;

    const double pack_start = amrex::second();
    amrex::Real* send_buffer = send_device.data();
    amrex::ParallelFor(
        pack_tags, ncomp,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int n,
                             const PackTag& tag) noexcept {
            pack_value(i, j, k, n, tag, send_buffer);
        });
    amrex::Gpu::streamSynchronize();
    if (!device_direct && plan.send_total != 0) {
        amrex::Gpu::dtoh_memcpy(send_host.data(), send_device.data(),
                                plan.send_total * sizeof(amrex::Real));
    }
    if (timing.pack != nullptr) {
        *timing.pack += amrex::second() - pack_start;
    }

    const double wait_start = amrex::second();
    const int mpi_tag = amrex::ParallelDescriptor::SeqNum();
    amrex::Vector<amrex::ParallelDescriptor::Message> receives;
    amrex::Vector<amrex::ParallelDescriptor::Message> sends;
    amrex::Real* recv_buffer = device_direct
                                   ? recv_device.data()
                                   : recv_host.data();
    post_receives(receives, plan.recv_counts, plan.recv_offsets, recv_buffer,
                  nprocs, mpi_tag);
    const amrex::Real* mpi_send_buffer = device_direct
                                             ? send_device.data()
                                             : send_host.data();
    post_sends(sends, plan.send_counts, plan.send_offsets, mpi_send_buffer,
               nprocs, mpi_tag);
    wait_messages(receives);
    wait_messages(sends);
    if (timing.wait != nullptr) {
        amrex::Gpu::streamSynchronize();
        *timing.wait += amrex::second() - wait_start;
    }

    const double unpack_start = amrex::second();
    if (!device_direct && plan.recv_total != 0) {
        amrex::Gpu::htod_memcpy(recv_device.data(), recv_host.data(),
                                plan.recv_total * sizeof(amrex::Real));
    }
    const amrex::Real* receive_buffer = recv_device.data();
    amrex::ParallelFor(
        unpack_tags, ncomp,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int n,
                             const UnpackTag& tag) noexcept {
            unpack_value(i, j, k, n, tag, receive_buffer);
        });
    amrex::Gpu::streamSynchronize();
    if (timing.unpack != nullptr) {
        *timing.unpack += amrex::second() - unpack_start;
    }
}


template <typename SendTagMap, typename RecvTagMap>
MpiPlan make_mpi_plan(const SendTagMap& send_tags,
                      const RecvTagMap& recv_tags,
                      int nprocs, int ncomp) {
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
    amrex::IntVect
    source_cell(LocalCopyTag const& tag, int i, int j, int k) noexcept {
    return tag.src_box.smallEnd() + amrex::IntVect(
                                        AMREX_D_DECL(i - tag.dst_box.smallEnd(0),
                                                     j - tag.dst_box.smallEnd(1),
                                                     k - tag.dst_box.smallEnd(2)));
}

} // namespace OSI

#endif
