#ifndef SCHED_H
#define SCHED_H

#include "common.h"

#ifndef MLQ_SCHED
#define MLQ_SCHED
#endif

#define MAX_PRIO 140

void sched_signal_loader_done(uint64_t time);
void sched_wait_for_loader(uint64_t time, int is_done);

int queue_empty(void);

void init_scheduler(void);
void finish_scheduler(void);

/* Get the next process from ready queue */
struct pcb_t * get_proc(void);

/* Put a process back to run queue */
void put_proc(struct pcb_t * proc);

/* Add a new process to ready queue */
void add_proc(struct pcb_t * proc);

/* Find a process while holding the scheduler queue lock */
struct pcb_t * find_proc_by_pid(uint32_t pid);

#endif


