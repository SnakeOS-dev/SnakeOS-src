#include "sched.h"
#include "gdt.h"
#include "vga.h"
static task_t *runq_head = 0;
static task_t *runq_tail = 0;
static int      need_resched = 0;
static void print(const char *s) { vga_print(s); }
static void putchar(char c)      { vga_putchar(c); }
static void print_dec(uint64_t v) {
    char buf[32];
    int i = 0;
    if (v == 0) { putchar('0'); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) putchar(buf[i]);
}

uint64_t g_user_rsp   = 0;
uint64_t g_kernel_rsp = 0;

void sched_init(void) {
    runq_head = 0;
    runq_tail = 0;
    need_resched = 0;
}

void sched_add(task_t *t) {
    t->next = 0;
    if (!runq_head) {
        runq_head = t;
        runq_tail = t;
        t->next   = t;
    } else {
        t->next = runq_head;
        runq_tail->next = t;
        runq_tail = t;
    }
    if (!g_current) {
        g_current = t;
        g_kernel_rsp = (uint64_t)(t->kstack + TASK_KSTACK_SIZE);
        tss_set_rsp0(g_kernel_rsp);
    }
}

void sched_need_resched(void) { need_resched = 1; }
void sched_yield(void)        { need_resched = 1; }

static task_t *next_runnable(task_t *from) {
    if (!from) return runq_head;
    task_t *t = from->next;
    while (t && t != from) {
        if (t->state != TASK_DEAD) return t;
        t = t->next;
    }
    return 0;
}


void sched_start(void) {
    need_resched = 1;
}

static uint64_t dbg_calls = 0;
static uint64_t dbg_nocur = 0;
static uint64_t dbg_noflag = 0;
static uint64_t dbg_nonext = 0;
static uint64_t dbg_switched = 0;

uint64_t sched_switch_stack(uint64_t rsp) {
    dbg_calls++;
    task_t *cur = g_current;
    if (!cur) { dbg_nocur++; return 0; }

    int dying = (cur->state == TASK_DEAD);
    if (!dying && !need_resched) { dbg_noflag++; return 0; }

    cur->rsp = rsp;

    if (dying) {
        if (cur->next == cur) {
            for (;;) __asm__ volatile ("cli; hlt");
        }
        task_t *prev = cur;
        task_t *p = runq_head;
        while (p->next != prev) p = p->next;
        p->next = prev->next;
        if (runq_head == prev) runq_head = prev->next;
        if (runq_tail == prev) runq_tail = p;
    }

    need_resched = 0;

    task_t *next = next_runnable(g_current);
    if (!next) { dbg_nonext++; return 0; }
    if (next == g_current && !dying) { dbg_nonext++; return 0; }

    next->state = TASK_RUNNING;
    g_current = next;
    g_kernel_rsp = (uint64_t)(next->kstack + TASK_KSTACK_SIZE);
    tss_set_rsp0(g_kernel_rsp);
    dbg_switched++;
    return next->rsp;
}

void sched_dump_stats(void) {
    print("sw calls="); print_dec(dbg_calls);
    print(" nocur=");   print_dec(dbg_nocur);
    print(" noflag=");  print_dec(dbg_noflag);
    print(" nonext=");  print_dec(dbg_nonext);
    print(" sw=");      print_dec(dbg_switched);
    print("\n");
}
