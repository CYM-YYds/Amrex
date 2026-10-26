#include "Kernels.H"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace amrex;

namespace {
void near(Real value, Real expected, const char* label) {
    if (!std::isfinite(value) || std::abs(value - expected) > 1.e-12) {
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
Real polynomial(Real x, Real y, Real z, bool quadratic) {
    return 2 + .2*x + .3*y + AMREX_D_PICK(0.,0.,.1*z) +
        (quadratic ? .02*x*x + .03*y*y + .01*x*y +
         AMREX_D_PICK(0.,0.,.04*z*z+.02*x*z+.03*y*z) : 0);
}
}

int main() {
    // 格子矩独立于推进布局，检查模型表本身而不只比较两条推进路径。
    Real total=0, first[AMREX_SPACEDIM]={}, second[AMREX_SPACEDIM][AMREX_SPACEDIM]={};
    static_assert(lattice_ez_host(0) == 0);
    for(int q=0;q<Q;++q) {
        // 每个有效方向都核对 host/device 入口，二维地址检查器同时约束第三分量访问。
        near(lattice_ez_host(q), AMREX_D_PICK(0,0,e_host[q][2]), "host z direction");
        near(lattice_ez(q), lattice_ez_host(q), "device z direction");
        total+=w[q];
        for(int d=0;d<AMREX_SPACEDIM;++d) {
            first[d]+=w[q]*e[q][d];
            for(int t=0;t<AMREX_SPACEDIM;++t) second[d][t]+=w[q]*e[q][d]*e[q][t];
        }
    }
    near(total,1,"weight sum");
    for(int d=0;d<AMREX_SPACEDIM;++d) {
        near(first[d],0,"first moment");
        for(int t=0;t<AMREX_SPACEDIM;++t) near(second[d][t], d==t ? Real(1)/3 : 0,"second moment");
    }
    Field coarse(-2,2,Q), fine(0,1,Q), restricted(0,0,Q);
    Array4<const Real> coarse_view=coarse.a;
    for(int mode=0;mode<3;++mode) {
        for(int degree=0;degree<3;++degree) {
            if(degree==2 && mode!=2) continue;
            cells(coarse.box,[&](int i,int j,int k) {
                for(int q=0;q<Q;++q) coarse.a(i,j,k,q)=degree==0 ? 2 : polynomial(i,j,k,degree==2);
            });
            if(mode==0) cells(fine.box,[&](int i,int j,int k){interp_bilinear_ddf(i,j,k,fine.a,coarse_view);});
            if(mode==1) interp_cell_cons_linear_children_ddf(0,0,0,fine.a,coarse_view,fine.box);
            if(mode==2) interp_cell_quadratic_children_ddf(0,0,0,fine.a,coarse_view,fine.box);
            cells(fine.box,[&](int i,int j,int k){
                for(int q=0;q<Q;++q) near(fine.a(i,j,k,q), degree==0 ? 2 :
                    polynomial(i==0 ? -.25:.25,j==0 ? -.25:.25,AMREX_D_PICK(0.,0.,k==0 ? -.25:.25),degree==2),"interpolation");
            });
            if(degree<2) {
                average_down_lbm(0,0,0,restricted.a,fine.a,IntVect(2));
                for(int q=0;q<Q;++q) near(restricted.a(0,0,0,q),coarse.a(0,0,0,q),"restriction");
            }
        }
    }
    Field canonical(-2,2,Q), raw(-2,2,Q), rho(-2,2,1), velocity(-2,2,AMREX_SPACEDIM), vort(-2,2,2);
    const auto context=OSI::MakeOsiFabContext(raw.box,17);
    near(context.fab.length.z,AMREX_D_PICK(1,1,5),"ring z length");
    for(int q=0;q<Q;++q) {
        std::vector<bool> seen(raw.box.numPts(),false);
        cells(raw.box,[&](int i,int j,int k) {
            const auto p=OSI::osi_address({i,j,k},{e[q][0],e[q][1],AMREX_D_PICK(0,0,e[q][2])},context.fab,context.phase_shift);
            const auto l=lbound(raw.box);
            const int index=((p.z-l.z)*5+(p.y-l.y))*5+p.x-l.x;
            if(seen.at(index)) std::exit(2);
            seen[index]=true;
            canonical.a(i,j,k,q)=feqQian(1.1,{AMREX_D_DECL(.01,.02,.03)},q)+1.e-4*(q+1);
            raw.a(p.x,p.y,p.z,q)=canonical.a(i,j,k,q);
        });
    }
    Field legacy(-2,2,Q);
    std::copy(canonical.data.begin(),canonical.data.end(),legacy.data.begin());
    collide(0,0,0,legacy.a,rho.a,velocity.a,Real(1)/Real(1.3),1,IntVect(2));
    cells(raw.box,[&](int i,int j,int k) {
        collide_bgk_register(i,j,k,canonical.a,1.3);
        collide_bgk_register_osi(i,j,k,raw.a,context.fab,context.phase_shift,1.3);
        for(int q=0;q<Q;++q) near(load_ddf<true>(i,j,k,q,raw.a,context.fab,context.phase_shift),canonical.a(i,j,k,q),"OSI collision");
    });
    for(int q=0;q<Q;++q) near(legacy.a(0,0,0,q),canonical.a(0,0,0,q),"BGK baseline");
    // 独立的先缩放、再平均作为融合内核对照；覆盖相位绕环与关闭缩放。
    for (std::uint64_t phase : {std::uint64_t(0), std::uint64_t(1), std::uint64_t(17)}) {
        const auto ctx = OSI::MakeOsiFabContext(raw.box, phase);
        cells(raw.box, [&](int i, int j, int k) {
            for (int q = 0; q < Q; ++q) {
                const auto p = OSI::osi_address({i,j,k}, {e[q][0], e[q][1], lattice_ez(q)}, ctx.fab, ctx.phase_shift);
                raw.a(p.x,p.y,p.z,q) = canonical.a(i,j,k,q);
            }
        });
        for (bool scaled : {false, true}) {
            Field workspace(-2,2,Q), expected(0,0,Q), ab_result(0,0,Q), osi_result(0,0,Q);
            std::copy(canonical.data.begin(), canonical.data.end(), workspace.data.begin());
            if (scaled) cells(workspace.box, [&](int i,int j,int k) { average_scale(i,j,k,workspace.a,Real(.7)); });
            average_down_lbm(0,0,0,expected.a,workspace.a,IntVect(2));
            average_down_to_canonical<false>(0,0,0,ab_result.a,canonical.a,IntVect(2),{}, {},Real(.7),scaled);
            average_down_to_canonical<true>(0,0,0,osi_result.a,raw.a,IntVect(2),ctx.fab,ctx.phase_shift,Real(.7),scaled);
            Field stored(-2,2,Q);
            average_down_to_canonical<false,true>(0,0,0,ab_result.a,canonical.a,IntVect(2),{}, {},Real(.7),scaled,stored.a);
            cells(fine.box,[&](int i,int j,int k) {
                for (int q=0;q<Q;++q) near(stored.a(i,j,k,q),workspace.a(i,j,k,q),"stored scaled fine");
            });
            for (int q=0;q<Q;++q) {
                near(ab_result.a(0,0,0,q), expected.a(0,0,0,q), "fused AB restriction");
                near(osi_result.a(0,0,0,q), expected.a(0,0,0,q), "fused OSI restriction");
            }
        }
    }
    cells(velocity.box,[&](int i,int j,int k) {
        velocity.a(i,j,k,0)=-j;velocity.a(i,j,k,1)=i;
#if AMREX_SPACEDIM == 3
        velocity.a(i,j,k,2)=0;
#endif
    });
    compute_vorticity(0,0,0,velocity.a,vort.a,1);
    near(vort.a(0,0,0,0),1,"Q criterion");near(vort.a(0,0,0,1),2,"vorticity");
    Field walls(0,4,Q), wallrho(0,4,1), wallu(0,4,AMREX_SPACEDIM);
    cells(walls.box,[&](int i,int j,int k){for(int q=0;q<Q;++q)walls.a(i,j,k,q)=feqQian(1,{AMREX_D_DECL(0.,0.,0.)},q);});
    const IntVect hi(4); const GpuArray<int,AMREX_SPACEDIM> periodic{AMREX_D_DECL(0,0,0)};
    // 顶盖角点和普通顶盖点都应采用移动顶盖速度。
    for(int corner=0;corner<2;++corner) {
        int i=corner ? 0:2, j=AMREX_D_PICK(0,4,2), k=AMREX_D_PICK(0,0,4);
        fill_boundary(i,j,k,walls.a,hi,periodic);
        compute_macro(i,j,k,walls.a,wallrho.a,wallu.a);
        near(wallu.a(i,j,k,0),U0,"lid velocity");near(wallu.a(i,j,k,1),0,"lid normal velocity");
    }
    std::cout << "PASS dimension kernels DIM=" << AMREX_SPACEDIM << " Q=" << Q << '\n';
}
