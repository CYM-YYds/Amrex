#ifndef BOX3D_OSI_INDEX_H_
#define BOX3D_OSI_INDEX_H_

#include <cstdint>

#if defined(__has_include)
#if __has_include(<AMReX_GpuQualifiers.H>)
#include <AMReX_GpuQualifiers.H>
#define BOX3D_OSI_GPU_HOST_DEVICE AMREX_GPU_HOST_DEVICE
#else
#define BOX3D_OSI_GPU_HOST_DEVICE
#endif
#else
#define BOX3D_OSI_GPU_HOST_DEVICE
#endif

namespace box3d_osi {

// AMReX Array4 可接受的原始坐标。
// 这些坐标被刻意保持独立于 IntVect，以便可以在不依赖 AMReX 运行时或 GPU 的情况下测试地址计算。
struct Coord3 {
    int x;
    int y;
    int z;
};

// 环状索引作用域由调用方显式给出。生产 grown-Fab 路径传入完整 fabbox，
// 使 valid 与 ghost 构成同一个有限时间保护环；地址层本身不推断 valid/ghost 语义。
struct FabGeometry {
    Coord3 lo;
    Coord3 length;
};

// 同一 level、同一 Fab 的 phase 模值对所有格点和离散速度都相同。
// 碰撞热路径在 host 启动 kernel 前只计算一次，避免每个 DDF 读写重复执行整数取模。
struct PhaseShift3 {
    int x;
    int y;
    int z;
};

BOX3D_OSI_GPU_HOST_DEVICE
constexpr PhaseShift3 osi_phase_shift(std::uint64_t phase,
                                      FabGeometry fab) noexcept {
    return {
        static_cast<int>(phase % static_cast<std::uint64_t>(fab.length.x)),
        static_cast<int>(phase % static_cast<std::uint64_t>(fab.length.y)),
        static_cast<int>(phase % static_cast<std::uint64_t>(fab.length.z))};
}

BOX3D_OSI_GPU_HOST_DEVICE
constexpr int osi_coord_precomputed(int logical, int velocity, int lo,
                                    int length, int phase_shift) noexcept {
    int shifted = logical - lo - velocity * phase_shift;
    // logical 属于本 Fab 环，velocity 仅为 -1/0/1，因此最多跨越一次环首或环尾。
    if (shifted < 0) {
        shifted += length;
    } else if (shifted >= length) {
        shifted -= length;
    }
    return lo + shifted;
}

BOX3D_OSI_GPU_HOST_DEVICE
constexpr Coord3 osi_address(Coord3 logical, Coord3 velocity,
                             FabGeometry fab, PhaseShift3 shift) noexcept {
    return {
        osi_coord_precomputed(logical.x, velocity.x, fab.lo.x, fab.length.x,
                              shift.x),
        osi_coord_precomputed(logical.y, velocity.y, fab.lo.y, fab.length.y,
                              shift.y),
        osi_coord_precomputed(logical.z, velocity.z, fab.lo.z, fab.length.z,
                              shift.z)};
}

BOX3D_OSI_GPU_HOST_DEVICE
constexpr bool operator==(Coord3 lhs, Coord3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace box3d_osi

#undef BOX3D_OSI_GPU_HOST_DEVICE

#endif
