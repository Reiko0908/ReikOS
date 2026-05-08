/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "../include/mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#if defined (MM64)
	#include "../include/mm64.h"
#endif

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (!mm || !mm->mmap) return NULL;
  struct vm_area_struct *pvma = mm->mmap;
  while (pvma != NULL) {
    if ((int)pvma->vm_id == vmaid) return pvma;
    pvma = pvma->vm_next;
  }
  return NULL; /* vmaid not found */
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
  __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
  return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 */
static struct vm_rg_struct *__get_vm_area_node_at_brk(struct mm_struct *mm, int vmaid, addr_t size, addr_t alignedsz) {
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (!cur_vma) return NULL;
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (!newrg) return NULL;

  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + alignedsz;
  newrg->rg_next = NULL;
  return newrg;
}

struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz) {
  if (!caller || !caller->mm) return NULL;
  return __get_vm_area_node_at_brk(caller->mm, vmaid, size, alignedsz);
}

struct vm_rg_struct *krnl_get_vm_area_node_at_brk(struct krnl_t *krnl, int vmaid, addr_t size, addr_t alignedsz) {
  if (!krnl || !krnl->mm) return NULL;
  return __get_vm_area_node_at_brk(krnl->mm, vmaid, size, alignedsz);
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 */
static int __validate_overlap_vm_area(struct mm_struct *mm, int vmaid, addr_t vmastart, addr_t vmaend) {
  if (!mm || vmastart >= vmaend) return -1;
  struct vm_area_struct *vma = mm->mmap;
  if (!vma) return -1;

  while (vma) {
    if (vmaid != vma->vm_id && OVERLAP(vma->vm_start, vma->vm_end, vmastart, vmaend)) return -1;
    vma = vma->vm_next;
  }
  return 0;
}
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend) {
  if (!caller || !caller->mm) return -1;
  return __validate_overlap_vm_area(caller->mm, vmaid, vmastart, vmaend);
}

int krnl_validate_overlap_vm_area(struct krnl_t *krnl, int vmaid, addr_t vmastart, addr_t vmaend) {
  if (!krnl || !krnl->mm) return -1;
  return __validate_overlap_vm_area(krnl->mm, vmaid, vmastart, vmaend);
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz) {
  if (!caller) return -1;
  addr_t inc_amt;
  int inc_pgn;
#ifdef MM64
  inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  inc_pgn = (int)(inc_amt >> PAGING64_ADDR_PT_SHIFT);
#else
  inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  inc_pgn = (int)(inc_amt >> PAGING_ADDR_PT_SHIFT);
#endif
  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
  if (!area) return -1;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!cur_vma) {
    free(area);
    return -1;
  }

  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0) {
    free(area);
    return -1;
  }
  addr_t old_end = cur_vma->vm_end;
  addr_t old_sbrk = cur_vma->sbrk;
  cur_vma->vm_end = old_end + inc_amt;
  cur_vma->sbrk = area->rg_end;
  
  struct vm_rg_struct newrg;
  if (vm_map_range(caller, area->rg_start, area->rg_end, old_end, inc_pgn, &newrg) != 0) {
    cur_vma->vm_end = old_end;
    cur_vma->sbrk   = old_sbrk;
    free(area);
    return -1;
  }
  free(area);
  return 0;
}

int krnl_inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz) {
  if (!caller->krnl) return -1;
  addr_t inc_amt;
  int inc_pgn;
#ifdef MM64
  inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  inc_pgn = (int)(inc_amt >> PAGING64_ADDR_PT_SHIFT);
#else
  inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  inc_pgn = (int)(inc_amt >> PAGING_ADDR_PT_SHIFT);
#endif
  struct vm_rg_struct *area = krnl_get_vm_area_node_at_brk(caller->krnl, vmaid, inc_sz, inc_amt);
  if (!area) return -1;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (!cur_vma) {
    free(area);
    return -1;
  }
  if (krnl_validate_overlap_vm_area(caller->krnl, vmaid, area->rg_start, area->rg_end) < 0) {
    free(area);
    return -1;
  }
  addr_t old_end = cur_vma->vm_end;
  addr_t old_sbrk = cur_vma->sbrk;
  cur_vma->vm_end = old_end + inc_amt;
  cur_vma->sbrk = area->rg_end;
  
  struct vm_rg_struct newrg;
  if (vm_map_kernel(caller, area->rg_start, area->rg_end, old_end, inc_pgn, &newrg) != 0) {
    cur_vma->vm_end = old_end;
    cur_vma->sbrk   = old_sbrk;
    free(area);
    return -1;
  }
  
  free(area);
  return 0;
}

/* =========================================================================
 * vm_map_range - validate arguments then delegate to vm_map_ram
 * ========================================================================= */
addr_t vm_map_range(struct pcb_t *caller, addr_t astart, addr_t aend,
                    addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  addr_t mapsz;
  if (!caller || !caller->krnl || !ret_rg) return -1;
  if (incpgnum <= 0 || astart >= aend) return -1;
#if defined (MM64)
	mapsz = (addr_t)incpgnum * PAGING64_PAGESZ;
#else
	mapsz = (addr_t)incpgnum * PAGING_PAGESZ;
#endif
  if (mapstart < astart || mapstart + mapsz > aend) return -1;
  return vm_map_ram(caller, astart, aend, mapstart, incpgnum, ret_rg);
}

/* ==========================================================================
* vm_map_kernel
* ========================================================================= */
addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend,
                    addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  addr_t mapsz;
  if (!caller || !caller->krnl || !ret_rg) return -1;
  if (incpgnum <= 0 || astart >= aend) return -1;
#if defined (MM64)
	mapsz = (addr_t)incpgnum * PAGING64_PAGESZ;
#else
	mapsz = (addr_t)incpgnum * PAGING_PAGESZ;
#endif
  if (mapstart < astart || mapstart + mapsz > aend) return -1;
  return krnl_vm_map_ram(caller, astart, aend, mapstart, incpgnum, ret_rg);
}
// #endif
