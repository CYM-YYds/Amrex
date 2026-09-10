#include "../src/OsiIndex.H"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using box3d_osi::Coord3;
using box3d_osi::FabGeometry;
using box3d_osi::osi_cell_coordinates;
using box3d_osi::osi_phase_shift;

constexpr std::array<Coord3, 27> d3q27_velocities{{
    {0, 0, 0},   {0, 1, 0},   {0, -1, 0},  {-1, 0, 0},
    {1, 0, 0},   {0, 0, 1},   {0, 0, -1},  {-1, 1, 0},
    {1, 1, 0},   {-1, -1, 0}, {1, -1, 0},  {0, 1, 1},
    {0, -1, 1},  {-1, 0, 1},  {1, 0, 1},   {0, 1, -1},
    {0, -1, -1}, {-1, 0, -1}, {1, 0, -1},  {1, 1, 1},
    {-1, 1, 1},  {1, -1, 1},  {-1, -1, 1}, {1, 1, -1},
    {-1, 1, -1}, {1, -1, -1}, {-1, -1, -1}}};

constexpr FabGeometry fab{{11, -7, 23}, {5, 6, 7}};

constexpr Coord3 osi_address(Coord3 logical, Coord3 velocity,
                             std::uint64_t phase,
                             FabGeometry geometry) noexcept {
    return box3d_osi::osi_address(
        logical, velocity, geometry,
        box3d_osi::osi_phase_shift(phase, geometry));
}

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "OSI index test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

int linear_offset(Coord3 raw) {
    const int x = raw.x - fab.lo.x;
    const int y = raw.y - fab.lo.y;
    const int z = raw.z - fab.lo.z;
    return (z * fab.length.y + y) * fab.length.x + x;
}

void test_phase_zero_and_stationary_direction() {
    constexpr std::array<std::uint64_t, 5> phases{
        0, 1, 17, 1234567890123456789ULL,
        std::numeric_limits<std::uint64_t>::max()};

    for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
        for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
            for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                const Coord3 logical{i, j, k};
                if (!(osi_address(logical, d3q27_velocities[1], 0, fab) ==
                      logical)) {
                    fail("phase zero is not the identity");
                }
                for (const auto phase : phases) {
                    if (!(osi_address(logical, d3q27_velocities[0], phase,
                                      fab) == logical)) {
                        fail("stationary direction moved");
                    }
                }
            }
        }
    }
}

void test_velocity_signs() {
    constexpr FabGeometry line{{11, 0, 0}, {5, 1, 1}};
    if (osi_address({13, 0, 0}, {1, 0, 0}, 1, line).x != 12 ||
        osi_address({13, 0, 0}, {-1, 0, 0}, 1, line).x != 14 ||
        osi_address({11, 0, 0}, {1, 0, 0}, 1, line).x != 15 ||
        osi_address({15, 0, 0}, {-1, 0, 0}, 1, line).x != 11) {
        fail("velocity sign or wrap direction");
    }
}

void test_each_mapping_is_a_permutation() {
    constexpr std::array<std::uint64_t, 7> phases{
        0, 1, 2, 7, 211, 1234567890123456789ULL,
        std::numeric_limits<std::uint64_t>::max()};
    const int point_count = fab.length.x * fab.length.y * fab.length.z;

    for (const auto velocity : d3q27_velocities) {
        for (const auto phase : phases) {
            std::vector<bool> visited(static_cast<std::size_t>(point_count),
                                      false);
            for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
                for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
                    for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                        const Coord3 raw =
                            osi_address({i, j, k}, velocity, phase, fab);
                        const int offset = linear_offset(raw);
                        if (offset < 0 || offset >= point_count ||
                            visited[static_cast<std::size_t>(offset)]) {
                            fail("mapping is not a permutation");
                        }
                        visited[static_cast<std::size_t>(offset)] = true;
                    }
                }
            }
        }
    }
}

void test_streaming_identity() {
    constexpr std::array<std::uint64_t, 5> phases{
        0, 1, 8, 211, 1234567890123456789ULL};

    for (const auto velocity : d3q27_velocities) {
        for (const auto phase : phases) {
            for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
                for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
                    for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                        const Coord3 logical{i, j, k};
                        const Coord3 upstream{i - velocity.x,
                                              j - velocity.y,
                                              k - velocity.z};
                        if (!(osi_address(logical, velocity, phase + 1, fab) ==
                              osi_address(upstream, velocity, phase, fab))) {
                            fail("A_q(x,p+1) != A_q(x-e_q,p)");
                        }
                    }
                }
            }
        }
    }
}

void test_phase_period() {
    // lcm(5,6,7) = 210, so every D3Q27 mapping must repeat after 210.
    constexpr std::uint64_t period = 210;
    constexpr std::uint64_t phase = 1234567890123456789ULL;
    for (const auto velocity : d3q27_velocities) {
        for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
            for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
                for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                    const Coord3 logical{i, j, k};
                    if (!(osi_address(logical, velocity, phase, fab) ==
                          osi_address(logical, velocity, phase + period, fab))) {
                        fail("phase period");
                    }
                }
            }
        }
    }
}

void test_cell_coordinate_cache() {
    constexpr std::array<std::uint64_t, 7> phases{
        0, 1, 2, 7, 211, 1234567890123456789ULL,
        std::numeric_limits<std::uint64_t>::max()};

    for (const auto phase : phases) {
        const auto shift = osi_phase_shift(phase, fab);
        for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
            for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
                for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                    const Coord3 logical{i, j, k};
                    const auto coordinates =
                        osi_cell_coordinates(logical, fab, shift);
                    for (const auto velocity : d3q27_velocities) {
                        const auto direct = box3d_osi::osi_address(
                            logical, velocity, fab, shift);
                        const auto cached = box3d_osi::osi_address(
                            coordinates, velocity);
                        if (!(cached == direct)) {
                            fail("cell coordinate cache differs from direct address");
                        }
                    }
                }
            }
        }
    }
}

static_assert(osi_address({13, 0, 0}, {1, 0, 0}, 1,
                          {{11, 0, 0}, {5, 1, 1}}).x == 12);

} // namespace

int main() {
    test_phase_zero_and_stationary_direction();
    test_velocity_signs();
    test_each_mapping_is_a_permutation();
    test_streaming_identity();
    test_phase_period();
    test_cell_coordinate_cache();
    std::cout << "OSI index tests passed\n";
    return EXIT_SUCCESS;
}
