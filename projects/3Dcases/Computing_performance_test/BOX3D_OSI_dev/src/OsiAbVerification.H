#ifndef OsiAbVerification_H_
#define OsiAbVerification_H_

class AmrCoreLBM;

// 只负责把检测动作编排到生产生命周期的明确阶段，不拥有生产 DDF 状态。
class OsiAbVerification {
  public:
    OsiAbVerification();

    void BeforeRegrid(AmrCoreLBM& lid, int step) const;
    void AfterAverageDown(AmrCoreLBM& lid, int step) const;
    void AfterRepair(AmrCoreLBM& lid, int step) const;
    void AfterRefineMesh(AmrCoreLBM& lid, int step) const;
    void AfterFirstFillGhost(AmrCoreLBM& lid, int lev);
    void AfterAdvance(AmrCoreLBM& lid, int lev, int step) const;

  private:
    bool IsTargetStep(int step) const noexcept;

    int step_entry_check_step_ = -1;
    int step_entry_check_interval_ = -1;
    bool check_after_average_valid_ = false;
    bool check_after_first_fill_ = false;
    bool first_fill_checked_ = false;
};

#endif
