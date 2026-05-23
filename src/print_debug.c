#include "print_debug.h"
#include "mm64.h"
#include "timer.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ── Gantt Chart Tracking (Integrated) ──────────────────────────────── */
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
    for (int i = 0; i < registry_count; i++) if (pthread_equal(thread_registry[i], self)) return i;
    if (registry_count < MAX_CPUS_TRACKED) { thread_registry[registry_count] = self; return registry_count++; }
    return 0;
}

static void log_scheduling(int pid) {
    pthread_mutex_lock(&gantt_tracking_lock);
    if (!gantt_init_done) {
        for (int c = 0; c < MAX_CPUS_TRACKED; c++)
            for (int t = 0; t < MAX_TIMELINE; t++) sched_history[c][t] = -1;
        gantt_init_done = 1;
    }
    int t = (int)current_time();
    int c = get_cpu_idx_from_thread();
    if (c < MAX_CPUS_TRACKED && t < MAX_TIMELINE) {
        sched_history[c][t] = pid;
        if (c > max_seen_cpu_idx)  max_seen_cpu_idx = c;
        if (t > max_seen_time_idx) max_seen_time_idx = t;
    }
    pthread_mutex_unlock(&gantt_tracking_lock);
}

void gantt_dump() {
    if (!gantt_init_done) return;
    printf("\n=== PROCESS SCHEDULING TIMELINE (GANTT CHART) ===\n\n");
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

/* ── Memory Region Classification ──────────────────────────────────── */
static const char *get_vma_label(struct vm_rg_struct *rg) {
    if (IS_KRNL_SPACE(rg->rg_start)) return (rg->vmaid == 0) ? "KERNEL DYNAMIC" : "KERNEL STATIC";
    return (rg->vmaid == 0) ? "USER DYNAMIC" : "USER STATIC";
}

/* ── Verbose Helpers ────────────────────────────────────────────────── */
void print_frame_list(const char *name, struct framephy_struct *head) {
    printf("%s: ", name);
    if (!head) { printf("(empty)\n"); return; }
    for (; head; head = head->fp_next) printf("[%lu] ", (unsigned long)head->fpn);
    printf("\n");
}

void print_storage_int(struct memphy_struct *m) {
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

void print_memphy(const char *name, struct memphy_struct *m) {
    printf("%s | size:%lu\n", name, (unsigned long)m->maxsz);
    print_storage_int(m);
    print_frame_list("FREE frames", m->free_fp_list);
    print_frame_list("USED frames", m->used_fp_list);
    printf("======================\n");
}

uint32_t pte_get_entry_kernel_print(struct krnl_t *os, addr_t pgn) {
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

static uint32_t walk_pt(struct mm_struct *mm, addr_t pgn) {
    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
    if (!mm->pgd || mm->pgd[pgd_i] == 0) return 0;
    addr_t *v = mm->pgd;
    if (!v[pgd_i]) return 0; v = (addr_t *)v[pgd_i];
    if (!v[p4d_i]) return 0; v = (addr_t *)v[p4d_i];
    if (!v[pud_i]) return 0; v = (addr_t *)v[pud_i];
    if (!v[pmd_i]) return 0; v = (addr_t *)v[pmd_i];
    return (uint32_t)v[pt_i];
}

void print_mm_struct(struct mm_struct *mm) {
    if (!mm) return;
    printf("MM STRUCT\nVMA | id=%lu: ", (unsigned long)mm->mmap ? mm->mmap->vm_id : 0);
    for (struct vm_area_struct *v = mm->mmap; v; v = v->vm_next)
        printf("[%lx-%lx sbrk:%lx] ", (unsigned long)v->vm_start, (unsigned long)v->vm_end, (unsigned long)v->sbrk);
    
    printf("\nSYMBOL TABLE: ");
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
        struct vm_rg_struct *rg = &mm->symrgtbl[i];
        if (rg->rg_start || rg->rg_end)
            printf("[rg%d:0x%lx-0x%lx (%s)] ", i, (unsigned long)rg->rg_start, (unsigned long)rg->rg_end, get_vma_label(rg));
    }
    printf("\nFIFO PAGE LIST: ");
    for (struct pgn_t *p = mm->fifo_pgn; p; p = p->pg_next) printf("[%lu] ", (unsigned long)p->pgn);
    printf("\n================================ END ================================\n\n");
}

void print_kernel_info(struct krnl_t *os) {
    if (!os) return;
    if (os->current) log_scheduling(os->current->pid);

    print_memphy("RAM", os->mram);
    print_memphy("ACTIVE SWAP", os->active_mswp);

    struct mm_struct *user_mm = (os->current && os->current->mm) ? os->current->mm : NULL;
    if (user_mm) print_mm_struct(user_mm);
    else         print_mm_struct(os->mm);
}

static pthread_mutex_t sync_log_lock = PTHREAD_MUTEX_INITIALIZER;
void sync_log_printf(const char *fmt, ...) {
    if (!fmt) return;
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) { va_end(args_copy); return; }
    char *msg = malloc(needed + 2);
    if (!msg) { va_end(args_copy); return; }
    vsnprintf(msg, needed + 1, fmt, args_copy);
    va_end(args_copy);
    if (needed == 0 || msg[needed - 1] != '\n') { msg[needed] = '\n'; msg[needed + 1] = '\0'; }
    pthread_mutex_lock(&sync_log_lock);
    fputs(msg, stdout); fflush(stdout);
    pthread_mutex_unlock(&sync_log_lock);
    free(msg);
}

void pg_dump(struct pcb_t *p, addr_t s, addr_t e) { (void)p; (void)s; (void)e; }
