// 本次融合的 GPU 数值对照；保留为相位绕环、尾组及无缩放分支的复现材料。
#include "Kernels.H"
#include <cuda_runtime.h>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace amrex;
constexpr int group_size = Q == 27 ? 16 : 32;
constexpr int block_size = Q == 27 ? 128 : 256;

void check(cudaError_t result) {
    if (result != cudaSuccess) {
        std::cerr << cudaGetErrorString(result) << '\n';
        std::exit(2);
    }
}

Real value(int i, int j, int k, int q) {
    return Real(.02) + Real(.001) * q + Real(.00003) * (i + 2*j + 3*k);
}

// 独立主机公式按方向串行求矩，再缩放每个子单元，最后求子单元平均。
Real expected(int i, int q, bool scale) {
    Real result = 0;
    for (int kr=0; kr<AMREX_D_PICK(1,1,2); ++kr) {
        for (int jr=0; jr<2; ++jr) {
            for (int ir=0; ir<2; ++ir) {
                Real rho = 0, momentum[AMREX_SPACEDIM]{};
                for (int t=0; t<Q; ++t) {
                    const Real f = value(2*i+ir,jr,kr,t);
                    rho += f;
                    for (int d=0; d<AMREX_SPACEDIM; ++d) momentum[d] += f*e_host[t][d];
                }
                Real eu=0, usq=0, weight=1;
                for (int d=0; d<AMREX_SPACEDIM; ++d) {
                    const Real u = momentum[d]/rho;
                    eu += e_host[q][d]*u;
                    usq += u*u;
                    weight *= e_host[q][d] == 0 ? Real(2)/3 : Real(1)/6;
                }
                const Real f=value(2*i+ir,jr,kr,q);
                const Real eq=weight*rho*(1+3*eu+Real(4.5)*eu*eu-Real(1.5)*usq);
                result += scale ? eq+(f-eq)*Real(.7) : f;
            }
        }
    }
    return result/AMREX_D_PICK(2,4,8);
}

template <bool Osi>
__global__ void restrict_values(Array4<Real> output, Array4<const Real> input,
                                OSI::OsiFabContext ctx, int parents, bool scale) {
    const int i=(blockIdx.x*blockDim.x+threadIdx.x)/group_size;
    if (i>=parents) return;
#if Q == 27
    average_down_to_canonical_half_warp<Osi>(
        i,0,0,threadIdx.x%group_size,output,input,ctx.fab,ctx.phase_shift,Real(.7),scale);
#else
    average_down_to_canonical_warp<Osi>(
        i,0,0,threadIdx.x%group_size,output,input,IntVect(2),ctx.fab,ctx.phase_shift,Real(.7),scale);
#endif
}

int main() {
    Real maximum=0;
    int cases=0;
    for (int parents : {1,7,8,9,17}) {
        const Box ring(IntVect(AMREX_D_DECL(-1,-1,-1)),IntVect(AMREX_D_DECL(2*parents,2,2)));
        const Box coarse(IntVect(0),IntVect(AMREX_D_DECL(parents-1,0,0)));
        const auto lo=lbound(ring), hi=ubound(ring);
        std::vector<Real> data(ring.numPts()*Q), result(coarse.numPts()*Q);
        Array4<Real> host(data.data(),ring,Q);
        Real *device_input=nullptr, *device_output=nullptr;
        check(cudaMalloc(&device_input,data.size()*sizeof(Real)));
        check(cudaMalloc(&device_output,result.size()*sizeof(Real)));
        const Array4<const Real> input(device_input,ring,Q);
        const Array4<Real> output(device_output,coarse,Q);
        for (std::uint64_t phase : {std::uint64_t(0),std::uint64_t(1),std::uint64_t(17),std::uint64_t(100003)}) {
            const auto ctx=OSI::MakeOsiFabContext(ring,phase);
            for (bool osi : {false,true}) {
                for (int q=0;q<Q;++q) for (int k=lo.z;k<=hi.z;++k)
                    for (int j=lo.y;j<=hi.y;++j) for (int i=lo.x;i<=hi.x;++i) {
                        const auto p=osi ? OSI::osi_address({i,j,k},{e_host[q][0],e_host[q][1],lattice_ez_host(q)},ctx.fab,ctx.phase_shift) : OSI::Coord3{i,j,k};
                        host(p.x,p.y,p.z,q)=value(i,j,k,q);
                    }
                check(cudaMemcpy(device_input,data.data(),data.size()*sizeof(Real),cudaMemcpyHostToDevice));
                for (bool scale : {false,true}) {
                    const int blocks=(parents*group_size+block_size-1)/block_size;
                    if (osi) restrict_values<true><<<blocks,block_size>>>(output,input,ctx,parents,scale);
                    else restrict_values<false><<<blocks,block_size>>>(output,input,ctx,parents,scale);
                    check(cudaGetLastError());
                    check(cudaMemcpy(result.data(),device_output,result.size()*sizeof(Real),cudaMemcpyDeviceToHost));
                    const Array4<const Real> got(result.data(),coarse,Q);
                    for (int q=0;q<Q;++q) for (int i=0;i<parents;++i) {
                        const Real error=std::abs(got(i,0,0,q)-expected(i,q,scale));
                        if (!std::isfinite(error) || error>1e-13) {
                            std::cerr << "FAIL parents=" << parents << " phase=" << phase << " osi=" << osi
                                      << " scale=" << scale << " i=" << i << " q=" << q << " error=" << error << '\n';
                            return 1;
                        }
                        maximum=std::max(maximum,error);
                    }
                    ++cases;
                }
            }
        }
        check(cudaFree(device_input));
        check(cudaFree(device_output));
    }
    std::cout << "PASS restriction GPU DIM=" << AMREX_SPACEDIM << " cases=" << cases << " linf=" << maximum << '\n';
}
