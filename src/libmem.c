/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c
 */

#include "../include/libmem.h"
#include "../include/mm.h"
#include "../include/mm64.h"
#include "string.h"
#include "../include/syscall.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/* Exported lock wrappers for other modules (used by print_debug etc.) */
void libmem_mmvm_lock(void) {
  pthread_mutex_lock(&mmvm_lock);
}

void libmem_mmvm_unlock(void) {
  pthread_mutex_unlock(&mmvm_lock);
}

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt) {
  if (!mm || !rg_elmt || rg_elmt->rg_start >= rg_elmt->rg_end) return -1;
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, rg_elmt->vmaid);
  if (!cur_vma) return -1;

  addr_t vma_end = cur_vma->vm_end;
  struct vm_rg_struct **sp = &cur_vma->vm_freerg_list;
  while (*sp) {
    if ((*sp)->rg_start == (*sp)->rg_end && (*sp)->rg_start != vma_end) {
      struct vm_rg_struct *tmp = *sp;
      *sp = tmp->rg_next;
      free(tmp);
    } else {
      sp = &(*sp)->rg_next;
    }
  }

  /* Insert in sorted order by rg_start */
  struct vm_rg_struct **prev = &cur_vma->vm_freerg_list;
  struct vm_rg_struct *cur = cur_vma->vm_freerg_list;
  while (cur && cur->rg_start < rg_elmt->rg_start) {
    prev = &cur->rg_next;
    cur  = cur->rg_next;
  }
  rg_elmt->rg_next = cur;
  *prev = rg_elmt;

  /* Merge with next node if adjacent */
  if (rg_elmt->rg_next && rg_elmt->rg_end == rg_elmt->rg_next->rg_start) {
    struct vm_rg_struct *nxt = rg_elmt->rg_next;
    rg_elmt->rg_end  = nxt->rg_end;
    rg_elmt->rg_next = nxt->rg_next;
    free(nxt);
  }

  /* Merge with previous node if adjacent — find the node just before rg_elmt */
  struct vm_rg_struct *p = cur_vma->vm_freerg_list;
  while (p && p->rg_next != rg_elmt) {
    p = p->rg_next;
  }

  if (p && p->rg_end == rg_elmt->rg_start) {
    p->rg_end  = rg_elmt->rg_end;
    p->rg_next = rg_elmt->rg_next;
    free(rg_elmt);
  }

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid) {
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*
 * refresh_vma_sentinel()
 *
 * After any allocation that may have brought sbrk up to vm_end, ensure the
 * free-region list holds exactly one sentinel node [vm_end, vm_end].
 *
 * Rules:
 *   - If sbrk < vm_end  → nothing to do (VMA still has uncommitted space).
 *   - If sbrk == vm_end → purge any stale sentinels sitting at a wrong
 *                         address, then add one at the current vm_end if
 *                         none already exists.
 */
static void refresh_vma_sentinel(struct vm_area_struct *vma)
{
  if (!vma || vma->sbrk != vma->vm_end)
    return;

  /* Purge stale sentinels (zero-length nodes at the wrong address) */
  struct vm_rg_struct **sp = &vma->vm_freerg_list;
  while (*sp) {
    struct vm_rg_struct *cur = *sp;
    if (cur->rg_start == cur->rg_end && cur->rg_start != vma->vm_end) {
      *sp = cur->rg_next;
      free(cur);
    } else {
      sp = &cur->rg_next;
    }
  }

  /* Add sentinel only if not already present */
  struct vm_rg_struct *rg = vma->vm_freerg_list;
  while (rg) {
    if (rg->rg_start == vma->vm_end && rg->rg_end == vma->vm_end)
      return;  /* already there */
    rg = rg->rg_next;
  }

  struct vm_rg_struct *sentinel = malloc(sizeof(struct vm_rg_struct));
  if (sentinel) {
    sentinel->rg_start = vma->vm_end;
    sentinel->rg_end   = vma->vm_end;
    sentinel->vmaid    = vma->vm_id;
    sentinel->rg_next  = vma->vm_freerg_list;
    vma->vm_freerg_list = sentinel;
  }
}


int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (caller->mm->symrgtbl[rgid].rg_start != 0 ||
      caller->mm->symrgtbl[rgid].rg_end   != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/
  struct vm_rg_struct rgnode;
  rgnode.mode_bit = 1;
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->mm->symrgtbl[rgid].mode_bit = rgnode.mode_bit;
    caller->mm->symrgtbl[rgid].vmaid = rgnode.vmaid;
    *alloc_addr = rgnode.rg_start;

    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
    if (cur_vma && rgnode.rg_end > cur_vma->sbrk)
      cur_vma->sbrk = rgnode.rg_end;

    refresh_vma_sentinel(cur_vma);            /* shared sentinel logic */
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!cur_vma) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t old_vm_end = cur_vma->vm_end;
  addr_t old_sbrk = cur_vma->sbrk;
  int inc_sz = 0;
#ifdef MM64
  inc_sz = PAGING64_PAGE_ALIGNSZ(size);
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

  libsyscall(caller, 17, SYSMEM_INC_OP, vmaid, inc_sz);

  if (cur_vma->sbrk == old_sbrk) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  caller->mm->symrgtbl[rgid].rg_start = old_vm_end;
  caller->mm->symrgtbl[rgid].rg_end = old_vm_end + size;
  caller->mm->symrgtbl[rgid].mode_bit = 1;
  caller->mm->symrgtbl[rgid].vmaid = cur_vma->vm_id;
  *alloc_addr = old_vm_end;
  cur_vma->sbrk = old_vm_end + size;

  /* Fragmentation recovery — page-aligned leftover back to free list */
  if (old_vm_end + size < cur_vma->vm_end) {
    struct vm_rg_struct *free_tail = malloc(sizeof(struct vm_rg_struct));
    if (free_tail) {
      free_tail->rg_start = old_vm_end + size;
      free_tail->rg_end   = cur_vma->vm_end;
      free_tail->mode_bit = 1;
      free_tail->vmaid    = cur_vma->vm_id;
      free_tail->rg_next  = NULL;
      enlist_vm_freerg_list(caller->mm, free_tail);
    }
  }

  refresh_vma_sentinel(cur_vma);             /* shared sentinel logic */
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (!rgnode || (rgnode->rg_start == 0 && rgnode->rg_end == 0) || rgnode->vmaid != vmaid) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *freerg = malloc(sizeof(struct vm_rg_struct));
  if (!freerg) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  freerg->rg_start = rgnode->rg_start;
  freerg->rg_end   = rgnode->rg_end;
  freerg->vmaid    = rgnode->vmaid;
  freerg->rg_next  = NULL;

  if (enlist_vm_freerg_list(caller->mm, freerg) != 0) {
    free(freerg);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  rgnode->rg_start = rgnode->rg_end = 0;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index) {
  addr_t addr = proc->regs[reg_index];

  if (__alloc(proc, 0, reg_index, size, &addr) != 0) {
    return -1;
  }

  proc->regs[reg_index] = addr;
#ifdef IODUMP
  printf("[LIBALLOC] PID %d: alloc [" FORMAT_ADDR "] [%u]\n", proc->pid, size, reg_index);
#endif
  return 0;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int libfree(struct pcb_t *proc, uint32_t reg_index) {
  struct vm_rg_struct *rgnode = get_symrg_byid(proc->mm, reg_index);
  if (!rgnode) return -1;
  int val = __free(proc, rgnode->vmaid, reg_index);
#ifdef IODUMP
  if (val == 0) {
    printf("[LIBFREE] PID %d: free [%u]\n", proc->pid, reg_index);
  }
#endif
#ifdef PAGETBL_DUMP
  printf("libfree:218\n");
  print_pgtbl(proc, 0, -1);
#endif
  return val;
}

int find_victim_page(struct mm_struct *mm, addr_t *retpgn) {
  struct pgn_t *pg = mm->fifo_pgn;

  if (!pg) {
    return -1;
  }

  struct pgn_t *prev = NULL;
  while (pg->pg_next) {
    prev = pg;
    pg = pg->pg_next;
  }

  *retpgn = pg->pgn;

  if (prev) {
    prev->pg_next = NULL;
  } else {
    mm->fifo_pgn = NULL;
  }

  free(pg);
  return 0;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller) {
  addr_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte)) {
    addr_t vicpgn, vicfpn, swpfpn;

    if (find_victim_page(caller->mm, &vicpgn) != 0) {
      return -1;
    }

    vicfpn = PAGING_FPN(pte_get_entry(caller, vicpgn));

    if (pte & PAGING_PTE_SWAPPED_MASK) {
      /* Target is in swap: exchange RAM[vicfpn] <-> SWAP[tgtswp].
       * Victim's data goes into tgtswp, target's data comes into vicfpn.
       * Victim reuses the target's existing swap slot — no new slot needed. */
      swpfpn = PAGING_SWP(pte);

      printf("[SWAP DEBUG] BEFORE syscall\n  vicpgn=%lu vicfpn=%lu swpfpn=%lu\n",
             (unsigned long)vicpgn, (unsigned long)vicfpn, (unsigned long)swpfpn);

      libsyscall(caller, 17, SYSMEM_SWP_OP, vicfpn, swpfpn);
      pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);
    } else {
      /* Target is fresh (never swapped): just evict victim to a new swap slot */
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0) {
        return -1;
      }

      printf("[SWAP DEBUG] BEFORE syscall\n  vicpgn=%lu vicfpn=%lu swpfpn=%lu\n",
             (unsigned long)vicpgn, (unsigned long)vicfpn, (unsigned long)swpfpn);

      libsyscall(caller, 17, SYSMEM_SWP_OP, vicfpn, swpfpn);
      pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);
    }

    pte_set_fpn(caller, pgn, vicfpn);
    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }

  *fpn = (int)PAGING_FPN(pte_get_entry(caller, pgn));
  return 0;
}


int krnl_pg_getpage(struct mm_struct *mm, addr_t pgn, int *fpn, struct pcb_t *caller) {
  uint32_t pte = krnl_pte_get_entry(caller, pgn);
  if (!PAGING_PAGE_PRESENT(pte)) return -1;
  *fpn = (int)PAGING_FPN(pte);
  return 0;
}

int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller) {
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1;
  addr_t phyaddr = ((addr_t)fpn << PAGING64_ADDR_FPN_SHIFT) + off;

  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  *data = (BYTE)regs.a3;
  return 0;
}



/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller) {
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1;
  addr_t phyaddr = ((addr_t)fpn << PAGING64_ADDR_FPN_SHIFT) + off;


  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;

  _syscall(caller->krnl, caller->pid, 17, &regs);
  return 0;
}



/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (!currg || currg->vmaid != vmaid || currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int res = pg_getval(caller->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return res;
}

/*libread - PAGING-based read a region memory */
int libread(struct pcb_t *proc, // Process executing the instruction
            uint32_t source,    // Index of source register
            addr_t offset,      // Source address = [source] + [offset]
            uint32_t *destination) {
  BYTE data;
  addr_t addr = proc->regs[source] + offset;
  if (!IS_USER_SPACE(addr)) return -1;

  int vmaid = get_vmaid_by_addr(proc->mm, addr);
  int rgid = get_symrgid_by_addr(proc->mm, addr);
  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, rgid);
  addr_t rel_off = (currg) ? (addr - currg->rg_start) : offset;

  int val = __read(proc, vmaid, rgid, rel_off, &data);
  if (val == -1) return -1;
  *destination = (uint32_t)(unsigned char)data;
#ifdef IODUMP
  printf("[LIBREAD] PID %d: read [%u] [" FORMAT_ADDR "] [%u]\n", proc->pid, source, offset, *destination);
#endif
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (!currg || currg->vmaid != vmaid || currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int res = pg_setval(caller->mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return res;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset) {
  addr_t addr = proc->regs[destination] + offset;
  if (!IS_USER_SPACE(addr)) return -1;

  int vmaid = get_vmaid_by_addr(proc->mm, addr);
  int rgid = get_symrgid_by_addr(proc->mm, addr);
  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, rgid);
  addr_t rel_off = (currg) ? (addr - currg->rg_start) : offset;

  int val = __write(proc, vmaid, rgid, rel_off, data);
  if (val == -1) return -1;
#ifdef IODUMP
  if (val == 0) {
    printf("[LIBWRITE] PID %d: write [%u] [%u] [" FORMAT_ADDR "]\n", proc->pid, data, destination, offset);
  }
#endif
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  return val;
}

int krnl_pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller) {
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
  int fpn;

  if (krnl_pg_getpage(mm, pgn, &fpn, caller) != 0) {
    return -1;
  }

  return MEMPHY_read(caller->krnl->mram, ((addr_t)fpn << PAGING64_ADDR_FPN_SHIFT) + off, data);
}

int krnl_pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller) {
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
  int fpn;

  if (krnl_pg_getpage(mm, pgn, &fpn, caller) != 0) {
    return -1;
  }

  return MEMPHY_write(caller->krnl->mram, ((addr_t)fpn << PAGING64_ADDR_FPN_SHIFT) + off, value);
}

int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index) {
  addr_t addr = caller->regs[reg_index];
  int val = __kmalloc(caller, 0, reg_index, size, &addr);
  if (val == -1) return -1;
  caller->regs[reg_index] = addr;
  return 0;
}

/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr) {
  pthread_mutex_lock(&mmvm_lock);

  struct mm_struct *krnl_mm = caller->krnl->mm;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

  if (krnl_mm->symrgtbl[rgid].rg_start != 0 || krnl_mm->symrgtbl[rgid].rg_end   != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

  struct vm_rg_struct rgnode;
  rgnode.mode_bit = 0;

    if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
      krnl_mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
      krnl_mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
      krnl_mm->symrgtbl[rgid].mode_bit = rgnode.mode_bit;
      krnl_mm->symrgtbl[rgid].vmaid    = rgnode.vmaid;
      *alloc_addr = rgnode.rg_start;

      struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
      if (cur_vma && rgnode.rg_end > cur_vma->sbrk)
        cur_vma->sbrk = rgnode.rg_end;

      refresh_vma_sentinel(cur_vma);            /* shared sentinel logic */
      pthread_mutex_unlock(&mmvm_lock);
      return 0;
    }

    /* ── PATH B: no free region — expand VMA with identity mapping ──── */
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (!cur_vma) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    addr_t old_vm_end __attribute__((unused)) = cur_vma->vm_end;
    /* old_srbk unused - removed */

    int inc_sz;
  #ifdef MM64
    inc_sz = PAGING64_PAGE_ALIGNSZ(size);
  #else
    inc_sz = PAGING_PAGE_ALIGNSZ(size);
  #endif
    if (krnl_inc_vma_limit(caller, vmaid, inc_sz) != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

    addr_t mapped_start = cur_vma->sbrk - (addr_t)PAGING64_PAGE_ALIGNSZ(size);
    krnl_mm->symrgtbl[rgid].rg_start = mapped_start;
    krnl_mm->symrgtbl[rgid].rg_end   = mapped_start + size;
    krnl_mm->symrgtbl[rgid].mode_bit = 0;
    krnl_mm->symrgtbl[rgid].vmaid    = cur_vma->vm_id;
    *alloc_addr = mapped_start;



    refresh_vma_sentinel(cur_vma);             /* shared sentinel logic */
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

int free_pcb_memph(struct pcb_t *caller) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_area_struct *vma = caller->mm->mmap;

  while (vma) {
    for (addr_t vaddr = vma->vm_start; vaddr < vma->vm_end; vaddr += PAGING64_PAGESZ) {
      addr_t pte = pte_get_entry(caller, PAGING64_PGN(vaddr));

      if (pte == 0) {
        continue;
      }

      if (PAGING_PAGE_PRESENT(pte)) {
        MEMPHY_put_freefp(caller->krnl->mram, PAGING_FPN(pte));
      } else {
        MEMPHY_put_freefp(caller->krnl->active_mswp, PAGING_SWP(pte));
      }

      pte_set_entry(caller, PAGING64_PGN(vaddr), 0);
    }
    vma = vma->vm_next;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* get_free_vmrg_area - First-fit logic with region splitting */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg) {
  if (size <= 0 || !newrg) {
    return -1;
  }

  struct vm_area_struct *cur_vma = get_vma_by_num(newrg->mode_bit ? caller->mm : caller->krnl->mm, vmaid);

  if (!cur_vma) {
    return -1;
  }

  struct vm_rg_struct **prev = &cur_vma->vm_freerg_list;
  struct vm_rg_struct *cur = cur_vma->vm_freerg_list;

  while (cur) {
    /* Skip zero-size sentinel nodes — never allocate from them */
    if (cur->rg_start >= cur->rg_end) {
      prev = &cur->rg_next;
      cur  = cur->rg_next;
      continue;
    }

    if (cur->rg_start + size <= cur->rg_end) {
      newrg->rg_start = cur->rg_start;
      newrg->rg_end   = cur->rg_start + size;
      newrg->vmaid    = vmaid;

      if (cur->rg_start + size < cur->rg_end) {
        /* Partial fit — shrink the node */
        cur->rg_start += size;
      } else {
        /* Perfect fit — remove the node entirely */
        *prev = cur->rg_next;
        free(cur);
      }
      return 0;
    }
    prev = &cur->rg_next;
    cur  = cur->rg_next;
  }
  return -1;
}

int __slab_alloc(struct pcb_t *caller, struct kcache_pool_struct *pool) {
  int slab_min_objs = 8;
  addr_t req_size = (addr_t)(pool->size * slab_min_objs + pool->align);
  addr_t slab_size = PAGING64_PAGE_ALIGNSZ(req_size);
  int rgid = -1;
  struct mm_struct *krnl_mm = caller->krnl->mm;

  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
    if (krnl_mm->symrgtbl[i].rg_start == 0 && krnl_mm->symrgtbl[i].rg_end == 0) {
      rgid = i;
      break;
    }
  }

  if (rgid == -1) {
    return -1;
  }

  addr_t new_slab_addr;
  if (__kmalloc(caller, 0, rgid, slab_size, &new_slab_addr) != 0) {
    return -1;
  }

  krnl_mm->symrgtbl[rgid].rg_start = 0;
  krnl_mm->symrgtbl[rgid].rg_end   = 0;
  krnl_mm->symrgtbl[rgid].mode_bit = 0;

  addr_t base_addr = new_slab_addr;
  if (pool->align > 0 && base_addr % pool->align != 0) {
    base_addr += pool->align - (base_addr % pool->align);
  }

  int num_objs = (int)(slab_size - (base_addr - new_slab_addr)) / pool->size;
  struct slab_struct *new_slab = malloc(sizeof(struct slab_struct));
  new_slab->addr = base_addr;
  new_slab->free_list = malloc((size_t)num_objs * sizeof(struct free_obj));
  new_slab->storage = 0;

  for (int i = 0; i < num_objs; i++) {
    new_slab->free_list[i].addr = base_addr + (addr_t)(i * pool->size);
    new_slab->free_list[i].next = (i < num_objs - 1) ? i + 1 : -1;
  }

  pthread_mutex_lock(&mmvm_lock);
  new_slab->next = pool->slabs;
  pool->slabs = new_slab;
  pool->storage = base_addr;
  pthread_mutex_unlock(&mmvm_lock);

  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id) {
  pthread_mutex_lock(&mmvm_lock);
  struct kcache_pool_struct *pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];
  if (align > 0 && size % align != 0) {
    size += align - (size % align);
  }

  pool->size = size;
  pool->align = align;
  pool->slabs = NULL;
  pool->storage = (addr_t) -1;

  pthread_mutex_unlock(&mmvm_lock);
  int ret = __slab_alloc(caller, pool);
  if (ret == -1) return -1;
  return ret;
}
/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr) {
  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *krnl_mm = caller->krnl->mm;
  struct kcache_pool_struct *pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];
  struct slab_struct *slab = NULL;

  while (1) {
    slab = pool->slabs;
    while (slab && slab->storage == (addr_t)-1) {
      slab = slab->next;
    }

    if (slab) break;
    pthread_mutex_unlock(&mmvm_lock);
    if (__slab_alloc(caller, pool) < 0)
      return (addr_t)-1;
    pthread_mutex_lock(&mmvm_lock);
  }

  int free_idx  = (int)slab->storage;
  *alloc_addr = slab->free_list[free_idx].addr;
  slab->storage = (addr_t)slab->free_list[free_idx].next;
  krnl_mm->symrgtbl[rgid].rg_start = *alloc_addr;
  krnl_mm->symrgtbl[rgid].rg_end = *alloc_addr + pool->size;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index) {
  addr_t alloc_addr;

  if (__kmem_cache_alloc(proc, 0, reg_index, cache_pool_id, &alloc_addr) != 0) {
    return -1;
  }
  return 0;
}

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data) {
  return __read(caller, vmaid, rgid, offset, data);
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value) {
  return __write(caller, vmaid, rgid, offset, value);
}

int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  if (!currg || (currg->rg_start == 0 && currg->rg_end == 0) || currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  krnl_pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  if (!currg || (currg->rg_start == 0 && currg->rg_end == 0) || currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  krnl_pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size) {
  struct vm_rg_struct *user_rg = get_symrg_byid(caller->mm, source);
  struct vm_rg_struct *krnl_rg = get_symrg_byid(caller->krnl->mm, destination);

  if (!user_rg || !krnl_rg || user_rg->rg_start + offset + size > user_rg->rg_end) {
    return -1;
  }

  BYTE data;
  for (uint32_t i = 0; i < size; i++) {
    __read_user_mem(caller, user_rg->vmaid, source, offset + i, &data);
    __write_kernel_mem(caller, krnl_rg->vmaid, destination, offset + i, data);
  }
  printf("libkmem_copy_from_user: pid=%d copy_from_user src=%d dst=%d off=%d size=%d\n",
         caller->pid, source, destination, offset, size);
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size) {
  /* source = kernel rgid, destination = user rgid */
  struct vm_rg_struct *krnl_rg = get_symrg_byid(caller->krnl->mm, source);
  struct vm_rg_struct *user_rg = get_symrg_byid(caller->mm, destination);

  if (!krnl_rg || !user_rg || krnl_rg->rg_start + offset + size > krnl_rg->rg_end) {
    return -1;
  }

  BYTE data;
  for (uint32_t i = 0; i < size; i++) {
    __read_kernel_mem(caller, krnl_rg->vmaid, source, offset + i, &data);
    __write_user_mem(caller, user_rg->vmaid, destination, offset + i, data);
  }
  printf("libkmem_copy_to_user: pid=%d copy_from_to src=%d dst=%d off=%d size=%d\n",
         caller->pid, source, destination, offset, size);
  return 0;
}
