#!/usr/bin/env bash
# 本次融合的临时 GPU 对照构建；保留为实验复现材料，在编译节点执行。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
for dim in 2 3; do
    nvcc -std=c++20 --expt-extended-lambda --expt-relaxed-constexpr --use_fast_math --relocatable-device-code=true -arch=sm_80 \
        -I"tmp_build_dir/s/${dim}d.gnu.TPROF.MPI.CUDA.EXE" -Isrc \
        -I../../../../amrex-26.06/Src/Base -I../../../../amrex-26.06/Src/Boundary \
        -I/home/HPCBase/mpi/hmpi/1.2.0-huawei-sp1/include \
        scripts/temporary/transfer_restriction_gpu.cu \
        -L/home/HPCBase/mpi/hmpi/1.2.0-huawei-sp1/lib -lmpi \
        -o "runs/temporary/ab_osi_transfer_fusion/candidate/restriction${dim}d.ex" \
        > "runs/temporary/ab_osi_transfer_fusion/candidate/unit_build${dim}d.log" 2>&1
done
