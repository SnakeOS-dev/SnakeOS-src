#include "syscall.h"
#include "task.h"
#include "sched.h"
#include "vga.h"
#include "keyboard.h"
#include "vmm.h"
#include "pmm.h"
#include "gdt.h"

extern void syscall_entry(void);

static uint64_t do_read(int fd, void *buf, uint64_t count) {
    if (fd != 0) return (uint64_t)-9;
    uint8_t *p = buf;
    uint64_t n = 0;
    while (n < count) {
        int c = keyboard_getchar();
        if (c < 0) break;
        p[n++] = (uint8_t)c;
    }
    return n;
}

static uint64_t do_write(int fd, const void *buf, uint64_t count) {
    if (fd != 1 && fd != 2) return (uint64_t)-9;
    const char *p = buf;
    for (uint64_t i = 0; i < count; i++) vga_putchar(p[i]);
    return count;
}

static uint64_t do_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t off) {
    (void)prot; (void)flags; (void)fd; (void)off;
    if (len == 0) return (uint64_t)-22;
    if (addr == 0) return (uint64_t)-22;
    uint64_t start = addr & ~4095ULL;
    uint64_t end   = (addr + len + 4095) & ~4095ULL;
    for (uint64_t p = start; p < end; p += 4096) {
        if (vmm_get_phys(p)) continue;
        void *phys = pmm_alloc_page();
        if (!phys) return (uint64_t)-12;
        vmm_map(p, (uint64_t)phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }
    return addr;
}

static uint64_t do_exit(int code) {
    (void)code;
    task_mark_dead();
    sched_need_resched();
    return 0;
}

void syscall_handler(regs_t *r) {
    uint64_t nr = r->rax;
    uint64_t a1 = r->rdi;
    uint64_t a2 = r->rsi;
    uint64_t a3 = r->rdx;
    uint64_t a4 = r->r10;
    uint64_t a5 = r->r8;
    uint64_t a6 = r->r9;
    uint64_t ret = (uint64_t)-38;

    switch (nr) {
        case SYS_READ:
            ret = do_read((int)a1, (void *)a2, a3);
            break;
        case SYS_WRITE:
            ret = do_write((int)a1, (const void *)a2, a3);
            break;
        case SYS_MMAP:
            ret = do_mmap(a1, a2, a3, a4, a5, a6);
            break;
        case SYS_BRK:
            ret = task_brk_set(a1);
            break;
        case SYS_SCHED_YIELD:
            sched_yield();
            ret = 0;
            break;
        case SYS_EXIT:
            ret = do_exit((int)a1);
            break;
    }
    r->rax = ret;
}

static void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

void syscall_init(void) {
    wrmsr(0xC0000081, (0x1BULL << 48) | (0x08ULL << 32));
    wrmsr(0xC0000082, (uint64_t)syscall_entry);
    wrmsr(0xC0000084, 0x200);
}
