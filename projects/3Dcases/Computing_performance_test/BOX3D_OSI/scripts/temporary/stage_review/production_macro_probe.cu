// 本次审查临时验证：对相同非均匀canonical数据调用冻结宏观量函数，phase=0。
#include "LbmLattice.H"
#include "OsiIndex.H"
#include "frozen_macros.inc"
#include <cuda_runtime.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstdlib>
#define CHECK(x) do { auto err=(x); if(err!=cudaSuccess){std::fprintf(stderr,"%s\n",cudaGetErrorString(err));std::exit(1);} } while(0)
__global__ void ab_kernel(amrex::Array4<amrex::Real> ddf,amrex::Array4<amrex::Real> rho,amrex::Array4<amrex::Real> vel){int tid=blockIdx.x*blockDim.x+threadIdx.x;int ix=tid%32,iy=(tid/32)%32,iz=tid/1024;if(iz<32)compute_macro(ix,iy,iz,ddf,rho,vel);}
__global__ void osi_kernel(amrex::Array4<const amrex::Real> ddf,amrex::Array4<amrex::Real> rho,amrex::Array4<amrex::Real> vel){int tid=blockIdx.x*blockDim.x+threadIdx.x;int ix=tid%32,iy=(tid/32)%32,iz=tid/1024;if(iz<32)compute_macro_osi(ix,iy,iz,ddf,rho,vel,{{0,0,0},{32,32,32}},{0,0,0});}
int main(){constexpr int pts=32768;std::vector<float> input(pts*27),left(pts*3),right(pts*3),ra(pts),rb(pts);unsigned rng=321;
for(auto &v:input){rng=1664525*rng+1013904223;v=.01f+.02f*(rng/4294967296.f);}
float *ddf,*rhoa,*rhob,*va,*vb;CHECK(cudaMalloc(&ddf,input.size()*4));CHECK(cudaMalloc(&rhoa,pts*4));CHECK(cudaMalloc(&rhob,pts*4));CHECK(cudaMalloc(&va,pts*3*4));CHECK(cudaMalloc(&vb,pts*3*4));CHECK(cudaMemcpy(ddf,input.data(),input.size()*4,cudaMemcpyHostToDevice));amrex::Dim3 lo{0,0,0},hi{32,32,32};amrex::Array4<float> df(ddf,lo,hi,27),ar(rhoa,lo,hi,1),br(rhob,lo,hi,1),av(va,lo,hi,3),bv(vb,lo,hi,3);ab_kernel<<<pts/128,128>>>(df,ar,av);osi_kernel<<<pts/128,128>>>(df,br,bv);CHECK(cudaDeviceSynchronize());CHECK(cudaMemcpy(left.data(),va,pts*3*4,cudaMemcpyDeviceToHost));CHECK(cudaMemcpy(right.data(),vb,pts*3*4,cudaMemcpyDeviceToHost));CHECK(cudaMemcpy(ra.data(),rhoa,pts*4,cudaMemcpyDeviceToHost));CHECK(cudaMemcpy(rb.data(),rhob,pts*4,cudaMemcpyDeviceToHost));int neq=0,rneq=0;float mx=0;for(int k=0;k<pts*3;++k){neq+=left[k]!=right[k];mx=fmaxf(mx,fabsf(left[k]-right[k]));}for(int k=0;k<pts;++k)rneq+=ra[k]!=rb[k];printf("{\"cells\":%d,\"velocity_unequal\":%d,\"rho_unequal\":%d,\"velocity_max_abs\":%.17g}\n",pts,neq,rneq,mx);}
