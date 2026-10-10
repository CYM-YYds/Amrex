#include "OsiCommunication.H"
#include <AMReX.H>
#include <AMReX_MFIter.H>

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

void copy_interpolation(OSI::CpcCache<true>& cache, std::uint64_t phase) {
    OSI::parallel_copy_local(cache.local_tags, components,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::LocalCopyTag& tag) {
            const auto source = OSI::source_cell(tag,i,j,k);
            const auto address = OSI::osi_address({source[0],source[1],source[2]},
                velocity(q),tag.src_fab,OSI::osi_phase_shift(phase,tag.src_fab));
            tag.dst(i,j,k,q) = tag.src(address.x,address.y,address.z,q);
        });
    OSI::parallel_copy_mpi(cache.pack_tags,cache.unpack_tags,cache.plan,false,
        components,ParallelDescriptor::NProcs(),
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::RawPackTag& tag,Real* buffer) {
            const auto address = OSI::osi_address({i,j,k},velocity(q),tag.fab,
                OSI::osi_phase_shift(phase,tag.fab));
            buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q] =
                tag.src(address.x,address.y,address.z,q);
        },
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::CanonicalUnpackTag& tag,const Real* buffer) {
            tag.dst(i,j,k,q) =
                buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q];
        },{},&cache.buffers);
}

void copy_average(OSI::CpcCache<false>& cache,std::uint64_t phase) {
    OSI::parallel_copy_local(cache.local_tags,components,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::LocalCopyTag& tag) {
            const auto source = OSI::source_cell(tag,i,j,k);
            const auto address = OSI::osi_address({i,j,k},velocity(q),tag.dst_fab,
                OSI::osi_phase_shift(phase,tag.dst_fab));
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
                OSI::osi_phase_shift(phase,tag.fab));
            tag.dst(address.x,address.y,address.z,q) =
                buffer[tag.offset+OSI::buffer_cell_index(tag.region,i,j,k)*components+q];
        },{},&cache.buffers);
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
        const Periodicity periodic(IntVect(16,0,0));
        OSI::CpcCache<true> interpolation;
        OSI::CpcCache<false> average;
        // 两种布局分别重建同一个缓存；每种布局连续复用三个 phase 和三份数值。
        for (int layout=0;layout<2;++layout) {
            BoxArray full(Box(IntVect(0),IntVect(15,7,7)));
            full.maxSize(layout==0?8:4);
            Vector<int> owners(full.size());
            for (int n=0;n<full.size();++n) owners[n]=(n+layout)%ranks;
            MultiFab source(full,DistributionMapping(owners),components,2);
            Vector<Box> boxes{Box(IntVect(-2,2,2),IntVect(1,5,5)),
                              Box(IntVect(6,2,2),IntVect(9,5,5)),
                              Box(IntVect(14,2,2),IntVect(17,5,5))};
            BoxArray sparse(boxes.data(),boxes.size());
            Vector<int> sparse_owners{(layout+1)%ranks,layout%ranks,(layout+1)%ranks};
            MultiFab stage(sparse,DistributionMapping(sparse_owners),components,0);
            interpolation.define(source,stage,periodic,components,false);
            // 平均源不重叠，且包含周期域外条带，验证周期平移写回。
            boxes.pop_back();
            BoxArray result_ba(boxes.data(),boxes.size());
            sparse_owners.pop_back();
            MultiFab result(result_ba,DistributionMapping(sparse_owners),components,0);
            MultiFab destination(full,DistributionMapping(owners),components,2);
            average.define(result,destination,periodic,components,false);
            Long remote_values=interpolation.plan.send_total+average.plan.send_total;
            ParallelDescriptor::ReduceLongSum(remote_values);
            AMREX_ALWAYS_ASSERT(ranks==1 || remote_values>0);
            auto* interpolation_buffer=interpolation.buffers.send_device.data();
            auto* average_buffer=average.buffers.send_device.data();
            for (int iteration=0;iteration<3;++iteration) {
                const std::uint64_t phase=iteration==0?0:(iteration==1?17:41);
                const int generation=layout*3+iteration+1;
                encode(source,phase,generation);
                stage.setVal(-777);
                copy_interpolation(interpolation,phase);
                check(stage,0,generation,false);
                for (MFIter mfi(result);mfi.isValid();++mfi) {
                    const auto dst=result.array(mfi);
                    ParallelFor(mfi.validbox(),components,
                        [=] AMREX_GPU_DEVICE(int i,int j,int k,int q) {
                            dst(i,j,k,q)=expected(i,j,k,q,generation);
                        });
                }
                destination.setVal(-777);
                copy_average(average,phase);
                check(destination,phase,generation,true);
                AMREX_ALWAYS_ASSERT(interpolation_buffer==interpolation.buffers.send_device.data());
                AMREX_ALWAYS_ASSERT(average_buffer==average.buffers.send_device.data());
                Print()<<"PASS layout="<<layout<<" phase="<<phase
                       <<" interpolation_linf=0 average_linf=0 untouched_linf=0\n";
            }
            interpolation.clear();
            average.clear();
            AMREX_ALWAYS_ASSERT(!interpolation.ready && !average.ready);
        }
        Print()<<"PASS CPC cache ranks="<<ranks<<" reuse and rebuild\n";
    }
    amrex::Finalize();
}
