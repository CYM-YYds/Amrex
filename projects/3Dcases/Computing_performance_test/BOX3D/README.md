# BOX3D AMR Performance Case

This case is used for 3D AMR-LBM performance experiments around the BOX3D
setup. It uses the AMReX `MultiFab` data layout, MPI, CUDA, particle support,
and the recursive `JaberCycle()` advance path.

## Build

Run from this case directory:

```bash
./scripts/compile.sh
```

The case-local script wraps the repository build flow and writes compile logs
under `logs/compile/`.

## Runtime Path

The current fixed-body path in `src/main.cpp` calls:

```text
JaberCycle(0, cur_time, lid)
```

`JaberCycle()` advances levels with `nghost = lid.ghostCells()`, so
`Collide()`, `Stream()`, and `SwapLevel()` are executed on grown tile boxes.
Coarse-fine data transfer still goes through:

```text
FillGhostLevel() -> FillDdfPatch() -> FillPatchTwoLevels()
AverageDownGhostLevel() -> average_down()
```

## Performance Log Fields

The solver prints a 1000-step window summary:

```text
perf(s): interp=... collide=... stream=... average=... comm=... boundary=... swap=...
```

The `interp` and `average` totals are intentionally broad. Use the detail line
to identify the actual source of cost:

```text
perf_detail(s): interp_scale=... interp_fillpatch=... average_alloc=... average_copy=... average_scale=... average_down=...
```

Field meanings:

- `interp_scale`: LBM non-equilibrium scaling on the coarse level before coarse-to-fine fill.
- `interp_fillpatch`: AMReX `FillPatchTwoLevels()` coarse-fine fill.
- `average_alloc`: construction of the temporary fine-side `MultiFab` used for restriction.
- `average_copy`: `MultiFab::Copy()` from the fine level into that temporary `MultiFab`.
- `average_scale`: LBM non-equilibrium scaling before fine-to-coarse restriction.
- `average_down`: AMReX `average_down()` restriction.

The count line records call volume and approximate kernel work:

```text
perf_count: fillghost_calls=... avgdown_calls=... interp_scale_cells=... average_scale_cells=...
```

Because these detail timers synchronize the GPU at each measured stage, use
them for attribution within the same instrumented build. Do not compare their
absolute timings directly against older logs from uninstrumented builds.

## Deep Profiling And Charts

The case has a single-GPU TinyProfiler launcher for resolving the internal
AMReX `FillPatchTwoLevels()` cost:

```bash
./scripts/compile.sh
dsub -s ./scripts/submit_tiny_profile.sh
```

It requires `TINY_PROFILE = TRUE` in `config/GNUmakefile` and runs a
synchronized 1000-step window using `main3d.gnu.TPROF.MPI.CUDA.ex`. The
synchronization makes the report suitable for attribution, not production
throughput comparison.

For the profiling workflow, measured results, and the reproducible pie chart,
read `docs/performance_profiling.md`.
