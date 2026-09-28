#include "OsiAbVerification.H"

#include <AMReX_ParmParse.H>

#include "AmrCoreLBM.H"

OsiAbVerification::OsiAbVerification() {
    amrex::ParmParse pp("verification");
    pp.query("osi_step_entry_check_step", step_entry_check_step_);
    pp.query("osi_check_after_average_valid", check_after_average_valid_);
    pp.query("check_after_first_fill", check_after_first_fill_);
}

bool OsiAbVerification::IsTargetStep(int step) const noexcept {
    return step_entry_check_step_ >= 0 &&
           step == step_entry_check_step_;
}

void OsiAbVerification::BeforeRegrid(AmrCoreLBM& lid, int step) const {
    if (lid.osiReferenceEnabled() && IsTargetStep(step)) {
        lid.CheckOsiReferenceLevel0(step, "StepEntry");
    }
}

void OsiAbVerification::AfterAverageDown(
    AmrCoreLBM& lid, int step) const {
    if (lid.osiReferenceEnabled() && check_after_average_valid_ &&
        IsTargetStep(step)) {
        lid.CheckOsiReferenceLevel0(step, "AfterAverageDownValid");
    }
}

void OsiAbVerification::AfterRepair(AmrCoreLBM& lid, int step) const {
    if (lid.osiReferenceEnabled() && IsTargetStep(step)) {
        lid.CheckOsiReferenceLevel0(step, "AfterRepair");
    }
}

void OsiAbVerification::AfterRefineMesh(AmrCoreLBM& lid, int step) const {
    if (lid.osiReferenceEnabled() && IsTargetStep(step)) {
        lid.PrintRegridDiagnostics();
        lid.CheckOsiReferenceLevel0(step, "AfterRefineMesh");
    }
}

void OsiAbVerification::AfterFirstFillGhost(AmrCoreLBM& lid, int lev) {
    if (lev == 0 && check_after_first_fill_ && !first_fill_checked_ &&
        lid.osiReferenceEnabled()) {
        first_fill_checked_ = true;
        lid.CheckOsiReferenceLevel0(-1, "AfterFirstFillGhost");
    }
}

void OsiAbVerification::AfterAdvance(
    AmrCoreLBM& lid, int lev, int step) const {
    if (lid.osiReferenceEnabled()) {
        lid.AdvanceAndCheckOsiReference(lev, step);
    }
}
