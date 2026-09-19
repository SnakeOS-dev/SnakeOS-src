#include "vmm.h"
#include "pmm.h"
#include "kernel.h"

static uint64_t kernel_pml4 = 0;

static inline uint64_t pml4_index(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline uint64_t pdpt_index(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline uint64_t pd_index(uint64_t v)   { return (v >> 21) & 0x1FF; }
static inline uint64_t pt_index(uint64_t v)   { return (v >> 12) & 0x1FF; }

static inline void invlpg(uint64_t v) {
    __asm__ volatile ("invlpg (%0)" :: "r"(v) : "memory");
}

static uint64_t *next_table(uint64_t *table, uint64_t idx, uint64_t flags) {
    if (!(table[idx] & PAGE_PRESENT)) {
        void *p = pmm_alloc_page();
        if (!p) return 0;
        uint8_t *z = (uint8_t *)p;
        for (size_t i = 0; i < PAGE_SIZE; i++) z[i] = 0;
        table[idx] = ((uint64_t)p & PAGE_ADDR_MASK)
                   | PAGE_PRESENT | PAGE_RW
                   | (flags & PAGE_USER);
    }
    return (uint64_t *)(table[idx] & PAGE_ADDR_MASK);
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)kernel_pml4;
    if (!pml4) return;

    uint64_t *pdpt = next_table(pml4, pml4_index(virt), flags);
    if (!pdpt) return;
    uint64_t *pd = next_table(pdpt, pdpt_index(virt), flags);
    if (!pd) return;

    if (pd[pd_index(virt)] & PAGE_HUGE)
        return;

    uint64_t *pt = next_table(pd, pd_index(virt), flags);
    if (!pt) return;

    pt[pt_index(virt)] = (phys & PAGE_ADDR_MASK)
                       | (flags & ~PAGE_ADDR_MASK & ~PAGE_HUGE)
                       | PAGE_PRESENT;
    invlpg(virt);
}

void vmm_map_huge(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)kernel_pml4;
    if (!pml4) return;

    uint64_t *pdpt = next_table(pml4, pml4_index(virt), flags);
    if (!pdpt) return;
    uint64_t *pd = next_table(pdpt, pdpt_index(virt), flags);
    if (!pd) return;

    pd[pd_index(virt)] = (phys & 0x000FFFFFFFE00000ULL)
                       | (flags & ~PAGE_ADDR_MASK & ~PAGE_HUGE)
                       | PAGE_PRESENT | PAGE_HUGE;
    invlpg(virt);
}

void vmm_map_range_huge(uint64_t virt, uint64_t size, uint64_t flags) {
    uint64_t off = 0;
    while (off < size) {
        vmm_map_huge(virt + off, virt + off, flags);
        off += 0x200000ULL;
    }
}

void vmm_unmap(uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)kernel_pml4;
    if (!pml4) return;

    if (!(pml4[pml4_index(virt)] & PAGE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index(virt)] & PAGE_ADDR_MASK);
    if (!(pdpt[pdpt_index(virt)] & PAGE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index(virt)] & PAGE_ADDR_MASK);
    if (!(pd[pd_index(virt)] & PAGE_PRESENT)) return;

    if (pd[pd_index(virt)] & PAGE_HUGE) {
        pd[pd_index(virt)] = 0;
        invlpg(virt);
        return;
    }

    uint64_t *pt = (uint64_t *)(pd[pd_index(virt)] & PAGE_ADDR_MASK);
    pt[pt_index(virt)] = 0;
    invlpg(virt);
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)kernel_pml4;
    if (!pml4) return 0;

    if (!(pml4[pml4_index(virt)] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index(virt)] & PAGE_ADDR_MASK);
    if (!(pdpt[pdpt_index(virt)] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index(virt)] & PAGE_ADDR_MASK);
    if (!(pd[pd_index(virt)] & PAGE_PRESENT)) return 0;

    if (pd[pd_index(virt)] & PAGE_HUGE)
        return (pd[pd_index(virt)] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFF);

    uint64_t *pt = (uint64_t *)(pd[pd_index(virt)] & PAGE_ADDR_MASK);
    if (!(pt[pt_index(virt)] & PAGE_PRESENT)) return 0;
    return (pt[pt_index(virt)] & PAGE_ADDR_MASK) | (virt & 0xFFF);
}

void *vmm_alloc_map(uint64_t virt, uint64_t flags) {
    void *p = pmm_alloc_page();
    if (!p) return 0;
    vmm_map(virt, (uint64_t)p, flags);
    return (void *)virt;
}

void vmm_switch(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys & PAGE_ADDR_MASK) : "memory");
}

uint64_t vmm_current_pml4(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & PAGE_ADDR_MASK;
}

void vmm_init(void) {
    void *page = pmm_alloc_page();
    if (!page) return;

    uint8_t *z = (uint8_t *)page;
    for (size_t i = 0; i < PAGE_SIZE; i++) z[i] = 0;

    kernel_pml4 = (uint64_t)page;

    vmm_map_range_huge(0, 0x100000000ULL, PAGE_PRESENT | PAGE_RW);

    vmm_switch(kernel_pml4);
}
