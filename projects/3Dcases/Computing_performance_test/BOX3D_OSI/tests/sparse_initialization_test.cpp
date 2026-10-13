#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_BoxList.H>
#include <AMReX_FillPatchUtil.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_PhysBCFunct.H>
#include <algorithm>
#include <cmath>
#include <sstream>
#include "Kernels.H"
#include "OsiCommunication.H"

using namespace amrex;

// 此 fixture 只替代驱动的状态容器；下方所有被测方法均从生产源码提取。
class AmrCoreLBM {
public:
    Vector<Geometry> geom;
    Vector<BCRec> bcs;
    Vector<Real> tau{0.73, 0.96};
    int max_level = 1;
    int interp_mode = 0;
    bool osi_mpi_device_direct = false;
    const Geometry& Geom(int lev) const { return geom.at(lev); }
    IntVect refRatio(int) const { return IntVect(2); }
    int nProper() const { return 1; }
    Interpolater* DdfInterpolater() const;
    void BuildInterpolationStage(int, const MultiFab&, const MultiFab&,
        const Vector<Box>&, const Vector<int>&, MultiFab&, Vector<unsigned char>&) const;
    void ApplyPhysicalBoundaryToInterpolationStage(int, MultiFab&, const Vector<unsigned char>&);
    void ScaleInterpolationStage(MultiFab&, Real);
    void InterpolateDdfStageToFine(MultiFab&, MultiFab&, const Vector<Box>&,
        const Vector<int>&, bool, std::uint64_t, bool, Real);
    void InitializeDdfFromCoarse(int, Real, const MultiFab&, bool, std::uint64_t, MultiFab&);
    void ParallelCopyOsi(int, const MultiFab&, std::uint64_t, MultiFab&, std::uint64_t);
};

#include "sparse_initialization_impl.inc"

// 原初始化路径：全父层求宏观量并缩放，随后交给 AMReX 独立插值。
void old_initialization(AmrCoreLBM& lbm, const MultiFab& source,
                        bool osi_macro, MultiFab& fine) {
    MultiFab coarse(source.boxArray(), source.DistributionMap(), Q, 2);
    MultiFab rho(source.boxArray(), source.DistributionMap(), 1, 0);
    MultiFab velocity(source.boxArray(), source.DistributionMap(), 3, 0);
    MultiFab::Copy(coarse, source, 0, 0, Q, 0);
    const Real scale = lbm.tau[1] / lbm.tau[0] / Real(2);
    for (MFIter mfi(coarse); mfi.isValid(); ++mfi) {
        const auto f = coarse.array(mfi);
        const auto r = rho.array(mfi);
        const auto u = velocity.array(mfi);
        const Box bx = mfi.validbox();
        // OSI 原路径用倒数乘法；canonical 原路径直接除法。
        ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            Real rt=0, ux=0, uy=0, uz=0;
            for (int q=0; q<Q; ++q) {
                const Real value=f(i,j,k,q);
                rt += value; ux += value*e[q][0];
                uy += value*e[q][1]; uz += value*e[q][2];
            }
            r(i,j,k)=rt;
            if (osi_macro) {
                const Real inv=Real(1)/rt;
                u(i,j,k,0)=ux*inv; u(i,j,k,1)=uy*inv; u(i,j,k,2)=uz*inv;
            } else {
                u(i,j,k,0)=ux/rt; u(i,j,k,1)=uy/rt; u(i,j,k,2)=uz/rt;
            }
        });
        ParallelFor(bx, Q, [=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            const Real eq=feqQian(r(i,j,k),{u(i,j,k,0),u(i,j,k,1),u(i,j,k,2)},q);
            f(i,j,k,q)=eq+(f(i,j,k,q)-eq)*scale;
        });
    }
    if (Gpu::inLaunchRegion()) {
        GpuBndryFuncFab<AmrCoreFill> bndry(AmrCoreFill{});
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cb(lbm.geom[0],lbm.bcs,bndry);
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fb(lbm.geom[1],lbm.bcs,bndry);
        InterpFromCoarseLevel(fine,0.,coarse,0,0,Q,lbm.geom[0],lbm.geom[1],
                             cb,0,fb,0,IntVect(2),lbm.DdfInterpolater(),lbm.bcs,0);
    } else {
        CpuBndryFuncFab bndry(nullptr);
        PhysBCFunct<CpuBndryFuncFab> cb(lbm.geom[0],lbm.bcs,bndry);
        PhysBCFunct<CpuBndryFuncFab> fb(lbm.geom[1],lbm.bcs,bndry);
        InterpFromCoarseLevel(fine,0.,coarse,0,0,Q,lbm.geom[0],lbm.geom[1],
                             cb,0,fb,0,IntVect(2),lbm.DdfInterpolater(),lbm.bcs,0);
    }
}

void run_case(int mode, int periodic_case, bool coarse_osi, std::uint64_t phase, bool fail_coverage = false) {
    const std::array<int,3> periodic = periodic_case == 0 ? std::array<int,3>{0,0,0}
        : periodic_case == 1 ? std::array<int,3>{1,1,1} : std::array<int,3>{1,0,1};
    AmrCoreLBM lbm;
    lbm.interp_mode=mode;
    const RealBox rb({0.,0.,0.},{1.,1.,1.});
    const Box domain(IntVect(0),IntVect(31));
    lbm.geom.emplace_back(domain,&rb,0,periodic.data());
    lbm.geom.emplace_back(amrex::refine(domain,2),&rb,0,periodic.data());
    lbm.bcs.resize(Q);
    for (auto& bc: lbm.bcs) {
        for (int d=0;d<3;++d) {
            const int type=periodic[d] ? BCType::int_dir : BCType::foextrap;
            bc.setLo(d,type); bc.setHi(d,type);
        }
    }
    BoxArray ba(fail_coverage ? Box(IntVect(0),IntVect(15)) : domain); ba.maxSize(12);
    Vector<int> owners(ba.size());
    for (int i=0;i<ba.size();++i) owners[i]=i%ParallelDescriptor::NProcs();
    DistributionMapping dm(owners);
    MultiFab canonical(ba,dm,Q,2), raw(ba,dm,Q,2);
    canonical.setVal(std::numeric_limits<Real>::quiet_NaN());
    raw.setVal(std::numeric_limits<Real>::quiet_NaN());
    for (MFIter mfi(canonical); mfi.isValid(); ++mfi) {
        const auto dst=canonical.array(mfi);
        const auto encoded=raw.array(mfi);
        const auto [fab,shift]=OSI::MakeOsiFabContext(raw[mfi].box(),phase);
        ParallelFor(mfi.validbox(),Q,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            // 正密度且空间非均匀，避免均匀初值掩盖错误模板或错误相位。
            const Real rho=Real(1)+Real(0.002)*(i+2*j+3*k);
            const Real ux=Real(0.02)*std::sin(Real(i)*Real(0.31));
            const Real uy=Real(0.015)*std::cos(Real(j)*Real(0.27));
            const Real uz=Real(0.01)*std::sin(Real(k)*Real(0.19));
            const Real value=feqQian(rho,{ux,uy,uz},q)+
                Real(1.e-5)*std::sin(Real(i+3*j+5*k+7*q));
            dst(i,j,k,q)=value;
            const auto address=OSI::osi_address({i,j,k},{e[q][0],e[q][1],e[q][2]},fab,shift);
            encoded(address.x,address.y,address.z,q)=value;
        });
    }
    // 包含两侧物理/周期边界和内部 patch；fine owner 与粗层交错。
    Vector<Box> fine_boxes{Box(IntVect(0),IntVect(7)),
        Box(IntVect(56),IntVect(63)),Box(IntVect(28),IntVect(35))};
    BoxArray fba(fine_boxes.data(),fine_boxes.size());
    Vector<int> fowners(fba.size());
    for (int i=0;i<fba.size();++i) fowners[i]=(i+1)%ParallelDescriptor::NProcs();
    DistributionMapping fdm(fowners);
    MultiFab reference(fba,fdm,Q,2), actual(fba,fdm,Q,2);
    reference.setVal(std::numeric_limits<Real>::quiet_NaN());
    actual.setVal(std::numeric_limits<Real>::quiet_NaN());
    if (fail_coverage) {
        lbm.InitializeDdfFromCoarse(1,0.,canonical,false,0,actual);
        Abort("missing coverage unexpectedly accepted");
    }
    old_initialization(lbm,canonical,coarse_osi,reference);
    // 被测入口接收驱动当前的全 foextrap 记录，入口应自行保留周期语义。
    const auto correct_bcs=lbm.bcs;
    for (auto& bc: lbm.bcs) {
        for (int d=0;d<3;++d) { bc.setLo(d,BCType::foextrap); bc.setHi(d,BCType::foextrap); }
    }
    lbm.InitializeDdfFromCoarse(1,0.,coarse_osi ? raw : canonical,coarse_osi,phase,actual);
    lbm.bcs=correct_bcs;
    AMREX_ALWAYS_ASSERT(!reference.contains_nan(0,Q,2));
    AMREX_ALWAYS_ASSERT(!actual.contains_nan(0,Q,2) && !actual.contains_inf(0,Q,2));
    MultiFab::Subtract(actual,reference,0,0,Q,2);
    const Real linf=actual.norm0(0,Q,IntVect(2));
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(linf < Real(2.e-14), "sparse initialization differs from old AMReX path");
    Vector<Box> work;
    Vector<int> indices;
    for (int i=0;i<fba.size();++i) {
        work.push_back(amrex::grow(fba[i],2)&lbm.Geom(1).growPeriodicDomain(2));
        indices.push_back(i);
    }
    MultiFab stage;
    Vector<unsigned char> physical;
    lbm.BuildInterpolationStage(1,canonical,reference,work,indices,stage,physical);
    AMREX_ALWAYS_ASSERT(stage.nGrow()==0 && stage.boxArray().numPts() < ba.numPts());
    Print() << "PASS sparse initialization mode=" << mode << " periodic=" << periodic_case
            << " osi=" << coarse_osi << " phase=" << phase << " linf=" << linf
            << " stage_cells=" << stage.boxArray().numPts() << " parent_cells=" << ba.numPts() << '\n';
}

int main(int argc,char** argv) {
    Initialize(argc,argv);
    {
        bool fail_coverage=false;
        ParmParse().query("coverage_fail",fail_coverage);
        if (fail_coverage) run_case(0,0,false,0,true);
        for (int mode=0;mode<3;++mode) {
            for (int periodic=0;periodic<3;++periodic) {
                run_case(mode,periodic,false,0);
                for (std::uint64_t phase: {0ULL,17ULL,41ULL}) run_case(mode,periodic,true,phase);
            }
        }
        Print() << "PASS sparse initialization matrix ranks=" << ParallelDescriptor::NProcs() << '\n';
    }
    Finalize();
}
