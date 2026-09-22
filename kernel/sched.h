#ifndef SCHED_H
#define SCHED_H

#include "task.h"
#include <stdint.h>

void     sched_init(void);
void     sched_add(task_t *t);
void     sched_need_resched(void);
void     sched_yield(void);
uint64_t sched_switch_stack(uint64_t rsp);
void     sched_start(void);
void sched_dump_stats(void);
extern uint64_t g_user_rsp;
extern uint64_t g_kernel_rsp;

#endif
