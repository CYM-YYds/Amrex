// 本次粒子单精度切换的临时解析解检查；保留用于复现，可在结果不再需要时删除。
#include "Kernels.H"
#include <vector>
#include <iostream>
#include <cstdlib>
using namespace amrex;
struct Point { ParticleReal x[3]; ParticleReal pos(int d) const { return x[d]; } };
int main() {
    static_assert(sizeof(ParticleReal)==4);
    Box box(IntVect(-4),IntVect(4));
    std::vector<Real> uv(box.numPts()*3), rv(box.numPts(),Real(1.125)), fv(box.numPts()*3,0);
    Array4<Real> u(uv.data(),box,3), rho(rv.data(),box,1), force(fv.data(),box,3);
    const Real velocity[3]={Real(.03125),Real(-.015625),Real(.0078125)};
    for(int k=-4;k<=4;++k) for(int j=-4;j<=4;++j) for(int i=-4;i<=4;++i)
        for(int d=0;d<3;++d) u(i,j,k,d)=velocity[d];
    LbmGridParams grid; grid.dt_min=Real(.5); grid.dx_min=Real(.25);
    RealVect zero(Real(0));
    double worst=0;
    auto check=[&](double got,double expected) {
        double error=std::abs(got-expected)/std::max(1.,std::abs(expected));
        worst=std::max(worst,error);
        if(!std::isfinite(got)||error>2.e-5) { std::cerr<<got<<" != "<<expected<<'\n'; std::exit(1); }
    };
    for(int n=0;n<17;++n) {
        std::fill(fv.begin(),fv.end(),Real(0));
        Point p{{ParticleReal(.13+n*.037),ParticleReal(.27+n*.019),ParticleReal(.41+n*.011)}};
        ParticleReal fx=0,fy=0,fz=0,tx=0,ty=0,tz=0,x=p.x[0],y=p.x[1],z=p.x[2],area=ParticleReal(.75),cp=0;
        force_interp_extrap(p,fx,fy,fz,tx,ty,tz,x,y,z,area,u,rho,force,Real(1),zero,zero,zero,grid);
        const double factor=2.*1.125/.5*double(LP_dr)*double(area);
        const double volume=.25*.25*.25;
        check(fx,factor*velocity[0]*volume); check(fy,factor*velocity[1]*volume); check(fz,factor*velocity[2]*volume);
        double sum[3]={};
        for(int k=-4;k<=4;++k) for(int j=-4;j<=4;++j) for(int i=-4;i<=4;++i)
            for(int d=0;d<3;++d) sum[d]+=force(i,j,k,d);
        for(int d=0;d<3;++d) check(sum[d],-factor*velocity[d]);
        pressure_interp(p,cp,rho,Real(1));
        check(cp,(1.125*double(cs2)-double(p0))/(.5*double(rho0)*double(U0)*double(U0)));
    }
    std::cout<<"PASS Real="<<sizeof(Real)<<" ParticleReal="<<sizeof(ParticleReal)<<" points=17 worst_scaled_error="<<worst<<'\n';
}
