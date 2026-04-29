# AI Collaboration Guide for This Repo

This repository contains AMReX-based LBM + AMR simulation cases. The main development model is case-centric: most work should happen inside a specific case directory under `projects/`, not from the repository root.

## Read This First

When starting a new task, read these files in order:

1. `README.md`
2. The target case directory under `projects/`
3. `<case>/src/main.cpp`
4. `<case>/src/AmrCoreLBM.H` and `<case>/src/AmrCoreLBM.cpp`
5. `<case>/config/inputs`
6. `<case>/config/GNUmakefile`
7. `scripts/compile.sh` if build behavior matters

For cylinder-flow work, a good default case is `projects/Cylindertest/Cylinder2D_IDF/`.

## Repository Shape

- `amrex-26.01/`: current primary AMReX source tree
- `amrex-23.09/`: older AMReX version kept for comparison or compatibility
- `projects/2Dcases/`, `projects/3Dcases/`, `projects/Cylindertest/`: simulation cases
- `projects/2Dshared/`, `projects/3Dshared/`: shared wrappers or templates
- `scripts/`: repo-level compile, sync, and clean helpers
- `后处理脚本/`: post-processing scripts and notes

Typical case layout:

- `src/`: main source code
- `config/`: `GNUmakefile`, `Make.package`, `inputs`, and often `compile_commands.json`
- `scripts/`: case-local wrappers for compile, clean, and submit
- `data/`, `logs/`, `tmp_build_dir/`: generated outputs and build artifacts

## Key Code Roles

- `src/main.cpp`: AMReX initialization, parameter read-in, time stepping, and output triggers
- `src/AmrCoreLBM.*`: AMR/LBM driver logic, data management, stepping, mesh refinement, diagnostics
- `src/Kernels.H`: performance-sensitive kernels and boundary/collision/streaming helpers
- `src/D2Q9.H` or `src/D3Q19.H`: lattice constants and stencil definitions
- `src/LagrangeParticleContainer.*`: particle or immersed-boundary style coupling when present

## Build And Run Workflow

Prefer operating from inside the target case directory.

Build:

```bash
./scripts/compile.sh
```

Submit on the cluster:

```bash
dsub -s ./scripts/submit.sh
```

Build and submit in one step:

```bash
./scripts/compile.sh --submit
```    

Important notes:

- The root `scripts/compile.sh` is the real build entrypoint; case-local compile scripts are wrappers.
- Build scripts assume an HPC environment and may jump to `whshare-agent-1`.
- Submission scripts assume DSUB, MPI, CUDA modules, and cluster-specific host setup.
- `config/GNUmakefile` is the main source of per-case build settings such as `DIM`, `USE_MPI`, `USE_CUDA`, and `AMREX_HOME`.

## Working Conventions For AI Agents

- Start from one concrete case and stay scoped to it unless the task clearly spans multiple cases.
- Treat checked-in outputs as normal for this repo: executables, `*-out.log`, `.dat`, `logs/`, and generated data may already exist.
- Do not assume a conventional unit-test layout; verification is often case build success or simulation behavior.
- Before making any code edit, first create a review baseline commit by running `git add -A` and `git commit -m "before codex"`.
- If `git commit -m "before codex"` reports there is nothing to commit, continue without creating a commit.
- If the baseline commit fails for another reason, stop and report the failure before editing files.
- Prefer reading `config/inputs` before changing physics, stepping, output cadence, or AMR behavior.
- If a task touches compilation behavior, inspect `scripts/compile.sh` and the target case `config/GNUmakefile`.
- If a task touches runtime launch behavior, inspect the case `scripts/submit.sh`.

## Current Repo-Specific Gotchas

- Some older docs mention `tools/sync_projects.py`, but the script currently lives at `scripts/sync_projects.py`.
- The repository does not behave like a single library with one entrypoint; each case can diverge.
- Some previously written AI instructions may mention paths like `projects/shared/lbm-core/` or `tools/`; verify before relying on them in this repo.
- Root `scripts/clean.sh` is broad and potentially destructive; inspect carefully before running it.

## Good Default Context To Carry Forward

Unless the user says otherwise, assume:

- Primary AMReX version is `amrex-26.01`
- Most active work is in `projects/Cylindertest/` or case-specific directories under `projects/`
- GPU + MPI builds are expected for representative production cases
- `config/inputs` and `src/main.cpp` are the fastest way to understand a case's runtime behavior
