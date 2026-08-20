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

// Raw coordinates accepted by an AMReX Array4.  They are deliberately kept
// independent of IntVect so the address mathematics can be tested without an
// AMReX runtime or a GPU.
struct Coord3 {
    int x;
    int y;
    int z;
};

// The ring is local to one FArrayBox.  lo and length must describe the same
// box, normally the full fabbox (valid region plus allocated ghost cells).
struct FabGeometry {
    Coord3 lo;
    Coord3 length;
};

BOX3D_OSI_GPU_HOST_DEVICE
constexpr int positive_mod(std::int64_t value, int length) noexcept {
    // Precondition: length > 0.  Every non-empty AMReX Fab satisfies it.
    const std::int64_t remainder = value % static_cast<std::int64_t>(length);
    return static_cast<int>(remainder < 0 ? remainder + length : remainder);
}

// Map one logical coordinate to its raw coordinate in the current OSI phase.
// Reducing phase before multiplication keeps the expression safe even when a
// level has advanced for much longer than the local Fab length.
BOX3D_OSI_GPU_HOST_DEVICE
constexpr int osi_coord(int logical, int velocity, std::uint64_t phase,
                        int lo, int length) noexcept {
    const auto phase_mod = static_cast<std::int64_t>(
        phase % static_cast<std::uint64_t>(length));
    const auto logical_offset = static_cast<std::int64_t>(logical) - lo;
    return lo + positive_mod(logical_offset - velocity * phase_mod, length);
}

// A_q(x,p) for one Fab.  velocity is e_q; q itself is intentionally absent so
// this helper is independent of a particular lattice ordering.
BOX3D_OSI_GPU_HOST_DEVICE
constexpr Coord3 osi_address(Coord3 logical, Coord3 velocity,
                             std::uint64_t phase,
                             FabGeometry fab) noexcept {
    return {
        osi_coord(logical.x, velocity.x, phase, fab.lo.x, fab.length.x),
        osi_coord(logical.y, velocity.y, phase, fab.lo.y, fab.length.y),
        osi_coord(logical.z, velocity.z, phase, fab.lo.z, fab.length.z)};
}

BOX3D_OSI_GPU_HOST_DEVICE
constexpr bool operator==(Coord3 lhs, Coord3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace box3d_osi

#undef BOX3D_OSI_GPU_HOST_DEVICE

#endif
