#ifndef OSI_COMMUNICATION_H_
#define OSI_COMMUNICATION_H_

#include <AMReX_Array4.H>
#include <AMReX_Box.H>
#include <AMReX_GpuQualifiers.H>
#include <AMReX_GpuContainers.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_MultiFab.H>
#include <AMReX_MFIter.H>
#include <AMReX_TagParallelFor.H>
#include <AMReX_Vector.H>

#include <cstddef>
#include <utility>
#include <type_traits>

#include "OsiIndex.H"

namespace OSI {

// 每层 OSI 状态的本地 Fab 相位表：全局盒编号只用于建立紧凑索引，
// GPU 数据量随本地 Fab 数增长。相位唯一来自 osi_phase，表不独立推进时间。
// 初始化/布局重建时 define；Stream 提交新相位时 update；释放状态前 clear。
// 更新和读取必须在同一 GPU stream 上有序执行，禁止跨 stream 并发改写。
struct FabPhaseCache {
    amrex::Vector<int> slots;
    amrex::Gpu::DeviceVector<FabGeometry> geometry;
    amrex::Gpu::DeviceVector<PhaseShift3> shifts;
    amrex::BoxArray layout;
    amrex::DistributionMapping mapping;
    amrex::IntVect ng;
    std::uint64_t prepared_phase = 0;
    bool ready = false;

    void clear() {
        amrex::Gpu::streamSynchronize();
        ready = false;
        slots.clear();
        geometry.clear();
        shifts.clear();
        layout = amrex::BoxArray{};
        mapping = amrex::DistributionMapping{};
    }

    void define(const amrex::MultiFab& state, std::uint64_t phase) {
        clear();
        layout = state.boxArray();
        mapping = state.DistributionMap();
        ng = state.nGrowVect();
        slots.assign(layout.size(), -1);
        amrex::Vector<FabGeometry> local;
        for (amrex::MFIter mfi(state, false); mfi.isValid(); ++mfi) {
            slots[mfi.index()] = static_cast<int>(local.size());
            local.push_back(MakeOsiFabContext(
                amrex::grow(mfi.validbox(), ng), 0).fab);
        }
        geometry.resize(local.size());
        shifts.resize(local.size());
        amrex::Gpu::copy(amrex::Gpu::hostToDevice,
                         local.begin(), local.end(), geometry.begin());
        ready = true;
        update(phase);
    }

    void update(std::uint64_t phase) {
        AMREX_ALWAYS_ASSERT(ready);
        const auto* fabs = geometry.data();
        auto* values = shifts.data();
        // 每个本地 Fab 一次取模；同一个 Fab 的所有 tag 和 Q 分量共用结果。
        amrex::ParallelFor(static_cast<int>(shifts.size()),
            [=] AMREX_GPU_DEVICE(int index) noexcept {
                values[index] = osi_phase_shift(phase, fabs[index]);
            });
        prepared_phase = phase;
    }

    [[nodiscard]] int slot(int global_index) const {
        AMREX_ALWAYS_ASSERT(ready && global_index >= 0 &&
                            global_index < static_cast<int>(slots.size()));
        AMREX_ALWAYS_ASSERT(slots[global_index] >= 0);
        return slots[global_index];
    }

    [[nodiscard]] bool matches(const amrex::MultiFab& state) const {
        return ready && layout == state.boxArray() &&
               mapping == state.DistributionMap() && ng == state.nGrowVect();
    }

    [[nodiscard]] const PhaseShift3* data(std::uint64_t phase) const {
        AMREX_ALWAYS_ASSERT(ready && prepared_phase == phase);
        return shifts.data();
    }
};

// 同一 rank 内的复制描述；持久任务使用相位表索引，临时迁移只使用 Fab 几何。
struct LocalCopyTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Array4<amrex::Real> dst;
    amrex::Box src_box;
    amrex::Box dst_box;
    FabGeometry src_fab;
    FabGeometry dst_fab;
    // 指向所属层的本地 Fab 偏移表；临时 raw 迁移保留 -1 并自行计算相位。
    int src_phase_index = -1;
    int dst_phase_index = -1;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return dst_box;
    }
};

// 跨 rank 通信的 GPU 融合打包描述；offset 指向聚合缓冲中的首元素。
struct MpiPackTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Box region;
    FabGeometry fab;
    std::size_t offset = 0;
    int phase_index = -1;

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
    int phase_index = -1;

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
    int phase_index = -1;

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

// 同层 FillBoundary 的按层缓存；引用旧 MultiFab 的 tag 必须先于状态释放失效。
struct FillBoundaryCache {
    amrex::TagVector<LocalCopyTag> local_tags;
    amrex::TagVector<MpiPackTag> pack_tags;
    amrex::TagVector<MpiUnpackTag> unpack_tags;
    MpiPlan plan;
    MpiBuffers buffers;
    // 发送源分类由 FB 计划生成，与布局缓存共同重建和失效。
    amrex::Vector<int> send_fabs;
    bool overlap_collision = false;

    void clear() {
        send_fabs.clear();
        overlap_collision = false;
        local_tags.undefine();
        pack_tags.undefine();
        unpack_tags.undefine();
        plan = MpiPlan{};
        buffers = MpiBuffers{};
    }
};

struct CopyTiming {
    double* pack = nullptr;
    double* wait = nullptr;
    double* unpack = nullptr;
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    std::size_t
    buffer_cell_index(const amrex::Box& region,
                      int i, int j, int k) noexcept {
    const auto lo = region.smallEnd();
    return static_cast<std::size_t>(
        ((k - AMREX_D_PICK(0, 0, lo[2])) * region.length(1) + (j - lo[1])) *
            region.length(0) +
        (i - lo[0]));
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
template <typename CopyValue>
void parallel_copy_local(const amrex::TagVector<LocalCopyTag>& tags, int ncomp,
                         CopyValue copy_value) {
    amrex::ParallelFor(
        tags, ncomp,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int n,
                             const LocalCopyTag& tag) noexcept {
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
                       CopyTiming timing = {}, MpiBuffers* cached_buffers = nullptr) {
    MpiBuffers temporary_buffers;
    if (cached_buffers == nullptr) {
        temporary_buffers.resize(plan, device_direct);
    }
    // 推进缓存复用既有缓冲；重网格中的临时复制仍可使用调用内缓冲。
    MpiBuffers& buffers = cached_buffers ? *cached_buffers : temporary_buffers;
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

// 同层 FillBoundary 与粗细层 CPC 共用 peer 顺序、offset 及最终 GPU tag 构建。
template <typename PackTag, typename UnpackTag, typename SendTagMap,
          typename RecvTagMap, typename MakePack, typename MakeUnpack>
void define_mpi_tags(const SendTagMap& send_tags,
                     const RecvTagMap& recv_tags, const MpiPlan& plan,
                     int ncomp, amrex::TagVector<PackTag>& pack_tags,
                     amrex::TagVector<UnpackTag>& unpack_tags,
                     MakePack make_pack, MakeUnpack make_unpack) {
    auto send_cursor = plan.send_offsets;
    auto recv_cursor = plan.recv_offsets;
    amrex::Vector<PackTag> pack;
    amrex::Vector<UnpackTag> unpack;
    for (const auto& [peer, tags] : send_tags) {
        for (const auto& tag : tags) {
            pack.push_back(make_pack(tag, send_cursor[peer]));
            send_cursor[peer] += static_cast<std::size_t>(tag.sbox.numPts()) * ncomp;
        }
    }
    for (const auto& [peer, tags] : recv_tags) {
        for (const auto& tag : tags) {
            unpack.push_back(make_unpack(tag, recv_cursor[peer]));
            recv_cursor[peer] += static_cast<std::size_t>(tag.dbox.numPts()) * ncomp;
        }
    }
    for (int peer = 0; peer < amrex::ParallelDescriptor::NProcs(); ++peer) {
        AMREX_ALWAYS_ASSERT(send_cursor[peer] ==
                            plan.send_offsets[peer] + plan.send_counts[peer]);
        AMREX_ALWAYS_ASSERT(recv_cursor[peer] ==
                            plan.recv_offsets[peer] + plan.recv_counts[peer]);
    }
    pack_tags.define(pack);
    unpack_tags.define(unpack);
}

// 插值与平均共用的持久 CPC 派生任务。SourceRaw=true 表示 raw 到 canonical，
// false 表示 canonical 到 raw；几何、数据地址及相位表索引随布局缓存，
// 执行时读取共享偏移表，不为每条 tag 单独保存或更新相位。
template <bool SourceRaw>
struct CpcCache {
    using PackTag = std::conditional_t<SourceRaw, RawPackTag, CanonicalPackTag>;
    using UnpackTag = std::conditional_t<SourceRaw, CanonicalUnpackTag,
                                         CanonicalRawUnpackTag>;
    amrex::TagVector<LocalCopyTag> local_tags;
    amrex::TagVector<PackTag> pack_tags;
    amrex::TagVector<UnpackTag> unpack_tags;
    MpiPlan plan;
    MpiBuffers buffers;
    bool ready = false;

    // 必须先使含 Array4 的任务失效，再释放或替换源、目标 MultiFab。
    void clear() {
        local_tags.undefine();
        pack_tags.undefine();
        unpack_tags.undefine();
        plan = MpiPlan{};
        buffers = MpiBuffers{};
        ready = false;
    }

    void define(const amrex::MultiFab& source, amrex::MultiFab& destination,
                const amrex::Periodicity& periodicity, int ncomp,
                bool device_direct, const FabPhaseCache& phases) {
        clear();
        AMREX_ALWAYS_ASSERT(phases.matches(SourceRaw ? source : destination));
        const auto& cpc = destination.getCPC(
            amrex::IntVect(0), source, amrex::IntVect(0), periodicity);
        AMREX_ALWAYS_ASSERT(cpc.m_LocTags && cpc.m_SndTags && cpc.m_RcvTags);
        const auto geometry = [](const amrex::MultiFab& state, int index) {
            return MakeOsiFabContext(
                amrex::grow(state.boxArray()[index], state.nGrowVect()), 0).fab;
        };
        amrex::Vector<LocalCopyTag> local;
        local.reserve(cpc.m_LocTags->size());
        for (const auto& tag : *cpc.m_LocTags) {
            local.push_back({source.const_array(tag.srcIndex),
                             destination.array(tag.dstIndex), tag.sbox, tag.dbox,
                             geometry(source, tag.srcIndex),
                             geometry(destination, tag.dstIndex),
                             SourceRaw ? phases.slot(tag.srcIndex) : -1,
                             SourceRaw ? -1 : phases.slot(tag.dstIndex)});
        }
        local_tags.define(local);
        plan = make_mpi_plan(*cpc.m_SndTags, *cpc.m_RcvTags,
                             amrex::ParallelDescriptor::NProcs(), ncomp);
        define_mpi_tags(
            *cpc.m_SndTags, *cpc.m_RcvTags, plan, ncomp,
            pack_tags, unpack_tags,
            [&](const auto& tag, std::size_t offset) -> PackTag {
                if constexpr (SourceRaw) {
                    return {source.const_array(tag.srcIndex), tag.sbox,
                            geometry(source, tag.srcIndex), offset,
                            phases.slot(tag.srcIndex)};
                } else {
                    return {source.const_array(tag.srcIndex), tag.sbox, offset};
                }
            },
            [&](const auto& tag, std::size_t offset) -> UnpackTag {
                if constexpr (SourceRaw) {
                    return {destination.array(tag.dstIndex), tag.dbox, offset};
                } else {
                    return {destination.array(tag.dstIndex), tag.dbox,
                            geometry(destination, tag.dstIndex), offset,
                            phases.slot(tag.dstIndex)};
                }
            });
        buffers.resize(plan, device_direct);
        ready = true;
    }
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    amrex::IntVect
    source_cell(LocalCopyTag const& tag, int i, int j, int k) noexcept {
    return tag.src_box.smallEnd() + amrex::IntVect(
                                        AMREX_D_DECL(i - tag.dst_box.smallEnd(0),
                                                     j - tag.dst_box.smallEnd(1),
                                                     k - tag.dst_box.smallEnd(2)));
} // 源坐标 = 目标坐标 + (src_box.smallEnd() - dst_box.smallEnd());
  // 非周期情况下括号里面的数值为0,  周期情况下为周期偏移量
} // namespace OSI

#endif
