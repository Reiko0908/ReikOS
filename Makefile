# ReikOS - Simple OS simulator for CO2018 (HCMUT)
# Based on the demacOprx release by pdnguyen, HCMC University of Technology VNU-HCM.
# Licensed only for studying in course CO2018.

INC = -Iinclude
LIB = -lpthread

SRC = src
OBJ = obj
INCLUDE = include
TEST = test

CC = gcc
CXX = g++
DEBUG = -g
CFLAGS   = -Wall -std=gnu11 -c $(DEBUG) -pthread
CXXFLAGS = -Wall -std=gnu++14 -c $(DEBUG) -pthread
LFLAGS   = -Wall $(DEBUG) -pthread

vpath %.c $(SRC)
vpath %.cpp $(SRC)
vpath %.h $(INCLUDE)

# ---------------------------------------------------------------------------
# Verilator location (override with: make VERILATOR_ROOT=/path/to/verilator)
# ---------------------------------------------------------------------------
VERILATOR_ROOT ?= /usr/share/verilator
ifeq ($(shell uname -s),Darwin)
    ifeq ($(shell uname -m),arm64)
        CXXFLAGS = -Wall -std=c++14 -c $(DEBUG) -pthread
        ifneq ($(wildcard /usr/local/share/verilator/include/verilated.h),)
            VERILATOR_ROOT := /usr/local/share/verilator
        else
            VERILATOR_ROOT := /opt/homebrew/share/verilator
        endif
    else
        VERILATOR_ROOT ?= /usr/local/share/verilator
    endif
endif
export VERILATOR_ROOT

MAKE = $(CC) $(INC)
CXX_MAKE = $(CXX) -iquote include -I$(VERILATOR_ROOT)/include -I$(VERILATOR_ROOT)/include/vltstd

# ---------------------------------------------------------------------------
# Kernel objects
# ---------------------------------------------------------------------------
MEM_OBJ     = $(addprefix $(OBJ)/, paging.o mem.o cpu.o loader.o)
SYSCALL_OBJ = $(addprefix $(OBJ)/, syscall.o sys_mem.o sys_listsyscall.o)
OS_OBJ      = $(addprefix $(OBJ)/, cpu.o mem.o loader.o queue.o os.o sched.o timer.o mm-vm.o mm64.o mm.o mm-memphy.o libstd.o libmem.o memlog.o)
OS_OBJ     += $(SYSCALL_OBJ)
SCHED_OBJ   = $(addprefix $(OBJ)/, cpu.o loader.o)

# ---------------------------------------------------------------------------
# DMA co-processor objects (Verilator-generated model + C/C++ glue)
# ---------------------------------------------------------------------------
VDMA_CLASSES_FAST = Vdma_top Vdma_top___024root__DepSet_had36784a__0 Vdma_top___024root__DepSet_hbdce0907__0
VDMA_CLASSES_SLOW = Vdma_top___024root__Slow Vdma_top___024root__DepSet_had36784a__0__Slow Vdma_top___024root__DepSet_hbdce0907__0__Slow Vdma_top___024unit__Slow Vdma_top___024unit__DepSet_hf68f5ab9__0__Slow
VDMA_SUPPORT_FAST = Vdma_top__Dpi Vdma_top__Trace__0
VDMA_SUPPORT_SLOW = Vdma_top__Syms Vdma_top__Trace__0__Slow Vdma_top__TraceDecls__0__Slow
VDMA_GLOBAL_FAST  = verilated verilated_dpi verilated_vcd_c verilated_threads

VDMA_OBJ_FAST = $(addprefix $(OBJ)/, $(addsuffix .o, $(VDMA_CLASSES_FAST) $(VDMA_SUPPORT_FAST) $(VDMA_GLOBAL_FAST)))
VDMA_OBJ_SLOW = $(addprefix $(OBJ)/, $(addsuffix .o, $(VDMA_CLASSES_SLOW) $(VDMA_SUPPORT_SLOW)))

# OS build links the DMA driver in library mode (DMA_MAIN=0)
VDMA_USER_OBJ_OS  = $(OBJ)/dpi_rtl.o $(OBJ)/sysbus.o $(OBJ)/dma_c_testbench.o
# Standalone testbench has its own main() (DMA_MAIN=1)
VDMA_USER_OBJ_DMA = $(OBJ)/dpi_rtl.o $(OBJ)/sysbus.o $(OBJ)/dma_c_testbench_dma.o

VDMA_OBJ_OS  = $(VDMA_OBJ_FAST) $(VDMA_OBJ_SLOW) $(VDMA_USER_OBJ_OS)
VDMA_OBJ_DMA = $(VDMA_OBJ_FAST) $(VDMA_OBJ_SLOW) $(VDMA_USER_OBJ_DMA)

OS_OBJ += $(VDMA_OBJ_OS)

HEADER = $(wildcard $(INCLUDE)/*.h)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all clean

all: gantt os

# Gantt chart visualiser (standalone: reads a log file, needs no OS headers)
gantt: $(SRC)/gantt.c
	$(CC) $(LFLAGS) $(SRC)/gantt.c -o gantt

boundary_test: $(OBJ) $(OBJ)/mm64.o $(OBJ)/mm64_boundary_test.o
	$(CC) $(LFLAGS) $(OBJ)/mm64.o $(OBJ)/mm64_boundary_test.o -o boundary_test $(LIB)

# Just compile memory management modules
mem: $(MEM_OBJ)
	$(MAKE) $(LFLAGS) $(MEM_OBJ) -o mem $(LIB)

# Just compile scheduler
sched: $(SCHED_OBJ)
	$(MAKE) $(LFLAGS) $(MEM_OBJ) -o sched $(LIB)

# Standalone DMA testbench -> ./dma_testbench
dma: $(OBJ) $(VDMA_OBJ_DMA)
	$(CXX) $(LFLAGS) $(VDMA_OBJ_DMA) -o dma_testbench $(LIB)

# Generate the syscall table
syscalltbl.lst: $(SRC)/syscall.tbl
	@echo $(OS_OBJ)
	chmod +x $(SRC)/syscalltbl.sh
	$(SRC)/syscalltbl.sh $< $(SRC)/$@

# Full OS simulation -> ./os (linked with g++ because of the C++ DMA model)
os: $(OBJ) syscalltbl.lst $(OS_OBJ)
	$(CXX) $(LFLAGS) $(OS_OBJ) -o os $(LIB)

# ---------------------------------------------------------------------------
# Compile rules
# ---------------------------------------------------------------------------
$(OBJ)/%.o: %.c $(HEADER) | $(OBJ)
	$(MAKE) $(CFLAGS) $< -o $@

$(OBJ)/%.o: %.cpp $(HEADER) | $(OBJ)
	$(CXX_MAKE) $(CXXFLAGS) -DDEBUG_ENABLED=1 $< -o $@

$(OBJ)/dma_c_testbench.o: $(SRC)/dma_c_testbench.cpp $(HEADER) | $(OBJ)
	$(CXX_MAKE) $(CXXFLAGS) -DDEBUG_ENABLED=1 -DDMA_MAIN=0 $< -o $@

$(OBJ)/dma_c_testbench_dma.o: $(SRC)/dma_c_testbench.cpp $(HEADER) | $(OBJ)
	$(CXX_MAKE) $(CXXFLAGS) -DDEBUG_ENABLED=1 -DDMA_MAIN=1 $< -o $@

# Verilator runtime sources
$(OBJ)/verilated.o: $(VERILATOR_ROOT)/include/verilated.cpp | $(OBJ)
	$(CXX_MAKE) $(CXXFLAGS) -DDEBUG_ENABLED=1 $< -o $@

$(OBJ)/verilated_%.o: $(VERILATOR_ROOT)/include/verilated_%.cpp | $(OBJ)
	$(CXX_MAKE) $(CXXFLAGS) -DDEBUG_ENABLED=1 $< -o $@

$(OBJ):
	mkdir -p $(OBJ)

clean:
	rm -f $(SRC)/*.lst
	rm -f os sched mem pdg gantt boundary_test dma_testbench *.vcd
	rm -rf $(OBJ)
	rm -f $(TEST)/*.txt
