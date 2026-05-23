#include "memlog.h"
#include "mm64.h"
#include "timer.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── Gantt Chart Tracking ───────────────────────────────────────────── */
#define MAX_TIMELINE 1000
#define MAX_CPUS_TRACKED 16
static int sched_history[MAX_CPUS_TRACKED][MAX_TIMELINE];
static int gantt_init_done = 0;
static int max_seen_cpu_idx = 0;
static int max_seen_time_idx = 0;
static pthread_mutex_t gantt_tracking_lock = PTHREAD_MUTEX_INITIALIZER;

static int get_cpu_idx_from_thread() {
    static pthread_t thread_registry[MAX_CPUS_TRACKED];
    static int registry_count = 0;
    pthread_t self = pthread_self();
    for (int i = 0; i < registry_count; i++)
        if (pthread_equal(thread_registry[i], self)) return i;
    if (registry_count < MAX_CPUS_TRACKED) {
        thread_registry[registry_count] = self;
        return registry_count++;
    }
    return 0;
}

static void log_scheduling(int pid) {
    pthread_mutex_lock(&gantt_tracking_lock);
    if (!gantt_init_done) {
        for (int c = 0; c < MAX_CPUS_TRACKED; c++)
            for (int t = 0; t < MAX_TIMELINE; t++) sched_history[c][t] = -1;
        gantt_init_done = 1;
    }
    uint64_t t = current_time();
    if (t >= MAX_TIMELINE) {
        pthread_mutex_unlock(&gantt_tracking_lock);  /* FIX: was leaking lock on early return */
        return;
    }
    int c = get_cpu_idx_from_thread();
    if (c < MAX_CPUS_TRACKED) {
        sched_history[c][t] = pid;
        if (c > max_seen_cpu_idx)  max_seen_cpu_idx = c;
        if ((int)t > max_seen_time_idx) max_seen_time_idx = (int)t;
    }
    pthread_mutex_unlock(&gantt_tracking_lock);
}


void gantt_dump() {
    if (!gantt_init_done) return;
    printf("\n=== SCHEDULING GANTT CHART ===\n\n");
    for (int block = 0; block <= max_seen_time_idx; block += 20) {
        int end = (block + 19 > max_seen_time_idx) ? max_seen_time_idx : block + 19;
        printf("Time  : ");
        for (int i = block; i <= end; i++) printf("%02d ", i);
        printf("\n");
        for (int c = 0; c <= max_seen_cpu_idx; c++) {
            printf("CPU %d : ", c);
            for (int i = block; i <= end; i++) {
                int p = sched_history[c][i];
                if (p == -1) printf("-- ");
                else         printf("P%d ", p);
            }
            printf("\n");
        }
        printf("\n");
    }
    fflush(stdout);
}

/* ── Memory Mode Identification ─────────────────────────────────────── */
static const char *get_addr_mode(addr_t addr) {
    if (IS_USER_DYNAMIC_SPACE(addr)) return "USER DYNAMIC";
    if (IS_USER_STATIC_SPACE(addr))  return "USER STATIC";
    if (IS_KRNL_DYNAMIC_SPACE(addr)) return "KRNL DYNAMIC";
    if (IS_KRNL_STATIC_SPACE(addr))  return "KRNL STATIC";
    return "UNK";
}

/* ── Page Table Walking ────────────────────────────────────────────── */
static uint32_t walk_pt(struct mm_struct *mm, addr_t pgn) {
    if (!mm || !mm->pgd) return 0;
    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
    addr_t *v = mm->pgd;
    if (!v[pgd_i]) return 0;
    v = (addr_t *)v[pgd_i];
    if (!v[p4d_i]) return 0;
    v = (addr_t *)v[p4d_i];
    if (!v[pud_i]) return 0;
    v = (addr_t *)v[pud_i];
    if (!v[pmd_i]) return 0;
    v = (addr_t *)v[pmd_i];
    return (uint32_t)v[pt_i];
}

static uint32_t walk_kernel_pt(struct krnl_t *os, addr_t pgn) {
    if (!os || !os->krnl_pgd) return 0;
    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
    addr_t *v = os->krnl_pgd;
    if (!v[pgd_i]) return 0;
    v = (addr_t *)v[pgd_i];
    if (!v[p4d_i]) return 0;
    v = (addr_t *)v[p4d_i];
    if (!v[pud_i]) return 0;
    v = (addr_t *)v[pud_i];
    if (!v[pmd_i]) return 0;
    v = (addr_t *)v[pmd_i];
    return (uint32_t)v[pt_i];
}

/* ── Dashboard Components ───────────────────────────────────────────── */

static void print_storage_hex(struct memphy_struct *m) {
    if (!m || !m->storage) return;
    int total_pages = m->maxsz / PAGING64_PAGESZ;
    printf("  STORAGE DATA:\n");
    for (int pg = 0; pg < total_pages; pg++) {
        unsigned char *p_data = (unsigned char *)m->storage + (pg * PAGING64_PAGESZ);
        int is_empty = 1;
        for (int i = 0; i < 32; i++) if (p_data[i] != 0) { is_empty = 0; break; }

        if (is_empty) {
            printf("  F%-2d [ -- empty / zeroed -- ]\n", pg);
        } else {
            printf("  F%-2d [ ", pg);
            for (int i = 0; i < 8; i++) printf("%02x ", p_data[i]);
            printf("| ");
            for (int i = 8; i < 16; i++) printf("%02x ", p_data[i]);
            printf("]\n");
        }
    }
}

static void print_vma_table(const char* label, struct mm_struct *mm) {
    if (!mm || !mm->mmap) return;
    printf("  %s VMAs:\n", label);
    for (struct vm_area_struct *v = mm->mmap; v; v = v->vm_next) {
        printf("ID:%02lu   START:0x%08lx   END:0x%08lx   SBRK:0x%08lx   REGION:[%s]\n",
                (unsigned long)v->vm_id, (unsigned long)v->vm_start,
                (unsigned long)v->vm_end, (unsigned long)v->sbrk, get_addr_mode(v->vm_start));
    }
}

/* ── print_kernel_info lock ─────────────────────────────────────────── */
static pthread_mutex_t print_kernel_info_lock = PTHREAD_MUTEX_INITIALIZER;

void print_kernel_info(struct krnl_t *os) {
    if (!os) return;
    pthread_mutex_lock(&print_kernel_info_lock);  /* FIX: serialize concurrent CPU snapshots */

    if (os->current) log_scheduling(os->current->pid);

    // RAM & Swap Section
    printf("\n● PHYSICAL MEMORY\n");
    if (os->mram) {
        printf("  RAM Frames: ");
        int tf = os->mram->maxsz / PAGING64_PAGESZ;
        for (int i = 0; i < tf; i++) {
            int used = 0;
            for (struct framephy_struct *c = os->mram->used_fp_list; c; c = c->fp_next)
                if (c->fpn == i) { used = 1; break; }
            printf("[%d:%c] ", i, used ? 'U' : 'F');
        }
        printf("\n");
        print_storage_hex(os->mram);
    }

    // Virtual Memory Section
    struct mm_struct *u_mm = (os->current && os->current->mm) ? os->current->mm : NULL;
    struct mm_struct *k_mm = os->mm;

    printf("\n● VIRTUAL MEMORY\n");
    if (u_mm) {
        printf("  User Page Table: ");
        for (int i = 0; i < 6; i++) {
            uint32_t pte = walk_pt(u_mm, i);
            if (PAGING_PAGE_PRESENT(pte)) printf("[%d->f%u] ", i, PAGING_PTE_FPN(pte));
            else if (pte & PAGING_PTE_SWAPPED_MASK) printf("[%d->s%u] ", i, PAGING_PTE_SWP(pte));
            else printf("[%d->x] ", i);
        }
        printf("\n");
        print_vma_table("User", u_mm);

        printf("  FIFO List: ");
        if (!u_mm->fifo_pgn) printf("(empty)");
        for (struct pgn_t *p = u_mm->fifo_pgn; p; p = p->pg_next)
            printf("[%lu]%s", (unsigned long)p->pgn, p->pg_next ? "->" : "");
        printf("\n");
    }

    if (k_mm) {
        printf("\n");
        print_vma_table("Kernel", k_mm);
    }

    printf("\n● KERNEL PAGING MAP\n");
    const addr_t KBASE_PGN = KRNL_DYNAMIC_START / PAGING64_PAGESZ;
    for (int i = 0; i < 4; i++) {
        addr_t current_pgn = KBASE_PGN + i;
        uint32_t pte = walk_kernel_pt(os, current_pgn);
        if (PAGING_PAGE_PRESENT(pte))
            printf("  [0x%05lx -> PFN %-2d] ", (unsigned long)current_pgn, PAGING_PTE_FPN(pte));
        else
            printf("  [0x%05lx -> EMPTY] ", (unsigned long)current_pgn);
    }
    printf("\n");
    printf("\n--------------------------------------------------------------------------------\n");

    pthread_mutex_unlock(&print_kernel_info_lock);  /* FIX: must unlock after all printf done */
}

/* ── Synchronized Logging ───────────────────────────────────────────── */
static pthread_mutex_t sync_log_lock = PTHREAD_MUTEX_INITIALIZER;
void sync_log_printf(const char *fmt, ...) {
    if (!fmt) return;
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed <= 0) { va_end(args_copy); return; }
    char *msg = malloc(needed + 2);
    if (!msg) { va_end(args_copy); return; }
    vsnprintf(msg, needed + 1, fmt, args_copy);
    va_end(args_copy);
    if (msg[needed - 1] != '\n') { msg[needed] = '\n'; msg[needed + 1] = '\0'; }
    pthread_mutex_lock(&sync_log_lock);
    fputs(msg, stdout);
    fflush(stdout);
    pthread_mutex_unlock(&sync_log_lock);
    free(msg);
}

void pg_dump(struct pcb_t *p, addr_t s, addr_t e) { (void)p; (void)s; (void)e; }
