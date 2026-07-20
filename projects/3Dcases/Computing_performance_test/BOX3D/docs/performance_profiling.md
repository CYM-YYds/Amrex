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

## Current Full-Run Baseline: Job 571393

`logs/submit/571393-out.log` completed the 64,000-step cavity case on one
GPU. It contains 64 independent 1000-step windows. The values below are sums
over those windows, not the final `step64000` line alone.

| Quantity | Accumulated time | Share of `JaberCycle2` |
|---|---:|---:|
| `JaberCycle2` | 10376.60 s | 100.00% |
| `Compute total` | 10451.08 s | - |
| `regrid_time` | 74.41 s | 0.72% |
| Collide | 3080.94 s | 29.69% |
| Interp | 2098.61 s | 20.22% |
| Average | 2049.56 s | 19.75% |
| Stream | 1763.04 s | 16.99% |
| Boundary | 882.91 s | 8.51% |
| Comm | 496.85 s | 4.79% |
| Swap | 4.34 s | 0.04% |

`Compute total` is measured from immediately before regridding to immediately
after `JaberCycle2()`. It therefore equals `regrid_time + JaberCycle_time`
plus the small host-side cost of computing the weighted-update metric and
loop/timer bookkeeping. Plot output is outside this interval. In this run the
residual was about 1--2 ms per 1000-step window.

`Interp + Average = 4148.17 s`, or 39.97% of `JaberCycle2`, is a useful
derived transfer subtotal. It must not be added to the phase rows above,
because it is composed of two of them.

The detailed transfer timers are synchronized nested timers. Their totals are
useful for attribution but are not a strict partition of `Interp` or
`Average`:

| Nested operation | Accumulated time |
|---|---:|
| `interp_fillpatch` | 1693.40 s |
| `interp_scale` | 435.95 s |
| `average_copy` | 700.81 s |
| `average_scale` | 741.09 s |
| `average_down` | 586.17 s |
| `average_alloc` | 14.68 s |

The run used `TINY_PROFILE = TRUE`, so its throughput is a profiling baseline,
not an unsynchronized production-speed benchmark. The case reports a
weighted `MLUPS_total` of 370.13 over all 64 windows.

### Performance Overview Plot

Generate a six-panel overview from any complete submit log:

```bash
python3 scripts/plot_run_performance.py logs/submit/571393-out.log \
  --output docs/571393_performance_overview.png
```

The checked-in output is
[`571393_performance_overview.png`](571393_performance_overview.png). The
fourth panel uses `average_scale_cells` as a proxy for repeated AMR transfer
work, not as the instantaneous number of valid mesh cells. In job 571393 it
has Pearson correlation 0.9994 with Stream time; this shows that both follow
the AMR coverage, not that restriction directly causes Stream to slow down.

### Scope Of The Jaber A6 Comparison

Jaber et al.'s A6 cavity test and this case share the broad target of a
`Re=1000`, `64^3`, D3Q27, double-precision, four-level cavity calculation
with regridding every 32 coarse steps. Both use linear coarse-to-fine spatial
interpolation. They are not direct performance peers: A6 is a single-GPU,
fixed-`4^3` block, GPU-native octree solver with interface-only restriction
and in-place shared-memory streaming. BOX3D uses AMReX patches, generic
`FillPatchTwoLevels()`/`average_down()`, coarse-level covered/interface masks,
and dual-MultiFab pull streaming. Its masks skip most covered coarse Collide and
Stream work through per-cell branches, but they are not Jaber's fine-level
`cells_ID_mask` and restriction still covers the fine valid patch. Match mesh
coverage, Mach number, refinement criterion, and active-node counting before
comparing MLUPS.

## Boundary Work-Box Experiment: Job 572280

`Boundary()` originally launched over every valid cell and let
`fill_boundary()` reject interior cells. The revised implementation caches
disjoint physical-boundary Box lists after mesh construction/regrid and launches
only those boxes. Job `572280` is a one-GPU, 1000-step run of this revision;
job `571805` is the immediately preceding one-GPU comparison run.

| Quantity | Job 571805 | Job 572280 | Change |
|---|---:|---:|---:|
| Boundary | 17.3817 s | 7.8342 s | -54.93% (2.219x speedup) |
| `JaberCycle2` | 187.1537 s | 161.3041 s | -13.81% |
| Compute total | 188.2410 s | 162.3385 s | -13.76% |
| `MLUPS_total` | 343.64 | 398.70 | +16.02% |

The new counters report `boundary_full_cells=69,021,204,480` and
`boundary_launch_cells=2,552,369,464`; the kernel launch region is therefore
3.698% of the former full-valid-cell region, a 96.302% geometric reduction.
The transfer work counters are close between the two runs
(`interp_scale_cells` differs by about 1.0%, while `average_scale_cells` differs
by about 0.06%), but this is not a controlled isolated-kernel benchmark: mesh
evolution and other phase times also differ. Attribute the 2.219x Boundary
improvement directly to this experiment; treat the total-time change as an
observed run-level result rather than a pure Boundary contribution.

The revision was also exercised by two-GPU, 64-step smoke job `572281`. The
test completed without an AMReX abort or MPI/CUDA failure; strict numerical
equivalence still requires field norms or a reference profile comparison.

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

## Further Measurement

The next useful measurement is level-indexed timing for levels 1, 2, and 3:
record transfer time, box count, valid cells, grown-box cells, covered cells,
and interface cells for both Interp and Average. The coarse covered/interface
masks now exist, but Collide and Stream still launch broad boxes and branch per
cell. Use the level data to decide whether regrid-cached active work regions
reduce enough work to offset extra kernel launches. After that, compare a
valid-only specialized restriction kernel and a narrowed interpolation-scaling
work list that includes the conservative-linear stencil halo.
