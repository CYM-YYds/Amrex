#include "OsiCommunication.H"
#include <AMReX.H>
#include <AMReX_MFIter.H>
#include <limits>

using namespace amrex;
namespace {
constexpr int components = 27;

AMREX_GPU_HOST_DEVICE
OSI::Coord3 velocity(int q) {
    return {q % 3 - 1, (q / 3) % 3 - 1, q / 9 - 1};
}

AMREX_GPU_HOST_DEVICE
Real expected(int i, int j, int k, int q, int generation) {
    const int x = (i % 16 + 16) % 16;
    return Real(generation * 100000 + q * 1000 + k * 100 + j * 16 + x);
}

void encode(MultiFab& raw, std::uint64_t phase, int generation) {
    raw.setVal(-777);
    for (MFIter mfi(raw); mfi.isValid(); ++mfi) {
        const auto dst = raw.array(mfi);
        const auto [fab, shift] = OSI::MakeOsiFabContext(raw[mfi].box(), phase);
        ParallelFor(mfi.validbox(), components,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                const auto address = OSI::osi_address({i,j,k}, velocity(q), fab, shift);
                dst(address.x,address.y,address.z,q) = expected(i,j,k,q,generation);
            });
    }
}

void copy_interpolation(OSI::CpcCache<true>& cache, const OSI::PhaseShift3* shifts) {
    OSI::parallel_copy_local(cache.local_tags, components,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::LocalCopyTag& tag) {
            const auto source = OSI::source_cell(tag,i,j,k);
            const auto address = OSI::osi_address({source[0],source[1],source[2]},
                velocity(q),tag.src_fab,shifts[tag.src_phase_index]);
            tag.dst(i,j,k,q) = tag.src(address.x,address.y,address.z,q);
        });
    OSI::parallel_copy_mpi(cache.pack_tags,cache.unpack_tags,cache.plan,false,
        components,ParallelDescriptor::NProcs(),
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::RawPackTag& tag,Real* buffer) {
            const auto address = OSI::osi_address({i,j,k},velocity(q),tag.fab,
                shifts[tag.phase_index]);
            buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q] =
                tag.src(address.x,address.y,address.z,q);
        },
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::CanonicalUnpackTag& tag,const Real* buffer) {
            tag.dst(i,j,k,q) =
                buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q];
        },{},&cache.buffers);
}

void copy_average(OSI::CpcCache<false>& cache,const OSI::PhaseShift3* shifts) {
    OSI::parallel_copy_local(cache.local_tags,components,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::LocalCopyTag& tag) {
            const auto source = OSI::source_cell(tag,i,j,k);
            const auto address = OSI::osi_address({i,j,k},velocity(q),tag.dst_fab,
                shifts[tag.dst_phase_index]);
            tag.dst(address.x,address.y,address.z,q) = tag.src(source[0],source[1],source[2],q);
        });
    OSI::parallel_copy_mpi(cache.pack_tags,cache.unpack_tags,cache.plan,false,
        components,ParallelDescriptor::NProcs(),
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::CanonicalPackTag& tag,Real* buffer) {
            buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q] = tag.src(i,j,k,q);
        },
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::CanonicalRawUnpackTag& tag,const Real* buffer) {
            const auto address = OSI::osi_address({i,j,k},velocity(q),tag.fab,
                shifts[tag.phase_index]);
            tag.dst(address.x,address.y,address.z,q) =
                buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q];
        },{},&cache.buffers);
}

// 使用独立的主机整数取模作为相位表 oracle，包含不同轴长和空 rank。
void check_phase_table(const OSI::FabPhaseCache& phases,
                       const MultiFab& state, std::uint64_t phase) {
    Vector<OSI::PhaseShift3> values(phases.shifts.size());
    Gpu::copy(Gpu::deviceToHost, phases.shifts.begin(), phases.shifts.end(), values.begin());
    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const auto& value = values[phases.slot(mfi.index())];
        const Box ring = state[mfi].box();
        AMREX_ALWAYS_ASSERT(value.x == int(phase % std::uint64_t(ring.length(0))));
        AMREX_ALWAYS_ASSERT(value.y == int(phase % std::uint64_t(ring.length(1))));
        AMREX_ALWAYS_ASSERT(value.z == int(phase % std::uint64_t(ring.length(2))));
    }
}

// 独立同层通信测试：复用真实 FB 几何计划，检查 valid 和域内/周期 ghost。
void define_boundary(OSI::FillBoundaryCache& cache, MultiFab& state,
                     const Periodicity& periodic, const OSI::FabPhaseCache& phases) {
    const auto& fb = state.getFB(state.nGrowVect(), periodic);
    const auto fab = [&](int index) {
        return OSI::MakeOsiFabContext(state[index].box(), 0).fab;
    };
    Vector<OSI::LocalCopyTag> local;
    for (const auto& tag : *fb.m_LocTags) {
        local.push_back({state.const_array(tag.srcIndex), state.array(tag.dstIndex),
                         tag.sbox, tag.dbox, fab(tag.srcIndex), fab(tag.dstIndex),
                         phases.slot(tag.srcIndex), phases.slot(tag.dstIndex)});
    }
    cache.local_tags.define(local);
    cache.plan = OSI::make_mpi_plan(*fb.m_SndTags, *fb.m_RcvTags,
                                  ParallelDescriptor::NProcs(), components);
    OSI::define_mpi_tags(*fb.m_SndTags, *fb.m_RcvTags, cache.plan, components,
        cache.pack_tags, cache.unpack_tags,
        [&](const auto& tag, std::size_t offset) -> OSI::MpiPackTag {
            return {state.const_array(tag.srcIndex), tag.sbox, fab(tag.srcIndex),
                    offset, phases.slot(tag.srcIndex)};
        },
        [&](const auto& tag, std::size_t offset) -> OSI::MpiUnpackTag {
            return {state.array(tag.dstIndex), tag.dbox, fab(tag.dstIndex),
                    offset, phases.slot(tag.dstIndex)};
        });
    cache.buffers.resize(cache.plan, false);
}

void copy_boundary(OSI::FillBoundaryCache& cache, const OSI::PhaseShift3* shifts) {
    OSI::parallel_copy_local(cache.local_tags, components,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q, const OSI::LocalCopyTag& tag) {
            const auto source = OSI::source_cell(tag, i, j, k);
            const auto src = OSI::osi_address({source[0], source[1], source[2]},
                velocity(q), tag.src_fab, shifts[tag.src_phase_index]);
            const auto dst = OSI::osi_address({i,j,k}, velocity(q), tag.dst_fab,
                shifts[tag.dst_phase_index]);
            tag.dst(dst.x,dst.y,dst.z,q) = tag.src(src.x,src.y,src.z,q);
        });
    OSI::parallel_copy_mpi(cache.pack_tags, cache.unpack_tags, cache.plan, false,
        components, ParallelDescriptor::NProcs(),
        [=] AMREX_GPU_DEVICE(int i,int j,int k,int q,const OSI::MpiPackTag& tag,Real* buffer) {
            const auto src = OSI::osi_address({i,j,k},velocity(q),tag.fab,shifts[tag.phase_index]);
            buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q] =
                tag.src(src.x,src.y,src.z,q);
        },
        [=] AMREX_GPU_DEVICE(int i,int j,int k,int q,const OSI::MpiUnpackTag& tag,const Real* buffer) {
            const auto dst = OSI::osi_address({i,j,k},velocity(q),tag.fab,shifts[tag.phase_index]);
            tag.dst(dst.x,dst.y,dst.z,q) =
                buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q];
        },{},&cache.buffers);
}

void check_boundary(MultiFab& state,std::uint64_t phase,int generation) {
    MultiFab errors(state.boxArray(),state.DistributionMap(),components,2);
    errors.setVal(0);
    // x 周期，y/z 非周期；排除实际通信不填充的物理域外 ghost。
    const Box domain(IntVect(-2,0,0),IntVect(17,7,7));
    for (MFIter mfi(state,false); mfi.isValid(); ++mfi) {
        const auto src=state.const_array(mfi);
        const auto dst=errors.array(mfi);
        const auto [fab,shift]=OSI::MakeOsiFabContext(state[mfi].box(),phase);
        const Box box=amrex::grow(mfi.validbox(),2)&domain;
        ParallelFor(box,components,[=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
            const auto address=OSI::osi_address({i,j,k},velocity(q),fab,shift);
            dst(i,j,k,q)=src(address.x,address.y,address.z,q)-expected(i,j,k,q,generation);
        });
    }
    AMREX_ALWAYS_ASSERT(!errors.contains_nan(0,components,2) && !errors.contains_inf(0,components,2));
    for (int q=0;q<components;++q) AMREX_ALWAYS_ASSERT(errors.norm0(q,2)==0);
}

void check(MultiFab& state,std::uint64_t phase,int generation,bool raw) {
    MultiFab errors(state.boxArray(),state.DistributionMap(),components,0);
    for (MFIter mfi(state);mfi.isValid();++mfi) {
        const auto src = state.const_array(mfi);
        const auto dst = errors.array(mfi);
        const auto [fab,shift] = OSI::MakeOsiFabContext(state[mfi].box(),phase);
        ParallelFor(mfi.validbox(),components,
            [=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
                Real reference = expected(i,j,k,q,generation);
                // 平均只覆盖两个条带，其他 valid 单元必须保留哨兵值。
                if (raw && !((i<=1 || i>=14 || (i>=6 && i<=9)) &&
                             j>=2 && j<=5 && k>=2 && k<=5)) {
                    reference = -777;
                }
                const auto address = raw ? OSI::osi_address({i,j,k},velocity(q),fab,shift)
                                         : OSI::Coord3{i,j,k};
                dst(i,j,k,q) = src(address.x,address.y,address.z,q)-reference;
            });
    }
    AMREX_ALWAYS_ASSERT(!errors.contains_nan() && !errors.contains_inf());
    for (int q=0;q<components;++q) {
        AMREX_ALWAYS_ASSERT(errors.norm0(q)==0);
    }
}
}

int main(int argc,char** argv) {
    amrex::Initialize(argc,argv);
    {
        const int ranks = ParallelDescriptor::NProcs();
        // 全部盒归 rank 0，验证其他 rank 没有本地 Fab 时仍可初始化、更新和清除。
        {
            BoxArray boxes(Box(IntVect(0),IntVect(3,4,5)));
            MultiFab state(boxes,DistributionMapping(Vector<int>{0}),components,2);
            OSI::FabPhaseCache phases;
            phases.define(state,0);
            phases.update(17);
            check_phase_table(phases,state,17);
            AMREX_ALWAYS_ASSERT(phases.shifts.size()==(ParallelDescriptor::MyProc()==0?1:0));
            phases.clear();
            AMREX_ALWAYS_ASSERT(!phases.ready && phases.shifts.empty());
        }
        const Periodicity periodic(IntVect(16,0,0));
        OSI::CpcCache<true> interpolation;
        OSI::CpcCache<false> average;
        // 两种布局分别重建同一个缓存；相位表覆盖连续推进、跳变、回绕和重置。
        for (int layout=0;layout<2;++layout) {
            BoxArray full(Box(IntVect(0),IntVect(15,7,7)));
            full.maxSize(layout==0?8:6);
            Vector<int> owners(full.size());
            for (int n=0;n<full.size();++n) owners[n]=(n+layout)%ranks;
            MultiFab source(full,DistributionMapping(owners),components,2);
            Vector<Box> boxes{Box(IntVect(-2,2,2),IntVect(1,5,5)),
                              Box(IntVect(6,2,2),IntVect(9,5,5)),
                              Box(IntVect(14,2,2),IntVect(17,5,5))};
            BoxArray sparse(boxes.data(),boxes.size());
            Vector<int> sparse_owners{(layout+1)%ranks,layout%ranks,(layout+1)%ranks};
            MultiFab stage(sparse,DistributionMapping(sparse_owners),components,0);
            OSI::FabPhaseCache source_phases;
            source_phases.define(source,0);
            OSI::FillBoundaryCache boundary;
            define_boundary(boundary,source,periodic,source_phases);
            interpolation.define(source,stage,periodic,components,false,source_phases);
            // 平均源不重叠，且包含周期域外条带，验证周期平移写回。
            boxes.pop_back();
            BoxArray result_ba(boxes.data(),boxes.size());
            sparse_owners.pop_back();
            MultiFab result(result_ba,DistributionMapping(sparse_owners),components,0);
            MultiFab destination(full,DistributionMapping(owners),components,2);
            OSI::FabPhaseCache destination_phases;
            destination_phases.define(destination,0);
            average.define(result,destination,periodic,components,false,destination_phases);
            Long remote_values=interpolation.plan.send_total+average.plan.send_total;
            ParallelDescriptor::ReduceLongSum(remote_values);
            AMREX_ALWAYS_ASSERT(ranks==1 || remote_values>0);
            auto* interpolation_buffer=interpolation.buffers.send_device.data();
            auto* average_buffer=average.buffers.send_device.data();
            const std::uint64_t phases_to_test[] = {0,1,2,17,41,
                std::numeric_limits<std::uint64_t>::max(),0};
            const auto* source_table = source_phases.data(0);
            const auto* destination_table = destination_phases.data(0);
            for (int iteration=0;iteration<7;++iteration) {
                const std::uint64_t phase=phases_to_test[iteration];
                source_phases.update(phase);
                destination_phases.update(phase);
                check_phase_table(source_phases,source,phase);
                check_phase_table(destination_phases,destination,phase);
                AMREX_ALWAYS_ASSERT(source_table==source_phases.data(phase));
                AMREX_ALWAYS_ASSERT(destination_table==destination_phases.data(phase));
                const int generation=layout*3+iteration+1;
                encode(source,phase,generation);
                stage.setVal(-777);
                copy_boundary(boundary,source_phases.data(phase));
                check_boundary(source,phase,generation);
                copy_interpolation(interpolation,source_phases.data(phase));
                check(stage,0,generation,false);
                for (MFIter mfi(result);mfi.isValid();++mfi) {
                    const auto dst=result.array(mfi);
                    ParallelFor(mfi.validbox(),components,
                        [=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
                            dst(i,j,k,q)=expected(i,j,k,q,generation);
                        });
                }
                destination.setVal(-777);
                copy_average(average,destination_phases.data(phase));
                check(destination,phase,generation,true);
                AMREX_ALWAYS_ASSERT(interpolation_buffer==interpolation.buffers.send_device.data());
                AMREX_ALWAYS_ASSERT(average_buffer==average.buffers.send_device.data());
                Print()<<"PASS layout="<<layout<<" phase="<<phase
                       <<" boundary_linf=0 interpolation_linf=0 average_linf=0 untouched_linf=0\n";
            }
            interpolation.clear();
            average.clear();
            boundary.clear();
            source_phases.clear();
            destination_phases.clear();
            AMREX_ALWAYS_ASSERT(!interpolation.ready && !average.ready);
        }
        Print()<<"PASS CPC cache ranks="<<ranks<<" reuse and rebuild\n";
    }
    amrex::Finalize();
}
