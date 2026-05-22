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

#include "mm.h"
#include "mm64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =======================================================================
 * Internal helpers for free_fp_list / used_fp_list
 * ======================================================================= */
int fp_list_push(struct framephy_struct **head, addr_t fpn, int order) {
  struct framephy_struct *node = malloc(sizeof(struct framephy_struct));
  if (!node)
    return -1;
  node->fpn = fpn;
  node->order = order;
  node->owner = NULL;
  node->fp_next = *head;
  *head = node;
  return 0;
}

int fp_list_remove(struct framephy_struct **head, addr_t target) {
  struct framephy_struct *prev = NULL;
  struct framephy_struct *cur = *head;
  while (cur) {
    if (cur->fpn == target) {
      if (prev)
        prev->fp_next = cur->fp_next;
      else
        *head = cur->fp_next;
      free(cur);
      return 0;
    }
    prev = cur;
    cur = cur->fp_next;
  }
  return -1;
}

static void fp_list_free_all(struct framephy_struct **head) {
  struct framephy_struct *cur = *head;
  while (cur) {
    struct framephy_struct *next = cur->fp_next;
    free(cur);
    cur = next;
  }
  *head = NULL;
}

/* =======================================================================
 * Buddy Allocator
 * ======================================================================= */

int buddy_init(struct buddy_system *bs, int total_frames) {
  if (!bs || total_frames <= 0)
    return -1;
  memset(bs->free_lists, 0, sizeof(bs->free_lists));
  bs->total_frames = total_frames;
  int fpn = 0, rem = total_frames;
  while (rem > 0) {
    int order = BUDDY_MAX_ORDER;
    while (order > 0) {
      int blk_sz = 1 << order;
      if (fpn % blk_sz == 0 && rem >= blk_sz)
        break;
      order--;
    }
    struct buddy_block *blk = malloc(sizeof(struct buddy_block));
    if (!blk)
      return -1;
    blk->fpn = fpn;
    blk->order = order;
    blk->next = bs->free_lists[order];
    bs->free_lists[order] = blk;
    fpn += 1 << order;
    rem -= 1 << order;
  }
  return 0;
}

int buddy_alloc(struct buddy_system *bs, int order, int *fpn) {
  if (!bs || !fpn || order < 0 || order > BUDDY_MAX_ORDER)
    return -1;
  int cur_order = order;
  while (cur_order <= BUDDY_MAX_ORDER && bs->free_lists[cur_order] == NULL)
    cur_order++;
  if (cur_order > BUDDY_MAX_ORDER)
    return -1;

  struct buddy_block *blk = bs->free_lists[cur_order];
  bs->free_lists[cur_order] = blk->next;
  while (cur_order > order) {
    cur_order--;
    struct buddy_block *split = malloc(sizeof(struct buddy_block));
    if (!split) {
      free(blk);
      return -1;
    }
    split->fpn = blk->fpn + (1 << cur_order);
    split->order = cur_order;
    split->next = bs->free_lists[cur_order];
    bs->free_lists[cur_order] = split;
  }
  *fpn = blk->fpn;
  free(blk);
  return 0;
}

int buddy_free(struct buddy_system *bs, int order, int fpn) {
  if (!bs || order < 0 || order > BUDDY_MAX_ORDER)
    return -1;
  while (order < BUDDY_MAX_ORDER) {
    int buddy_fpn = fpn ^ (1 << order);
    if (buddy_fpn >= bs->total_frames)
      break;
    struct buddy_block *prev = NULL;
    struct buddy_block *cur = bs->free_lists[order];
    while (cur && cur->fpn != buddy_fpn) {
      prev = cur;
      cur = cur->next;
    }
    if (!cur)
      break;
    if (prev)
      prev->next = cur->next;
    else
      bs->free_lists[order] = cur->next;
    free(cur);
    fpn = (fpn < buddy_fpn) ? fpn : buddy_fpn;
    order++;
  }
  struct buddy_block *blk = malloc(sizeof(struct buddy_block));
  if (!blk)
    return -1;
  blk->fpn = fpn;
  blk->order = order;
  blk->next = bs->free_lists[order];
  bs->free_lists[order] = blk;
  return 0;
}

/* =======================================================================
 * MEMPHY device I/O
 * ======================================================================= */

int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset) {
  int numstep = 0;
  mp->cursor = 0;
  while (numstep < offset && numstep < mp->maxsz) {
    mp->cursor = (mp->cursor + 1) % mp->maxsz;
    numstep++;
  }
  return 0;
}

int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value) {
  if (mp == NULL)
    return -1;
  MEMPHY_mv_csr(mp, addr);
  *value = (BYTE)mp->storage[addr];
  return 0;
}

int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value) {
  if (mp == NULL)
    return -1;
  if (mp->rdmflg)
    *value = mp->storage[addr];
  else
    return MEMPHY_seq_read(mp, addr, value);
  return 0;
}

int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value) {
  if (mp == NULL)
    return -1;
  MEMPHY_mv_csr(mp, addr);
  mp->storage[addr] = value;
  return 0;
}

int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data) {
  if (mp == NULL)
    return -1;
  if (mp->rdmflg)
    mp->storage[addr] = data;
  else
    return MEMPHY_seq_write(mp, addr, data);
  return 0;
}

/* =======================================================================
 * Frame allocation / deallocation
 * ======================================================================= */

/*
 * MEMPHY_format - initialise buddy and rebuild free_fp_list from it.
 *
 * free_fp_list mirrors buddy.free_lists[] so that callers can iterate
 * a single flat list of free frames (e.g. print_frame_list).
 * used_fp_list starts empty; frames move into it on every alloc.
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz) {
  if (!mp || pagesz <= 0)
    return -1;
  int numfp = mp->maxsz / PAGING64_PAGESZ;
  if (numfp <= 0)
    return -1;

  fp_list_free_all(&mp->free_fp_list);
  fp_list_free_all(&mp->used_fp_list);

  if (buddy_init(&mp->buddy, numfp) != 0)
    return -1;
  mp->buddy_initialized = 1;

  /* Build free_fp_list: one node per frame (order 0) so the list is
   * a flat enumeration of every free physical frame number.
   * Iterate high→low so that head-insertion yields ascending [0][1][2]...*/
  for (int ord = BUDDY_MAX_ORDER; ord >= 0; ord--) {
    struct buddy_block *blk = mp->buddy.free_lists[ord];
    while (blk) {
      int count = 1 << ord;
      for (int i = count - 1; i >= 0; i--)
        fp_list_push(&mp->free_fp_list, (addr_t)(blk->fpn + i), 0);
      blk = blk->next;
    }
  }
  return 0;
}

/*
 * MEMPHY_get_freefp - allocate a single frame.
 * Moves it from free_fp_list → used_fp_list.
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn) {
  if (!mp || !retfpn || !mp->buddy_initialized)
    return -1;
  int fpn;
  if (buddy_alloc(&mp->buddy, 0, &fpn) != 0)
    return -1;
  fp_list_remove(&mp->free_fp_list, (addr_t)fpn);
  fp_list_push(&mp->used_fp_list, (addr_t)fpn, 0);
  *retfpn = (addr_t)fpn;
  return 0;
}

/*
 * MEMPHY_get_contiguous_freefp - allocate 2^order contiguous frames.
 * Each frame in the block moves from free_fp_list → used_fp_list
 * as individual order-0 entries so the list stays frame-granular.
 */
int MEMPHY_get_contiguous_freefp(struct memphy_struct *mp, int req_pgnum,
                                 addr_t *retfpn) {
  if (!mp || !retfpn || !mp->buddy_initialized || req_pgnum <= 0)
    return -1;
  int order = 0;
  while ((1 << order) < req_pgnum)
    order++;
  int fpn;
  if (buddy_alloc(&mp->buddy, order, &fpn) != 0)
    return -1;
  int count = 1 << order;
  for (int i = 0; i < count; i++) {
    fp_list_remove(&mp->free_fp_list, (addr_t)(fpn + i));
    fp_list_push(&mp->used_fp_list, (addr_t)(fpn + i), 0);
  }
  *retfpn = (addr_t)fpn;
  return 0;
}

/*
 * MEMPHY_put_freefp - release a single frame.
 * Moves it from used_fp_list → free_fp_list.
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn) {
  if (!mp || !mp->buddy_initialized)
    return -1;
  fp_list_remove(&mp->used_fp_list, fpn);
  if (buddy_free(&mp->buddy, 0, (int)fpn) != 0)
    return -1;
  fp_list_push(&mp->free_fp_list, fpn, 0);
  return 0;
}

int MEMPHY_put_contiguous_freefp(struct memphy_struct *mp, addr_t fpn) {
  if (!mp || !mp->buddy_initialized)
    return -1;
  struct framephy_struct *cur = mp->used_fp_list;
  int order = -1;
  while (cur) {
    if (cur->fpn == fpn) {
      order = cur->order;
      break;
    }
    cur = cur->fp_next;
  }
  if (order < 0)
    return -1;

  int count = 1 << order;
  /* Remove all frames in the block from used list and add to free list */
  for (int i = 0; i < count; i++) {
    fp_list_remove(&mp->used_fp_list, (addr_t)(fpn + i));
    fp_list_push(&mp->free_fp_list, (addr_t)(fpn + i), 0);
  }
  /* Free the whole block once at the base frame, not per-frame */
  if (buddy_free(&mp->buddy, order, (int)fpn) != 0)
    return -1;
  return 0;
}

/*
 * MEMPHY_set_fp_owner - stamp the owner on a used_fp_list node.
 * Call after MEMPHY_get_freefp / MEMPHY_get_contiguous_freefp.
 */
int MEMPHY_set_fp_owner(struct memphy_struct *mp, addr_t fpn, void *owner) {
  if (!mp)
    return -1;
  struct framephy_struct *cur = mp->used_fp_list;
  while (cur) {
    if (cur->fpn == fpn) {
      cur->owner = owner;
      return 0;
    }
    cur = cur->fp_next;
  }
  return -1;
}

/* =======================================================================
 * Diagnostics
 * ======================================================================= */
int MEMPHY_dump(struct memphy_struct *mp) {
  if (!mp || !mp->storage)
    return -1;
  for (int i = 0; i < mp->maxsz; i++) {
    printf("%02x ", mp->storage[i]);
    if ((i + 1) % 16 == 0)
      printf("\n");
  }
  printf("\n");
  return 0;
}

/* =======================================================================
 * Initialisation
 * ======================================================================= */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg) {
  mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
  if (!mp->storage)
    return -1;
  mp->maxsz = max_size;
  memset(mp->storage, 0, max_size * sizeof(BYTE));

  mp->rdmflg = (randomflg != 0) ? 1 : 0;
  mp->cursor = 0;

  mp->buddy_initialized = 0;
  for (int i = 0; i <= BUDDY_MAX_ORDER; i++)
    mp->buddy.free_lists[i] = NULL;

  mp->free_fp_list = NULL;
  mp->used_fp_list = NULL;

  return MEMPHY_format(mp, PAGING64_PAGESZ);
}

// #endif
