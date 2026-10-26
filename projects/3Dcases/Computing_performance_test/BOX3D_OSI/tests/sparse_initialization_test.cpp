#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_BoxList.H>
#include <AMReX_FillPatchUtil.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_TagParallelFor.H>
#include <algorithm>
#include <cmath>
#include <sstream>
#include "Kernels.H"
#include "OsiCommunication.H"

using namespace amrex;
namespace Box3dDetail {
#include "sparse_initialization_types.inc"
}
using namespace Box3dDetail;

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
        const Vector<InterpolationPatchTag>&, MultiFab&) const;
    void ApplyPhysicalBoundaryToInterpolationStage(int, MultiFab&);
    void BuildInterpolationWork(MultiFab&, const MultiFab&,
        const Vector<InterpolationPatchTag>&, InterpolationBatchCache&,
        const OSI::FabPhaseCache* = nullptr) const;
    template <bool UseOsiAddress>
    void InterpolateDdfStageBatch(MultiFab&, MultiFab&, const InterpolationBatchCache&,
        const OSI::PhaseShift3*, bool, Real);
    void InitializeDdfFromCoarse(int, Real, const MultiFab&, bool, std::uint64_t, MultiFab&,
        const Vector<Box>& = {}, const Vector<int>& = {});
    void ParallelCopyOsi(int, const MultiFab&, std::uint64_t, MultiFab&, std::uint64_t);
};

#include "sparse_initialization_impl.inc"

// 原初始化路径：全父层求宏观量并缩放，随后交给 AMReX 独立插值。
void old_initialization(AmrCoreLBM& lbm, const MultiFab& source,
                        bool osi_macro, MultiFab& fine, bool custom_reference=false) {
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
    if (custom_reference && lbm.interp_mode != 0) {
        // 独立逐点 oracle：在完整父层缩放后收集模板，按标量系数求值，
        // 不调用生产 children kernel 或生产插值调度函数。
        BoxArray patch_ba=amrex::coarsen(fine.boxArray(),2);
        patch_ba.grow(2);
        MultiFab patch(patch_ba,fine.DistributionMap(),Q,0);
        patch.ParallelCopy(coarse,0,0,Q,IntVect(0),IntVect(0),lbm.geom[0].periodicity());
        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> bndry(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cb(lbm.geom[0],lbm.bcs,bndry);
            cb(patch,0,Q,IntVect(0),0.,0);
        } else {
            CpuBndryFuncFab bndry(nullptr);
            PhysBCFunct<CpuBndryFuncFab> cb(lbm.geom[0],lbm.bcs,bndry);
            cb(patch,0,Q,IntVect(0),0.,0);
        }
        const int mode=lbm.interp_mode;
        const Box domain=lbm.geom[1].growPeriodicDomain(fine.nGrowVect());
        for (MFIter mfi(fine);mfi.isValid();++mfi) {
            const auto samples=patch.const_array(mfi);
            const auto f=fine.array(mfi);
            const Box work=amrex::grow(mfi.validbox(),fine.nGrowVect())&domain;
            ParallelFor(work,Q,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
                const int x=amrex::coarsen(i,2), y=amrex::coarsen(j,2), z=amrex::coarsen(k,2);
                const Real ox=(i-2*x)==0 ? Real(-0.25) : Real(0.25);
                const Real oy=(j-2*y)==0 ? Real(-0.25) : Real(0.25);
                const Real oz=(k-2*z)==0 ? Real(-0.25) : Real(0.25);
                const Real center=samples(x,y,z,q);
                Real value;
                if (mode==1) {
                    const Real lo[3]={center-samples(x-1,y,z,q),center-samples(x,y-1,z,q),center-samples(x,y,z-1,q)};
                    const Real hi[3]={samples(x+1,y,z,q)-center,samples(x,y+1,z,q)-center,samples(x,y,z+1,q)-center};
                    const Real offsets[3]={ox,oy,oz};
                    value=center;
                    for(int d=0;d<3;++d) {
                        Real slope=0;
                        if(lo[d]>0 && hi[d]>0) slope=amrex::min(lo[d],hi[d]);
                        if(lo[d]<0 && hi[d]<0) slope=amrex::max(lo[d],hi[d]);
                        value+=offsets[d]*slope;
                    }
                } else {
                    // 二阶多项式的直接 stencil 系数形式，独立于生产斜率展开。
                    value=center*(1-ox*ox-oy*oy-oz*oz);
                    value+=samples(x+1,y,z,q)*(ox+ox*ox)/2+samples(x-1,y,z,q)*(-ox+ox*ox)/2;
                    value+=samples(x,y+1,z,q)*(oy+oy*oy)/2+samples(x,y-1,z,q)*(-oy+oy*oy)/2;
                    value+=samples(x,y,z+1,q)*(oz+oz*oz)/2+samples(x,y,z-1,q)*(-oz+oz*oz)/2;
                    for(int a=-1;a<=1;a+=2) for(int b=-1;b<=1;b+=2) {
                        value+=Real(a*b)/4*(ox*oy*samples(x+a,y+b,z,q)
                            +ox*oz*samples(x+a,y,z+b,q)+oy*oz*samples(x,y+a,z+b,q));
                    }
                }
                f(i,j,k,q)=value;
            });
        }
        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> bndry(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fb(lbm.geom[1],lbm.bcs,bndry);
            fb(fine,0,Q,fine.nGrowVect(),0.,0);
        } else {
            CpuBndryFuncFab bndry(nullptr);
            PhysBCFunct<CpuBndryFuncFab> fb(lbm.geom[1],lbm.bcs,bndry);
            fb(fine,0,Q,fine.nGrowVect(),0.,0);
        }
        return;
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
    actual.setVal(-777);
    if (fail_coverage) {
        lbm.InitializeDdfFromCoarse(1,0.,canonical,false,0,actual);
        Abort("missing coverage unexpectedly accepted");
    }
    old_initialization(lbm,canonical,coarse_osi,reference,true);
    MultiFab amrex_reference(fba,fdm,Q,2);
    old_initialization(lbm,canonical,coarse_osi,amrex_reference);
    lbm.InitializeDdfFromCoarse(1,0.,coarse_osi ? raw : canonical,coarse_osi,phase,actual);
    AMREX_ALWAYS_ASSERT(!reference.contains_nan(0,Q,0));
    AMREX_ALWAYS_ASSERT(!actual.contains_nan(0,Q,0) && !actual.contains_inf(0,Q,0));
    // 所有非 valid 单元（物理、周期和内部 ghost）必须保持输入 sentinel。
    MultiFab ghost_error(fba,fdm,Q,2);
    for (MFIter mfi(actual);mfi.isValid();++mfi) {
        const auto values=actual.const_array(mfi);
        const auto error=ghost_error.array(mfi);
        const Box valid=mfi.validbox();
        ParallelFor(actual[mfi].box(),Q,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            error(i,j,k,q)=!valid.contains(i,j,k) && values(i,j,k,q)!=Real(-777) ? Real(1) : Real(0);
        });
    }
    const Real ghost_linf=ghost_error.norm0(0,Q,IntVect(2));
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(ghost_linf==0,"initialization overwrote fine ghost");
    // 不完整父单元 patch：只允许写入请求区，其他 valid 和 ghost 都保留 sentinel。
    Vector<Box> requested;
    Vector<int> requested_indices;
    for (int fi=0; fi<fba.size(); ++fi) {
        Box target=fba[fi];
        target.setSmall(0,target.smallEnd(0)+1);
        target.setBig(1,target.bigEnd(1)-1);
        requested.push_back(target);
        requested_indices.push_back(fi);
    }
    MultiFab partial(fba,fdm,Q,2), partial_error(fba,fdm,Q,2);
    partial.setVal(-777);
    lbm.InitializeDdfFromCoarse(1,0.,coarse_osi ? raw : canonical,coarse_osi,phase,
                                partial,requested,requested_indices);
    for (MFIter mfi(partial);mfi.isValid();++mfi) {
        const auto values=partial.const_array(mfi);
        const auto expected=reference.const_array(mfi);
        const auto error=partial_error.array(mfi);
        const Box target=requested.at(mfi.index());
        ParallelFor(partial[mfi].box(),Q,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            const Real truth=target.contains(i,j,k) ? expected(i,j,k,q) : Real(-777);
            error(i,j,k,q)=std::abs(values(i,j,k,q)-truth);
        });
    }
    AMREX_ALWAYS_ASSERT(!partial_error.contains_nan(0,Q,2));
    const Real partial_linf=partial_error.norm0(0,Q,IntVect(2));
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(partial_linf<Real(2.e-14),"partial patch or sentinel mismatch");
    MultiFab::Subtract(amrex_reference,reference,0,0,Q,0);
    const Real amrex_linf=amrex_reference.norm0(0,Q,IntVect(0));
    MultiFab::Subtract(actual,reference,0,0,Q,0);
    const Real linf=actual.norm0(0,Q,IntVect(0));
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(linf < Real(2.e-14), "sparse initialization differs from independent custom oracle");
    Vector<InterpolationPatchTag> patches;
    for (int i=0;i<fba.size();++i) {
        patches.push_back({fba[i],i});
    }
    MultiFab stage;
    lbm.BuildInterpolationStage(1,canonical,reference,patches,stage);
    AMREX_ALWAYS_ASSERT(stage.nGrow()==0 && stage.boxArray().numPts() < ba.numPts());
    Print() << "PASS sparse initialization mode=" << mode << " periodic=" << periodic_case
            << " osi=" << coarse_osi << " phase=" << phase << " linf=" << linf
            << " partial_linf=" << partial_linf << " ghost_linf=" << ghost_linf << " amrex_linf=" << amrex_linf << " stage_cells=" << stage.boxArray().numPts() << " parent_cells=" << ba.numPts() << '\n';
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
