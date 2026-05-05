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
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/mm.h"
#include "../include/mm64.h"

/* =======================================================================
 * Buddy Allocator
 * ======================================================================= */
/*
 * buddy_init - partition [0, total_frames) into naturally-aligned
 * power-of-two blocks and insert each into the matching free list.
 */
int buddy_init(struct buddy_system *bs, int total_frames) {
  if (!bs || total_frames <= 0) return -1;
  memset(bs->free_lists, 0, sizeof(bs->free_lists));
  bs->total_frames = total_frames;
  int fpn = 0, rem = total_frames;
  while (rem > 0) {
    int order = BUDDY_MAX_ORDER;
    while (order > 0) {
      int blk_sz = 1 << order;
      if (fpn % blk_sz == 0 && rem >= blk_sz) break;
      order--;
    }
    struct buddy_block *blk = malloc(sizeof(struct buddy_block));
    if (!blk) return -1;
    blk->fpn = fpn;
    blk->order = order;
    blk->next = bs->free_lists[order];
    bs->free_lists[order] = blk;
    fpn += 1 << order;
    rem -= 1 << order;
  }
  return 0;
} 

/*
 * buddy_alloc - Allocate a block of 2^order contiguos frames.
 */
int buddy_alloc(struct buddy_system *bs, int order, int *fpn) {
  if (!bs || !fpn || order < 0 || order > BUDDY_MAX_ORDER) return -1;
  int cur_order = order;
  while (cur_order <= BUDDY_MAX_ORDER && bs->free_lists[cur_order] == NULL)
    cur_order++;
  if (cur_order > BUDDY_MAX_ORDER) return -1;
  
  struct buddy_block *blk = bs->free_lists[cur_order];
  bs->free_lists[cur_order] = blk->next;
  while (cur_order > order) {
    cur_order--;
    struct buddy_block *split = malloc(sizeof(struct buddy_block));
    if (!split) {free(blk); return -1;}
    split->fpn = blk->fpn + (1 << cur_order);
    split->order = cur_order;
    split->next = bs->free_lists[cur_order];
    bs->free_lists[cur_order] = split;
  }
  *fpn = blk->fpn;
  free(blk);
  return 0;
}

/*
 * buddy_free - Return a block of 2^order contiguous frames starting at fpn.
 */
int buddy_free(struct buddy_system *bs, int order, int fpn) {
  if (!bs || order < 0 || order > BUDDY_MAX_ORDER) return -1;
  while (order < BUDDY_MAX_ORDER) {
    int buddy_fpn = fpn ^ (1 << order);
    if (buddy_fpn >= bs->total_frames) break;
    struct buddy_block *prev = NULL;
    struct buddy_block *cur = bs->free_lists[order];
    while (cur && cur->fpn != buddy_fpn) {
      prev = cur;
      cur = cur->next;
    }
    if (!cur) break;
    if (prev) prev->next = cur->next;
    else bs->free_lists[order] = cur->next;
    free(cur);
    
    fpn = (fpn < buddy_fpn) ? fpn : buddy_fpn;
    order++;
  }
  struct buddy_block *blk = malloc(sizeof(struct buddy_block));
  if (!blk) return -1;
  blk->fpn = fpn;
  blk->order = order;
  blk->next = bs->free_lists[order];
  bs->free_lists[order] = blk;
  return 0;
}

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
  int numstep = 0;
  mp->cursor = 0;
  while (numstep < offset && numstep < mp->maxsz) {
    /* Traverse sequentially */
    mp->cursor = (mp->cursor + 1) % mp->maxsz;
    numstep++;
  }
  return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
  if (mp == NULL) return -1;
  MEMPHY_mv_csr(mp, addr);
  *value = (BYTE)mp->storage[addr];
  return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
  if (mp == NULL) return -1;
  if (mp->rdmflg)
    *value = mp->storage[addr];
  else /* Sequential access device */
    return MEMPHY_seq_read(mp, addr, value);
  return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
  if (mp == NULL) return -1;
  MEMPHY_mv_csr(mp, addr);
  mp->storage[addr] = value;
  return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
  if (mp == NULL) return -1;

  if (mp->rdmflg)
    mp->storage[addr] = data;
  else /* Sequential access device */
    return MEMPHY_seq_write(mp, addr, data);
  return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
  if (!mp || pagesz <= 0) return -1;
  int numfp = mp->maxsz / pagesz;
  if (numfp <= 0) return -1;
  if (buddy_init(&mp->buddy, numfp) != 0) return -1;
  mp->buddy_initialized = 1;
  return 0;
}

/*
 * MEMPHY_get_freefp - allocate a single frame
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
  if (!mp || !retfpn || !mp->buddy_initialized) return -1;
  int fpn;
  if (buddy_alloc(&mp->buddy, 0, &fpn) != 0) return -1;
  *retfpn = (addr_t)fpn;
  return 0;
}

/*
 * MEMPHY_get_contiguous_freefp - allocate 2^order contiguous frames.
 */
int MEMPHY_get_contiguous_freefp(struct memphy_struct *mp, int req_pgnum, addr_t *retfpn)
{
  if (!mp || !retfpn || !mp->buddy_initialized || req_pgnum <= 0) return -1;

  int order = 0;
  while ((1 << order) < req_pgnum) order++;
  int fpn;
  if (buddy_alloc(&mp->buddy, order, &fpn) != 0) return -1;
  *retfpn = (addr_t)fpn;
  return 0;
}

/*
 * MEMPHY_dump - print raw storage bytes in hex, 16 per line.
 *  @mp : memphy struct
 */
int MEMPHY_dump(struct memphy_struct *mp)
{
  if (!mp || !mp->storage) return -1;
  for (int i = 0; i < mp->maxsz; i++) {
    printf("%02x ", mp->storage[i]);
    if ((i + 1) % 16 == 0) printf("\n");
  }
  printf("\n");
  return 0;
}

/*
 * MEMPHY_put_freefp - release a single frame 
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
  if (!mp || !mp->buddy_initialized) return -1;
  return buddy_free(&mp->buddy, 0, (int)fpn);
}

/*
 * MEMPHY_put_contiguous_freefp - release a block of 2^order frames.
 */
int MEMPHY_put_contiguous_freefp(struct memphy_struct *mp, addr_t fpn, int req_pgnum)
{
  if (!mp || !mp->buddy_initialized || req_pgnum <= 0) return -1;
  int order = 0;
  while ((1 << order) < req_pgnum) order++;
  return buddy_free(&mp->buddy, order, (int)fpn);
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
  mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
  mp->maxsz = max_size;
  memset(mp->storage, 0, max_size * sizeof(BYTE));
  mp->rdmflg = (randomflg != 0) ? 1 : 0;
  if (mp->rdmflg)
    mp->cursor = 0;
  else
    mp->cursor = 1;
  mp->buddy_initialized = 0;
  if (MEMPHY_format(mp, PAGING64_PAGESZ) != 0) {
    free(mp->storage);
    mp->storage = NULL;
    return -1;
  }
  return 0;
}

// #endif
