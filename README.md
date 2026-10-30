# ReikOS

A Unix-inspired OS kernel simulator written in C. It implements core operating system concepts: multi-level queue scheduling, paged virtual memory, multi-core CPU simulation, a syscall interface and, new this semester, a DMA co-processor modelled in RTL with Verilator.

Built for the CO2018 course at HCMC University of Technology (VNU-HCM).

---

## Features

* **Multi-Level Queue (MLQ) Scheduler**: 140 priority levels with time-slice preemption. Processes are dispatched by priority and run for a configurable time quantum.
* **Paged Virtual Memory**: 22-bit virtual address space with 256B pages. Supports RAM plus up to 4 swap partitions, with page-in and page-out.
* **64-bit Address Extension (MM64)**: an optional 5-level page table (PGD → P4D → PUD → PMD → PT) for 64-bit address mode.
* **Separate user and kernel space**: user programs use `alloc`, `read` and `write`. The kernel side uses `kmalloc`, `copy_from_user` and `copy_to_user`.
* **Multi-core CPU**: each CPU core runs as a POSIX thread. Cores are synchronized through a shared timer and mutex-protected run queues.
* **Process Loader**: loads process images from config files, with configurable arrival times and priorities.
* **Syscall Interface**: a syscall table with memory-management syscalls (`listsyscall`, `memmap`).
* **DMA Co-processor (new)**: the CPU starts a transfer and keeps running other work while the DMA engine copies memory over the system bus. The engine raises an interrupt when the copy is done. Programs use it through the `dma_memcpy` instruction. The DMA chip is simulated in RTL with Verilator (64-bit addresses, 32-bit data).

---

## Project Structure

```
ReikOS/
├── src/                    # Kernel source files
│   ├── os.c                # Main kernel: boot, CPU threads, loader thread
│   ├── sched.c             # MLQ process scheduler
│   ├── mm64.c              # Memory management (64-bit, 5-level page table)
│   ├── mm-memphy.c         # Physical memory (RAM/swap) management
│   ├── syscall.c           # Syscall dispatch
│   ├── sysbus.c            # System bus between the CPU, memory and the DMA
│   ├── dpi_rtl.cpp         # DPI bridge between C and the Verilator DMA model
│   ├── dma_c_testbench.cpp # DMA driver (library mode) and standalone testbench
│   ├── Vdma_top*.cpp       # Verilator-generated DMA model (do not edit)
│   └── ...
├── include/                # Header files
│   ├── os-cfg.h            # Build-time feature flags
│   ├── common.h            # Shared types and address layout
│   ├── dma.h, sysbus.h, dpi_rtl.h
│   ├── Vdma_top*.h         # Verilator-generated headers
│   └── ...
├── input/                  # Simulation config files
├── output/                 # Sample outputs
├── test/                   # Test output logs
├── Makefile
└── run.sh
```

---

## Building & Running

**Requirements:** GCC/G++, GNU Make, pthreads and Verilator. The Makefile looks for Verilator in `/usr/share/verilator`, `/usr/local/share/verilator` or `/opt/homebrew/share/verilator`. To use a different location, set `VERILATOR_ROOT`.

```bash
make os     # build the full OS simulation -> ./os
make dma    # build only the DMA module    -> ./dma_testbench
```

### Basic Execution

```bash
./os <config_name>
```

The simulator looks up the config file under `input/`.

### DMA Testbench

```bash
./dma_testbench                       # default transfer
./dma_testbench 0x1000 0x2000 1024    # <src> <dst> <size>
```

---

## Testing & Validation

ReikOS includes test scenarios that check specific kernel subsystems. Redirect each run's output to the `test/` directory so you can analyse it.

### 1. Memory Subsystem Tests

These tests check the 64-bit paging structures, the allocation logic and the kernel slab allocator (kcache).

Set these flags in `include/os-cfg.h`:

```c
#define MM_PAGING
//#define MM_FIXED_MEMSZ
//#define VMDBG 1
//#define MMDBG 1
//#define IODUMP 1
//#define PAGETBL_DUMP 1
#define MEMLOG 1
```

| Test Case | Command | Summary |
| --- | --- | --- |
| **Basic Alloc** | `./os mem_alloc > test/mem_alloc.txt` | Checks the user-space `alloc` and `free` syscalls. |
| **Kmalloc** | `./os mem_kmalloc > test/mem_kmalloc.txt` | Tests kernel-space memory allocation. |
| **I/O Ops** | `./os mem_read_write_copy > test/mem_rwc.txt` | Checks that data stays intact across page boundaries. |
| **Kcache** | `./os mem_kcache > test/mem_kcache.txt` | Checks the object cache (slab) allocator. |

### 2. Scheduler & Multi-core Tests

These tests run the MLQ algorithm with different CPU counts and queue configurations.

Set these flags in `include/os-cfg.h`:

```c
#define MM_PAGING
//#define MM_FIXED_MEMSZ
//#define VMDBG 1
//#define MMDBG 1
//#define IODUMP 1
//#define PAGETBL_DUMP 1
//#define MEMLOG 1
```

```bash
# Single CPU Scheduling
./os sched_0 > test/sched_0.txt
./os sched_1 > test/sched_1.txt
./os sched_1q > test/sched_1q.txt

# Multi-core Scheduling
./os sched > test/sched.txt
./os sched_2cpu_1q > test/sched_2cpu_1q.txt
./os sched_5cpu_1q > test/sched_5cpu_1q.txt
```

### 3. Visualizing Scheduling (Gantt Charts)

Use the `gantt` utility to turn the log files into a visual timeline:

```bash
./gantt test/sched.txt
./gantt test/sched_0.txt
./gantt test/sched_1.txt
./gantt test/sched_1q.txt
./gantt test/sched_2cpu_1q.txt
./gantt test/sched_5cpu_1q.txt
```

---

## License

See [LICENSE](LICENSE) — educational use only for CO2018.
