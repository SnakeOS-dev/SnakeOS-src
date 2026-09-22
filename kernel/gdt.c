#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[8];
static struct gdt_ptr   gp;
static struct tss       tss;

extern void gdt_flush(struct gdt_ptr *gp);
extern void tss_flush(void);

static void set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].access      = access;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high   = (base >> 24) & 0xFF;
}

static void set_tss_gate(int i, uint64_t base, uint32_t limit) {
    uint8_t *p = (uint8_t *)&gdt[i];
    for (int k = 0; k < 16; k++) p[k] = 0;

    p[0] = limit & 0xFF;
    p[1] = (limit >> 8) & 0xFF;
    p[2] = base & 0xFF;
    p[3] = (base >> 8) & 0xFF;
    p[4] = (base >> 16) & 0xFF;
    p[5] = 0x89;
    p[6] = ((limit >> 16) & 0x0F);
    p[7] = (base >> 24) & 0xFF;
    p[8]  = (base >> 32) & 0xFF;
    p[9]  = (base >> 40) & 0xFF;
    p[10] = (base >> 48) & 0xFF;
    p[11] = (base >> 56) & 0xFF;
    p[12] = 0;
    p[13] = 0;
    p[14] = 0;
    p[15] = 0;
}

static void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void gdt_init(void) {
    set_gate(0, 0, 0, 0x00, 0x00);
    set_gate(1, 0, 0, 0x9A, 0x20);
    set_gate(2, 0, 0, 0x92, 0x00);
    set_gate(3, 0, 0, 0xFA, 0x20);
    set_gate(4, 0, 0, 0xF2, 0x00);
    set_gate(5, 0, 0, 0xFA, 0x20);

    uint8_t *tp = (uint8_t *)&tss;
    for (unsigned i = 0; i < sizeof(tss); i++) tp[i] = 0;
    tss.iomap_base = sizeof(tss);
    set_tss_gate(6, (uint64_t)&tss, sizeof(tss) - 1);

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)&gdt;

    gdt_flush(&gp);

    uint64_t efer = rdmsr(0xC0000080);
    efer |= 1;
    wrmsr(0xC0000080, efer);

    tss_flush();
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
