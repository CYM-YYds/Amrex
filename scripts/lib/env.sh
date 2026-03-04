#!/bin/bash
# env.sh
# ------------------------------------------------------------
# 环境与版本相关函数：
#   - load_environment                 加载 HPC module（MPI/CUDA/GCC）
#   - detect_amrex_release_from_project 从项目配置推断 AMReX 发布版本字符串
#
# 说明：
#   - 版本推断用于 AMREX_GIT_VERSION 的“稳定显示”模式（release）。
#   - 若推断失败，调用方会回退到 AMReX 默认 git describe 逻辑。

load_environment() {
	# 仅负责确保工具链可用，不修改编译参数策略。
	if ! command -v module >/dev/null 2>&1; then
		if [ -f /home/HPCBase/tools/module-5.2.0/init/profile.sh ]; then
			source /home/HPCBase/tools/module-5.2.0/init/profile.sh
		elif [ -f /etc/profile.d/modules.sh ]; then
			source /etc/profile.d/modules.sh
		fi
	fi

	module purge || true
	module use /home/HPCBase/modulefiles/ || true
	module load mpi/hmpi/1.2.0_bs2.4.0_sp1 || true
	module load compilers/cuda/12.1.0 || true
	module load compilers/gcc/10.3.1 || true
	export USE_CUDA=1 || true
	export CUDA_HOME=/home/HPCBase/compilers/cuda/12.1.0 || true

	command -v nvcc >/dev/null 2>&1 || echo "警告: nvcc 未找到，可能无法进行 CUDA 构建。" >&2
	command -v mpirun >/dev/null 2>&1 || echo "警告: mpirun 未找到，MPI 可能不可用。" >&2
}

detect_amrex_release_from_project() {
	# 优先级：
	# 1) AMREX_GIT_VERSION_OVERRIDE（用户显式覆盖）
	# 2) 从 AMREX_HOME 路径名提取版本（如 amrex-26.01）
	# 3) 从 CHANGES.md/CHANGES 提取版本号
	local amrex_home="${AMREX_HOME:-}"
	local line=""

	if [[ -z "$amrex_home" ]]; then
		if [[ -f "$PROJECT_ROOT/config/GNUmakefile" ]]; then
			line="$(awk -F'\\?=' '/^[[:space:]]*AMREX_HOME[[:space:]]*\\?=/{print $2; exit}' "$PROJECT_ROOT/config/GNUmakefile" | xargs || true)"
			amrex_home="$line"
		fi
	fi
	if [[ -z "$amrex_home" ]]; then
		if [[ -f "$PROJECT_ROOT/GNUmakefile" ]]; then
			line="$(awk -F'\\?=' '/^[[:space:]]*AMREX_HOME[[:space:]]*\\?=/{print $2; exit}' "$PROJECT_ROOT/GNUmakefile" | xargs || true)"
			amrex_home="$line"
		fi
	fi

	amrex_home="${amrex_home%/}"

	if [[ -n "$AMREX_GIT_VERSION_OVERRIDE" ]]; then
		printf '%s\n' "$AMREX_GIT_VERSION_OVERRIDE"
		return 0
	fi

	local base="$(basename "$amrex_home" 2>/dev/null || true)"
	if [[ "$base" =~ ([0-9]{2}\.[0-9]{2}(\.[0-9]+)?) ]]; then
		printf '%s\n' "${BASH_REMATCH[1]}"
		return 0
	fi

	if [[ -f "$amrex_home/CHANGES.md" ]]; then
		grep -o -m 1 -E "[0-9]{2}\.[0-9]{2}(\.[0-9]+)?" "$amrex_home/CHANGES.md" || true
		return 0
	fi
	if [[ -f "$amrex_home/CHANGES" ]]; then
		grep -o -m 1 -E "[0-9]{2}\.[0-9]{2}(\.[0-9]+)?" "$amrex_home/CHANGES" || true
		return 0
	fi
	return 0
}
