# 9_LES SGS viscosity models

This note records the SGS viscosity helpers in `projects/3Dcases/ChannelFlowtest/9_LES`.
It is a code-facing reference for `src/Kernels.H` and `src/AmrCoreLBM.cpp`.

## Current call path

`AmrCoreLBM::ComputeViscositysgs(amrex::Real cur_time)` fills velocity ghost cells before
computing the SGS viscosity field:

```text
ComputeViscositysgs(cur_time)
  -> FillMacroGhostLevel(lev, cur_time)
  -> ComputeViscositysgsLevel(lev)
     -> compute_viscosity_sgs_CSM(...)
```

`ComputeViscositysgsLevel()` passes the domain low/high indices and periodicity from
`Geom(lev)` into the kernel. The finite-difference helper uses centered differences in
periodic directions and in non-periodic interiors; it uses one-sided differences on
non-periodic physical boundaries.

The active time-stepping path still calls `collide_cumulant(...)`. That collision kernel
accepts the `viscosity_sgs` field but does not currently use it to modify the relaxation
parameters. The older BGK-style `collide(...)` path contains an SGS relaxation-time form,
but it is not the active collision path.

## Model helpers

### `compute_viscosity_sgs_CSM`

This is the coherent-structure model helper currently called by `ComputeViscositysgsLevel()`.
It computes the velocity-gradient tensor from `velocity` and then forms:

```text
S_ij = 0.5 * (du_i/dx_j + du_j/dx_i)
|S| = sqrt(2 S_ij S_ij)
Q = -0.5 * tr(grad(u)^2)
E =  0.5 * grad(u)_ij grad(u)_ij
F = clamp(Q / E, -1, 1)
C = (1 / 22) * |F|^1.5 * (1 - F)
nu_sgs = C * Delta^2 * |S|
```

The filter width is represented by the level cell size `dx`.

### `compute_viscosity_sgs_SM`

This is a standard Smagorinsky backup helper. It uses only the local distribution
functions at one Euler cell and computes the strain-rate tensor from the non-equilibrium
second moment:

```text
Pi_neq_ab = sum_i e_i,a e_i,b (f_i - f_i^eq)
S_ab = -Pi_neq_ab / (2 rho c_s^2 tau dt)
nu_sgs = Cs^2 * Delta^2 * sqrt(2 S_ab S_ab)
```

The current constant is `Cs^2 = 0.01`. Because this helper does not use velocity
finite differences, it does not need velocity ghost cells.

### `compute_viscosity_sgs_WALE`

This is a WALE backup helper. It uses the same velocity-gradient input pattern as CSM
and reuses `velocity_derivative(...)` for boundary-aware derivatives:

```text
g_ij = du_i/dx_j
S_ij = 0.5 * (g_ij + g_ji)
S^d_ij = 0.5 * (g_ik g_kj + g_jk g_ki) - (1/3) delta_ij tr(g^2)
nu_sgs = (Cw Delta)^2 * (S^d_ij S^d_ij)^1.5
         / ((S_ij S_ij)^2.5 + (S^d_ij S^d_ij)^1.25)
```

The current constant is `Cw = 0.325`.

## Naming

The current helper names are intentionally model-explicit:

```text
compute_viscosity_sgs_CSM
compute_viscosity_sgs_SM
compute_viscosity_sgs_WALE
```

They are longer than generic names, but they make model selection at the call site
unambiguous. If a runtime model switch is added later, keep these helpers as model-specific
implementation functions and place the dispatch logic outside them.

## Verification

Use the case build script after changing any SGS helper:

```bash
cd projects/3Dcases/ChannelFlowtest/9_LES
./scripts/compile.sh
```
