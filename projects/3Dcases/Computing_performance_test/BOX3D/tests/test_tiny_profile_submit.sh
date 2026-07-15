#!/bin/bash

set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAKEFILE="${CASE_DIR}/config/GNUmakefile"
SUBMIT_SCRIPT="${CASE_DIR}/scripts/submit_tiny_profile.sh"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

rg -q '^TINY_PROFILE[[:space:]]*=[[:space:]]*TRUE$' "${MAKEFILE}" ||
    fail "TinyProfiler is not enabled in config/GNUmakefile"

[[ -f "${SUBMIT_SCRIPT}" ]] || fail "scripts/submit_tiny_profile.sh is missing"
bash -n "${SUBMIT_SCRIPT}" || fail "submit_tiny_profile.sh has invalid shell syntax"

rg -q '^#DSUB -R cpu=32;mem=49152;gpu=1$' "${SUBMIT_SCRIPT}" ||
    fail "the profiling job must request one GPU"
rg -q 'main3d\.gnu\.TPROF\.MPI\.CUDA\.ex' "${SUBMIT_SCRIPT}" ||
    fail "the profiling launcher must require the TPROF executable"
rg -q 'max_step=1000' "${SUBMIT_SCRIPT}" ||
    fail "the profiling run must cover one 1000-step timing window"
rg -q 'amr\.plot_int=100000' "${SUBMIT_SCRIPT}" ||
    fail "plot output must be disabled for the short run"
rg -q 'checkpoint\.chk_int=-1' "${SUBMIT_SCRIPT}" ||
    fail "checkpoint output must be disabled for the short run"
rg -q 'tiny_profiler\.device_synchronize_around_region=1' "${SUBMIT_SCRIPT}" ||
    fail "GPU synchronization around TinyProfiler regions must be enabled"
rg -q 'tiny_profiler\.print_threshold=0\.01' "${SUBMIT_SCRIPT}" ||
    fail "the TinyProfiler report threshold must retain small subregions"

echo "PASS: BOX3D TinyProfiler submission contract"
