// 本次审查临时算术对照：仅测试除法和倒数乘法，不是生产宏观量kernel验证。
#include <cuda_runtime.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstdlib>
#define CHECK(x) do { auto err=(x); if(err!=cudaSuccess){std::fprintf(stderr,"%s\n",cudaGetErrorString(err));std::exit(1);} } while(0)
__global__ void direct(const float* r,const float* m,float* u,int n){int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<n)u[i]=m[i]/r[i];}
__global__ void inverse(const float* r,const float* m,float* u,int n){int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<n){float inv=1.f/r[i];u[i]=m[i]*inv;}}
int main(){const int n=1048576;std::vector<float> r(n),m(n),a(n),b(n);unsigned x=321;
for(int i=0;i<n;++i){x=1664525*x+1013904223;r[i]=.9f+.2f*(x/4294967296.f);x=1664525*x+1013904223;m[i]=-.05f+.1f*(x/4294967296.f);}
float *dr,*dm,*da,*db;CHECK(cudaMalloc(&dr,n*4));CHECK(cudaMalloc(&dm,n*4));CHECK(cudaMalloc(&da,n*4));CHECK(cudaMalloc(&db,n*4));CHECK(cudaMemcpy(dr,r.data(),n*4,cudaMemcpyHostToDevice));CHECK(cudaMemcpy(dm,m.data(),n*4,cudaMemcpyHostToDevice));direct<<<n/128,128>>>(dr,dm,da,n);inverse<<<n/128,128>>>(dr,dm,db,n);CHECK(cudaDeviceSynchronize());CHECK(cudaMemcpy(a.data(),da,n*4,cudaMemcpyDeviceToHost));CHECK(cudaMemcpy(b.data(),db,n*4,cudaMemcpyDeviceToHost));int diff=0;float max=0;for(int i=0;i<n;++i){diff+=a[i]!=b[i];max=fmaxf(max,fabsf(a[i]-b[i]));}std::printf("{\"samples\":%d,\"unequal\":%d,\"max_abs\":%.17g}\n",n,diff,max);}
