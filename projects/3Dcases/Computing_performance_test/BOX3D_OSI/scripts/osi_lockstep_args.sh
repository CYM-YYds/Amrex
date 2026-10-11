#!/usr/bin/env bash

# 锁步算例共用输入；步数、失败策略和逐步检查间隔由各作业入口指定。
osi_lockstep_args() {
    local steps=$1 continue_on_mismatch=$2 check_interval=$3
    export AMREX_RUN_ARGS="max_step=${steps} lbm.reynolds_number=1000.0 lbm.stream_mode=1 amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=64 64 64 amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 amr.blocking_factor_z=32 32 32 amr.regrid_int=32 amr.plot_int=-1 performance.report_int=32 checkpoint.chk_int=-1 verification.convergence_enabled=false verification.osi_seed_pattern=false verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=${continue_on_mismatch} verification.osi_step_entry_check_interval=${check_interval} verification.osi_check_after_average_valid=true verification.check_after_first_fill=true"
}
