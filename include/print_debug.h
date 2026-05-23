#ifndef PRINT_DEBUG_H
#define PRINT_DEBUG_H

#include "cpu.h"
#include "mm.h"

/* Snapshot of the entire system (Physical RAM/Swap, VMAs, etc.) */
void system_dump(struct krnl_t *os);

/* Compatibility / Specialized stubs */
void pg_dump(struct pcb_t *p, addr_t s, addr_t e);

#endif
