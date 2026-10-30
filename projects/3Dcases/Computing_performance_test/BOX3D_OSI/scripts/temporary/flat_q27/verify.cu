// 本轮临时数值验证：对所有小网格流体/壁面单元及多个相位使用独立地址和double公式。
#define main unused_ldc_main
#include "verify_source.cu"
#undef main
static pfint reference_address(pfint id,int q,pfint phase){
    long long delta=HOST_E[q][0]*(long long)NY*NZ+HOST_E[q][1]*(long long)NZ+HOST_E[q][2];
    long long value=((long long)id-delta*(long long)phase)%(long long)NODES;
    if(value<0)value+=NODES;
    return (pfint)q*NODES+value;
}
int main(int argc,char**argv){
    MPI_Init(&argc,&argv);checkCudaErrors(cudaSetDevice(0));mallocMemoryCUDA();verify_only=true;
    init_flag(0,flagHost);checkCudaErrors(cudaMemcpy(flagDevice,flagHost,sizeof(int)*NODES,cudaMemcpyHostToDevice));
    for(pfint phase : {0ULL,1ULL,19ULL,20ULL,41ULL,1200ULL}){
        init_f(0,phase,fDHost,pHost,uHost,FHost,flagHost);
        for(pfint id=0;id<NODES;++id)for(int q=0;q<Q;++q)
            if(INDEX(id,q,phase)!=reference_address(id,q,phase))MPI_Abort(MPI_COMM_WORLD,10);
        std::vector<float> initial(fDHost,fDHost+Q*(pfint)NODES);
        checkCudaErrors(cudaMemcpy(fDDevice,fDHost,Q*memSizeNODES,cudaMemcpyHostToDevice));
        fluidCollisionInnerCUDA<<<(NODES+nThreads-1)/nThreads,nThreads,0,stream1>>>(phase,fDDevice,pDevice,uDevice,FDevice,flagDevice);
        checkCudaErrors(cudaStreamSynchronize(stream1));
        checkCudaErrors(cudaMemcpy(fDHost,fDDevice,Q*memSizeNODES,cudaMemcpyDeviceToHost));
        double max_error=0;long long cells=0;
        for(pfint id=0;id<NODES;++id)if(flagHost[id]==1){
            double fq[27],rho=0,mx=0,my=0,mz=0;
            for(int q=0;q<Q;++q){fq[q]=initial[reference_address(id,q,phase)];rho+=fq[q];mx+=fq[q]*HOST_E[q][0];my+=fq[q]*HOST_E[q][1];mz+=fq[q]*HOST_E[q][2];}
            double ux=mx/rho,uy=my/rho,uz=mz/rho;
            for(int q=0;q<Q;++q){double eu=HOST_E[q][0]*ux+HOST_E[q][1]*uy+HOST_E[q][2]*uz;
                double expected=(1.0-(float)omega)*fq[q]+(float)omega*HOST_W[q]*rho*(1-1.5*(ux*ux+uy*uy+uz*uz)+3*eu+4.5*eu*eu);
                double actual=fDHost[reference_address(id,q,phase)];if(!std::isfinite(actual))MPI_Abort(MPI_COMM_WORLD,11);
                max_error=std::max(max_error,std::abs(actual-expected));
            }++cells;
        }
        if(max_error>2e-6)MPI_Abort(MPI_COMM_WORLD,12);
        std::vector<float> before_bc(fDHost,fDHost+Q*(pfint)NODES);
        ldcWallBCCUDA<<<(NODES+nThreads-1)/nThreads,nThreads,0,stream1>>>(phase,fDDevice,pDevice,uDevice,FDevice,flagDevice);
        checkCudaErrors(cudaStreamSynchronize(stream1));
        checkCudaErrors(cudaMemcpy(fDHost,fDDevice,Q*memSizeNODES,cudaMemcpyDeviceToHost));
        double bc_error=0;long long walls=0;
        for(pfint id=0;id<NODES;++id)if(flagHost[id]==0){
            double uw=(id%NZ==Nz2)?float(U0):0;
            for(int q=0;q<Q;++q){int opposite=-1;
                for(int o=0;o<Q;++o)if(HOST_E[o][0]==-HOST_E[q][0]&&HOST_E[o][1]==-HOST_E[q][1]&&HOST_E[o][2]==-HOST_E[q][2])opposite=o;
                if(opposite<0)MPI_Abort(MPI_COMM_WORLD,13);
                double expected=before_bc[reference_address(id,opposite,phase)]+6.0*HOST_W[q]*HOST_E[q][0]*uw;
                double actual=fDHost[reference_address(id,q,phase)];if(!std::isfinite(actual))MPI_Abort(MPI_COMM_WORLD,14);
                bc_error=std::max(bc_error,std::abs(actual-expected));
            }++walls;
        }
        if(bc_error>2e-6)MPI_Abort(MPI_COMM_WORLD,15);
        printf("VERIFY_ALL phase=%llu fluid=%lld walls=%lld collision_linf=%.12g boundary_linf=%.12g mapping=exact\n",phase,cells,walls,max_error,bc_error);
    }
    freeMemoryCUDA();MPI_Finalize();return 0;
}
