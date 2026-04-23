#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$case_dir/src/LagrangeParticleContainer.cpp"

compute_cf_body="$(
    awk '
        /void LagrangeParticleContainer::ComputeCf\(/ { in_body = 1 }
        /void LagrangeParticleContainer::ComputeCf_from_force_pressure\(/ { in_body = 0 }
        in_body { print }
    ' "$src"
)"

if ! grep -q "Real tx = std::sin(theta);" <<< "$compute_cf_body"; then
    echo "ComputeCf must define tx with the same base tangent as ComputeCf_from_force_pressure." >&2
    exit 1
fi

if ! grep -q "Real ty = -std::cos(theta);" <<< "$compute_cf_body"; then
    echo "ComputeCf must define ty with the same base tangent as ComputeCf_from_force_pressure." >&2
    exit 1
fi

if ! grep -q "if (tx < 0.0)" <<< "$compute_cf_body"; then
    echo "ComputeCf must flip the tangent when tx < 0.0." >&2
    exit 1
fi

if ! grep -q "tx = -tx;" <<< "$compute_cf_body" || ! grep -q "ty = -ty;" <<< "$compute_cf_body"; then
    echo "ComputeCf must flip both tangent components." >&2
    exit 1
fi

if ! grep -q "std::abs(std::sin(th" <<< "$compute_cf_body"; then
    echo "ComputeCf must integrate Cd_v with the flipped tangent's x component, |sin(theta)|." >&2
    exit 1
fi
