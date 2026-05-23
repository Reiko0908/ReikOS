#include "print_debug.h"
#include "mm64.h"
#include "timer.h"
#include <pthread.h>

/* ─────────────────────────────────────────────────────────────────────────
 *  Scheduling Visualizer (Gantt Chart) - Private Implementation
 * ───────────────────────────────────────────────────────────────────────── */

#define MAX_TIMELINE_STEPS 1000
#define MAX_SUPPORTED_CPUS 16

/* Global state for tracking process execution over time */
static int schedule_matrix[MAX_SUPPORTED_CPUS][MAX_TIMELINE_STEPS];
static int scheduler_initialized = 0;
static int max_cpu_seen = 0;
static int max_time_seen = 0;
static pthread_mutex_t scheduler_data_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * identify_cpu_index - Maps current pthread to a virtual CPU ID (0..15)
 */
static int identify_cpu_index() {
    static pthread_t thread_map[MAX_SUPPORTED_CPUS];
    static int map_size = 0;
    pthread_t current_thread = pthread_self();

    for (int i = 0; i < map_size; i++) {
        if (pthread_equal(thread_map[i], current_thread)) return i;
    }

    if (map_size < MAX_SUPPORTED_CPUS) {
        thread_map[map_size] = current_thread;
        return map_size++;
    }
    return 0;
}

/**
 * track_process_execution - Records the active process for the current CPU/Time
 */
static void track_process_execution(int pid) {
    pthread_mutex_lock(&scheduler_data_lock);

    /* Initialize the schedule matrix with IDLE marker (-1) on first call */
    if (!scheduler_initialized) {
        for (int c = 0; c < MAX_SUPPORTED_CPUS; c++) {
            for (int t = 0; t < MAX_TIMELINE_STEPS; t++) {
                schedule_matrix[c][t] = -1; 
            }
        }
        scheduler_initialized = 1;
    }

    int t_now = (int)current_time();
    int cpu_id = identify_cpu_index();

    if (cpu_id < MAX_SUPPORTED_CPUS && t_now < MAX_TIMELINE_STEPS) {
        schedule_matrix[cpu_id][t_now] = pid;
        
        if (cpu_id > max_cpu_seen)   max_cpu_seen = cpu_id;
        if (t_now > max_time_seen) max_time_seen = t_now;
    }

    pthread_mutex_unlock(&scheduler_data_lock);
}

/**
 * display_gantt_chart - Output function executed at simulation exit
 */
void __attribute__((destructor)) display_gantt_chart() {
    if (!scheduler_initialized) return;

    printf("\n=== PROCESS SCHEDULING TIMELINE (GANTT CHART) ===\n\n");

    const int steps_per_block = 20;
    for (int block_start = 0; block_start <= max_time_seen; block_start += steps_per_block) {
        int block_end = (block_start + steps_per_block - 1 > max_time_seen) 
                        ? max_time_seen 
                        : block_start + steps_per_block - 1;

        /* Print Timeline header */
        printf("Time  : ");
        for (int t = block_start; t <= block_end; t++) printf("%02d ", t);
        printf("\n");

        /* Print each CPU's workload (No Color) */
        for (int c = 0; c <= max_cpu_seen; c++) {
            printf("CPU %d : ", c);
            for (int t = block_start; t <= block_end; t++) {
                int p = schedule_matrix[c][t];
                if (p == -1) printf("-- ");
                else         printf("P%d ", p);
            }
            printf("\n");
        }
        printf("\n");
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Memory Inspection Helpers
 * ───────────────────────────────────────────────────────────────────────── */

/**
 * get_vma_region_label - Returns a descriptive name for a memory region
 */
static const char *get_vma_region_label(struct vm_rg_struct *rg) {
    if (IS_KRNL_SPACE(rg->rg_start)) {
        return (rg->vmaid == 0) ? "KERNEL DYNAMIC" : "KERNEL STATIC";
    }
    return (rg->vmaid == 0) ? "USER DYNAMIC" : "USER STATIC";
}

/**
 * show_physical_layer - Prints first 16 bytes per page of a memory device
 */
static void show_physical_layer(const char *name, struct memphy_struct *mp) {
    if (!mp || !mp->storage) return;

    printf("%s (Total size: %d bytes):\n", name, (int)mp->maxsz);
    int pages = mp->maxsz / PAGING64_PAGESZ;

    for (int p_idx = 0; p_idx < pages; p_idx++) {
        printf("  [Page %2d] : ", p_idx);
        for (int offset = 0; offset < 16; offset++) {
            unsigned char byte = ((unsigned char *)mp->storage)[p_idx * PAGING64_PAGESZ + offset];
            printf("%02x ", byte);
        }
        printf("\n");
    }
}

/**
 * system_dump - Standard interface for per-step system state dump
 */
void system_dump(struct krnl_t *os) {
    if (!os) return;

    /* Feed current scheduling data into the visualizer */
    if (os->current) {
        track_process_execution(os->current->pid);
    }

    /* Hardware Layer */
    show_physical_layer("Physical RAM", os->mram);
    show_physical_layer("Swap Storage", os->active_mswp);

    /* Virtual Layer (Segmentation/VMAs) */
    struct mm_struct *active_mm = (os->current && os->current->mm) ? os->current->mm : os->mm;
    
    printf("Segments (VMAs):");
    for (struct vm_area_struct *vma = active_mm->mmap; vma; vma = vma->vm_next) {
        printf(" [ID:%lu 0x%lx-0x%lx, brk:0x%lx]", 
               (unsigned long)vma->vm_id, 
               (unsigned long)vma->vm_start, 
               (unsigned long)vma->vm_end, 
               (unsigned long)vma->sbrk);
    }

    /* Symbol Table with Region Labels */
    printf("\nSymbols (REGs): ");
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
        struct mm_struct *u_mm = os->current ? os->current->mm : NULL;
        if (u_mm && (u_mm->symrgtbl[i].rg_start || u_mm->symrgtbl[i].rg_end)) {
            struct vm_rg_struct *rg = &u_mm->symrgtbl[i];
            printf("[U%d: 0x%lx-0x%lx (%s)] ", i, (unsigned long)rg->rg_start, (unsigned long)rg->rg_end, get_vma_region_label(rg));
        }
        if (os->mm && (os->mm->symrgtbl[i].rg_start || os->mm->symrgtbl[i].rg_end)) {
            struct vm_rg_struct *rg = &os->mm->symrgtbl[i];
            printf("[K%d: 0x%lx-0x%lx (%s)] ", i, (unsigned long)rg->rg_start, (unsigned long)rg->rg_end, get_vma_region_label(rg));
        }
    }

    printf("\n\n");
}

void pg_dump(struct pcb_t *p, addr_t s, addr_t e) { (void)p; (void)s; (void)e; }
