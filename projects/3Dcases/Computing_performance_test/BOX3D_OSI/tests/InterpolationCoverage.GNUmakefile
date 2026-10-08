# 仅构建 AMReX Base 与几何覆盖测试，使用独立构建目录，不覆盖生产可执行文件。
AMREX_HOME ?= $(abspath $(TEST_DIR)/../../../../../amrex-26.06)
EBASE = interpolation_coverage
DIM = 3
COMP = gnu
DEBUG = FALSE
BL_NO_FORT = TRUE
USE_MPI = FALSE
USE_OMP = FALSE
USE_CUDA = FALSE
USE_PARTICLES = FALSE

include $(AMREX_HOME)/Tools/GNUMake/Make.defs
include $(AMREX_HOME)/Src/Base/Make.package

CEXE_sources += interpolation_coverage_test.cpp
CEXE_headers += InterpolationCoverage.H
INCLUDE_LOCATIONS += $(TEST_DIR)/../src
VPATH_LOCATIONS += $(TEST_DIR) $(TEST_DIR)/../src

include $(AMREX_HOME)/Tools/GNUMake/Make.rules
