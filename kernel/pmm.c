#include "pmm.h"
#include "kernel.h"

extern uint8_t kernel_end;

#define MAX_PAGES   (1024 * 1024)
#define BITMAP_SIZE (MAX_PAGES / 8)

static uint8_t bitmap[BITMAP_SIZE];
static size_t  total_pages = 0;
static size_t  free_pages  = 0;
static size_t  next_hint   = 0;

static inline int  test_bit(size_t i) { return bitmap[i >> 3] & (1 << (i & 7)); }
static inline void set_bit(size_t i)  { bitmap[i >> 3] |=  (1 << (i & 7)); }
static inline void clear_bit(size_t i){ bitmap[i >> 3] &= ~(1 << (i & 7)); }

static void mark_used(size_t i) {
    if (!test_bit(i)) { set_bit(i); if (free_pages) free_pages--; }
}

static void mark_free(size_t i) {
    if (test_bit(i)) { clear_bit(i); free_pages++; }
}

static void reserve_region(uint64_t base, uint64_t len) {
    uint64_t s = (base + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t e = (base + len) / PAGE_SIZE;
    if (e > MAX_PAGES) e = MAX_PAGES;
    for (uint64_t p = s; p < e; p++) mark_used((size_t)p);
}

static void free_region(uint64_t base, uint64_t len) {
    uint64_t s = (base + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t e = (base + len) / PAGE_SIZE;
    if (e > MAX_PAGES) e = MAX_PAGES;
    for (uint64_t p = s; p < e; p++) mark_free((size_t)p);
}

void pmm_init(uint32_t mbi_addr) {
    for (size_t i = 0; i < BITMAP_SIZE; i++) bitmap[i] = 0xFF;
    total_pages = MAX_PAGES;
    free_pages  = 0;

    if (mbi_addr) {
        uint32_t flags = *(uint32_t *)mbi_addr;
        if (flags & (1u << 6)) {
            uint32_t mmap_len  = *(uint32_t *)(mbi_addr + 44);
            uint32_t mmap_addr = *(uint32_t *)(mbi_addr + 48);
            uint32_t p   = mmap_addr;
            uint32_t end = mmap_addr + mmap_len;

            while (p < end) {
                uint32_t size = *(uint32_t *)p;
                uint64_t base = *(uint64_t *)(p + 4);
                uint64_t len  = *(uint64_t *)(p + 12);
                uint32_t type = *(uint32_t *)(p + 20);

                if (type == 1)
                    free_region(base, len);

                p += size + 4;
            }
        }
    }

    total_pages = free_pages;

    reserve_region(0, 0x100000);

    uint64_t kend = (uint64_t)&kernel_end;
    if (kend > 0x100000)
        reserve_region(0x100000, kend - 0x100000);

    next_hint = 0;
}

void *pmm_alloc_page(void) {
    for (size_t i = next_hint; i < MAX_PAGES; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            if (free_pages) free_pages--;
            next_hint = i + 1;
            return (void *)(i * PAGE_SIZE);
        }
    }
    for (size_t i = 0; i < next_hint; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            if (free_pages) free_pages--;
            next_hint = i + 1;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *page) {
    size_t i = (size_t)page / PAGE_SIZE;
    if (i < MAX_PAGES && test_bit(i)) {
        clear_bit(i);
        free_pages++;
        if (i < next_hint) next_hint = i;
    }
}

size_t pmm_free_pages(void) {
    return free_pages;
}

size_t pmm_total_pages(void) {
    return total_pages;
}
