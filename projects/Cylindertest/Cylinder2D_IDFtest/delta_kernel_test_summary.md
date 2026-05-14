# Cylinder2D_IDFtest IBM Delta Kernel Test Summary

Generated: 2026-04-29

## Scope

This test set compares IBM/IDF delta kernels derived from `projects/Cylindertest/Cylinder2D_IDFtest`.

- Baseline reference: existing `delta3pSmoothed` result; not resubmitted.
- New submitted variants: `delta2p`, `delta3p`, `delta4p`.
- All new variants keep the existing `drs = 1.9` and `IB_weight = ds_r * drs`.
- All new variants restart from the shared checkpoint symlink `chk00520000`.
- All new variants set `checkpoint.begin_step = 520000`, `checkpoint.chk_int = 0`, and `max_step = 600000`.

## Implementation

- Added a unified `ibm_delta(r)` entry in `src/Kernels.H`.
- Added `delta4p(r)` using the standard 4-point Peskin kernel.
- Replaced hard-coded `delta3pSmoothed(lx) * delta3pSmoothed(ly)` in the active IBM/IDF paths:
  - `ibm_interpolate`
  - `ibm_spread_force`
  - `force_interp_extrap`
  - `BuildActiveEulerSet`
  - `IDF_AssembleMatrix`
- Created three independent derived cases so binaries and outputs do not overwrite each other:
  - `projects/Cylindertest/Cylinder2D_IDFtest_delta2p`
  - `projects/Cylindertest/Cylinder2D_IDFtest_delta3p`
  - `projects/Cylindertest/Cylinder2D_IDFtest_delta4p`

## Compile And Submit Status

| Kernel | Case directory | Compile status | Job ID | Scheduler status at submission check | Submit log |
|---|---|---:|---:|---|---|
| delta2p | `projects/Cylindertest/Cylinder2D_IDFtest_delta2p` | success | 543519 | PENDING | `logs/submit/543519-out.log` |
| delta3p | `projects/Cylindertest/Cylinder2D_IDFtest_delta3p` | success | 543518 | RUNNING | `logs/submit/543518-out.log` |
| delta4p | `projects/Cylindertest/Cylinder2D_IDFtest_delta4p` | success | 543520 | PENDING | `logs/submit/543520-out.log` |

Compile logs:

- delta2p: `projects/Cylindertest/Cylinder2D_IDFtest_delta2p/logs/compile/compile-20260429T143629.log`
- delta3p: `projects/Cylindertest/Cylinder2D_IDFtest_delta3p/logs/compile/compile-20260429T143924.log`
- delta4p: `projects/Cylindertest/Cylinder2D_IDFtest_delta4p/logs/compile/compile-20260429T143924.log`

## Results

| Kernel | Status | Restart evidence | NL_global | NE_global | A inverse | Convergence | Cd_raw | Cd_p | Cd_v | Total Time | Notes |
|---|---|---|---:|---:|---|---|---:|---:|---:|---:|---|
| delta3pSmoothed | existing baseline complete | restarted at step 500000 | 502 | 2396 | success, max\|A^(-1)\|=57.83665678 | step 523000, Cd=1.529458076 | 1.529458076 | 0.9911773447 | 0.5382807312 | 1686.698506 | Existing log `projects/Cylindertest/Cylinder2D_IDFtest/logs/submit/541963-out.log`; baseline used `begin_step=500000`, not resubmitted. |
| delta2p | submitted, pending | pending |  |  | pending | pending |  |  |  |  | Job 543519 submitted successfully. |
| delta3p | running | restarted at step 520000, time=650 | 502 | 1744 | success, max\|A^(-1)\|=26.70864618 | pending |  |  |  |  | Job 543518 is running; latest checked progress reached `STEP 526303`; no Convergence/Cf_force_pressure/NaN/ERROR/Abort found in current available log scan. |
| delta4p | submitted, pending | pending |  |  | pending | pending |  |  |  |  | Job 543520 submitted successfully. |
