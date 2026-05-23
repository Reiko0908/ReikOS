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
void gantt_dump();

/* Compatibility / Specialized stubs */
void pg_dump(struct pcb_t *p, addr_t s, addr_t e);

/* Thread-safe scheduler logging */
void sync_log_printf(const char *fmt, ...);

#endif
