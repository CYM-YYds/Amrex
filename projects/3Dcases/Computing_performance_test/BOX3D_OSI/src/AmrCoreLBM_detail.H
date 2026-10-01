#ifndef BOX3D_OSI_AMRCORELBM_DETAIL_H_
#define BOX3D_OSI_AMRCORELBM_DETAIL_H_

#include <AMReX_Gpu.H>
#include <AMReX_Utility.H>

// 多个实现文件共用的计时器与检查点格式常量；此处不保存运行状态。
namespace Box3dDetail {
inline constexpr char checkpoint_label_v1[] = "LBMCheckpoint";
inline constexpr char checkpoint_label_v2[] = "LBMCheckpointV2";
inline constexpr char checkpoint_layout_ab[] = "canonical_ab_two_array_v1";
inline constexpr char checkpoint_layout_osi[] = "canonical_osi_single_array_v1";
class ScopedPerfTimer {
  public:
    explicit ScopedPerfTimer(double& accum)
        : m_accum(accum), m_start(amrex::second()) {}

    ~ScopedPerfTimer() {
        amrex::Gpu::streamSynchronize();
        m_accum += amrex::second() - m_start;
    }

  private:
    double& m_accum;
    double m_start;
};
}

#endif
