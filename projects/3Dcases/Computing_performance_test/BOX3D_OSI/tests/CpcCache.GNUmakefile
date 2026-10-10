# 通信缓存逐值测试使用独立构建目录，避免覆盖生产目标文件。
AMREX_HOME ?= $(abspath $(TEST_DIR)/../../../../../amrex-26.06)
EBASE = cpc_cache
DIM = 3
COMP = gnu
DEBUG = FALSE
BL_NO_FORT = TRUE
USE_MPI = TRUE
USE_OMP = FALSE
USE_CUDA ?= TRUE
USE_PARTICLES = FALSE
CXXSTD = c++20
include $(AMREX_HOME)/Tools/GNUMake/Make.defs
include $(AMREX_HOME)/Src/Base/Make.package
CEXE_sources += cpc_cache_test.cpp
INCLUDE_LOCATIONS += $(TEST_DIR)/../src
VPATH_LOCATIONS += $(TEST_DIR)
include $(AMREX_HOME)/Tools/GNUMake/Make.rules
