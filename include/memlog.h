#ifndef MEMLOG_H
#define MEMLOG_H

#include "cpu.h"
#include "mm.h"
#include "mm64.h"
#include "libmem.h"
#include <stdio.h>
#include <stdlib.h>

/* Verbose OS Snapshot API */
void print_kernel_info(struct krnl_t *os);
void print_mm_struct(struct mm_struct *mm);
void gantt_dump();

/* Verbose Component Helpers */
void print_memphy(const char *name, struct memphy_struct *m);
void print_frame_list(const char *name, struct framephy_struct *head);
void print_storage_int(struct memphy_struct *m);

/* Compatibility / Specialized stubs */
void pg_dump(struct pcb_t *p, addr_t s, addr_t e);

/* Thread-safe scheduler logging */
void sync_log_printf(const char *fmt, ...);

#endif
