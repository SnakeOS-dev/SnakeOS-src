#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define TASK_KSTACK_SIZE 16384
#define TASK_USTACK_SIZE 65536

#define TASK_READY   0
#define TASK_RUNNING 1
#define TASK_DEAD    2

typedef struct task {
    uint64_t      rsp;
    uint8_t      *kstack;
    uint64_t      pid;
    uint64_t      brk;
    uint64_t      brk_start;
    uint64_t      ustk;
    int           state;
    struct task  *next;
} task_t;

task_t *task_create(uint64_t entry, uint64_t ustk_top);
task_t *task_current(void);
void    task_set_current(task_t *t);
uint64_t task_alloc_stack(void);
void    task_mark_dead(void);
uint64_t task_brk_get(void);
uint64_t task_brk_set(uint64_t addr);

extern task_t *g_current;

#endif
