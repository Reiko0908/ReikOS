/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

// #include <pthread.h>
#include <stdint.h>

#define MM_PAGING
#define PAGING_MAX_MMSWP 4 /* max number of supported swapped space */
#define PAGING_MAX_SYMTBL_SZ 30
#define MAX_CACHE_POOL 100
#define BUDDY_MAX_ORDER 10

/* 
 * @bksysnet: in long address mode of 64bit or original 32bit
 * the address type need to be redefined
 */

#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;
//typedef unsigned int uint32_t;

/* 
 * @bksysnet: the format string need to be redefined
 *            based on the address mode
 */
#ifdef MM64
#define FORMAT_ADDR "%lu"
#define FORMATX_ADDR "%16p"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

struct pgn_t{
  addr_t pgn;
  struct pgn_t *pg_next; 
};

/*
 *  Memory region struct
 */
struct vm_rg_struct {
  int vmaid;
  addr_t rg_start;
  addr_t rg_end;

  /* Priviledged mode bit
  * usermode - mode bit = 1
  * kernelmode - mode bit = 0
  */
  unsigned int mode_bit;
  struct vm_rg_struct *rg_next;
};

/*
 *  Memory area struct
 */
struct vm_area_struct {
  unsigned long vm_id;
  addr_t vm_start;
  addr_t vm_end;

  addr_t sbrk;
  /*
 * Derived field
 * unsigned long vm_limit = vm_end - vm_start
 */
  struct mm_struct *vm_mm;
  struct vm_rg_struct *vm_freerg_list;
  struct vm_area_struct *vm_next;
};

/*
 * Free object struct
 */
struct free_obj {
   addr_t addr;
   int next;
};

/*
 * Kernel slab struct
 */
struct slab_struct {
   addr_t addr;
   struct free_obj *free_list;
#ifdef MM64
   addr_t storage;
#else
   uint32_t storage;
#endif
   struct slab_struct *next;
};

/* 
 * Kernel cache pool struct
 */
struct kcache_pool_struct {
   int size;
   int align;
   struct slab_struct *slabs;
#ifdef MM64
   addr_t storage; // index of the first available slot in a slab
#else
   uint32_t storage;
#endif
};

/* 
 * Memory management struct
 */
struct mm_struct {
#ifdef MM64
  addr_t *pgd;
  addr_t *p4d;
  addr_t *pud;
  addr_t *pmd;
  addr_t *pt;
#else
  uint32_t *pgd;
#endif

  struct vm_area_struct *mmap;

  /* Currently we support a fixed number of symbol */
  struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

  /* list of free page */
  struct pgn_t *fifo_pgn;

  /* kmem cache pool */
  struct kcache_pool_struct *kcpooltbl;
};

/*
 * Buddy allocator
 */
struct buddy_block {
    int fpn;                    /* start frame number */
    int order;                  /* block size = 2^order frames */
    struct buddy_block *next;
};

struct buddy_system {
    struct buddy_block *free_lists[BUDDY_MAX_ORDER + 1];
    int total_frames;
};

/*
 * FRAME/MEM PHY struct
 */
struct framephy_struct {
    addr_t fpn;       /* frame page number */
    int    order;     /* buddy order: block covers 2^order frames */
    struct framephy_struct *fp_next;
    void *owner;      /* mm_struct that owns this frame */
};


struct memphy_struct {
//   pthread_mutex_t lock;
  BYTE   *storage;
  addr_t  maxsz;
  int     rdmflg;
  addr_t  cursor;

  struct buddy_system buddy;
  int     buddy_initialized;
 
  struct framephy_struct *free_fp_list;
  struct framephy_struct *used_fp_list;
};

#endif
