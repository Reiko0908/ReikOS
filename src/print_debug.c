#include "print_debug.h"
#include <stdlib.h>
#include <string.h>

/* ── forward declarations ──────────────────────────────────────────── */
static uint32_t pte_get_entry_print(struct mm_struct *mm, addr_t pgn);
static void print_symtbl_merged(struct mm_struct *user_mm, struct mm_struct *krnl_mm);

/* ══════════════════════════════════════════════════════════════════════
 *  Low-level helpers
 * ══════════════════════════════════════════════════════════════════════ */

void print_frame_list(const char *name, struct framephy_struct *head)
{
    printf("%s: ", name);
    if (!head) { printf("(empty)\n"); return; }
    for (; head; head = head->fp_next)
        printf("[%lu] ", (unsigned long)head->fpn);
    printf("\n");
}

void print_storage_int(struct memphy_struct *m)
{
    if (!m || !m->storage) { printf("storage is NULL\n"); return; }

    printf("STORAGE\n");
    int ints_per_page = PAGING64_PAGESZ / sizeof(int);
    int total_pages   = m->maxsz / PAGING64_PAGESZ;

    for (int pg = 0; pg < total_pages; pg++) {
        printf("[PAGE %d] : ", pg);
        for (int i = 0; i < 16; i++) {
            int idx = pg * ints_per_page + i;
            printf("%08x ", *(int *)(m->storage + idx * sizeof(int)));
        }
        printf("\n");
    }
}

void print_memphy(const char *name, struct memphy_struct *m)
{
    printf("%s | size:%d\n", name, m->maxsz);
    print_storage_int(m);
    print_frame_list("FREE frames", m->free_fp_list);
    print_frame_list("USED frames", m->used_fp_list);
    printf("======================\n");
}

/* ── 5-level page-table walk for the kernel's own pgd ─────────────── */
uint32_t pte_get_entry_kernel_print(struct krnl_t *os, addr_t pgn)
{
    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);

    if (!os->krnl_pgd || os->krnl_pgd[pgd_i] == 0) return 0;
    addr_t *p4d = (addr_t *)os->krnl_pgd[pgd_i];
    if (!p4d || p4d[p4d_i] == 0) return 0;
    addr_t *pud = (addr_t *)p4d[p4d_i];
    if (!pud || pud[pud_i] == 0) return 0;
    addr_t *pmd = (addr_t *)pud[pud_i];
    if (!pmd || pmd[pmd_i] == 0) return 0;
    addr_t *pt  = (addr_t *)pmd[pmd_i];
    if (!pt) return 0;
    return (uint32_t)pt[pt_i];
}

/* ── 5-level page-table walk for a user mm ────────────────────────── */
static uint32_t pte_get_entry_print(struct mm_struct *mm, addr_t pgn)
{
    addr_t pgd_i = 0, p4d_i = 0, pud_i = 0, pmd_i = 0, pt_i = 0;
    get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);

    if (!mm->pgd || mm->pgd[pgd_i] == 0) return 0;
    addr_t *p4d = (addr_t *)mm->pgd[pgd_i];
    if (!p4d || p4d[p4d_i] == 0) return 0;
    addr_t *pud = (addr_t *)p4d[p4d_i];
    if (!pud || pud[pud_i] == 0) return 0;
    addr_t *pmd = (addr_t *)pud[pud_i];
    if (!pmd || pmd[pmd_i] == 0) return 0;
    addr_t *pt  = (addr_t *)pmd[pmd_i];
    if (!pt) return 0;
    return (uint32_t)pt[pt_i];
}

/* ── Print PAGE TABLE entries for a given mm (6 entries) ─────────── */
static void print_page_table(struct mm_struct *mm)
{
    printf("PAGE TABLE: ");
    for (int i = 0; i < 6; i++) {
        uint32_t pte = pte_get_entry_print(mm, i);
        if      (PAGING_PAGE_PRESENT(pte))      printf("[page=%d -> frame=%u] ", i, PAGING_PTE_FPN(pte));
        else if (pte & PAGING_PTE_SWAPPED_MASK) printf("[page=%d -> swap=%u] ",  i, PAGING_PTE_SWP(pte));
        else                                    printf("[page=%d -> none] ",      i);
    }
    printf("\n");
}

/* ── Print VMA list for a given mm ───────────────────────────────── */
static void print_vma_list(struct mm_struct *mm)
{
    struct vm_area_struct *vma = mm->mmap;
    if (!vma) printf("  (empty)\n");
    for (; vma; vma = vma->vm_next) {
        printf("VMA | id=%lu | start=%lu | end=%lu | sbrk=%lu | free regions: ",
               vma->vm_id,
               (unsigned long)vma->vm_start,
               (unsigned long)vma->vm_end,
               (unsigned long)vma->sbrk);
        struct vm_rg_struct *rg = vma->vm_freerg_list;
        if (!rg) printf("(empty)");
        for (; rg; rg = rg->rg_next) {
            printf("[%lu-%lu]", (unsigned long)rg->rg_start, (unsigned long)rg->rg_end);
            if (rg->rg_next) printf(" -> ");
        }
        printf("\n");
    }
}

/* ── Print FIFO page list for a given mm ─────────────────────────── */
static void print_fifo_list(struct mm_struct *mm)
{
    printf("FIFO PAGE LIST: ");
    struct pgn_t *p = mm->fifo_pgn;
    if (!p) printf("(empty)");
    for (; p; p = p->pg_next) {
        printf("[%lu]", (unsigned long)p->pgn);
        if (p->pg_next) printf(" -> ");
    }
    printf("\n");
}

/* ── Print cache pools from a given mm ───────────────────────────── */
static void print_cache_pools(struct mm_struct *mm)
{
    printf("KMEM CACHE POOLS:\n");
    if (!mm || !mm->kcpooltbl) return;
    for (int i = 0; i < 4; i++) {
        struct kcache_pool_struct *pool = &mm->kcpooltbl[i];
        if (pool->size != 0 || pool->align != 0 || pool->storage != 0)
            printf("  pool[%d] size=%d align=%d storage=0x%lx\n",
                   i, pool->size, pool->align, (unsigned long)pool->storage);
    }
}

/* ── Merge user + kernel symrgtbls by index order ────────────────── */
static void print_symtbl_merged(struct mm_struct *user_mm, struct mm_struct *krnl_mm)
{
    printf("SYMBOL TABLE: ");
    int empty = 1;
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
        if (user_mm && (user_mm->symrgtbl[i].rg_start != 0 || user_mm->symrgtbl[i].rg_end != 0)) {
            printf("[rg%d:0x%lx-0x%lx mode=%s] ", i,
                   (unsigned long)user_mm->symrgtbl[i].rg_start,
                   (unsigned long)user_mm->symrgtbl[i].rg_end,
                   user_mm->symrgtbl[i].mode_bit ? "USER" : "KERNEL");
            empty = 0;
        }
        if (krnl_mm && (krnl_mm->symrgtbl[i].rg_start != 0 || krnl_mm->symrgtbl[i].rg_end != 0)) {
            printf("[rg%d:0x%lx-0x%lx mode=%s] ", i,
                   (unsigned long)krnl_mm->symrgtbl[i].rg_start,
                   (unsigned long)krnl_mm->symrgtbl[i].rg_end,
                   krnl_mm->symrgtbl[i].mode_bit ? "USER" : "KERNEL");
            empty = 0;
        }
    }
    if (empty) printf("(empty)");
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  print_mm_struct  –  standalone MM snapshot (single symrgtbl)
 * ══════════════════════════════════════════════════════════════════════ */
void print_mm_struct(struct mm_struct *mm)
{
    printf("MM STRUCT\n");
    if (!mm) { printf("mm = NULL\n"); return; }

    print_page_table(mm);
    print_vma_list(mm);
    print_symtbl_merged(mm, NULL);   /* single-mm: pass NULL for krnl */
    print_fifo_list(mm);
    print_cache_pools(mm);
    printf("\n================================ END ================================\n\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  print_kernel_info  –  full system snapshot:
 *    RAM + swap → kernel page table → MM STRUCT (merged symbol table)
 * ══════════════════════════════════════════════════════════════════════ */
void print_kernel_info(struct krnl_t *os)
{
    /* hardware state */
    print_memphy("RAM", os->mram);
    print_memphy("ACTIVE SWAP", os->active_mswp);

    /* kernel page-table window */
    printf("PAGE TABLE KENEL: ");
    const addr_t KBASE_PGN = KRNL_SPACE_BASE_ADDR / PAGING64_PAGESZ;
    for (int i = 0; i < 4; i++) {
        uint32_t pte = pte_get_entry_kernel_print(os, KBASE_PGN + i);
        if      (PAGING_PAGE_PRESENT(pte))      printf("[page=%lx -> frame=%u] ", KBASE_PGN + i, PAGING_PTE_FPN(pte));
        else if (pte & PAGING_PTE_SWAPPED_MASK) printf("[page=%lx -> swap=%u] ",  KBASE_PGN + i, PAGING_PTE_SWP(pte));
        else                                    printf("[page=%lx -> none] ",      KBASE_PGN + i);
    }
    printf("\n======================\n");

    /* MM STRUCT – user mm for page table/VMA/FIFO, kernel mm for caches */
    struct mm_struct *user_mm  = (os->current && os->current->mm) ? os->current->mm : NULL;
    struct mm_struct *krnl_mm  = os->mm;
    struct mm_struct *pgtbl_mm = user_mm ? user_mm : krnl_mm;

    printf("MM STRUCT\n");
    print_page_table(pgtbl_mm);
    print_vma_list(pgtbl_mm);
    print_symtbl_merged(user_mm, krnl_mm);
    print_fifo_list(pgtbl_mm);
    print_cache_pools(krnl_mm);
    printf("\n================================ END ================================\n\n");
}
