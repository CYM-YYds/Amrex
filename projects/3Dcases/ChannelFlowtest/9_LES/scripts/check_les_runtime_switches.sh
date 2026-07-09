#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
amr_h="${case_dir}/src/AmrCoreLBM.H"
amr_cpp="${case_dir}/src/AmrCoreLBM.cpp"
main_cpp="${case_dir}/src/main.cpp"
kernels="${case_dir}/src/Kernels.H"
inputs="${case_dir}/config/inputs"

require_pattern() {
    local pattern="$1"
    local file="$2"
    local label="$3"
    if ! rg -q "${pattern}" "${file}"; then
        echo "missing ${label}: ${pattern}" >&2
        exit 1
    fi
}

reject_pattern() {
    local pattern="$1"
    local file="$2"
    local label="$3"
    if rg -q "${pattern}" "${file}"; then
        echo "unexpected ${label}: ${pattern}" >&2
        exit 1
    fi
}

require_pattern "bool use_les = false;" "${amr_h}" "runtime use_les default"
require_pattern "bool use_wall_model = false;" "${amr_h}" "runtime use_wall_model default"
require_pattern "pp\\.query\\(\"use_les\", params_\\.use_les\\);" "${amr_cpp}" "use_les ParmParse read"
require_pattern "pp\\.query\\(\"use_wall_model\", params_\\.use_wall_model\\);" "${amr_cpp}" "use_wall_model ParmParse read"
require_pattern "use_les[[:space:]]*=" "${amr_cpp}" "startup use_les print"
require_pattern "use_wall_model[[:space:]]*=" "${amr_cpp}" "startup use_wall_model print"
require_pattern "lbm\\.use_les[[:space:]]*=[[:space:]]*0" "${inputs}" "inputs use_les default"
require_pattern "lbm\\.use_wall_model[[:space:]]*=[[:space:]]*0" "${inputs}" "inputs use_wall_model default"

require_pattern "if \\(params_\\.use_les\\)" "${amr_cpp}" "conditional LES allocation path"
require_pattern "ComputeViscositysgs\\(cur_time\\)" "${main_cpp}" "JaberCycle SGS update"
require_pattern "compute_viscosity_sgs_WALE" "${amr_cpp}" "WALE SGS helper selection"
require_pattern "nu_sgs_local" "${kernels}" "scalar SGS collision argument"
require_pattern "tau_eff = tau_lev \\+ nu_sgs_local / \\(cs2 \\* dt\\)" "${kernels}" "cumulant tau_eff correction"
require_pattern "const amrex::Real nu_sgs_local = nu_sgs\\(i, j, k, 0\\);" "${amr_cpp}" "LES host branch SGS load"
require_pattern "collide_cumulant\\(i, j, k, fold, 0\\.0, Ft, tau_lev, dt, hi\\);" "${amr_cpp}" "non-LES host branch"
require_pattern "params_\\.use_les && params_\\.use_wall_model" "${amr_cpp}" "wall model gated by LES"
require_pattern "bool wall_model_valid = true;" "${kernels}" "APGPL fallback variable rename"

reject_pattern "bool use_wall_model = true;" "${kernels}" "local wall model variable name collision"
reject_pattern "viscosity_sgs_lev\\.define" "${amr_cpp}" "unconditional viscosity_sgs define"

echo "LES runtime switch structure looks consistent."
