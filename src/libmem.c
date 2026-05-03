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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt) {
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;
  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid) {
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr) {
  /*Allocate at the toproof */
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

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!cur_vma) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  int old_sbrk = cur_vma->sbrk;

  /*Attempt to increate limit to get space */
  int inc_sz = 0;
#ifdef MM64
  inc_sz = PAGING64_PAGE_ALIGNSZ(size);
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  arg_t a1 = SYSMEM_INC_OP;
  arg_t a2 = vmaid;
  arg_t a3 = inc_sz;
  libsyscall(caller, 17, a1, a2, a3); /* SYSCALL 17 sys_memmap */

  /* Failed: Out of memory */
  if (cur_vma->sbrk == old_sbrk) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; 
  }

  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->mm->symrgtbl[rgid].mode_bit = 1;
  caller->mm->symrgtbl[rgid].vmaid = cur_vma->vm_id;
  *alloc_addr = old_sbrk;

  /* Fragmentation recovery */
  if (old_sbrk + size < cur_vma->sbrk) {
    struct vm_rg_struct *free_tail_rg = malloc(sizeof(struct vm_rg_struct));
    if (free_tail_rg) {
      free_tail_rg->rg_start = old_sbrk + size;
      free_tail_rg->rg_end = cur_vma->sbrk;
      free_tail_rg->mode_bit = 1;
      free_tail_rg->vmaid = cur_vma->vm_id;
      free_tail_rg->rg_next = NULL;
      /* Enlist the new region */
      enlist_vm_freerg_list(caller->mm, free_tail_rg);
    }
  }
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
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
  if (!rgnode) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (rgnode->rg_start == 0 && rgnode->rg_end == 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (rgnode->vmaid != vmaid) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (!freerg_node) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;
  freerg_node->vmaid = rgnode->vmaid;
  enlist_vm_freerg_list(caller->mm, freerg_node);

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index) {
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1) return -1;
  proc->regs[reg_index] = addr;
#ifdef IODUMP
  printf("[LIBALLOC] PID %d: alloc [" FORMAT_ADDR "] [%u]\n", proc->pid, size, reg_index);
#endif
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  addr_t address = proc->regs[reg_index];
  int vmaid = vm_lookup_vmaid(proc->mm, address);
  if (vmaid == -1) return -1;
  int val = __free(proc, vmaid, reg_index);
  if (val == -1) return -1;
#ifdef IODUMP
  printf("[LIBFREE] PID %d: free [%u]\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller) {
  uint32_t pte = pte_get_entry(caller, pgn);
  if (!PAGING_PAGE_PRESENT(pte)) {
    addr_t vicpgn, vicfpn, swpfpn;
    if (find_victim_page(caller->mm, &vicpgn) != 0)
      return -1;
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0)
      return -1;

    vicfpn = PAGING_FPN(pte_get_entry(caller, vicpgn));
    struct sc_regs regs;
    /* TODO copy victim frame to swap
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */

    arg_t a1 = SYSMEM_SWP_OP;
    arg_t a2 = vicfpn;
    arg_t a3 = swpfpn;
    libsyscall(caller, 17, a1, a2, a3);
    pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

    /* 2. nếu target ở SWAP → swap-in */
    if (pte & PAGING_PTE_SWAPPED_MASK) {
      addr_t tgtfpn = PAGING_SWP(pte);
      a2 = tgtfpn;
      a3 = vicfpn;
      libsyscall(caller, 17, a1, a2, a3);
      MEMPHY_put_freefp(caller->krnl->active_mswp, tgtfpn);
    }
    pte_set_fpn(caller, pgn, vicfpn);
    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }
  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
  return 0;
}

int krnl_pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller) {
  uint32_t pte = krnl_pte_get_entry(caller, pgn);
  /* Kernel pages must always be present — never swapped */
  if (!PAGING_PAGE_PRESENT(pte)) return -1;
  *fpn = PAGING_FPN(pte);
  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data,
              struct pcb_t *caller) {
  int pgn = PAGING64_PGN(addr);
  int off = PAGING64_OFFST(addr);
  int fpn;
  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1;
  int phyaddr = (fpn << PAGING64_ADDR_FPN_LOBIT) + off;

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
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
int pg_setval(struct mm_struct *mm, int addr, BYTE value,
              struct pcb_t *caller) {
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1;
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
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
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset,
           BYTE *data) {
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!currg || !cur_vma) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(curr->rg_end <= currg->rg_start) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  adrd_t addr = currg->rg_start + offset;
  if(addr < currg->rg_start || addr >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if(pg_getval(caller->mm, currg->rg_start + offset, data, caller) != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(struct pcb_t *proc, // Process executing the instruction
            uint32_t source,    // Index of source register
            addr_t offset,      // Source address = [source] + [offset]
            uint32_t *destination) {
  BYTE data;
  printf("%s:%d\n", __func__, __LINE__);
  if (!IS_USER_SPACE(offset + proc->regs[source])) return -1;
  int val = __read(proc, 0, source, offset, &data);
  if (val == 0) proc->regs[*destination] = (addr_t)data;

#ifdef IODUMP
  printf("[LIBREAD] PID %d: read [%u] [" FORMAT_ADDR "] [%u]\n", proc->pid, source, offset, *destination);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
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
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value) 
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  pg_setval(caller->mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(struct pcb_t *proc,   // Process executing the instruction
             BYTE data,            // Data to be wrttien into memory
             uint32_t destination, // Index of destination register
             addr_t offset) {
  addr_t addr = proc->regs[destination] + offset;
  if (!IS_USER_SPACE(addr)) return -1;
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1) return -1;
#ifdef IODUMP
  printf("[LIBWRITE] PID %d: write [%u] [%u] [" FORMAT_ADDR "]\n", proc->pid, data, destination, offset);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return val;
}

/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index) {
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t  addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */

  return 0;
}

/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size,
                 addr_t *alloc_addr) {
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->symrgtbl...
  // krnl->krnl_pgd ...

  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size,
                              uint32_t align, uint32_t cache_pool_id) {
  /* TODO: provide OS level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has
 * identical size the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id,
                        uint32_t reg_index) {
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid,
                          int cache_pool_id, addr_t *alloc_addr) {
  /* TODO: provide OS level management */
  /* TODO: provide OS level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->symrgtbl...
  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...

  return 0;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source,
                           uint32_t destination, uint32_t offset,
                           uint32_t size) {
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source,
                         uint32_t destination, uint32_t offset, uint32_t size) {
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  return 1;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset,
                      BYTE *data) {
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset,
                       BYTE value) {
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset,
                    BYTE *data) {
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ...

  return 0;
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset,
                     BYTE value) {
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ...

  return 0;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller) {
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++) {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte)) {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    } else {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn) {
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg) {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next) {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size,
                       struct vm_rg_struct *newrg) {
  struct vm_area_struct *cur_vma;
  if (newrg->mode_bit == 0) cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  else cur_vma = get_vma_by_num(caller->mm, vmaid);
  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  if (rgit == NULL) return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL) {
    if (rgit->rg_start + size <= rgit->rg_end) {
      /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end) {
        rgit->rg_start = rgit->rg_start + size;
      } else { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL) {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;
          rgit->rg_next = nextrg->rg_next;
          free(nextrg);
        } else {                         /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    } else {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
