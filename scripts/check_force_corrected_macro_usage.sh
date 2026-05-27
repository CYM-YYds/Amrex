#!/usr/bin/env bash
set -euo pipefail

cases=(
  "projects/Cylindertest/Cylinder2D_IDFtest:2d"
  "projects/Cylindertest/Cylinder2D_IDFtest_delta2p:2d"
  "projects/Cylindertest/Cylinder2D_IDFtest_delta3p:2d"
  "projects/Cylindertest/Cylinder2D_IDFtest_delta4p:2d"
  "projects/3Dshared:3d"
)

fail=0

require_pattern() {
  local file="$1"
  local pattern="$2"
  local desc="$3"
  if ! rg -q "$pattern" "$file"; then
    printf 'missing: %s (%s)\n' "$desc" "$file" >&2
    fail=1
  fi
}

for entry in "${cases[@]}"; do
  IFS=: read -r case_dir dim <<< "$entry"
  kernels="$case_dir/src/Kernels.H"
  amr_cpp="$case_dir/src/AmrCoreLBM.cpp"
  amr_h="$case_dir/src/AmrCoreLBM.H"
  main_cpp="$case_dir/src/main.cpp"

  require_pattern "$kernels" "void compute_macro_force_corrected" "force-corrected macro kernel"
  require_pattern "$amr_h" "ComputeMacroForceCorrectedLevel" "force-corrected level declaration"
  require_pattern "$amr_h" "ComputeMacroForceCorrected\\(" "force-corrected all-level declaration"
  require_pattern "$amr_cpp" "void AmrCoreLBM::ComputeMacroForceCorrectedLevel" "force-corrected level implementation"
  require_pattern "$amr_cpp" "void AmrCoreLBM::ComputeMacroForceCorrected\\(" "force-corrected all-level implementation"
  require_pattern "$main_cpp" "ComputeMacroForceCorrected\\(\\)" "velocity output uses force-corrected macro"

  if [[ "$dim" == "2d" ]]; then
    require_pattern "$kernels" "0\\.5 \\* dt \\* Ft\\(i, j, k, 0\\) / \\(p0 / \\(cs \\* cs\\)\\)" "2D pressure-density force correction"
    if ! rg -Uq "void AmrCoreLBM::ComputeCf\\(int lev, int step\\)[\\s\\S]*?ComputeMacroForceCorrectedLevel\\(lev\\)" "$amr_cpp"; then
      printf 'missing: %s (%s)\n' "2D Cf uses force-corrected velocity" "$amr_cpp" >&2
      fail=1
    fi
  else
    require_pattern "$kernels" "0\\.5 \\* dt \\* Ft\\(i, j, k, 0\\) / rhot" "3D density force correction"
    require_pattern "$amr_h" "ComputeMacroForceCorrectedSlice" "3D force-corrected slice declaration"
    require_pattern "$amr_cpp" "ComputeMacroForceCorrectedSlice\\(lev, ix_mid\\)" "3D mean profile uses force-corrected slice"
  fi
done

exit "$fail"
