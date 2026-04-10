# ReikOS

A Unix-inspired OS kernel simulator written in C, implementing core operating system concepts including multi-level queue scheduling, paged virtual memory, multi-core CPU simulation, and a syscall interface.

Built for the CO2018 course at HCMC University of Technology (VNU-HCM).

---

## Features

- **Multi-Level Queue (MLQ) Scheduler** — 140 priority levels with time-slice based preemption. Processes are dispatched by priority and run for configurable time quanta.
- **Paged Virtual Memory** — 22-bit virtual address space with 256B pages. Supports RAM + up to 4 swap partitions with page-in/page-out.
- **64-bit Address Extension (MM64)** — Optional 5-level page table (PGD → P4D → PUD → PMD → PT) for 64-bit address mode.
- **Multi-core CPU** — Each CPU core runs as a POSIX thread, synchronized through a shared timer and mutex-protected run queues.
- **Process Loader** — Loads process images from config files with configurable arrival times and priorities.
- **Syscall Interface** — Syscall table with memory management syscalls (`alloc`, `free`, `read`, `write`).

---

## Project Structure

```
ReikOS/
├── src/              # Kernel source files
│   ├── os.c          # Main kernel: boot, CPU threads, loader thread
│   ├── sched.c       # MLQ scheduler (get/put/add process)
│   ├── mm.c          # Memory management (32-bit paging, deprecated)
│   ├── mm64.c        # Memory management (64-bit, 5-level page table)
│   ├── mm-vm.c       # Virtual memory operations
│   ├── mm-memphy.c   # Physical memory (RAM/swap) management
│   ├── cpu.c         # CPU instruction execution
│   ├── loader.c      # Process image loader
│   ├── timer.c       # Global timer and event system
│   ├── sched.c       # Process scheduler
│   ├── queue.c       # Ready/run queue primitives
│   ├── syscall.c     # Syscall dispatch
│   ├── sys_mem.c     # Memory syscall implementations
│   └── ...
├── include/          # Header files
│   ├── os-cfg.h      # Build-time feature flags
│   ├── mm.h          # Paging constants and PTE bit definitions
│   ├── common.h      # Shared types and address layout
│   └── ...
├── input/            # Simulation config files
│   ├── proc/         # Process instruction files
│   └── os_*          # OS configuration scenarios
├── output/           # Expected simulation outputs
├── Makefile
└── run.sh
```

---

## Building

**Requirements:** GCC, GNU Make, pthreads

```bash
make
```

This produces the `os` executable. You can also build individual modules:

```bash
make mem    # Memory management only
make sched  # Scheduler only
```

To clean build artifacts:

```bash
make clean
```

---

## Running

```bash
./os <config>
```

The config file is looked up under `input/`. For example:

```bash
./os os_1_mlq_paging
./os os_1_singleCPU_mlq_paging
./os os_syscall
```

To run all scenarios and capture output:

```bash
bash run.sh
```

---

## Configuration File Format

Each config file under `input/` defines a simulation scenario:

```
<time_slice> <num_cpus> <num_processes>
<mem_ram_sz> <mem_swp0_sz> <mem_swp1_sz> <mem_swp2_sz> <mem_swp3_sz>
<start_time> <proc_file> <priority>
...
```

**Example** (`os_1_mlq_paging`):
```
2 4 8
268435456 16777216 0 0 0
1  p1s  15
5  m0s  120
7  p0s  130
...
```

Process instruction files (`input/proc/`) list operations the process will execute:

```
1 14        # PID, code length
calc
alloc 300 0
write 100 1 20
read 1 20 20
free 0
```

---

## Build-time Flags (`include/os-cfg.h`)

| Flag | Description |
|---|---|
| `MLQ_SCHED` | Enable Multi-Level Queue scheduler (140 priority levels) |
| `MAX_PRIO` | Number of priority levels (default: 140) |
| `MM_PAGING` | Enable paged memory management |
| `MM64` | Use 64-bit 5-level page table mode |
| `MM_FIXED_MEMSZ` | Use fixed memory sizes (legacy compat mode) |
| `IODUMP` | Dump memory I/O operations to stdout |
| `PAGETBL_DUMP` | Dump page table state to stdout |
| `VMDBG` / `MMDBG` | Verbose virtual/physical memory debug output |

Toggle features by commenting/uncommenting these defines before building.

---

## Memory Model

### Virtual Address Layout (22-bit)
```
[ Page Number (14 bits) | Page Offset (8 bits) ]
```

Page size: **256 bytes**  
Max virtual pages: **16,384**

### Page Table Entry (PTE) Bits
```
[31] Present  [30] Swapped  [29] Reserved  [28] Dirty
[27:15] UserNum  [14:13] Empty  [12:0] FPN / Swap info
```

### 64-bit Mode (MM64)
Five-level page table: PGD → P4D → PUD → PMD → PT, matching the Linux x86-64 kernel structure.

---

## License

Copyright is reserved by the faculty staff members involved in code development. Licensees are granted personal permission to use and modify the source code solely for studying in course CO2018 at HCMUT. See LICENSE for full terms.
