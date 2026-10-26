# 从生产源码提取数值函数，在独立目录测试，避免修改生产目标文件。
AMREX_HOME ?= $(abspath $(TEST_DIR)/../../../../../amrex-26.06)
EBASE = sparse_initialization
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
include $(AMREX_HOME)/Src/AmrCore/Make.package
include $(AMREX_HOME)/Src/Boundary/Make.package
CEXE_sources += sparse_initialization_test.cpp
CEXE_headers += sparse_initialization_impl.inc sparse_initialization_types.inc
INCLUDE_LOCATIONS += $(TEST_DIR)/../src
VPATH_LOCATIONS += $(TEST_DIR)
include $(AMREX_HOME)/Tools/GNUMake/Make.rules
