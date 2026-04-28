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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (!mp->rdmflg) return -1; /* Not compatible mode for sequential read */
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

   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

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
   if (mp == NULL)
      return -1;

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
   /* This setting come with fixed constant PAGESZ */
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0)
      return -1;

   /* Init head of free framephy list */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   mp->free_fp_list = fst;

   /* We have list with first element, fill in the rest num-1 element member*/
   for (iter = 1; iter < numfp; iter++)
   {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp = mp->free_fp_list;

   if (fp == NULL)
      return -1;

   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;

   /* MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   free(fp);

   return 0;
}

int MEMPHY_get_contiguous_freefp(struct memphy_struct *mp, int req_pgnum, struct framephy_struct **frm_lst)
{
  if (!mp || req_pgnum <= 0 || mp->maxsz <= 0) return -1;
  int fpnum = (mp->maxsz >> PAGING64_ADDR_PT_SHIFT);
  BYTE *freefp_tbl = (BYTE *)calloc(fpnum, sizeof(BYTE));
  if (!freefp_tbl) return -1;

  struct *cur = mp->free_fp_list;
  while (cur) {
    if (cur->fpn < fpnum) 
      freefp_tbl[cur->fpn] = 1;
    cur = cur->fp_next;
  }
  
  int start_fpn = -1, count = 0;
  for (int i = 0; i < fpnum; i++) {
    if (freefp_table[i] == 1) {
      if (count == 0) start_fpn = i;
      count++;
      if (count == req_pgnum) break;
    } else {
      count = 0;
      start_fpn = -1;
    }
  }
  free(freefp_tbl);
  if (count < req_pgnum) return -1;
  struct framephy_struct **slots = calloc(req_pgnum, sizeof(*slots));
  if (!slots) return -1;
  struct framephy_struct *prev = NULL;
  cur = mp->free_fp_list;
  int found = 0;
  while (cur && found < req_pgnum) {
    struct framephy_struct *next = cur->fp_next;
    if (cur->fpn >= start_fpn && cur->fpn < start_fpn + req_pgnum) {
      if (prev) prev->fp_next = next;
      else mp->free_fp_list = next;
      int idx = cur->fpn - start_fpn;
      slots[idx] = cur;
      found++;
    } else {
      prev = cur;
    }
    cur = next;
  }
  for (int i = 0; i < req_pgnum; i++) {
    slots[i]->fp_next = slots[i + 1];
  }
  slots[req_pgnum - 1]->fp_next = NULL;
  *frm_lst = slots[0];
  return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
    if (!mp || !mp->strorage) return -1;
    for (int i = 0; i < mp->maxsz; i++) {
      printf("%02x ", mp->storage[i]);
      if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   struct framephy_struct *fp = mp->free_fp_list;
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));

   /* Create new node with value fpn */
   newnode->fpn = fpn;
   newnode->fp_next = fp;
   mp->free_fp_list = newnode;

   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   memset(mp->storage, 0, max_size * sizeof(BYTE));

   MEMPHY_format(mp, PAGING_PAGESZ);

   mp->rdmflg = (randomflg != 0) ? 1 : 0;

   if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

// #endif
