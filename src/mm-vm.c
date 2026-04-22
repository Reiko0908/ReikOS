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
#include "mm.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm->mmap == NULL) return NULL;
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
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  /* TODO: update the newrg boundary */

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;
  /* END TODO */
  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  //struct vm_area_struct *vma = caller->krnl->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend) return -1;

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL) return -1;

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL) return -1;

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(cur_area->vm_start, cur_area->vm_end, vma->vm_start, vma->vm_end))
      return -1;
    vma = vma->vm_next;
  }
  /* End TODO*/
  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
#ifdef MM64
  addr_t inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int inc_pgn = (int)(int_amt >> PAGING64_ADDR_PT_SHIFT);
#else
  addr_t inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int inc_pgn = (int)(inc_amt >> PAGING_ADDR_PT_SHIFT);
#endif
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (!cur_vma) return -1;
  addr_t old_sbrk = cur_vma->sbrk;
  cur_vma->sbrk += inc_amt;
  if (cur_vma->sbrk > cur_vma->vm_end)
    cur_vma->vm_end = cur_vma->sbrk;
  if (validate_overlap_vm_area(caller, vmaid, cur_vma->vm_start, cur_vma->vm_end) < 0) {
    cur_vma->sbrk = old_sbrk; /* roll back */
    cur_vma->vm_end = old_sbrk; /* roll back */
    return -1; /*Overlap and failed allocation */
  }
  struct vm_rg_struct newrg;
  if (vm_map_range(caller, old_sbrk, cur_vma->sbrk, old_sbrk, incnumpage, &newrg) < 0) {
    cur_vma->sbrk = old_sbrk;
    cur_vma->vm_end = old_sbrk;
    return -1;
  }
  struct vm_rg_struct *free_rg = malloc(sizeof(struct vm_rg_struct));
  free_rg->rg_start = newrg.rg_start;
  free_rg->rg_end   = newrg.rg_end;
  free_rg->rg_next  = NULL;
  enlist_vm_freerg_list(caller->mm, free_rg);
  return 0;
}

// #endif
