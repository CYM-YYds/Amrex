#ifndef LAGRANGEPARTICLECONTAINER_H_
#define LAGRANGEPARTICLECONTAINER_H_

#include <AMReX_MultiFab.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_AmrParGDB.H>
#include <AMReX_Particles.H>
#include <AMReX_AmrParticles.H>

#include "D3Q19.H"

struct PIdx {
    enum {
        fx = 0,
        fy,
        fz,
        tx,
        ty,
        tz,
        xlocal,
        ylocal,
        zlocal,
        area,
        nattribs
    };
};

class MyParIter : public amrex::ParIter<0, 0, PIdx::nattribs> {
  public:
    using amrex::ParIter<0, 0, PIdx::nattribs>::ParIter;
    using RealVector = amrex::ParIter<0, 0, PIdx::nattribs>::ContainerType::RealVector;

    const std::array<RealVector, PIdx::nattribs>& GetAttribs() const {
        return GetStructOfArrays().GetRealData();
    }

    std::array<RealVector, PIdx::nattribs>& GetAttribs() {
        return GetStructOfArrays().GetRealData();
    }

    const RealVector& GetAttribs(int comp) const {
        return GetStructOfArrays().GetRealData(comp);
    }

    RealVector& GetAttribs(int comp) {
        return GetStructOfArrays().GetRealData(comp);
    }
};

//********************************************************************//
//                     原版                                           //
//********************************************************************//
/**
 * @brief 拉格朗日粒子容器类，继承自AMReX的ParticleContainer
 * @tparam 0, 0, PIdx::nattribs 粒子容器的模板参数，分别表示网格数据索引、属性索引和属性数量
 */
class LagrangeParticleContainer : public amrex::ParticleContainer<0, 0, PIdx::nattribs> {
  public:
    LagrangeParticleContainer(amrex::AmrCore* amr_core)
        : amrex::ParticleContainer<0, 0, PIdx::nattribs>(amr_core->GetParGDB()),
          radius(rate * R),
          centre{AMREX_D_DECL(X, Y, Z)},
          F_tot{AMREX_D_DECL(0., 0., 0.)},
          T_tot{AMREX_D_DECL(0., 0., 0.)},
          vel{AMREX_D_DECL(0., 0., 0.)},
          vel_old{AMREX_D_DECL(0., 0., 0.)},
          angvel{AMREX_D_DECL(0., 0., 0.)},
          angvel_old{AMREX_D_DECL(0., 0., 0.)} {
        amrex::Print() << "Particle construct successed ..." << std::endl; // 打印构造成功信息
    }
    LagrangeParticleContainer(amrex::AmrCore* amr_core, amrex::RealVect& point, const int idx)
        : amrex::ParticleContainer<0, 0, PIdx::nattribs>(amr_core->GetParGDB()),
          id(idx),
          radius(rb),
          centre{AMREX_D_DECL(point[0], point[1], point[2])},
          F_tot{AMREX_D_DECL(0., 0., 0.)},
          T_tot{AMREX_D_DECL(0., 0., 0.)},
          F_lub{AMREX_D_DECL(0., 0., 0.)},
          vel{AMREX_D_DECL(0., 0., 0.)},
          vel_old{AMREX_D_DECL(0., 0., 0.)},
          angvel{AMREX_D_DECL(0., 0., 0.)},
          angvel_old{AMREX_D_DECL(0., 0., 0.)} {
        amrex::Print() << "Particle " << idx << " construct successed ..." << std::endl;
    }

    ~LagrangeParticleContainer() override = default;

    LagrangeParticleContainer(const LagrangeParticleContainer&) = delete;
    LagrangeParticleContainer& operator=(const LagrangeParticleContainer&) = delete;

    LagrangeParticleContainer(LagrangeParticleContainer&&) noexcept = default;
    LagrangeParticleContainer& operator=(LagrangeParticleContainer&&) noexcept = default;

    void PrintParticleParm();

    void InitParticle(int lev);
    void InitChannelParticle(int lev);
    void InitCylinderParticle(int lev);
    void InitParticleFromFile(int lev, const std::string object);

    void MoveParticle(int lev, amrex::Real cur_time);

    amrex::RealVect ReturnCentre();
    amrex::RealVect ReturnVelocity();

    void SaveVelocity(int lev, int step);
    void SetLubVal(amrex::RealVect& force_lub);
    void SavePosition(int lev, int step);

    //********************************************************************//
    //                     MDF (Multi-Direct Forcing)                     //
    //********************************************************************//
#if USE_MDF_TWO_STAGE
    void InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev, amrex::MultiFab& force_delta_lev);
#else
    void InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev);
#endif

    // 壁面模型版本（保留原有接口）
    void InterpForceWallModel(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev);

    // 清零粒子力（用于迭代开始前）
    void ZeroParticleForce(int lev);

    void SaveFxy(int lev, int step);

    void WriteParticle(int step);

    void CollideParticle(const std::unique_ptr<LagrangeParticleContainer>& p2);
    void CollideWall();

    //********************************************************************//
    //                     IDF (Implicit Direct Forcing)                  //
    //********************************************************************//

    // 收集本进程粒子的位置和ID（用于矩阵组装）
    // 返回本进程的粒子数量
    int IDF_CollectParticleData(int lev,
                                std::vector<amrex::Real>& local_lag_pos,
                                std::vector<int>& local_lag_ids);

    // 在拉格朗日点上执行插值（欧拉->拉格朗日）
    // IDF 插值：直接写入提供的向量，避免覆盖粒子属性
    void IDF_Interpolate(int lev,
                         amrex::MultiFab& u_lev,
                         amrex::MultiFab& rho_lev,
                         std::vector<amrex::Real>& local_interp_ux,
                         std::vector<amrex::Real>& local_interp_uy,
                         std::vector<amrex::Real>& local_interp_uz,
                         std::vector<amrex::Real>& local_interp_rho,
                         std::vector<int>& local_interp_ids,
                         int local_NL);

    // 将求解得到的力写回粒子属性（3D版本：fx,fy,fz）
    void IDF_WriteForceToParticles(int lev,
                                   const std::vector<amrex::Real>& sol_x,
                                   const std::vector<amrex::Real>& sol_y,
                                   const std::vector<amrex::Real>& sol_z,
                                   int idf_NL_global,
                                   const std::unordered_map<int, int>& pid_to_idx);

    // 将拉格朗日力传播到欧拉网格
    void IDF_SpreadForce(int lev, amrex::MultiFab& force_lev);

  private: // 颗粒信息
    int id;

    amrex::Real radius;

    amrex::RealVect centre;

    amrex::RealVect F_tot;
    amrex::RealVect T_tot;
    amrex::RealVect F_lub;

    amrex::RealVect vel;
    amrex::RealVect vel_old;
    amrex::RealVect angvel;
    amrex::RealVect angvel_old;
};

#endif // LAGRANGEPARTICLECONTAINER_H_