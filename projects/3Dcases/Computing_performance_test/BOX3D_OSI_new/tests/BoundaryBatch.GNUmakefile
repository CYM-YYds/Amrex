# 独立构建边界批量执行测试，不覆盖生产目标文件。
AMREX_HOME ?= $(abspath $(TEST_DIR)/../../../../../amrex-26.06)
EBASE = boundary_batch
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
CEXE_sources += boundary_batch_test.cpp
CEXE_headers += boundary_batch_types.inc boundary_batch_impl.inc
INCLUDE_LOCATIONS += $(TEST_DIR)/../src
VPATH_LOCATIONS += $(TEST_DIR)
include $(AMREX_HOME)/Tools/GNUMake/Make.rules
