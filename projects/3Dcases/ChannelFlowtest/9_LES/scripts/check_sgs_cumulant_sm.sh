#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
kernel="${case_dir}/src/Kernels.H"
doc="${case_dir}/docs/les_sgs_models.md"

required_kernel_patterns=(
    "void compute_viscosity_sgs_SM_cumulant"
    "const Real cneq_xx = C_200 - rhot * cs2"
    "const Real cneq_yy = C_020 - rhot * cs2"
    "const Real cneq_zz = C_002 - rhot * cs2"
    "const Real strain_factor = -1.0 / (2.0 * tau_2 * dt * rhot * cs2)"
    "void compute_viscosity_sgs_SM_BGK"
)

for pattern in "${required_kernel_patterns[@]}"; do
    if ! grep -Fq "${pattern}" "${kernel}"; then
        echo "missing cumulant SM kernel pattern: ${pattern}" >&2
        exit 1
    fi
done

required_doc_patterns=(
    "compute_viscosity_sgs_SM_cumulant"
    "C_ab^neq = -tau_2 dt rho cs2 (du_a/db + du_b/da)"
    "compute_viscosity_sgs_SM_BGK"
)

for pattern in "${required_doc_patterns[@]}"; do
    if ! grep -Fq "${pattern}" "${doc}"; then
        echo "missing cumulant SM doc pattern: ${pattern}" >&2
        exit 1
    fi
done

echo "Cumulant Smagorinsky SGS patterns found."
