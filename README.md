# ReikOS

A Unix-inspired OS kernel simulator written in C, implementing core operating system concepts including multi-level queue scheduling, paged virtual memory, multi-core CPU simulation, and a syscall interface.

Built for the CO2018 course at HCMC University of Technology (VNU-HCM).

---

## Features

* **Multi-Level Queue (MLQ) Scheduler** — 140 priority levels with time-slice based preemption. Processes are dispatched by priority and run for configurable time quanta.
* **Paged Virtual Memory** — 22-bit virtual address space with 256B pages. Supports RAM + up to 4 swap partitions with page-in/page-out.
* **64-bit Address Extension (MM64)** — Optional 5-level page table (PGD → P4D → PUD → PMD → PT) for 64-bit address mode.
* **Multi-core CPU** — Each CPU core runs as a POSIX thread, synchronized through a shared timer and mutex-protected run queues.
* **Process Loader** — Loads process images from config files with configurable arrival times and priorities.
* **Syscall Interface** — Syscall table with memory management syscalls (`alloc`, `free`, `read`, `write`).

---

## Project Structure

```
ReikOS/
├── src/               # Kernel source files
│   ├── os.c           # Main kernel: boot, CPU threads, loader thread
│   ├── mm64.c         # Memory management (64-bit, 5-level page table)
│   ├── mm-memphy.c    # Physical memory (RAM/swap) management
│   ├── sched.c        # MLQ process scheduler
│   ├── syscall.c      # Syscall dispatch
│   └── ...
├── include/           # Header files
│   ├── os-cfg.h       # Build-time feature flags
│   ├── common.h       # Shared types and address layout
│   └── ...
├── input/             # Simulation config files
├── test/              # Test output logs
├── Makefile
└── run.sh

```

---

## Building & Running

**Requirements:** GCC, GNU Make, pthreads

```bash
# Build the kernel and tools
make

```

### Basic Execution

```bash
./os <config_name>

```

The config file is looked up under `input/`. Example: `./os os_1_mlq_paging`

---

## Testing & Validation

ReikOS includes a suite of test scenarios to validate specific kernel subsystems. It is recommended to redirect output to the `test/` directory for analysis.

### 1. Memory Subsystem Tests

These verify 64-bit paging structures, allocation logic, and the kernel slab allocator (kcache).
### Use this config in include/os-cfg.h
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
| **Basic Alloc** | `./os mem_alloc > test/mem_alloc.txt` | Verifies user-space `alloc` and `free` syscalls. |
| **Kmalloc** | `./os mem_kmalloc > test/mem_kmalloc.txt` | Tests internal kernel-space memory allocation. |
| **I/O Ops** | `./os mem_read_write_copy > test/mem_rwc.txt` | Validates data integrity across page boundaries. |
| **Kcache** | `./os mem_kcache > test/mem_kcache.txt` | Verifies the object cache (slab) allocator. |

### 2. Scheduler & Multi-core Tests

These cases test the MLQ algorithm across different CPU counts and queue configurations.

### Use this config in include/os-cfg.h
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
./os sched > test/sched.txt
./os sched_0 > test/sched_0.txt
./os sched_1 > test/sched_1.txt
./os sched_1q > test/sched_1q.txt

# Multi-core Scheduling
./os sched_2cpu_1q > test/sched_2cpu_1q.txt
./os sched_5cpu_1q > test/sched_5cpu_1q.txt

```

### 3. Visualizing Scheduling (Gantt Charts)

Use the `gantt` utility to parse the generated log files into a visual timeline:

```bash
./gantt test/sched.txt
./gantt test/sched_0.txt
./gantt test/sched_1.txt
./gantt test/sched_1q.txt
./gantt test/sched_2cpu_1q.txt
./gantt test/sched_5cpu_1q.txt

```

---

## Memory Model

### Virtual Address Layout (22-bit)

```
[ Page Number (14 bits) | Page Offset (8 bits) ]

```

Page size: **256 bytes** | Max virtual pages: **16,384**

### 64-bit Mode (MM64)

Five-level page table: **PGD → P4D → PUD → PMD → PT**, matching the Linux x86-64 kernel structure.

---

## Build-time Flags (`include/os-cfg.h`)

| Flag | Description |
| --- | --- |
| `MLQ_SCHED` | Enable Multi-Level Queue scheduler |
| `MM_PAGING` | Enable paged memory management |
| `MM64` | Use 64-bit 5-level page table mode |
| `PAGETBL_DUMP` | Dump page table state to stdout |

---

## License

Copyright is reserved by the faculty staff members involved in code development. Licensees are granted personal permission to use and modify the source code solely for studying in course CO2018 at HCMUT.