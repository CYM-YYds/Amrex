#include "InterpolationCoverage.H"

#include <AMReX.H>
#include <AMReX_Print.H>

#include <array>
#include <string>

using namespace amrex;
using namespace Box3dDetail;

namespace {
Box cube(int lo, int hi) {
    return Box(IntVect(lo), IntVect(hi));
}

Geometry geometry(const std::array<int, AMREX_SPACEDIM>& periodic) {
    return Geometry(cube(0, 15),
                    RealBox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}),
                    CoordSys::cartesian, periodic);
}

void expect_coverage(const char* label, const Box& stencil,
                     const BoxArray& valid, const Geometry& geom,
                     bool covered) {
    const auto missing = MissingInterpolationSources(stencil, valid, geom);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(missing.isEmpty() == covered, label);
    Print() << "PASS " << label << '\n';
}
}

int main(int argc, char* argv[]) {
    const bool expect_abort =
        argc > 1 && std::string(argv[1]) == "coverage_fail=1";
    amrex::Initialize(argc, argv);
    {
        const auto physical = geometry({0, 0, 0});
        const auto periodic = geometry({1, 1, 1});
        const auto mixed = geometry({1, 0, 0});
        const BoxArray whole(physical.Domain());
        const BoxArray interior(cube(4, 11));
        expect_coverage("interior covered", cube(5, 10), interior,
                        physical, true);
        expect_coverage("internal gap rejected", cube(3, 12), interior,
                        physical, false);
        expect_coverage("physical faces edges corners allowed",
                        cube(-1, 16), whole, physical, true);
        expect_coverage("periodic faces edges corners allowed",
                        cube(-1, 16), whole, periodic, true);
        expect_coverage("mixed boundary allowed", cube(-1, 16), whole,
                        mixed, true);

        // 多个 Fab 的 valid 并集可以共同提供模板，不要求单个 Fab 包含模板。
        const Vector<Box> pieces{
            Box(IntVect(0), IntVect(7, 15, 15)),
            Box(IntVect(8, 0, 0), IntVect(15))};
        const BoxArray split(pieces.data(), static_cast<int>(pieces.size()));
        expect_coverage("multi Fab union allowed", cube(-1, 16), split,
                        physical, true);

        // 周期缝可以从另一端的 valid 取值，但周期域内真正的空洞仍须报错。
        const BoxArray high_x(Box(IntVect(14, 0, 0), IntVect(15)));
        const Box across_x(IntVect(-2, 4, 4), IntVect(-1, 11, 11));
        expect_coverage("periodic image allowed", across_x, high_x,
                        mixed, true);
        expect_coverage("periodic interior gap rejected", cube(3, 12),
                        interior, periodic, false);
        expect_coverage("mixed internal gap rejected", cube(3, 12),
                        interior, mixed, false);

        // 墙外位置依赖钳制后的墙内来源，不能无条件忽略物理域外模板。
        const Box exterior(IntVect(-2, 4, 4), IntVect(-1, 11, 11));
        expect_coverage("physical extension source allowed", exterior,
                        whole, physical, true);
        expect_coverage("physical extension source gap rejected", exterior,
                        interior, physical, false);

        // 缺口位置和大小应精确落在父层 valid 外，不能把整块模板当作缺口。
        const Box one_gap(IntVect(3, 4, 4), IntVect(4, 11, 11));
        const auto missing =
            MissingInterpolationSources(one_gap, interior, physical);
        Long missing_cells = 0;
        for (const auto& box : missing) {
            AMREX_ALWAYS_ASSERT(box.smallEnd(0) == 3 && box.bigEnd(0) == 3);
            missing_cells += box.numPts();
        }
        AMREX_ALWAYS_ASSERT(missing_cells == 64);
        Print() << "PASS exact missing cells=64\n";

        RequireInterpolationCoverage(cube(-1, 16), split, physical,
                                     2, 7, cube(0, 31), 0, IntVect(2), 1);
        Print() << "PASS valid coverage guard returns\n";
        if (expect_abort) {
            RequireInterpolationCoverage(cube(3, 12), interior, physical,
                                         2, 7, cube(6, 25), 0, IntVect(2), 0);
            amrex::Abort("missing coverage guard unexpectedly returned");
        }
    }
    amrex::Finalize();
}
