#!/bin/bash
# DSUB headers must be in the script passed to dsub; keep them here
#DSUB --job_type cosched
#DSUB -n case_Cylinder3D_perf
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=4
#DSUB -N 1
#DSUB -o %J-out.log
#DSUB -e %J-out.log
#DSUB -l cuda122

set -euo pipefail

# PERF_MODE: stat | record
PERF_MODE="${PERF_MODE:-stat}"
# perf record sampling frequency
PERF_FREQ="${PERF_FREQ:-99}"
# 可执行程序与输入文件（优先 TPROF 产物）
if [[ -z "${APP_EXE:-}" ]]; then
	if [[ -x "./main3d.gnu.TPROF.MPI.CUDA.ex" ]]; then
		APP_EXE="./main3d.gnu.TPROF.MPI.CUDA.ex"
	else
		APP_EXE="./main3d.gnu.MPI.CUDA.ex"
	fi
fi
APP_INPUT="${APP_INPUT:-config/inputs}"

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.1.0
module load compilers/gcc/10.3.1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"

if [[ -z "${CCS_ALLOC_FILE:-}" ]]; then
	echo "ERROR: CCS_ALLOC_FILE is empty. This script must run inside dsub allocation."
	exit 1
fi

HOSTFILE="/tmp/hostfile.$$"
rm -f "${HOSTFILE}"
touch "${HOSTFILE}"

ntask=$(awk -v fff="${HOSTFILE}" '
{
    split($0, a, " ")
    if (length(a[1]) > 0 && length(a[3]) > 0) {
        print a[1]" slots="a[2] >> fff
        total_task += a[3]
    }
}
END { print total_task }
' "${CCS_ALLOC_FILE}")

echo "openmpi hostfile ${HOSTFILE} generated:"
cat "${HOSTFILE}"
echo "Total tasks is ${ntask}"

NGPUS_PER_NODE=1
if [[ -n "${CUDA_VISIBLE_DEVICES:-}" ]]; then
	NGPUS_PER_NODE=$(echo "${CUDA_VISIBLE_DEVICES}" | awk -F',' '{print NF}')
elif command -v nvidia-smi >/dev/null 2>&1; then
	NGPUS_PER_NODE=$(nvidia-smi -L 2>/dev/null | wc -l)
fi
if [[ -z "${NGPUS_PER_NODE}" || "${NGPUS_PER_NODE}" -lt 1 ]]; then
	NGPUS_PER_NODE=1
fi

echo "Detected GPUs per node: ${NGPUS_PER_NODE}"

echo "PERF_MODE=${PERF_MODE}, APP=${APP_EXE} ${APP_INPUT}"

RUNNER='rank=${OMPI_COMM_WORLD_RANK:-0}; lrank=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; host=$(hostname -s); export CUDA_VISIBLE_DEVICES=${lrank}; if ! command -v perf >/dev/null 2>&1; then echo "[WARN] perf not found on ${host}, rank ${rank}. Run app directly."; exec '"${APP_EXE}"' '"${APP_INPUT}"'; fi; if [[ '"${PERF_MODE}"' == "record" ]]; then out="perf_${host}_r${rank}.data"; echo "[perf record] ${out}"; exec perf record -F '"${PERF_FREQ}"' -g --call-graph dwarf -o "${out}" '"${APP_EXE}"' '"${APP_INPUT}"'; else out="perf_stat_${host}_r${rank}.log"; echo "[perf stat] ${out}"; exec perf stat -d -d -d -o "${out}" '"${APP_EXE}"' '"${APP_INPUT}"'; fi'

mpirun \
	-hostfile "${HOSTFILE}" \
	-npernode "${NGPUS_PER_NODE}" \
	-x PATH -x LD_LIBRARY_PATH \
	--mca plm_rsh_agent /opt/batch/agent/tools/dstart \
	bash -lc "${RUNNER}"

ret=$?
rm -f "${HOSTFILE}"
exit ${ret}
