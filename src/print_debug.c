#include "print_debug.h"
#include "mm64.h"
#include "timer.h"
#include <pthread.h>

#define G_MAX_T 1000
#define G_MAX_C 16
static int g_sched[G_MAX_C][G_MAX_T];
static int g_init = 0;
static int g_max_c = 0;
static int g_max_t = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int get_cpu_id() {
    static pthread_t ids[G_MAX_C];
    static int count = 0;
    pthread_t self = pthread_self();
    for (int i = 0; i < count; i++) if (pthread_equal(ids[i], self)) return i;
    if (count < G_MAX_C) { ids[count] = self; return count++; }
    return 0;
}

static void g_track(int pid) {
    pthread_mutex_lock(&g_lock);
    if (!g_init) {
        for (int c = 0; c < G_MAX_C; c++) for (int t = 0; t < G_MAX_T; t++) g_sched[c][t] = -1;
        g_init = 1;
    }
    int t = (int)current_time();
    int c = get_cpu_id();
    if (c < G_MAX_C && t < G_MAX_T) {
        g_sched[c][t] = pid;
        if (c > g_max_c) g_max_c = c;
        if (t > g_max_t) g_max_t = t;
    }
    pthread_mutex_unlock(&g_lock);
}

void __attribute__((destructor)) g_dump() {
    if (!g_init) return;
    printf("\n=== GANTT CHART ===\n\n");
    for (int s = 0; s <= g_max_t; s += 20) {
        int e = (s + 19 > g_max_t) ? g_max_t : s + 19;
        printf("Time : ");
        for (int i = s; i <= e; i++) printf("%02d ", i);
        printf("\n");
        for (int c = 0; c <= g_max_c; c++) {
            printf("CPU %d: ", c);
            for (int i = s; i <= e; i++) {
                int p = g_sched[c][i];
                if (p == -1) printf("-- ");
                else printf("\033[1;%dmP%d\033[0m ", 31 + (p % 6), p);
            }
            printf("\n");
        }
        printf("\n");
    }
}

static const char *get_region(addr_t a) {
    if (IS_USER_SPACE(a)) return "USER";
    if (IS_KRNL_SPACE(a)) return "KERNEL";
    return "HOLE";
}

static void dump_phy(const char *label, struct memphy_struct *m) {
    if (!m) return;
    printf("%s (%d bytes):\n", label, (int)m->maxsz);
    for (int p = 0; p < (m->maxsz / PAGING64_PAGESZ); p++) {
        printf("  P%d: ", p);
        for (int i = 0; i < 16; i++)
            printf("%02x ", ((unsigned char *)m->storage)[p * PAGING64_PAGESZ + i]);
        printf("\n");
    }
}

void system_dump(struct krnl_t *os) {
    if (!os) return;
    if (os->current) g_track(os->current->pid);

    dump_phy("RAM", os->mram);
    dump_phy("SWAP", os->active_mswp);

    struct mm_struct *m = (os->current && os->current->mm) ? os->current->mm : os->mm;
    
    printf("VMAs:");
    for (struct vm_area_struct *v = m->mmap; v; v = v->vm_next)
        printf(" [%lu:%lx-%lx sbrk:%lx]", v->vm_id, (unsigned long)v->vm_start, (unsigned long)v->vm_end, (unsigned long)v->sbrk);

    printf("\n\n");
}

void pg_dump(struct pcb_t *p, addr_t s, addr_t e) {}
