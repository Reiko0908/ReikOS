/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/mm64.h"

#if defined(MM64)
/*
 * init_pte - Initialize a Page Table Entry (PTE) with specific flags and values
 */
int init_pte(addr_t *pte, int pre, /* present */
             addr_t fpn,           /* frame page number */
             int drt,              /* dirty */
             int swp,              /* swapped */
             int swptyp,           /* swap type */
             addr_t swpoff)        /* swap offset */
{
  if (pre != 0) {
    if (swp == 0) {
      /* Page is in RAM */
      if (fpn == 0)
        return -1; /* FPN 0 is reserved / invalid */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    } else {
      /* Page is swapped out */
      CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }
  return 0;
}

/*
 * get_pd_from_address - Extract 5-level indices from a virtual address
 */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt) {
  *pgd = PAGING64_ADDR_PGD(addr);
  *p4d = PAGING64_ADDR_P4D(addr);
  *pud = PAGING64_ADDR_PUD(addr);
  *pmd = PAGING64_ADDR_PMD(addr);
  *pt = PAGING64_ADDR_PT(addr);
  return 0;
}

/*
 * get_pd_from_pagenum - Extract 5-level indices from a page number
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud,
                        addr_t *pmd, addr_t *pt) {
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT, pgd, p4d, pud, pmd, pt);
}

/*
 * get_pte - Walk 5-level page table and return target PTE
 */
static addr_t *get_pte(struct mm_struct *mm, addr_t pgn, int alloc) {
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  // Grab the PGD root directly from the passed mm_struct
  addr_t *pgd_root = mm->pgd;

  if (pgd_root[pgd_idx] == 0) {
    if (!alloc) return NULL;
    pgd_root[pgd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!pgd_root[pgd_idx]) return NULL;
  }
  addr_t *p4d_tbl = (addr_t *)pgd_root[pgd_idx];
  mm->p4d = p4d_tbl; // FIX: Link P4D back to mm_struct

  if (p4d_tbl[p4d_idx] == 0) {
    if (!alloc) return NULL;
    p4d_tbl[p4d_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!p4d_tbl[p4d_idx]) return NULL;
  }
  addr_t *pud_tbl = (addr_t *)p4d_tbl[p4d_idx];
  mm->pud = pud_tbl; // FIX: Link PUD back to mm_struct

  if (pud_tbl[pud_idx] == 0) {
    if (!alloc) return NULL;
    pud_tbl[pud_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!pud_tbl[pud_idx]) return NULL;
  }
  addr_t *pmd_tbl = (addr_t *)pud_tbl[pud_idx];
  mm->pmd = pmd_tbl; // FIX: Link PMD back to mm_struct

  if (pmd_tbl[pmd_idx] == 0) {
    if (!alloc) return NULL;
    pmd_tbl[pmd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!pmd_tbl[pmd_idx]) return NULL;
  }
  addr_t *pt_tbl = (addr_t *)pmd_tbl[pmd_idx];
  mm->pt = pt_tbl; // FIX: Link PT back to mm_struct

  return &pt_tbl[pt_idx];
}

/*
 * pte_set_swap - Set PTE entry for a swapped out page
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff) {
  // Pass caller->mm to track pointers
  addr_t *pte = get_pte(caller->mm, pgn, 1);
  if (!pte) return -1;

  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
  return 0;
}

/*
 * __pte_set_fpn - Set PTE entry for a mapped (on-line) user page
 */
static int __pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn) {
  addr_t *pte = get_pte(mm, pgn, 1);
  if (!pte) return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK); /* IS in RAM */
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK); /* NOT on swap */
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  return 0;
}

int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn) {
  if (caller == NULL || caller->mm == NULL) return -1;
  return __pte_set_fpn(caller->mm, pgn, fpn);
}

int krnl_pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn) {
  if (caller == NULL || caller->krnl == NULL) return -1;
  // Kernel has no mm_struct, pass NULL safely
  return __pte_set_fpn(caller->krnl->mm, pgn, fpn); 
}

/*
 * pte_get_entry - Retrieve the raw User PTE value
 */
static uint32_t __pte_get_entry(struct mm_struct *mm, addr_t pgn) {
  addr_t *pte = get_pte(mm, pgn, 0);
  return pte ? (uint32_t)(*pte) : 0;
}

uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn) {
  if (caller == NULL || caller->mm == NULL) return 0;
  return __pte_get_entry(caller->mm, pgn);
}

uint32_t krnl_pte_get_entry(struct pcb_t *caller, addr_t pgn) {
  if (caller == NULL || caller->krnl == NULL) return 0;
  return __pte_get_entry(caller->krnl->mm, pgn);
}

/*
 * pte_set_entry - Directly overwrite a User PTE with a raw value
 */
static int __pte_set_entry(struct mm_struct *mm, addr_t pgn, uint32_t pte_val) {
  addr_t *pte = get_pte(mm, pgn, 1);
  if (!pte) return -1;
  *pte = pte_val;
  return 0;
}

int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val) {
  if (caller == NULL || caller->mm == NULL) return -1;
  return __pte_set_entry(caller->mm, pgn, pte_val);
}

int krnl_pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val) {
  if (caller == NULL || caller->krnl == NULL) return -1;
  return __pte_set_entry(caller->krnl->mm, pgn, pte_val);
}

/*
 * vmap_pgd_memset - Initialize a range of page table entries with a specific pattern
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
	uint32_t pattern = 0xdeadbeef;
	if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL)
		return -1;

	for (int pgit = 0; pgit < pgnum; pgit++) {
    addr_t pgn = PAGING64_PGN(addr + (pgit << PAGING64_ADDR_PT_SHIFT));
		addr_t *pte = get_pte(caller->mm, pgn, 1);
		if (pte) *pte = pattern;
	}
	return 0;
}

/*
 * vmap_page_range - Map a range of pages to physical frames
 */
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                       struct framephy_struct *frames, struct vm_rg_struct *ret_rg) {
  if (!caller || !ret_rg) return -1;
  struct framephy_struct *fp = frames;
  ret_rg->rg_start = addr;
  ret_rg->rg_end   = addr + (pgnum << PAGING64_ADDR_PT_SHIFT);
  for (int pgit = 0; pgit < pgnum && fp != NULL; pgit++) {
    addr_t pgn = PAGING64_PGN(addr + (pgit << PAGING64_ADDR_PT_SHIFT));
    pte_set_fpn(caller, pgn, fp->fpn);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
    fp = fp->fp_next;
  }
  return 0;
}

addr_t krnl_vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                            struct framephy_struct *frames, struct vm_rg_struct *ret_rg) {
  if (!caller || !ret_rg) return -1;
  struct framephy_struct *fp = frames;
  ret_rg->rg_start = addr;
  ret_rg->rg_end   = addr + (pgnum << PAGING64_ADDR_PT_SHIFT);
  for (int pgit = 0; pgit < pgnum && fp != NULL; pgit++) {
    addr_t pgn = PAGING64_PGN(addr + (pgit << PAGING64_ADDR_PT_SHIFT));
    krnl_pte_set_fpn(caller, pgn, fp->fpn);
    fp = fp->fp_next;
  }
  return 0;
}

/*
 * alloc_pages_range - Allocate a sequence of physical frames from RAM
 */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum,
                         struct framephy_struct **frm_lst) {
  addr_t fpn;
  struct framephy_struct *newfp = NULL;
  struct framephy_struct *tail = NULL;
  *frm_lst = NULL;
  int max_ram_frames = (caller->krnl->mram->maxsz) >> PAGING64_ADDR_PT_SHIFT;
  if (req_pgnum > max_ram_frames) return -3000;

  for (int pgit = 0; pgit < req_pgnum; pgit++) {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0) {
      addr_t victim_pgn, swpfpn;
      if (find_victim_page(caller->mm, &victim_pgn) != 0)
        return -3000;
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0)
        return -3000;
      fpn = PAGING_FPN(pte_get_entry(caller, victim_pgn));
      __swap_cp_page(caller->krnl->mram, fpn, caller->krnl->active_mswp,
                     swpfpn);
      pte_set_swap(caller, victim_pgn, 0, swpfpn);
    }
    newfp = malloc(sizeof(struct framephy_struct));
    if (!newfp)
      return -3000;
    newfp->fpn = fpn;
    newfp->fp_next = NULL;
    newfp->owner = caller->mm;
    if (*frm_lst == NULL) {
      *frm_lst = newfp;
      tail = newfp;
    } else {
      tail->fp_next = newfp;
      tail = newfp;
    }
  }
  return 0;
}

addr_t krnl_alloc_pages_range(struct pcb_t *caller, int req_pgnum,
                               struct framephy_struct **frm_lst)
{
  if (!caller || !caller->krnl || !caller->krnl->mram) return -1;
  addr_t base_fpn;
  if (MEMPHY_get_contiguous_freefp(caller->krnl->mram, req_pgnum, &base_fpn) != 0)
    return -1;

  struct framephy_struct *head = NULL;
  struct framephy_struct *tail = NULL;
  for (int i = 0; i < req_pgnum; i++) {
    struct framephy_struct *node = malloc(sizeof(struct framephy_struct));
    if (!node) {
      MEMPHY_put_contiguous_freefp(caller->krnl->mram, base_fpn, req_pgnum);
      while (head) {
        struct framephy_struct *nx = head->fp_next;
        free(head);
        head = nx;
      }
      return -1;
    }
    node->fpn = base_fpn + i;
    node->fp_next = NULL;
    node->owner   = caller->krnl->mm;
    if (!head) { head = node; tail = node; }
    else {tail->fp_next = node; tail = node; }
  }
  *frm_lst = head;
  return 0;
}

/*
 * vm_map_ram - Map a virtual memory area to physical RAM
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg) {
  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
  if (ret_alloc < 0) return -1;
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

addr_t krnl_vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg) {
  if (!caller || !caller->krnl || !caller->krnl->mram) return -1;
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = krnl_alloc_pages_range(caller, incpgnum, &frm_lst);
  if (ret_alloc != 0 || frm_lst == NULL) return -1;

	if (krnl_vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg) < 0) {
		struct framephy_struct *fpit = frm_lst;
		while (fpit != NULL) {
			struct framephy_struct *next = fpit->fp_next;
			MEMPHY_put_freefp(caller->krnl->mram, fpit->fpn);
			free(fpit);
			fpit = next;
		}
		return -1;
	}
	ret_rg->mode_bit = 0;
	return 0;
}

/*
 * __swap_cp_page - Copy a full page of data between memory devices
 */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn) {
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++) {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 * __init_mm - Initialize the Memory Management structure for a User Process
 */
static int __init_mm(struct mm_struct *mm, addr_t *pgd_root, addr_t vm_base_addr) {
  mm->pgd = pgd_root;
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;

  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (!vma0) return -1;
  vma0->vm_id = 0;
  vma0->vm_start = vm_base_addr;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  vma0->vm_freerg_list = NULL;
  vma0->vm_mm = mm;
  vma0->vm_next = NULL;

  struct vm_rg_struct *init_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  if (!init_rg) {
    free(vma0);
    return -1;
  }
  enlist_vm_rg_node(&vma0->vm_freerg_list, init_rg);

  mm->mmap = vma0;
  mm->fifo_pgn = NULL;
  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
    mm->symrgtbl[i].rg_start = 0;
    mm->symrgtbl[i].rg_end = 0;
    mm->symrgtbl[i].rg_next = NULL;
  }
  return 0;
}

int init_mm(struct mm_struct *mm, struct pcb_t *caller) {
  addr_t *pgd_root = (addr_t *)calloc(512, sizeof(addr_t));
  __init_mm(mm, pgd_root, 0);
  mm->kcpooltbl = NULL;
  return 0;
}

int krnl_init_mm(struct mm_struct *mm, struct krnl_t *krnl) {
  addr_t base_addr = KRNL_SPACE_BASE_ADDR;
  __init_mm(mm, krnl->krnl_pgd, base_addr);
  mm->kcpooltbl = calloc(MAX_CACHE_POOL, sizeof(struct kcache_pool_struct));
  if (!mm->kcpooltbl) return -1;
  return 0;
}

/*
 * init_vm_rg - Initialize a virtual memory region structure
 */
struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end) {
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;
  return rgnode;
}

/*
 * enlist_vm_rg_node - Insert a memory region into a linked list
 */
int enlist_vm_rg_node(struct vm_rg_struct **rglist,
                      struct vm_rg_struct *rgnode) {
  rgnode->rg_next = *rglist;
  *rglist = rgnode;
  return 0;
}

/*
 * enlist_pgn_node - Insert a page number into a tracking list
 */
int enlist_pgn_node(struct pgn_t **plist, addr_t pgn) {
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;
  return 0;
}

/*
 * print_list_fp - Print a linked list of physical frames
 */
int print_list_fp(struct framephy_struct *ifp) {
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (fp != NULL) {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

/*
 * print_list_rg - Print a linked list of virtual memory regions
 */
int print_list_rg(struct vm_rg_struct *irg) {
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (rg != NULL) {
    printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

/*
 * print_list_vma - Print a linked list of virtual memory areas
 */
int print_list_vma(struct vm_area_struct *ivma) {
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (vma != NULL) {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start,
           vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

/*
 * print_list_pgn - Print a linked list of tracked page numbers
 */
int print_list_pgn(struct pgn_t *ip) {
  printf("print_list_pgn: ");
  if (ip == NULL) {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (ip != NULL) {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

/*
 * print_pgtbl - Dump all mapped PTEs with their 5-level directory indices
 */

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end) {
  struct mm_struct *mm = caller->mm;
  addr_t pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgn_end =
      (end == (addr_t)-1)
          ? (mm->mmap ? (mm->mmap->vm_end >> PAGING64_ADDR_PT_SHIFT) : 0)
          : (end >> PAGING64_ADDR_PT_SHIFT);
  printf("Page table dump [pgn %lu .. %lu]:\n", (unsigned long)pgn_start,
         (unsigned long)pgn_end);
  for (addr_t pgn = pgn_start; pgn < pgn_end; ++pgn) {
    addr_t *pte_ptr = get_pte(mm, pgn, 0);
    if (pte_ptr == NULL || *pte_ptr == 0)
      continue;
    addr_t pte = *pte_ptr;
    addr_t pdg_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_pagenum(pgn, &pdg_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
    if (pte & PAGING_PTE_SWAPPED_MASK) {
      printf("pgn [%4lu] PGD[%3lu] P4D[%3lu] PUD[%3lu] PMD[%3lu] PT[%3lu]"
             "-> SWAPPED swpoff=0x%lx\n",
             (unsigned long)pgn, (unsigned long)pdg_idx, (unsigned long)p4d_idx,
             (unsigned long)pud_idx, (unsigned long)pmd_idx,
             (unsigned long)pt_idx, (unsigned long)PAGING_SWP(pte));
    } else if (pte & PAGING_PTE_PRESENT_MASK) {
      addr_t fpn = PAGING_FPN(pte);
      printf("pgn [%4lu] PGD[%3lu] P4D[%3lu] PUD[%3lu] PMD[%3lu] PT[%3lu]"
             "-> fpn=0x%lx phys=0x%lx\n",
             (unsigned long)pgn, (unsigned long)pdg_idx, (unsigned long)p4d_idx,
             (unsigned long)pud_idx, (unsigned long)pmd_idx,
             (unsigned long)pt_idx, (unsigned long)fpn,
             (unsigned long)(fpn << PAGING64_ADDR_PT_SHIFT));
    }
  }
  return 0;
}

#endif // def MM64
