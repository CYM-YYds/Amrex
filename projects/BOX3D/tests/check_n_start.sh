#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
kernel_file="$repo_root/projects/BOX3D/src/Kernels.H"

if ! rg -n 'constexpr int n_start = -4;' "$kernel_file" >/dev/null; then
    echo "expected state_error_6 to use n_start = -4"
    exit 1
fi

echo "n_start is set to -4"
