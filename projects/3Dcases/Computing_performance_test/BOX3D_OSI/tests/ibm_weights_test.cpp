// 与优化前直接核求值路径逐值比较，覆盖负坐标、半格边界及多标记点重叠。
#define AMREX_SINGLE_PRECISION_PARTICLES 1
#include "Kernels.H"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <limits>

using namespace amrex;

namespace {
void near(Real value, Real expected, const char* label) {
    // 解析解和独立路径比较允许浮点舍入误差；双精度保持原判据。
    const Real tolerance = sizeof(Real) == 8 ? Real(1.e-12) : Real(32) *
        std::numeric_limits<Real>::epsilon() * std::max(Real(1), std::abs(expected));
    if (!std::isfinite(value) || !std::isfinite(expected) || std::abs(value - expected) > tolerance) {
        std::cerr << label << ": got=" << value << " expected=" << expected << '\n';
        std::exit(1);
    }
}
struct Field {
    Box box;
    std::vector<Real> data;
    Array4<Real> a;
    Field(int lo, int hi, int nc)
        : box(IntVect(lo), IntVect(hi)), data(box.numPts()*nc, 0),
          a(data.data(), box, nc) {}
};
template <class F> void cells(const Box& b, F f) {
    const auto lo=lbound(b), hi=ubound(b);
    for(int k=lo.z;k<=hi.z;++k)
        for(int j=lo.y;j<=hi.y;++j)
            for(int i=lo.x;i<=hi.x;++i) f(i,j,k);
}
}
#include "ibm_weight_reference.H"
int main() {
    Field u(-6,6,AMREX_SPACEDIM), rho(-6,6,1);
    Field actual(-6,6,AMREX_SPACEDIM), expected(-6,6,AMREX_SPACEDIM);
    cells(u.box,[&](int i,int j,int k) {
        for(int d=0;d<AMREX_SPACEDIM;++d)
            u.a(i,j,k,d)=Real(.01)*(Real(i*i)+Real(.3)*j-Real(.2)*k+d);
        rho.a(i,j,k,0)=Real(1)+Real(.001)*(i+j+k);
    });
    struct Marker { Real x[3]; Real pos(int d) const { return x[d]; } };
    LbmGridParams grid{}; grid.dx_min=Real(.5); grid.dt_min=Real(.5);
    RealVect target(Real(.005)), omega(Real(.002)), centre(Real(0));
    for(int n=0;n<61;++n) {
        Marker p{{Real(n-30)/Real(20),Real((n*7)%61-30)/Real(20),Real((n*13)%61-30)/Real(20)}};
        ParticleReal a[10]={},b[10]={};
        a[6]=b[6]=ParticleReal(.13); a[7]=b[7]=ParticleReal(-.21);
        a[8]=b[8]=ParticleReal(.17); a[9]=b[9]=ParticleReal(.9);
#if AMREX_SPACEDIM == 2
        force_interp_extrap(p,a[0],a[1],a[5],a[6],a[7],a[9],u.a,rho.a,actual.a,Real(.5),target,grid);
        reference_force_interp_extrap(p,b[0],b[1],b[5],b[6],b[7],b[9],u.a,rho.a,expected.a,Real(.5),target,grid);
#else
        force_interp_extrap(p,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],u.a,rho.a,actual.a,Real(.5),target,omega,centre,grid);
        reference_force_interp_extrap(p,b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],u.a,rho.a,expected.a,Real(.5),target,omega,centre,grid);
#endif
        for(int d=0;d<6;++d) near(a[d],b[d],"marker force/torque reference");
    }
    for(std::size_t i=0;i<actual.data.size();++i)
        near(actual.data[i],expected.data[i],"Eulerian force reference");
    std::cout << "IBM reference PASS DIM=" << AMREX_SPACEDIM << "\n";
}
