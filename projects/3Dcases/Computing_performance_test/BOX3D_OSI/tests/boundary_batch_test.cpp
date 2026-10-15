#include "OsiCommunication.H"
#include "Kernels.H"
#include <AMReX.H>
#include <AMReX_Geometry.H>
#include <AMReX_iMultiFab.H>
#include <limits>

using namespace amrex;

// 用真实生产函数测试批量调度；参考路径逐 Fab 扫描完整 grown 区域，独立判定旧厚盒。
class AmrCoreLBM {
public:
#include "boundary_batch_types.inc"
    int finest_level = 1;
    int nghost = 2;
    int mode = 1;
    Geometry geometry;
    Vector<MultiFab> f_new{2}, f_old{2}, osi_state{2};
    Vector<iMultiFab> covered_mask{2};
    Vector<OSI::FabPhaseCache> osi_fab_phase_cache{2};
    Vector<std::uint64_t> osi_phase = Vector<std::uint64_t>(2, 0);
    Vector<BoundaryWorkCache> boundary_work_cache{2};
    struct { Long boundary_full_cells=0, boundary_launch_cells=0; } perf_stats;
    int streamMode() const { return mode; }
    const Geometry& Geom(int) const { return geometry; }
    const BoxArray& boxArray(int lev) const { return f_new[lev].boxArray(); }
    const DistributionMapping& DistributionMap(int lev) const { return f_new[lev].DistributionMap(); }
    void BuildBoundaryWorkBoxes();
    enum class DdfLayout { Canonical, Osi };
    void ApplyPhysicalBoundaryLevel(int, MultiFab&, DdfLayout, bool);
};

#include "boundary_batch_impl.inc"

void seed(MultiFab& state, int generation) {
    for (MFIter mfi(state); mfi.isValid(); ++mfi) {
        const auto data=state.array(mfi);
        ParallelFor(mfi.fabbox(),Q,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            data(i,j,k,q)=1.+0.001*(i+2*j+3*k+q+generation);
        });
    }
}

void reference(AmrCoreLBM& driver, MultiFab& expected, bool osi, bool repair) {
    const Box domain=driver.geometry.Domain();
    const auto hi=domain.bigEnd();
    const auto periodic=driver.geometry.isPeriodicArray();
    const bool has_fine=driver.finest_level>0;
    const int ng=driver.nghost;
    for (MFIter mfi(expected); mfi.isValid(); ++mfi) {
        const Box valid=mfi.validbox();
        const Box clipped=grow(valid,ng)&domain;
        const auto data=expected.array(mfi);
        const auto mask=driver.covered_mask[0].const_array(mfi);
        const auto context=OSI::MakeOsiFabContext(mfi.fabbox(),driver.osi_phase[0]);
        ParallelFor(mfi.fabbox(),[=] AMREX_GPU_DEVICE(int i,int j,int k) {
            const IntVect cell(i,j,k);
            if (!clipped.contains(cell) || (repair && !valid.contains(cell))) return;
            if (has_fine && (repair ? mask(i,j,k)==0 : mask(i,j,k)!=0)) return;
            bool in_work=false;
            for(int dir=0;dir<3;++dir) {
                if(periodic[dir]) continue;
                in_work = in_work ||
                    (valid.smallEnd(dir)==domain.smallEnd(dir) && cell[dir]<=domain.smallEnd(dir)+ng) ||
                    (domain.bigEnd(dir)!=domain.smallEnd(dir) && valid.bigEnd(dir)==domain.bigEnd(dir) && cell[dir]>=domain.bigEnd(dir)-ng);
            }
            if(!in_work) return;
            if(osi) fill_boundary_osi_state(i,j,k,data,hi,periodic,context.fab,context.phase_shift);
            else fill_boundary(i,j,k,data,hi,periodic);
        });
    }
}

int main(int argc,char** argv) {
    Initialize(argc,argv);
    {
        int checks=0;
        const Box domain(IntVect(0),IntVect(15));
        const RealBox physical({0.,0.,0.},{1.,1.,1.});
        // 同一个 fixture 反复替换布局，检验缓存重建与本地索引、空 rank。
        AmrCoreLBM driver;
        for(int periodic_bits=0;periodic_bits<8;++periodic_bits) {
            int periodic[3]={periodic_bits&1,(periodic_bits>>1)&1,(periodic_bits>>2)&1};
            driver.geometry=Geometry(domain,&physical,0,periodic);
            for(int partition=0;partition<3;++partition) {
                BoxArray ba(domain);
                if(partition==1) ba.maxSize(IntVect(4,8,16));
                if(partition==2) {
                    BoxList boxes;
                    boxes.push_back(Box(IntVect(0,0,0),IntVect(4,15,15)));
                    boxes.push_back(Box(IntVect(5,0,0),IntVect(15,7,15)));
                    boxes.push_back(Box(IntVect(5,8,0),IntVect(15,15,15)));
                    ba=BoxArray(boxes);
                }
                DistributionMapping dm(ba);
                for(int lev=0;lev<2;++lev) {
                    driver.boundary_work_cache[lev].clear();
                    driver.osi_fab_phase_cache[lev].clear();
                    driver.f_new[lev].define(ba,dm,Q,2);
                    driver.f_old[lev].define(ba,dm,Q,2);
                    driver.osi_state[lev].define(ba,dm,Q,2);
                    driver.covered_mask[lev].define(ba,dm,1,2);
                    for(MFIter mfi(driver.covered_mask[lev]);mfi.isValid();++mfi) {
                        const auto mask=driver.covered_mask[lev].array(mfi);
                        ParallelFor(mfi.fabbox(),[=] AMREX_GPU_DEVICE(int i,int j,int k) {
                            mask(i,j,k)=((i+2*j+3*k)%3==0);
                        });
                    }
                    driver.osi_fab_phase_cache[lev].define(driver.osi_state[lev],0);
                }
                for(bool osi: {false,true}) {
                    driver.mode=osi?1:0;
                    driver.finest_level=1;
                    driver.BuildBoundaryWorkBoxes();
                    for(std::uint64_t phase: {std::uint64_t(0),std::uint64_t(17),std::numeric_limits<std::uint64_t>::max()}) {
                        driver.osi_phase[0]=phase;
                        driver.osi_fab_phase_cache[0].update(phase);
                        // canonical 任务必须在双缓冲交换后仍访问当前 f_new。
                        std::swap(driver.f_new[0],driver.f_old[0]);
                        MultiFab& state=osi?driver.osi_state[0]:driver.f_new[0];
                        for(int operation=0;operation<3;++operation) {
                            driver.finest_level=operation==0?0:1;
                            const bool repair=operation==2;
                            seed(state,checks);
                            MultiFab expected(ba,dm,Q,2),difference(ba,dm,Q,2);
                            MultiFab::Copy(expected,state,0,0,Q,2);
                            reference(driver,expected,osi,repair);
                            driver.ApplyPhysicalBoundaryLevel(0,state,osi?AmrCoreLBM::DdfLayout::Osi:AmrCoreLBM::DdfLayout::Canonical,repair);
                            AMREX_ALWAYS_ASSERT(!state.contains_nan(0,Q,2) && !state.contains_inf(0,Q,2));
                            AMREX_ALWAYS_ASSERT(!expected.contains_nan(0,Q,2) && !expected.contains_inf(0,Q,2));
                            MultiFab::Copy(difference,state,0,0,Q,2);
                            MultiFab::Subtract(difference,expected,0,0,Q,2);
                            for(int q=0;q<Q;++q) AMREX_ALWAYS_ASSERT(difference.norminf(q,2)==0.);
                            ++checks;
                        }
                    }
                }
            }
        }
        Print()<<"boundary_batch_test: checks="<<checks<<" ranks="<<ParallelDescriptor::NProcs()
               <<" all_raw_ghost=1 canonical_swap=1 layout_rebuild=1 linf=0\n";
    }
    Finalize();
}
