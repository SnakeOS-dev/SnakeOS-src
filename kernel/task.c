#include "task.h"
#include "heap.h"
#include "vmm.h"
#include "pmm.h"

static uint64_t next_pid = 1;
task_t *g_current = 0;

task_t *task_current(void) { return g_current; }
void    task_set_current(task_t *t) { g_current = t; }

uint64_t task_alloc_stack(void) {
    return (uint64_t)kmalloc(TASK_USTACK_SIZE);
}

static uint64_t build_frame(uint8_t *kstack_top, uint64_t entry, uint64_t ustk) {
    uint64_t *sp = (uint64_t *)kstack_top;
    *(--sp) = 0x23;
    *(--sp) = ustk;
    *(--sp) = 0x202;
    *(--sp) = 0x2B;
    *(--sp) = entry;
    *(--sp) = 0;
    *(--sp) = 0;
    for (int i = 0; i < 15; i++) *(--sp) = 0;
    return (uint64_t)sp;
}

task_t *task_create(uint64_t entry, uint64_t ustk_top) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return 0;
    uint8_t *ks = (uint8_t *)kmalloc(TASK_KSTACK_SIZE);
    if (!ks) { kfree(t); return 0; }

    t->kstack     = ks;
    t->pid        = next_pid++;
    t->ustk       = ustk_top;
    t->brk_start  = 0x0000500000000000ULL + (t->pid << 32);
    t->brk        = t->brk_start;
    t->state      = TASK_READY;
    t->next       = 0;
    t->rsp        = build_frame(ks + TASK_KSTACK_SIZE, entry, ustk_top);
    return t;
}

void task_mark_dead(void) {
    if (g_current) g_current->state = TASK_DEAD;
}

uint64_t task_brk_get(void) {
    return g_current ? g_current->brk : 0;
}

uint64_t task_brk_set(uint64_t addr) {
    if (!g_current) return (uint64_t)-1;
    task_t *t = g_current;
    if (addr == 0) return t->brk;
    if (addr < t->brk_start) return t->brk;
    uint64_t old = t->brk;
    uint64_t page_old = (old + 4095) & ~4095ULL;
    uint64_t page_new = (addr + 4095) & ~4095ULL;
    for (uint64_t p = page_old; p < page_new; p += 4096) {
        if (vmm_get_phys(p)) continue;
        void *phys = pmm_alloc_page();
        if (!phys) return t->brk;
        vmm_map(p, (uint64_t)phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }
    t->brk = addr;
    return t->brk;
}
