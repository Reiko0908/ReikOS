#include "print_debug.h"
#include <stdlib.h>

void print_frame_list(const char *name, struct framephy_struct *head)
{
    printf("  %s:\n", name);

    if (!head)
    {
        printf("    (NULL / empty)\n");
        return;
    }

    while (head)
    {
        printf("    [fpn=%lu, owner=%p]\n",
               (unsigned long)head->fpn,
               (void *)head->owner);
        head = head->fp_next;
    }
}

void print_storage_int(struct memphy_struct *m)
{
    if (!m || !m->storage)
    {
        printf("storage is NULL\n");
        return;
    }

    printf("===== STORAGE DUMP =====\n");
    printf("base: %p\n", (void *)m->storage);

    int total = m->maxsz / sizeof(int);
    int per_row = 8;
    int limit = per_row * 3; // 3 hàng

    for (int i = 0; i < total && i < limit; i++)
    {
        if (i % per_row == 0)
            printf("\n%p : ", (void *)(m->storage + i * 4));

        int *p = (int *)(m->storage + i * 4);
        printf("%08x ", *p);
    }
    printf("\n");
}

void print_memphy(const char *name, struct memphy_struct *m)
{
    printf("\n===== %s =====\n", name);

    if (!m)
    {
        printf("(NULL)\n");
        return;
    }

    printf("max size : %d\n", m->maxsz);
    printf("rdm flag : %d\n", m->rdmflg);
    printf("cursor   : %d\n", m->cursor);

    print_storage_int(m);
    print_frame_list("FREE frames", m->free_fp_list);
    print_frame_list("USED frames", m->used_fp_list);
}

void print_kernel_info(struct krnl_t *os)
{
    printf("\n========== KERNEL INFO ==========\n");

    if (!os)
    {
        printf("Kernel NULL\n");
        return;
    }

    print_memphy("RAM", os->mram);

    // for (int i = 0; i < 4; i++)
    // {
    //     char name[32];
    //     sprintf(name, "SWAP[%d]", i);
    //     print_memphy(name, os->mswp[i]);
    // }

    // printf("Active swap ID: %u\n", os->active_mswp_id);

    print_mm_struct(os->mm);
}

void print_pcb_info(struct pcb_t *proc)
{
    if (!proc || !proc->krnl)
    {
        printf("Invalid PCB\n");
        return;
    }

    printf("\n===== PCB INFO =====\n");
    printf("PID: %u\n", proc->pid);

    struct mm_struct *mm = proc->krnl->mm;

    if (mm)
    {
        printf("MM: %p\n", (void *)mm);

        printf("FIFO: ");
        struct pgn_t *p = mm->fifo_pgn;
        while (p)
        {
            printf("%lu -> ", (unsigned long)p->pgn);
            p = p->pg_next;
        }
        printf("NULL\n");
    }
}

void print_mm_struct(struct mm_struct *mm)
{
    printf("\n===== MM STRUCT =====\n");

    if (!mm)
    {
        printf("mm = NULL\n");
        return;
    }

    printf("\n[PAGE TABLE]\n");

    printf("PGD: %p\n", (void *)mm->pgd);
    printf("P4D: %p\n", (void *)mm->p4d);
    printf("PUD: %p\n", (void *)mm->pud);
    printf("PMD: %p\n", (void *)mm->pmd);
    printf("PT : %p\n", (void *)mm->pt);

    /* --- VMA list --- */
    printf("\n[VMA LIST]\n");
    struct vm_area_struct *vma = mm->mmap;

    if (!vma)
        printf("  (empty)\n");

    while (vma)
    {
        printf("  VMA id=%lu start=%lu end=%lu sbrk=%lu\n",
               vma->vm_id,
               (unsigned long)vma->vm_start,
               (unsigned long)vma->vm_end,
               (unsigned long)vma->sbrk);

        /* free region list */
        struct vm_rg_struct *rg = vma->vm_freerg_list;
        printf("    free regions:\n");
        while (rg)
        {
            printf("      [%lu - %lu]\n",
                   (unsigned long)rg->rg_start,
                   (unsigned long)rg->rg_end);
            rg = rg->rg_next;
        }

        vma = vma->vm_next;
    }

    /* --- Symbol table --- */
    printf("\n[SYMBOL TABLE]\n");
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
        if (mm->symrgtbl[i].rg_start != 0 ||
            mm->symrgtbl[i].rg_end != 0)
        {
            printf("  rgid=%d: [%lu - %lu]\n",
                   i,
                   (unsigned long)mm->symrgtbl[i].rg_start,
                   (unsigned long)mm->symrgtbl[i].rg_end);
        }
    }

    /* --- FIFO page list --- */
    // printf("\n[FIFO PAGE LIST]\n");
    // struct pgn_t *p = mm->fifo_pgn;

    // if (!p)
    //     printf("  (empty)\n");

    // while (p)
    // {
    //     printf("  pgn=%lu -> ", (unsigned long)p->pgn);
    //     p = p->pg_next;
    // }
    // printf("NULL\n");

    printf("======================\n");
}