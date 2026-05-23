#include "memlog.h"
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

/* ── Memory Mode Identification (Simple addr_t check) ──────────────── */
static const char *get_addr_mode(addr_t addr) {
    if (IS_USER_DYNAMIC_SPACE(addr)) return "USER DYNAMIC";
    if (IS_USER_STATIC_SPACE(addr))  return "USER STATIC";
    if (IS_KRNL_DYNAMIC_SPACE(addr)) return "KERNEL DYNAMIC";
    if (IS_KRNL_STATIC_SPACE(addr))  return "KERNEL STATIC";
    if (IS_CANONICAL_HOLE(addr))     return "CANONICAL_HOLE";

    return "UNKNOWN";
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

static void print_symtbl_merged(struct mm_struct *u_mm, struct mm_struct *k_mm) {
    printf("\nSYMBOL TABLE: ");
    int empty = 1;
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
        if (u_mm && (u_mm->symrgtbl[i].rg_start || u_mm->symrgtbl[i].rg_end)) {
            struct vm_rg_struct *rg = &u_mm->symrgtbl[i];
            printf("[%c%d:0x%lx-0x%lx (%s)] ", (u_mm == k_mm ? 'K' : 'U'), i, (unsigned long)rg->rg_start, (unsigned long)rg->rg_end, get_addr_mode(rg->rg_start));
            empty = 0;
        }
        if (k_mm && (k_mm->symrgtbl[i].rg_start || k_mm->symrgtbl[i].rg_end)) {
            struct vm_rg_struct *rg = &k_mm->symrgtbl[i];
            printf("[K%d:0x%lx-0x%lx (%s)] ", i, (unsigned long)rg->rg_start, (unsigned long)rg->rg_end, get_addr_mode(rg->rg_start));
            empty = 0;
        }
    }
    if (empty) printf("(empty)");
    printf("\n");
}

void print_mm_struct(struct mm_struct *mm) {
    if (!mm) return;
    printf("MM STRUCT\nVMA | id=%lu: ", (unsigned long)mm->mmap ? mm->mmap->vm_id : 0);
    for (struct vm_area_struct *v = mm->mmap; v; v = v->vm_next)
        printf("[%lx-%lx sbrk:%lx] ", (unsigned long)v->vm_start, (unsigned long)v->vm_end, (unsigned long)v->sbrk);
    
    print_symtbl_merged(mm, NULL);

    printf("FIFO PAGE LIST: ");
    for (struct pgn_t *p = mm->fifo_pgn; p; p = p->pg_next) printf("[%lu] ", (unsigned long)p->pgn);
    printf("\n================================ END ================================\n\n");
}

void print_kernel_info(struct krnl_t *os) {
    if (!os) return;
    if (os->current) log_scheduling(os->current->pid);

    print_memphy("RAM", os->mram);
    print_memphy("ACTIVE SWAP", os->active_mswp);

    struct mm_struct *u_mm = (os->current && os->current->mm) ? os->current->mm : NULL;
    struct mm_struct *k_mm = os->mm;

    printf("MM STRUCT\n");
    struct mm_struct *active_vma_mm = u_mm ? u_mm : k_mm;
    printf("VMA | id=%lu: ", (unsigned long)active_vma_mm->mmap ? active_vma_mm->mmap->vm_id : 0);
    for (struct vm_area_struct *v = active_vma_mm->mmap; v; v = v->vm_next)
        printf("[%lx-%lx sbrk:%lx] ", (unsigned long)v->vm_start, (unsigned long)v->vm_end, (unsigned long)v->sbrk);

    print_symtbl_merged(u_mm, k_mm);

    printf("FIFO PAGE LIST: ");
    for (struct pgn_t *p = active_vma_mm->fifo_pgn; p; p = p->pg_next) printf("[%lu] ", (unsigned long)p->pgn);
    printf("\n================================ END ================================\n\n");
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