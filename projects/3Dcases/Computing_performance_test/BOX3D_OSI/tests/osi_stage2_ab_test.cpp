#include "../src/OsiIndex.H"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using box3d_osi::Coord3;
using box3d_osi::FabGeometry;
using box3d_osi::osi_address;
using box3d_osi::positive_mod;

constexpr int q_count = 27;
constexpr int nghost = 2;
constexpr Coord3 valid_lo{11, -7, 23};
constexpr Coord3 valid_len{4, 3, 5};
constexpr FabGeometry fab{{valid_lo.x - nghost, valid_lo.y - nghost,
                           valid_lo.z - nghost},
                          {valid_len.x + 2 * nghost,
                           valid_len.y + 2 * nghost,
                           valid_len.z + 2 * nghost}};

constexpr std::array<Coord3, q_count> velocities{{
    {0, 0, 0},   {0, 1, 0},   {0, -1, 0},  {-1, 0, 0},
    {1, 0, 0},   {0, 0, 1},   {0, 0, -1},  {-1, 1, 0},
    {1, 1, 0},   {-1, -1, 0}, {1, -1, 0},  {0, 1, 1},
    {0, -1, 1},  {-1, 0, 1},  {1, 0, 1},   {0, 1, -1},
    {0, -1, -1}, {-1, 0, -1}, {1, 0, -1},  {1, 1, 1},
    {-1, 1, 1},  {1, -1, 1},  {-1, -1, 1}, {1, 1, -1},
    {-1, 1, -1}, {1, -1, -1}, {-1, -1, -1}}};

constexpr std::array<double, q_count> weights{{
    8.0 / 27.0,
    2.0 / 27.0, 2.0 / 27.0, 2.0 / 27.0, 2.0 / 27.0,
    2.0 / 27.0, 2.0 / 27.0,
    1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0,
    1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0,
    1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0, 1.0 / 54.0,
    1.0 / 216.0, 1.0 / 216.0, 1.0 / 216.0, 1.0 / 216.0,
    1.0 / 216.0, 1.0 / 216.0, 1.0 / 216.0, 1.0 / 216.0}};

[[noreturn]] void fail(const char* message, int step,
                       double error = 0.0) {
    std::cerr << "OSI stage-2 A/B test failed at step " << step << ": "
              << message << ", error=" << error << '\n';
    std::exit(EXIT_FAILURE);
}

std::size_t index(Coord3 raw, int q) {
    const int x = raw.x - fab.lo.x;
    const int y = raw.y - fab.lo.y;
    const int z = raw.z - fab.lo.z;
    return static_cast<std::size_t>(
        (((z * fab.length.y + y) * fab.length.x + x) * q_count) + q);
}

bool is_valid(Coord3 point) {
    return point.x >= valid_lo.x && point.x < valid_lo.x + valid_len.x &&
           point.y >= valid_lo.y && point.y < valid_lo.y + valid_len.y &&
           point.z >= valid_lo.z && point.z < valid_lo.z + valid_len.z;
}

Coord3 periodic_source(Coord3 point) {
    return {
        valid_lo.x + positive_mod(point.x - valid_lo.x, valid_len.x),
        valid_lo.y + positive_mod(point.y - valid_lo.y, valid_len.y),
        valid_lo.z + positive_mod(point.z - valid_lo.z, valid_len.z)};
}

void collide(std::array<double, q_count>& fq, double omega) {
    double rho = 0.0;
    double ux = 0.0;
    double uy = 0.0;
    double uz = 0.0;
    for (int q = 0; q < q_count; ++q) {
        rho += fq[q];
        ux += fq[q] * velocities[q].x;
        uy += fq[q] * velocities[q].y;
        uz += fq[q] * velocities[q].z;
    }
    ux /= rho;
    uy /= rho;
    uz /= rho;
    const double velocity_sq = ux * ux + uy * uy + uz * uz;
    const double common = 1.0 - 1.5 * velocity_sq;
    for (int q = 0; q < q_count; ++q) {
        const double eu = velocities[q].x * ux + velocities[q].y * uy +
                          velocities[q].z * uz;
        const double feq = weights[q] * rho *
                           (common + 3.0 * eu + 4.5 * eu * eu);
        fq[q] = (1.0 - omega) * fq[q] + omega * feq;
    }
}

void fill_periodic_canonical(std::vector<double>& state) {
    for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
        for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
            for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                const Coord3 target{i, j, k};
                if (is_valid(target)) {
                    continue;
                }
                const Coord3 source = periodic_source(target);
                for (int q = 0; q < q_count; ++q) {
                    state[index(target, q)] = state[index(source, q)];
                }
            }
        }
    }
}

void fill_periodic_osi(std::vector<double>& state, std::uint64_t phase) {
    for (int k = fab.lo.z; k < fab.lo.z + fab.length.z; ++k) {
        for (int j = fab.lo.y; j < fab.lo.y + fab.length.y; ++j) {
            for (int i = fab.lo.x; i < fab.lo.x + fab.length.x; ++i) {
                const Coord3 target_logical{i, j, k};
                if (is_valid(target_logical)) {
                    continue;
                }
                const Coord3 source_logical = periodic_source(target_logical);
                for (int q = 0; q < q_count; ++q) {
                    const Coord3 source = osi_address(
                        source_logical, velocities[q], phase, fab);
                    const Coord3 target = osi_address(
                        target_logical, velocities[q], phase, fab);
                    state[index(target, q)] = state[index(source, q)];
                }
            }
        }
    }
}

void collide_ab(std::vector<double>& state, double omega) {
    for (int k = valid_lo.z; k < valid_lo.z + valid_len.z; ++k) {
        for (int j = valid_lo.y; j < valid_lo.y + valid_len.y; ++j) {
            for (int i = valid_lo.x; i < valid_lo.x + valid_len.x; ++i) {
                std::array<double, q_count> fq{};
                for (int q = 0; q < q_count; ++q) {
                    fq[q] = state[index({i, j, k}, q)];
                }
                collide(fq, omega);
                for (int q = 0; q < q_count; ++q) {
                    state[index({i, j, k}, q)] = fq[q];
                }
            }
        }
    }
}

void collide_osi(std::vector<double>& state, std::uint64_t phase,
                 double omega) {
    for (int k = valid_lo.z; k < valid_lo.z + valid_len.z; ++k) {
        for (int j = valid_lo.y; j < valid_lo.y + valid_len.y; ++j) {
            for (int i = valid_lo.x; i < valid_lo.x + valid_len.x; ++i) {
                std::array<double, q_count> fq{};
                for (int q = 0; q < q_count; ++q) {
                    const Coord3 raw = osi_address(
                        {i, j, k}, velocities[q], phase, fab);
                    fq[q] = state[index(raw, q)];
                }
                collide(fq, omega);
                for (int q = 0; q < q_count; ++q) {
                    const Coord3 raw = osi_address(
                        {i, j, k}, velocities[q], phase, fab);
                    state[index(raw, q)] = fq[q];
                }
            }
        }
    }
}

void stream_ab(const std::vector<double>& old_state,
               std::vector<double>& new_state) {
    for (int k = valid_lo.z; k < valid_lo.z + valid_len.z; ++k) {
        for (int j = valid_lo.y; j < valid_lo.y + valid_len.y; ++j) {
            for (int i = valid_lo.x; i < valid_lo.x + valid_len.x; ++i) {
                for (int q = 0; q < q_count; ++q) {
                    const Coord3 upstream{i - velocities[q].x,
                                          j - velocities[q].y,
                                          k - velocities[q].z};
                    new_state[index({i, j, k}, q)] =
                        old_state[index(upstream, q)];
                }
            }
        }
    }
}

double compare_valid(const std::vector<double>& ab,
                     const std::vector<double>& osi,
                     std::uint64_t phase) {
    double max_error = 0.0;
    for (int k = valid_lo.z; k < valid_lo.z + valid_len.z; ++k) {
        for (int j = valid_lo.y; j < valid_lo.y + valid_len.y; ++j) {
            for (int i = valid_lo.x; i < valid_lo.x + valid_len.x; ++i) {
                for (int q = 0; q < q_count; ++q) {
                    const Coord3 raw = osi_address(
                        {i, j, k}, velocities[q], phase, fab);
                    max_error = std::max(
                        max_error,
                        std::abs(ab[index({i, j, k}, q)] -
                                 osi[index(raw, q)]));
                }
            }
        }
    }
    return max_error;
}

} // namespace

int main() {
    const std::size_t value_count = static_cast<std::size_t>(
        fab.length.x * fab.length.y * fab.length.z * q_count);
    std::vector<double> ab(value_count, 0.0);
    std::vector<double> ab_next(value_count, 0.0);

    for (int k = valid_lo.z; k < valid_lo.z + valid_len.z; ++k) {
        for (int j = valid_lo.y; j < valid_lo.y + valid_len.y; ++j) {
            for (int i = valid_lo.x; i < valid_lo.x + valid_len.x; ++i) {
                const double rho = 1.0 + 1.0e-3 * (i + 2 * j - k);
                const double ux = 1.0e-2 * (j - valid_lo.y + 1);
                const double uy = -7.0e-3 * (k - valid_lo.z + 1);
                const double uz = 5.0e-3 * (i - valid_lo.x + 1);
                const double u2 = ux * ux + uy * uy + uz * uz;
                for (int q = 0; q < q_count; ++q) {
                    const double eu = velocities[q].x * ux +
                                      velocities[q].y * uy +
                                      velocities[q].z * uz;
                    ab[index({i, j, k}, q)] = weights[q] * rho *
                        (1.0 + 3.0 * eu + 4.5 * eu * eu - 1.5 * u2);
                }
            }
        }
    }
    fill_periodic_canonical(ab);
    std::vector<double> osi = ab;

    constexpr double omega = 1.0 / 0.61;
    constexpr int step_count = 37;
    std::uint64_t phase = 0;
    for (int step = 1; step <= step_count; ++step) {
        collide_ab(ab, omega);
        fill_periodic_canonical(ab);
        stream_ab(ab, ab_next);
        ab.swap(ab_next);

        collide_osi(osi, phase, omega);
        fill_periodic_osi(osi, phase);
        ++phase;

        const double max_error = compare_valid(ab, osi, phase);
        if (!std::isfinite(max_error) ||
            max_error > 64.0 * std::numeric_limits<double>::epsilon()) {
            fail("valid DDF mismatch", step, max_error);
        }
    }

    std::cout << "OSI stage-2 A/B test passed for " << step_count
              << " periodic steps\n";
    return EXIT_SUCCESS;
}
