#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
kernel="${case_dir}/src/Kernels.H"

required_patterns=(
    "const int ix0 = static_cast<int>(amrex::Math::floor(xt))"
    "const int iy0 = static_cast<int>(amrex::Math::floor(yt))"
    "const int iz0 = static_cast<int>(amrex::Math::floor(zt))"
    "for (int d = -1; d <= 1; ++d)"
    "const int slot = d + 1"
    "amrex::Real wx[3]"
    "wx[slot] = delta3p(xt - (ix0 + d + 0.5))"
    "const int xx = ix0 + x"
    "int xx = ix0 + x"
    "for (int x = -1; x <= 1; ++x)"
    "for (int y = -1; y <= 1; ++y)"
    "for (int z = -1; z <= 1; ++z)"
    "const int sx = x + 1"
    "const amrex::Real wxy = wx[sx] * wy[sy]"
    "const amrex::Real IB_Interp = wxy * wz[sz]"
)

for pattern in "${required_patterns[@]}"; do
    if ! grep -Fq "${pattern}" "${kernel}"; then
        echo "missing optimized stencil pattern: ${pattern}" >&2
        exit 1
    fi
done

if grep -Fq "int ix[3], iy[3], iz[3]" "${kernel}"; then
    echo "stencil index arrays should not be present" >&2
    exit 1
fi

echo "MDF stencil optimization patterns found."
