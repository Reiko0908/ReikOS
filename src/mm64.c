/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm64.c
 *
 * Design notes:
 *   caller->mm               : per-process mm_struct (page table, VMA, symrgtbl)
 *   caller->krnl->mram       : physical RAM device (shared hardware)
 *   caller->krnl->active_mswp: active swap device  (shared hardware)
 *   caller->krnl->krnl_pgd   : kernel-space page table (kmalloc/cache only)
 *
 * Rule: all process virtual memory → caller->mm
 *       all physical hardware      → caller->krnl->mram / active_mswp
 */

#include "mm64.h"
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(MM64)

/* =========================================================================
 * init_pte - initialise a PTE entry value in place
 * ========================================================================= */
int init_pte(addr_t *pte,
             int pre,       /* present */
             addr_t fpn,    /* frame page number */
             int drt,       /* dirty */
             int swp,       /* swapped */
             int swptyp,    /* swap type */
             addr_t swpoff) /* swap offset */
{
  if (pre != 0) {
    if (swp == 0) {
      /* Page is in RAM */
      if (fpn == 0) return -1;  /* FPN 0 is reserved / invalid */
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
 
/* =========================================================================
 * get_pd_from_address - extract 5-level page directory indices from address
 *
 * Bit layout (57-bit virtual address):
 *   [56:48] PGD  [47:39] P4D  [38:30] PUD  [29:21] PMD  [20:12] PT  [11:0] offset
 * ========================================================================= */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt = (addr & PAGING64_ADDR_PT_MASK) >> PAGING64_ADDR_PT_LOBIT;
  return 0;
}

/* =========================================================================
 * get_pd_from_pagenum - extract 5-level indices from a page number
 * ========================================================================= */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                             pgd, p4d, pud, pmd, pt);
}

/* =========================================================================
 * get_pte - walk the 5-level page table and return a pointer to the PTE
 *
 * @mm    : the process's mm_struct  (always caller->mm)
 * @pgn   : page number to look up
 * @alloc : 1 = allocate missing intermediate tables on the way down
 *          0 = read-only walk; return NULL if any level is missing
 *
 * Tree structure (each level is a 512-entry array of addr_t):
 *   mm->pgd[pgd_idx]         → pointer to P4D table
 *     p4d_tbl[p4d_idx]       → pointer to PUD table
 *       pud_tbl[pud_idx]     → pointer to PMD table
 *         pmd_tbl[pmd_idx]   → pointer to PT  table
 *           pt_tbl[pt_idx]   → the PTE value itself  ← returned here
 *
 * Sparse: sub-tables only exist for pages that have actually been mapped.
 * ========================================================================= */
static addr_t *get_pte(struct mm_struct *mm, addr_t pgn, int alloc)
{
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
 
  if (mm->pgd[pgd_idx] == 0) {
    if (!alloc) return NULL;
    mm->pgd[pgd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!mm->pgd[pgd_idx]) return NULL;
  }
  addr_t *p4d_tbl = (addr_t *)mm->pgd[pgd_idx];
 
  if (p4d_tbl[p4d_idx] == 0) {
    if (!alloc) return NULL;
    p4d_tbl[p4d_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!p4d_tbl[p4d_idx]) return NULL;
  }
  addr_t *pud_tbl = (addr_t *)p4d_tbl[p4d_idx];
 
  if (pud_tbl[pud_idx] == 0) {
    if (!alloc) return NULL;
    pud_tbl[pud_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!pud_tbl[pud_idx]) return NULL;
  }
  addr_t *pmd_tbl = (addr_t *)pud_tbl[pud_idx];
 
  if (pmd_tbl[pmd_idx] == 0) {
    if (!alloc) return NULL;
    pmd_tbl[pmd_idx] = (addr_t)calloc(512, sizeof(addr_t));
    if (!pmd_tbl[pmd_idx]) return NULL;
  }
  addr_t *pt_tbl = (addr_t *)pmd_tbl[pmd_idx];
  return &pt_tbl[pt_idx];
}

/* =========================================================================
 * pte_set_swap - record that a page has been swapped out
 * present=0, swapped=1 — page fault triggers on next access → swap-in runs
 * ========================================================================= */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff) 
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;
#ifdef MM64
  pte = get_pte(mm, pgn, 1);
  if (!pte) return -1;
#else
  pte = &mm->pgd[pgn];
#endif
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);  /* NOT in RAM */
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);  /* IS on swap */
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
  return 0;
}

/* =========================================================================
 * pte_set_fpn - record that a page is present in RAM at frame fpn
 * present=1, swapped=0 — page found in RAM on access, no fault needed
 * ========================================================================= */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;
#ifdef MM64
  pte = get_pte(mm, pgn, 1);
  if (!pte) return -1;
#else
  pte = &mm->pgd[pgn];
#endif
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);  /* IS in RAM */
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);  /* NOT on swap */
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  return 0;
}

/* =========================================================================
 * pte_get_entry - read the full PTE value for a page number
 *
 * Returns 0 if the page has never been mapped (no path in the tree).
 * Returns addr_t (64-bit) to avoid truncating large FPN or swap offsets.
 * ========================================================================= */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;
#ifdef MM64
  pte = get_pte(mm, pgn, 0);
  if (pte == NULL) return 0;
  return (uint32_t)(*pte);
#else
  return (uint32_t)mm->pgd[pgn]);
#endif
}

/* =========================================================================
 * pte_set_entry - write a raw PTE value for a page number
 * ========================================================================= */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct mm_struct *mm = caller->mm;
#ifdef MM64
  addr_t *pte = get_pte(mm, pgn, 1);
  if (!pte) return -1;
  *pte = pte_val;
#else
  mm->pgd[pgn] = (uint32_t)pte_val;
#endif
  return 0;
}

/* =========================================================================
 * vmap_pgd_memset - fill a page range with a dummy pattern (SYSMEM_MAP_OP)
 *
 * Emulates page-directory setup without real physical allocation.
 * 0xdeadbeef marks entries as dummy-mapped for testing purposes.
 * ========================================================================= */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  struct mm_struct *mm = caller->mm;
  addr_t pattern = 0xdeadbeef;
  for (int pgit = 0; pgit < pgnum; pgit++) {
  addr_t pgn = (addr >> PAGING64_ADDR_PT_SHIFT) + pgit;
#ifdef MM64
  addr_t *pte = get_pte(mm, pgn, 1);
  if (pte != NULL) 
    *pte = (pattern & PAGING_PTE_FPN_MASK) | PAGING_PTE_PRESENT_MASK;
#else
  mm->pgd[pgn] = (uint32_t)pattern;
#endif
  }
  return 0;
}

/* =========================================================================
 * vmap_page_range - install frame→page mappings for a virtual address range
 *
 * Walks the frame list, calls pte_set_fpn() for each page, and records
 * each page in the FIFO list for future victim selection.
 * ========================================================================= */
addr_t vmap_page_range(struct pcb_t *caller,
                       addr_t addr,
                       int pgnum,
                       struct framephy_struct *frames,
                       struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fp = frames;
  int pgit = 0;
  ret_rg->rg_start = addr;
  ret_rg->vmaid = 0;
  for(;pgit < pgnum && fp != NULL; pgit++){
    addr_t pgn = (addr >> PAGING64_ADDR_PT_SHIFT) + pgit;
    pte_set_fpn(caller, pgn, fp->fpn);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
    fp = fp->fp_next;
  }
  ret_rg->rg_end = ((addr_t)pgit << PAGING64_ADDR_PT_SHIFT) + addr;
  return (pgit < pgnum) ? -1 : 0;
}

/* =========================================================================
 * alloc_pages_range - obtain req_pgnum physical frames from RAM
 *
 * If RAM is full, evicts a FIFO victim page to swap to make room.
 * Returns 0 on success, -3000 if memory is completely exhausted.
 * ========================================================================= */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum,
                         struct framephy_struct **frm_lst) {
  addr_t fpn;
  int pgit = 0;
  struct framephy_struct *newfp = NULL;
  struct framephy_struct *tail = NULL;
  *frm_lst = NULL;

  int max_ram_frames = (caller->krnl->mram->max_size) >> PAGING64_ADDR_PT_SHIFT;
  if (req_pgnum > max_ram_frames) return -3000;
  for (; pgit < req_pgnum; pgit++) {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0) {
      addr_t victim_pgn, swpfpn;
      if (find_victim_page(caller->mm, &victim_pgn) != 0) return -3000;
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0) return -3000;
      fpn = PAGING_FPN(pte_get_entry(caller, victim_pgn));
      __swap_cp_page(caller->krnl->mram, fpn, caller->krnl->active_mswp, swpfpn);
      pte_set_swap(caller, victim_pgn, 0, swpfpn);
    }
    newfp = malloc(sizeof(struct framephy_struct));
    if (!newfp) return -3000;
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

/* =========================================================================
 * vm_map_ram - do the mapping all vm are to ram storage device
 * ========================================================================= */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg) {
  struct framephy_struct *frm_lst = NULL;
  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  addr_t ret_alloc = 0;
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
  if (ret_alloc < 0) return -1;
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

/* =========================================================================
 * __swap_cp_page - copy one full page of data between two memphy devices
 * ========================================================================= */
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

/* =========================================================================
 * init_mm - initialise a fresh mm_struct for a new process
 *
 * Sparse allocation: only the root PGD level is allocated here.
 * P4D / PUD / PMD / PT sub-tables are created on demand by get_pte()
 * the first time a page in that address range is mapped.
 * ========================================================================= */
int init_mm(struct mm_struct *mm, struct pcb_t *caller) {
  mm->pgd = (addr_t *)calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  if (!mm->pgd) return -1;
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;

  /* By default the owner comes with at least one vma */
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (!vma0) {free(mm->pgd); return -1;}
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  vma0->vm_mm = mm;
  vma0->vm_next = NULL
  vma0->vm_freerg_list = NULL;

  mm->mmap = vma0;
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;
  memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));
  return 0;
}

/* =========================================================================
 * List helpers
 * ========================================================================= */
struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end) {
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist,
                      struct vm_rg_struct *rgnode) {
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn) {
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

/* =========================================================================
 * Debug list printers
 * ========================================================================= */
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
  printf("n");
  return 0;
}

/* =========================================================================
 * print_pgtbl - dump all mapped PTEs with their 5-level directory indices
 *
 * Uses get_pte(alloc=0) — read-only walk, no tables are created.
 * Upper bound defaults to vma->vm_end so only mapped pages are shown.
 * ========================================================================= */
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end) {
  struct mm_struct *mm = caller->mm;
  addr_t pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgn_end = (end == (addr_t)-1) 
                 ? (mm->mmap ? (mm->mmap->vm_end >> PAGING64_ADDR_PT_SHIFT) : 0)
                 : (end >> PAGING64_ADDR_PT_SHIFT);
  printf("Page table dump [pgn %lu .. %lu]:\n",
         (unsigned long)pgn_start, (unsigned long)pgn_end);
  for (addr_t pgn = pgn_start; pgn < pgn_end; ++pgn) {
    addr_t *pte_ptr = get_pte(mm, pgn, 0);
    if (pte_ptr == NULL || *pte_ptr == 0) continue;
    addr_t pte = *pte_ptr;
    addr_t pdg_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_pagenum(pgn, &pdg_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
    if (pte & PAGING_PTE_SWAPPED_MASK) {
      printf ("pgn [%4lu] PGD[%3lu] P4D[%3lu] PUD[%3lu] PMD[%3lu] PT[%3lu]"
              "-> SWAPPED swpoff=0x%lx\n", (unsigned long)pgn,
              (unsigned long)pdg_idx, (unsigned long)p4d_idx, (unsigned long)pud_idx,
              (unsigned long)pmd_idx, (unsigned long)pt_idx, (unsigned long)PAGING_SWPOFF(pte));
    } else if (pte & PAGING_PTE_PRESENT_MASK) {
      printf ("pgn [%4lu] PGD[%3lu] P4D[%3lu] PUD[%3lu] PMD[%3lu] PT[%3lu]"
            "-> fpn=0x%lx phys=0x%lx\n", (unsigned long)pgn,
            (unsigned long)pdg_idx, (unsigned long)p4d_idx, (unsigned long)pud_idx,
            (unsigned long)pmd_idx, (unsigned long)pt_idx, (unsigned long)fpn,
            (unsigned long)(fpn << PAGING64_ADDR_PT_SHIFT));
    }
  }
  return 0;
}

#endif // def MM64
