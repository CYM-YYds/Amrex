# BOX3D Performance Profiling

## Purpose

This case measures coarse-fine AMR transfer costs in the recursive
`JaberCycle2()` path. The relevant data path is:

```text
FillGhostLevel -> FillDdfPatch -> FillPatchTwoLevels
AverageDownGhostLevel -> MultiFab::Copy -> average_scale -> average_down
```

The detailed case timers identify the high-level subphases. AMReX
TinyProfiler then resolves the nested operations inside `FillPatchTwoLevels()`.

## Build And Submit

`config/GNUmakefile` currently enables AMReX TinyProfiler:

```make
TINY_PROFILE = TRUE
```

Build the profiling executable and submit the one-GPU, 1000-step job from the
case root. Use the intended HPC compiler environment; AMReX requires GCC 8 or
newer:

```bash
./scripts/compile.sh
dsub -s ./scripts/submit_tiny_profile.sh
```

The submit script requires `main3d.gnu.TPROF.MPI.CUDA.ex`, suppresses plot and
checkpoint output, and passes:

```text
tiny_profiler.device_synchronize_around_region=1
tiny_profiler.print_threshold=0.01
```

The first option synchronizes the GPU at profiling-region boundaries. It gives
meaningful attribution for asynchronous CUDA kernels, but increases measured
wall time. Do not compare the resulting `JaberCycle_time` directly with a
normal `TINY_PROFILE = FALSE` run.

Validate the launch contract before submitting:

```bash
bash tests/test_tiny_profile_submit.sh
```

## Historical Profile Result

The completed `569665-tiny-profile.log` 1000-step run recorded:

| Operation | Time | Interpretation |
|---|---:|---|
| `FillPatchTwoLevels` inclusive | 54.01 s | dominant Interp subpath |
| `CellConservativeLinear::interp()` | 28.54 s | conservative coarse-to-fine interpolation in that historical build |
| `FillPatchSingleLevel` inclusive | 21.19 s | source fill, ghost fill, and physical-boundary work |
| `amrex::Copy()` | 13.15 s | fine-side temporary data copy before restriction |
| `amrex::average_down()` inclusive | 8.98 s | fine-to-coarse restriction |

This profile predates the current DDF-path selection of `cell_bilinear_interp` and the current
two-ghost stream changes. It remains evidence for the older conservative-mapper path, not a
direct benchmark for the present source tree.

The `FillPatchTwoLevels` wrapper itself has only 0.413 s exclusive time.
Optimization should therefore target interpolation work, boundary/ghost fills,
and patch granularity, not wrapper metadata. The temporary `MultiFab`
allocation is also not a primary target; its case-level timer was 0.15 s in
the same 1000-step window.

TinyProfiler counts include regridding work. In job `569665`, it recorded 7136
`FillPatchTwoLevels` calls while JaberCycle recorded 7000 `FillGhostLevel`
calls. The additional 136 calls come from regridding paths such as
`RemakeLevel()`. Similarly, 7087 `average_down` calls include 87 regridding
restrictions in addition to the 7000 JaberCycle calls.

## Pie Chart

Generate the three pie charts from the 64-window aggregated timing data:

```bash
python3 scripts/plot_transfer_cost_pies.py
```

The script requires `matplotlib` and writes
`docs/transfer_cost_pies.png`. The overall chart uses the supplied JaberCycle
total of 7608.30 s. The printed two-decimal categories sum to 7608.29 s due to
rounding.

The Interp chart removes 24.98 s of regridding transfer time from the raw
`FillDdfPatch` subphase before comparing it to the JaberCycle-only Interp
total. The Average chart retains a 3.30 s residual for temporary destruction,
loop overhead, and timer-boundary work, so each pie closes to its stated total.

## Next Measurement

The next useful measurement is level-indexed timing for levels 1, 2, and 3:
record transfer time, box count, valid cells, and grown-box cells for both
Interp and Average. Function-level profiling has already isolated the main
mechanisms; level-level data is needed before selecting a mesh-layout change
or a specialized LBM coarse-fine transfer implementation.
