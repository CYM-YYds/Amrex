# LBM-AMR Repository Instructions For Copilot

Use `AGENTS.md` at the repository root as the primary project guide. It is intended to be the shared source of truth for AI assistants in this repository.

## High-Value Defaults

- This repo is case-centric, not a single-library codebase.
- Most work should happen inside a specific case directory under `projects/`.
- Current primary AMReX version is `amrex-26.01`.
- A representative case for orientation is `projects/Cylindertest/Cylinder2D_IDF/`.

## Files To Read First

When starting a new task, prefer this order:

1. `AGENTS.md`
2. `README.md`
3. The target case directory
4. `<case>/src/main.cpp`
5. `<case>/src/AmrCoreLBM.*`
6. `<case>/config/inputs`
7. `<case>/config/GNUmakefile`

## Practical Repo Notes

- Build from inside a case directory with `./scripts/compile.sh`.
- Submit jobs with `dsub -s ./scripts/submit.sh`.
- `config/GNUmakefile` usually defines `DIM`, `USE_MPI`, `USE_CUDA`, and `AMREX_HOME`.
- Checked-in outputs like executables, `.log`, `.dat`, `logs/`, and generated data are normal in this repository.
- Older docs may mention `tools/sync_projects.py`; the current script is `scripts/sync_projects.py`.

## Editing Guidance

- Keep changes scoped to the target case unless the task explicitly asks for shared behavior.
- Read `config/inputs` before changing physics parameters, AMR behavior, output cadence, or runtime settings.
- Inspect `scripts/compile.sh` before changing build workflow assumptions.
- Inspect case `scripts/submit.sh` before changing runtime launch behavior.
- Verify repository paths before relying on older AI-generated instructions; some historical path references are stale.
