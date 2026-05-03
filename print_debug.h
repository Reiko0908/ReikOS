#ifndef PRINT_DEBUG_H
#define PRINT_DEBUG_H

#include "include/cpu.h"
#include "include/mm.h"
#include "include/mm64.h"
#include "include/libmem.h"
#include <stdio.h>
#include <stdlib.h>

/* RAM / SWAP / Kernel debug */
void print_kernel_info(struct krnl_t *os);
void print_memphy(const char *name, struct memphy_struct *m);
void print_frame_list(const char *name, struct framephy_struct *head);
void print_storage_int(struct memphy_struct *m);
void print_pcb_info(struct pcb_t *proc);
void print_mm_struct(struct mm_struct *mm);
#endif