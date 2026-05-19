# Cylinder2D_IDFtest IBM Delta Kernel Test Summary

Generated: 2026-04-29
Updated: 2026-05-15

## Scope

This test set compares IBM/IDF delta kernels derived from `projects/Cylindertest/Cylinder2D_IDFtest`.

- Baseline reference: `delta3pSmoothed` in `projects/Cylindertest/Cylinder2D_IDFtest`.
- Submitted variants: `delta2p`, `delta3p`, `delta4p`.
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

## Compile And Runtime Status

| Kernel | Case directory | Compile status | Final force job | Runtime status | Submit log |
|---|---|---:|---:|---|---|
| delta3pSmoothed | `projects/Cylindertest/Cylinder2D_IDFtest` | success | 548347 | succeeded | `logs/submit/548347-out.log` |
| delta2p | `projects/Cylindertest/Cylinder2D_IDFtest_delta2p` | success | 548344 | succeeded | `logs/submit/548344-out.log` |
| delta3p | `projects/Cylindertest/Cylinder2D_IDFtest_delta3p` | success | 548345 | succeeded | `logs/submit/548345-out.log` |
| delta4p | `projects/Cylindertest/Cylinder2D_IDFtest_delta4p` | success | 548346 | succeeded | `logs/submit/548346-out.log` |

## Results

| Kernel | Status | Restart evidence | NL_global | NE_global | A inverse | Convergence | Cd_raw | Cd_p | Cd_v | Total Time | Force plotfile |
|---|---|---|---:|---:|---|---|---:|---:|---:|---:|---|
| delta3pSmoothed | complete | restarted at step 520000, time=650 | 502 | 2396 | success, max\|A^(-1)\|=57.83665678 | step 522000, Cd=1.52945956 | 1.52945956 | 0.9911783506 | 0.5382812093 | 144.2999514 | `data/cyl_lev3_Re40_force_522000` |
| delta2p | complete | restarted at step 520000, time=650 | 502 | 1116 | success, max\|A^(-1)\|=21.85411643 | step 523000, Cd=1.528479492 | 1.528479492 | 0.9926815721 | 0.5357979197 | 207.4880589 | `../Cylinder2D_IDFtest_delta2p/data/cyl_delta2p_lev3_Re40_force_523000` |
| delta3p | complete | restarted at step 520000, time=650 | 502 | 1744 | success, max\|A^(-1)\|=26.70864618 | step 544000, Cd=1.529053973 | 1.529053973 | 0.9918034396 | 0.5372505335 | 1664.367172 | `../Cylinder2D_IDFtest_delta3p/data/cyl_delta3p_lev3_Re40_force_544000` |
| delta4p | complete | restarted at step 520000, time=650 | 502 | 2396 | success, max\|A^(-1)\|=3630.622576 | step 545000, Cd=1.53026853 | 1.53026853 | 0.9892923835 | 0.5409761466 | 1732.880289 | `../Cylinder2D_IDFtest_delta4p/data/cyl_delta4p_lev3_Re40_force_545000` |

Each force plotfile Header contains the two variables `Fx` and `Fy`.
