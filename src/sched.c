/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "../include/queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

/* Simple pid -> proc hash table to avoid linear scans */
#define SCHED_HT_BITS 10
#define SCHED_HT_SIZE (1 << SCHED_HT_BITS)
struct ht_node {
    uint32_t pid;
    struct pcb_t *p;
    struct ht_node *next;
};
static struct ht_node *sched_ht[SCHED_HT_SIZE];
static inline uint32_t sched_ht_idx(uint32_t pid) { return pid & (SCHED_HT_SIZE - 1); }
static struct pcb_t *ht_lookup_locked(uint32_t pid) {
	struct ht_node *n = sched_ht[sched_ht_idx(pid)];
	while (n) { if (n->pid == pid) return n->p; n = n->next; }
	return NULL;
}
static void ht_insert_locked(uint32_t pid, struct pcb_t *p) {
	uint32_t idx = sched_ht_idx(pid);
	struct ht_node *n = sched_ht[idx];
	while (n) { if (n->pid == pid) { n->p = p; return; } n = n->next; }
	n = malloc(sizeof(*n)); n->pid = pid; n->p = p; n->next = sched_ht[idx]; sched_ht[idx] = n;
}
static void ht_remove_locked(uint32_t pid) {
	uint32_t idx = sched_ht_idx(pid);
	struct ht_node **pp = &sched_ht[idx];
	while (*pp) {
		if ((*pp)->pid == pid) {
			struct ht_node *rem = *pp;
			*pp = rem->next;
			free(rem);
			return;
		}
		pp = &(*pp)->next;
	}
}

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

static pthread_mutex_t loader_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t loader_cond = PTHREAD_COND_INITIALIZER;
static int loader_time = -1;

void sched_signal_loader_done(uint64_t time) {
  pthread_mutex_lock(&loader_lock);
  loader_time = time;
  pthread_cond_broadcast(&loader_cond);
  pthread_mutex_unlock(&loader_lock);
}

void sched_wait_for_loader(uint64_t time, int is_done) {
  pthread_mutex_lock(&loader_lock);
  while ((int)loader_time < (int)time && !is_done) {
    pthread_cond_wait(&loader_cond, &loader_lock);
  }
  pthread_mutex_unlock(&loader_lock);
}

int queue_empty(void) {
	int is_empty;
	pthread_mutex_lock(&queue_lock);
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) {
			pthread_mutex_unlock(&queue_lock);
			return -1;
		}
#endif
	is_empty = (empty(&ready_queue) && empty(&run_queue));
	pthread_mutex_unlock(&queue_lock);
	return is_empty;
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i;
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);

    /* Clear hash table */
    for (int i = 0; i < SCHED_HT_SIZE; i++) sched_ht[i] = NULL;
}

struct pcb_t * find_proc_by_pid(uint32_t pid) {
	struct pcb_t *proc = NULL;

	/* Scheduler queues are shared by loader, CPU threads, and syscalls. */
	pthread_mutex_lock(&queue_lock);
	/* Fast path: hash-table lookup */
	proc = ht_lookup_locked(pid);
	pthread_mutex_unlock(&queue_lock);
	return proc;
}

#ifdef MLQ_SCHED
/*
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {
  struct pcb_t * proc = NULL;
  pthread_mutex_lock(&queue_lock);

  /*TODO: get a process from PRIORITY [ready_queue].
   *      It worth to protect by a mechanism.
   * */

  int found = -1;
  for (int i = 0; i < MAX_PRIO; i++) {
    if (!empty(&mlq_ready_queue[i]) && slot[i] > 0) {
      found = i;
      break;
    }
  }

  if (found == -1) {
    int has_proc = 0;
    for (int i = 0; i < MAX_PRIO; i++) {
      if (!empty(&mlq_ready_queue[i])) {
        has_proc = 1;
        break;
      }
    }

    if (has_proc) {
      for (int i = 0; i < MAX_PRIO; i++) {
        slot[i] = MAX_PRIO - i;
      }
      for (int i = 0; i < MAX_PRIO; i++) {
        if (!empty(&mlq_ready_queue[i]) && slot[i] > 0) {
          found = i;
          break;
        }
      }
    }
  }

  if (found != -1) {
    proc = dequeue(&mlq_ready_queue[found]);
    if (proc != NULL) slot[found]--;
  }

  if (proc != NULL)
    enqueue(&running_list, proc);
  pthread_mutex_unlock(&queue_lock);
  return proc;
}

void put_mlq_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 */

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	/* Ensure pid->proc mapping exists for lookups */
	ht_insert_locked(proc->pid, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	/* Track process by pid for fast lookup */
	ht_insert_locked(proc->pid, proc);
	pthread_mutex_unlock(&queue_lock);
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from [ready_queue].
	 *       It worth to protect by a mechanism.
	 */
	proc = dequeue(&ready_queue);
	if (proc != NULL) {
		enqueue(&running_list, proc);
		/* Ensure mapping exists (in case proc was added elsewhere) */
		ht_insert_locked(proc->pid, proc);
	}
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 */

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	/* Track process by pid for fast lookup */
	ht_insert_locked(proc->pid, proc);
	pthread_mutex_unlock(&queue_lock);
}
#endif

void sched_remove_proc(uint32_t pid) {
    pthread_mutex_lock(&queue_lock);
    ht_remove_locked(pid);
    pthread_mutex_unlock(&queue_lock);
}
