# 本轮D3Q27对照的临时生成入口；保留实验期间作为复现材料。
from pathlib import Path
import re,hashlib,shutil,json,sys
case=Path(__file__).resolve().parents[3];exp=case/'runs/temporary/flat_q27/round1';exp.mkdir(parents=True,exist_ok=True)
orig=case/'main_ldc_nee.cu';s=orig.read_text();shutil.copy2(orig,exp/'original.cu')
if '--original' in sys.argv:
    exp=case/'runs/temporary/flat_original/round1';exp.mkdir(parents=True,exist_ok=True)
    shutil.copy2(orig,exp/'original.cu')
    source=s.replace('#include "../basic/errCUDA.h"', '#define checkCudaErrors(call) do { cudaError_t code=(call); if(code!=cudaSuccess){fprintf(stderr,"CUDA %s:%d: %s\\n",__FILE__,__LINE__,cudaGetErrorString(code)); MPI_Abort(MPI_COMM_WORLD,2);} } while(0)\n#include <cmath>')
    source=source.replace('#define OUTPUT_VEL_EVERY 10000','#define OUTPUT_VEL_EVERY 0').replace('stepmax = 10000;','stepmax = 3000;')
    source=source.replace('dfloat feq[Q];','dfloat feq[Q];\n    feq[0]=0.0f; // 原模型不推进静止分量，仅初始化未使用槽位，避免未初始化读取。')
    source=source.replace('double total_time = 0.0;','double total_time = 0.0, precise_time=0.0;')
    source=source.replace('total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();','total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n        precise_time += std::chrono::duration<double,std::milli>(end-start).count();')
    source=source.replace('\t\t\ttotal_time = 0.0;', '            printf("PRECISE ms=%.9f MLUPS_fluid=%.9f MLUPS_storage=%.9f\\n",precise_time,(double)NxT*NyT*NzT/precise_time,(double)NODES/precise_time);\n            precise_time=0;\n\t\t\ttotal_time = 0.0;')
    # 退役的CPU_time统计读取未初始化时间戳，与实际推进无关，实验不使用它。
    source=source.replace('\tCPU_time(me);','').replace('\t\tcom3_time += endwtime_com3 - endwtime_Fcolli;','').replace('\t\tstream_time += endwtime_stream - endwtime_com3;','')
    for n in [256,586]:
        variant=source.replace('#define NxT (586*NPROCx)',f'#define NxT ({n}*NPROCx)').replace('#define NyT 586',f'#define NyT {n}').replace('#define NzT 586',f'#define NzT {n}')
        for start,end in [('__global__ void fluidCollisionInnerCUDA','__global__ void ldcWallBCCUDA'),('__global__ void ldcWallBCCUDA','__global__ void pdfsExPackXCUDA')]:
            assert s[s.index(start):s.index(end)]==variant[variant.index(start):variant.index(end)]
        (exp/f'main_original_{n}.cu').write_text(variant)
    (exp/'source_manifest.json').write_text(json.dumps({'original_sha256':hashlib.sha256(orig.read_bytes()).hexdigest(),'kernel_bodies_byte_identical':True,'steps':3000,'sizes':[256,586]},indent=2)+'\n')
    print(exp);sys.exit(0)
lattice=(case/'src/LbmLattice.H').read_text().split('#define LBM_D3Q27_ORDER0(X)')[1].split('#define LBM_D3Q27_ORDER1')[0]
rows=re.findall(r'X\((-?\d+),\s*(-?\d+),\s*(-?\d+),\s*([^)]*)\)',lattice);assert len(rows)==27
es=[tuple(map(int,r[:3])) for r in rows];ws=[r[3].replace('_rt','f') for r in rows]
opp=[es.index(tuple(-x for x in e)) for e in es]
tables='\n'.join(f'__device__ __constant__ int E{axis}[27]={{'+','.join(str(e[d]) for e in es)+'};' for d,axis in enumerate('XYZ'))
tables+='\n__device__ __constant__ float W[27]={'+','.join(ws)+'};\n'
tables+='constexpr int HOST_E[27][3]={'+','.join('{'+','.join(map(str,e))+'}' for e in es)+'};\n'
tables+='constexpr float HOST_W[27]={'+','.join(ws)+'};\n'
tables+='__device__ __constant__ int OPP[27]={'+','.join(map(str,opp))+'};\n'
s=s.replace('#include "../basic/errCUDA.h"','''#define checkCudaErrors(call) do { cudaError_t code=(call); if(code!=cudaSuccess){fprintf(stderr,"CUDA %s:%d: %s\\n",__FILE__,__LINE__,cudaGetErrorString(code)); MPI_Abort(MPI_COMM_WORLD,2);} } while(0)
#include <vector>
#include <algorithm>
#include <cstdint>
''')
s=s.replace('#define nThreads 128','#ifndef nThreads\n#define nThreads 128\n#endif')
s=s.replace('#define NxT (586*NPROCx)','#define NxT (256*NPROCx)').replace('#define NyT 586','#define NyT 256').replace('#define NzT 586','#define NzT 256')
s=s.replace('#define Q 19','#define Q 27\n'+tables).replace('#define Re 1600.0','#define Re 1000.0').replace('#define U0 (Ma * cs)','#define U0 (0.05f)')
s=s.replace('#define OUTPUT_VEL_EVERY 10000','#define OUTPUT_VEL_EVERY 0')
a=s.index('__device__ __host__ inline pfint INDEX');b=s.index('void CPU_time',a)
idx='''// 一维OSI环：每方向位移由固定网格尺寸决定，读写复用相同地址。
__device__ __host__ inline pfint INDEX(pfint node, pfint q, pfint phase) {
    const long long offset[27]={OFFSETS};
    const pfint shift=(pfint)((offset[q]+(long long)NODES)%(long long)NODES);
    return q*(pfint)NODES+(node+(pfint)NODES*phase-shift*phase)%(pfint)NODES;
}
'''.replace('OFFSETS',','.join(f'({x}LL*NY*NZ+{y}LL*NZ+{z}LL)' for x,y,z in es))
s=s[:a]+idx+'\n'+s[b:]
a=s.index('void init_f(');b=s.index('void init_flag(',a)
s=s[:a]+'''bool verify_only=false;
void init_f(int me, pfint phase, dfloat* fD, dfloat* pr, dfloat* u, dfloat* F,int* flag){
    for(pfint id=0;id<NODES;++id){
        pr[id]=1.0f/3.0f;
        for(int d=0;d<3;++d){u[d*(pfint)NODES+id]=0;F[d*(pfint)NODES+id]=0;}
        for(int q=0;q<Q;++q) fD[INDEX(id,q,phase)]=HOST_W[q]*(verify_only?(1.0f+0.001f*sinf(float(id%97)+q)):1.0f);
    }
}

'''+s[b:]
a=s.index('__global__ void fluidCollisionInnerCUDA');b=s.index('__global__ void pdfsExPackXCUDA',a)
kernel='''// 与现有求解器使用相同D3Q27 BGK公式；保留原程序一维OSI和宏观场写回。
__global__ void fluidCollisionInnerCUDA(pfint phase,dfloat* fD,dfloat* pr,dfloat* u,dfloat* F,int* flag){
    const pfint id=threadIdx.x+(pfint)blockDim.x*blockIdx.x;
    if(id>=NODES || flag[id]!=1) return;
    float fq[27]; pfint address[27];float rho=0, mx=0,my=0,mz=0;
#pragma unroll
    for(int q=0;q<27;++q){
        address[q]=INDEX(id,q,phase);float f=fD[address[q]];fq[q]=f;
        rho+=f;mx+=f*EX[q];my+=f*EY[q];mz+=f*EZ[q];
    }
    float inv=1.0f/rho, ux=mx*inv,uy=my*inv,uz=mz*inv;
    float common=1.0f-1.5f*(ux*ux+uy*uy+uz*uz);
    float om=(float)omega,keep=1.0f-om;
#pragma unroll
    for(int q=0;q<27;++q){
        float eu=EX[q]*ux+EY[q]*uy+EZ[q]*uz;
        float eq=W[q]*rho*(common+3.0f*eu+4.5f*eu*eu);
        fD[address[q]]=keep*fq[q]+om*eq;
    }
    pr[id]=rho/3.0f;u[id]=ux;u[NODES+id]=uy;u[2*(pfint)NODES+id]=uz;
}
// 保留原程序的壁面本地全路反弹；运动壁改到z高端，与现有算例方向一致。
__global__ void ldcWallBCCUDA(pfint phase,dfloat* fD,dfloat* pr,dfloat* u,dfloat* F,int* flag){
    pfint id=threadIdx.x+(pfint)blockDim.x*blockIdx.x;
    if(id>=NODES || flag[id]!=0) return;
    float fq[27];
#pragma unroll
    for(int q=0;q<27;++q)fq[q]=fD[INDEX(id,q,phase)];
    float uw=(id%NZ==Nz2)?float(U0):0.0f;
#pragma unroll
    for(int q=0;q<27;++q) fD[INDEX(id,q,phase)]=fq[OPP[q]]+6.0f*W[q]*EX[q]*uw;
}

'''
s=s[:a]+kernel+s[b:]
s=s.replace('stepmax = 10000;','stepmax = 1200;\n    verify_only=(argc>1 && strcmp(argv[1],"--verify")==0);')
s=s.replace('100 * sizeof(dfloat), stream1','0, stream1')
s=s.replace('cudaStreamSynchronize(stream1);','checkCudaErrors(cudaStreamSynchronize(stream1));')
s=s.replace('std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()','std::chrono::duration<double,std::milli>(end-start).count()')
s=s.replace('step %1000 == 0','step %200 == 0')
s=s.replace('(double)NODES / total_time','(double)NODES * 200.0 / (total_time * 1000.0)').replace('(double)NxT * NyT * NzT / total_time','(double)NxT * NyT * NzT * 200.0 / (total_time * 1000.0)')
s=s.replace('mlups_all * 152.0','mlups_fluid * 216.0').replace('152B/格点','216B/流体格点，仅DDF')
s=s.replace('\tCPU_time(me);','')
s=s.replace('\t\tcom3_time += endwtime_com3 - endwtime_Fcolli;','').replace('\t\tstream_time += endwtime_stream - endwtime_com3;','')
# 验证读取初始化值，使用独立double参考验证全部方向；失败时不进入性能。
verify='''
    if(verify_only){
        std::vector<pfint> ids;
        std::vector<double> expected;
        for(int sample=0;sample<64;++sample){
            pfint x=2+(sample*37)%Nx,y=2+(sample*19)%Ny,z=2+(sample*11)%Nz;
            pfint id=x*NY*NZ+y*NZ+z;ids.push_back(id);
            double fq[27],rho=0,mx=0,my=0,mz=0;
            for(int q=0;q<27;++q){fq[q]=fDHost[INDEX(id,q,1)];rho+=fq[q];mx+=fq[q]*HOST_E[q][0];my+=fq[q]*HOST_E[q][1];mz+=fq[q]*HOST_E[q][2];}
            double ux=mx/rho,uy=my/rho,uz=mz/rho;
            for(int q=0;q<27;++q){double eu=HOST_E[q][0]*ux+HOST_E[q][1]*uy+HOST_E[q][2]*uz;
                expected.push_back((1.0-(float)omega)*fq[q]+(float)omega*HOST_W[q]*rho*(1-1.5*(ux*ux+uy*uy+uz*uz)+3*eu+4.5*eu*eu));}
        }
        fluidCollisionInnerCUDA<<<(NODES+nThreads-1)/nThreads,nThreads,0,stream1>>>(1,fDDevice,pDevice,uDevice,FDevice,flagDevice);
        checkCudaErrors(cudaStreamSynchronize(stream1));
        checkCudaErrors(cudaMemcpy(fDHost,fDDevice,Q*memSizeNODES,cudaMemcpyDeviceToHost));
        double err=0;
        for(size_t a=0;a<ids.size();++a)for(int q=0;q<27;++q){double actual=fDHost[INDEX(ids[a],q,1)];if(!std::isfinite(actual))MPI_Abort(MPI_COMM_WORLD,5);err=std::max(err,std::abs(actual-expected[a*27+q]));}
        printf("VERIFY collision_samples=64 directions=27 linf=%.12g tolerance=2e-6\\n",err);
        if(err>2e-6)MPI_Abort(MPI_COMM_WORLD,4);
        freeMemoryCUDA();MPI_Finalize();return 0;
    }
'''
pos=s.index('\n\tstartwtime = MPI_Wtime();',s.index('int main('));s=s[:pos]+verify+s[pos:]
# 性能末态只做有限值检查，不冒充物理精度验证。
pos=s.index('\n\tendwtime = MPI_Wtime();',s.index('int main('))
s=s[:pos]+'''
    checkCudaErrors(cudaMemcpy(pHost,pDevice,memSizeNODES,cudaMemcpyDeviceToHost));
    checkCudaErrors(cudaMemcpy(uHost,uDevice,3*memSizeNODES,cudaMemcpyDeviceToHost));
    double vmax=0,rmin=1e99,rmax=-1e99;
    for(pfint x=2;x<=Nx1;++x)for(pfint y=2;y<=Ny1;++y)for(pfint z=2;z<=Nz1;++z){
        pfint id=x*NY*NZ+y*NZ+z;double rho=3*pHost[id];
        if(!std::isfinite(rho)||rho<=0)MPI_Abort(MPI_COMM_WORLD,7);
        rmin=std::min(rmin,rho);rmax=std::max(rmax,rho);
        for(int d=0;d<3;++d){double v=uHost[d*(pfint)NODES+id];if(!std::isfinite(v))MPI_Abort(MPI_COMM_WORLD,8);vmax=std::max(vmax,std::abs(v));}
    }
    printf("FINITE rho_min=%g rho_max=%g velocity_linf=%g\\n",rmin,rmax,vmax);
'''+s[pos:]
# 逐步CUDA事件沿用现有求解器的计时思路，复用事件且不添加阶段同步。
s=s.replace('#include <vector>', '#include <cmath>\n#include <vector>')
s=s.replace('double total_time = 0.0;', 'double total_time = 0.0, gpu_bc_ms=0.0,gpu_collision_ms=0.0;\n    cudaEvent_t ev0,ev1,ev2;\n    checkCudaErrors(cudaEventCreate(&ev0));checkCudaErrors(cudaEventCreate(&ev1));checkCudaErrors(cudaEventCreate(&ev2));')
s=s.replace('\t\tldcWallBCCUDA<<<', '\t\tcheckCudaErrors(cudaEventRecord(ev0,stream1));\n\t\tldcWallBCCUDA<<<')
s=s.replace('\t\tfluidCollisionInnerCUDA<<<', '\t\tcheckCudaErrors(cudaEventRecord(ev1,stream1));\n\t\tfluidCollisionInnerCUDA<<<')
s=s.replace('\n\t\tendwtime_Fcolli = MPI_Wtime();', '\n        checkCudaErrors(cudaEventRecord(ev2,stream1));\n\t\tendwtime_Fcolli = MPI_Wtime();')
s=s.replace('total_time += std::chrono::duration<double,std::milli>(end-start).count();', 'total_time += std::chrono::duration<double,std::milli>(end-start).count();\n        float bc_ms=0,cl_ms=0;checkCudaErrors(cudaEventElapsedTime(&bc_ms,ev0,ev1));checkCudaErrors(cudaEventElapsedTime(&cl_ms,ev1,ev2));gpu_bc_ms+=bc_ms;gpu_collision_ms+=cl_ms;')
s=s.replace('\t\t\ttotal_time = 0.0;', '            printf("STAGE ms_per_step boundary=%.9f collision=%.9f\\n",gpu_bc_ms/200.0,gpu_collision_ms/200.0);\n            gpu_bc_ms=0;gpu_collision_ms=0;\n\t\t\ttotal_time = 0.0;')
s=s.replace('// 全部格点（含 halo），Nx4^3 = 256^3，与我们 OSI 版口径一致','// 全存储格点，仅辅助指标，不用于判优').replace('// 仅流体格点 252^3','// 有效流体256^3').replace('// total_time = 1000 步的毫秒数 -> MLUPs = 格点数 / total_time','// 200步窗口，采用浮点毫秒，保留真实时间精度')

(exp/'main_q27.cu').write_text(s)
(exp/'verify_source.cu').write_text(s.replace('#define NxT (256*NPROCx)','#define NxT (16*NPROCx)').replace('#define NyT 256','#define NyT 16').replace('#define NzT 256','#define NzT 16'))
(exp/'source_manifest.json').write_text(json.dumps({'original_sha256':hashlib.sha256(orig.read_bytes()).hexdigest(),'q27_sha256':hashlib.sha256(s.encode()).hexdigest(),'lattice_source':'src/LbmLattice.H LBM_D3Q27_ORDER0'},indent=2)+'\n')
print(exp)
