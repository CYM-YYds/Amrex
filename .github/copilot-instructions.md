# LBM-AMR Simulation Project - AI Agent Instructions

## Project Architecture Overview

This workspace contains multiple **AMReX-based** example projects for **LBM + AMR**, including cases like 2D/3D cavity, cylinder flows, channel flows, and particle/immersed-boundary coupling variants.

The code is organized as:
- **Per-case projects** under `projects/<case>/` (each case has its own `GNUmakefile`, `inputs`, `src/`, etc.)
- A **shared core** under `projects/shared/lbm-core/` that many cases include via `Make.package`

### Core Components (shared + per-case)
- **Per-case main**: `projects/<case>/src/main.cpp` (entry point, AMReX init, time loop glue)
- **Core AMR/LBM driver**: `AmrCoreLBM.{H,cpp}` (either from `projects/shared/lbm-core/` or copied/overridden in the case `src/`)
- **GPU kernels**: `Kernels.H` (core LBM kernels + coupling kernels)
- **Lattice model headers**:
    - 3D cases typically use `D3Q19.H`
    - 2D cases typically use `D2Q9.H` (some cases keep it in their own `src/`)
- **Particle / immersed boundary**: `LagrangeParticleContainer.{H,cpp}` (IBM/IDF/DF variants depending on case)
- **Case runtime config**: `projects/<case>/inputs`

### Key Design Patterns

#### 1. GPU Kernel Organization
All computational kernels in `Kernels.H` must have `AMREX_GPU_DEVICE` and `AMREX_FORCE_INLINE` decorators:
```cpp
AMREX_GPU_DEVICE AMREX_FORCE_INLINE
void compute_macro(int i, int j, int k, amrex::Array4<amrex::Real> const &fold,
                   amrex::Array4<amrex::Real> const &rho, amrex::Array4<amrex::Real> const &u)
```

#### 2. Multi-Level AMR Data Management
Use `MultiFab` arrays for multi-level data:
```cpp
amrex::Vector<amrex::MultiFab> f_old;  // Old distribution functions
amrex::Vector<amrex::MultiFab> f_new;  // New distribution functions
amrex::Vector<amrex::MultiFab> rho;    // Density fields
amrex::Vector<amrex::MultiFab> u;      // Velocity fields
```

#### 3. LBM Collision Models
Support multiple collision operators:
- **BGK**: `collide_bgk()` - Single relaxation time
- **Cumulant**: `collide_cumulant()` - Multiple relaxation times for better stability
- **Regularized**: `collide_regularized()` - Improved Galilean invariance

#### 4. Boundary Conditions
- **Wall boundaries**: `fill_boundary()` with bounce-back scheme
- **Periodic boundaries**: AMReX built-in periodic BC
- **Inlet/Outlet**: Density-driven pressure boundaries

## Build System & Dependencies

### Compilation Requirements
- **AMReX 23.09+**: Set `AMREX_HOME` environment variable
- **CUDA 11.7+**: For GPU acceleration
- **MPI**: For parallel execution
- **GCC 10.3+**: Compiler toolchain

### Build Configuration (GNUmakefile)
```makefile
AMREX_HOME ?= /path/to/amrex-23.09/
DEBUG = FALSE
TINY_PROFILE = TRUE  # Required for performance profiling
DIM = 2  # or 3 depending on the case
USE_MPI = TRUE
USE_PARTICLES = TRUE
USE_CUDA = TRUE
```

### Build Commands (per case)
```bash
# Example: build a specific case (run inside that case directory)
cd projects/Cylindertest/Cylinder2D_IDF
make -f GNUmakefile -j8

# Clean build artifacts
make -f GNUmakefile clean

# Rebuild from scratch
make -f GNUmakefile realclean
make -f GNUmakefile -j8
```

## Runtime Configuration

### Key Parameters (inputs file)
```ini
# Time stepping
max_step = 80000
stop_time = 2500000.0

# AMR configuration
amr.n_cell = 128 64 64      # Base grid resolution
amr.max_level = 2            # Maximum refinement levels
amr.ref_ratio = 2 2 2 2      # Refinement ratios

# LBM parameters
lbm.tau = 0.6                # Relaxation time
lbm.err = 0.6 0.8 1.0 1.2    # Refinement criteria

# Output control
amr.plot_int = 200           # Plot interval
amr.plot_file = case_name_   # Output file prefix (varies by case)
```

### Execution Commands (examples)
```bash
# Run from within a case directory after building.
# Executable name depends on DIM/MPI/CUDA/TINY_PROFILE/DEBUG options.

# Example (2D case)
mpirun -n 4 ./main2d.gnu.MPI.CUDA.ex inputs

# Example (3D case, name may differ)
mpirun -n 4 ./main3d.gnu.MPI.CUDA.ex inputs
```

## Development Workflow

### Adding New LBM Kernels
1. Define kernel function in `Kernels.H` with proper decorators
2. Implement collision/streaming logic following LBM patterns
3. Add kernel call in `AmrCoreLBM.cpp` using `ParallelFor`
4. Test on both CPU and GPU configurations

### Modifying Boundary Conditions
1. Update `fill_boundary()` function in `Kernels.H`
2. Handle 6 boundary faces (i=0, i=hi[0], j=0, j=hi[1], k=0, k=hi[2])
3. Implement appropriate bounce-back or pressure boundary schemes

### AMR Refinement Criteria
1. Define error estimation functions in `Kernels.H`
2. Use velocity gradients, density variations, or vorticity
3. Update `inputs` file with appropriate `lbm.err` values

## Common Patterns & Conventions

### Memory Management
- Use AMReX `MultiFab` for distributed arrays
- Always check `isValid()` before accessing MFIter
- Use `growntilebox()` for ghost cell access

### GPU Optimization
- Prefer `ParallelFor` over manual kernel launches
- Use `AMREX_GPU_DEVICE` functions for device code
- Minimize host-device memory transfers

### Error Handling
- Check CUDA errors with `AMREX_GPU_ERROR_CHECK()`
- Use AMReX assertion macros for debugging
- Monitor GPU memory usage in long simulations

### File Organization (current)
- `projects/<case>/`
    - `GNUmakefile` / `config/Make.package`: per-case build settings; many cases include `projects/shared/lbm-core/Make.package`
    - `src/`: per-case sources (may override shared core)
    - `inputs`: runtime parameters for that case
    - `scripts/`: run/submit helpers (HPC environments)
    - `tmp_build_dir/`, `logs/`, `plt*`: build and runtime outputs
- `projects/shared/lbm-core/`: shared driver + kernels + particle containers (common code reused by many cases)
- `tools/`: helper scripts (copy/sync/compile)
- `dev_profiles/`: editor/tooling profiles (clangd/cpptools)

## Integration Points

### External Dependencies
- **AMReX**: Core framework for AMR and parallel computing
- **CUDA**: GPU acceleration library
- **MPI**: Inter-process communication
- **HDF5**: Data output (optional)

### Cross-Component Communication
- `main.cpp` ↔ `AmrCoreLBM` ↔ `Kernels.H`: Main computation pipeline
- `AmrCoreLBM` ↔ `LagrangeParticleContainer`: Fluid-particle coupling
- `Kernels.H` ↔ `D3Q19.H`: LBM model parameters
- All components share `inputs` configuration

## Performance Considerations

### GPU Memory Layout
- Use AMReX `Array4` for efficient GPU memory access
- Prefer structure-of-arrays over array-of-structures
- Align data access patterns with GPU warp size

### Load Balancing
- AMReX handles automatic domain decomposition
- Monitor load imbalance with `amr.grid_eff` parameter
- Adjust `max_grid_size` for optimal GPU utilization

### Profiling
- Use `TINY_PROFILE=TRUE` for basic timing
- Enable `DEBUG=TRUE` for detailed diagnostics
- Monitor GPU utilization with `nvidia-smi`

## Key Files for Reference
- `projects/shared/lbm-core/Kernels.H`: core computational algorithms (GPU kernels)
- `projects/shared/lbm-core/AmrCoreLBM.cpp`: AMR/LBM integration driver (shared)
- `projects/shared/lbm-core/D3Q19.H`: common 3D lattice constants
- `projects/<case>/src/D2Q9.H`: common 2D lattice constants (where present)
- `projects/<case>/inputs`: runtime configuration for a case
- `projects/<case>/GNUmakefile`: build configuration for a case
