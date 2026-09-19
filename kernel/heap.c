#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "kernel.h"

#define HEAP_BASE   0xFFFF800000000000ULL
#define HEAP_MAX    (64ULL * 1024 * 1024)
#define HEAP_MAGIC  0xA110C8ED

typedef struct block {
    uint32_t magic;
    uint32_t flags;
    size_t   size;
    struct block *next;
    struct block *prev;
} block_t;

#define BLOCK_FREE 1

#define HDR_SIZE   (sizeof(block_t))
#define ALIGN8(x)  (((x) + 7) & ~(size_t)7)

static block_t *head = 0;
static uint64_t heap_end = HEAP_BASE;
static size_t   heap_used = 0;

extern uint8_t kernel_end;

static block_t *split_block(block_t *b, size_t size) {
    if (b->size < size + HDR_SIZE + 16)
        return b;

    block_t *nb = (block_t *)((uint8_t *)b + HDR_SIZE + size);
    nb->magic = HEAP_MAGIC;
    nb->flags = BLOCK_FREE;
    nb->size  = b->size - size - HDR_SIZE;
    nb->next  = b->next;
    nb->prev  = b;
    if (nb->next) nb->next->prev = nb;

    b->size = size;
    b->next = nb;
    return b;
}

static void coalesce(block_t *b) {
    if (b->next && (b->next->flags & BLOCK_FREE)) {
        block_t *n = b->next;
        b->size += HDR_SIZE + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && (b->prev->flags & BLOCK_FREE)) {
        block_t *p = b->prev;
        p->size += HDR_SIZE + b->size;
        p->next = b->next;
        if (p->next) p->next->prev = p;
    }
}

static int grow_heap(size_t need) {
    if (heap_end - HEAP_BASE + need > HEAP_MAX)
        return -1;

    uint64_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) return -1;
        vmm_map(heap_end, (uint64_t)phys, PAGE_PRESENT | PAGE_RW);
        heap_end += PAGE_SIZE;
    }
    return 0;
}

void heap_init(void) {
    void *phys = pmm_alloc_page();
    if (!phys) return;
    vmm_map(HEAP_BASE, (uint64_t)phys, PAGE_PRESENT | PAGE_RW);

    heap_end  = HEAP_BASE + PAGE_SIZE;
    heap_used = 0;

    head = (block_t *)HEAP_BASE;
    head->magic = HEAP_MAGIC;
    head->flags = BLOCK_FREE;
    head->size  = PAGE_SIZE - HDR_SIZE;
    head->next  = 0;
    head->prev  = 0;
}

static block_t *find_free(size_t size) {
    for (block_t *b = head; b; b = b->next) {
        if ((b->flags & BLOCK_FREE) && b->size >= size)
            return b;
    }
    return 0;
}

static block_t *extend_tail(size_t size) {
    block_t *last = head;
    while (last->next) last = last->next;

    if (!(last->flags & BLOCK_FREE)) {
        size_t need = HDR_SIZE + size;
        if (grow_heap(need) < 0) return 0;

        block_t *nb = (block_t *)((uint8_t *)last + HDR_SIZE + last->size);
        nb->magic = HEAP_MAGIC;
        nb->flags = BLOCK_FREE;
        nb->size  = (heap_end - (uint64_t)nb - HDR_SIZE);
        nb->next  = 0;
        nb->prev  = last;
        last->next = nb;
        last = nb;
    } else {
        size_t need = size;
        if (last->size < need) {
            size_t extra = need - last->size;
            extra = (extra + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
            if (grow_heap(extra) < 0) return 0;
            last->size += extra;
        }
    }
    return last;
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;
    size = ALIGN8(size);

    block_t *b = find_free(size);
    if (!b) {
        b = extend_tail(size);
        if (!b) return 0;
    }

    b = split_block(b, size);
    b->flags &= ~BLOCK_FREE;
    heap_used += b->size + HDR_SIZE;

    return (uint8_t *)b + HDR_SIZE;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_t *b = (block_t *)((uint8_t *)ptr - HDR_SIZE);
    if (b->magic != HEAP_MAGIC) return;
    if (b->flags & BLOCK_FREE) return;

    b->flags |= BLOCK_FREE;
    heap_used -= b->size + HDR_SIZE;
    coalesce(b);
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (!p) return 0;
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < size; i++) b[i] = 0;
    return p;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return 0; }

    block_t *b = (block_t *)((uint8_t *)ptr - HDR_SIZE);
    if (b->magic != HEAP_MAGIC) return 0;

    size = ALIGN8(size);
    if (b->size >= size) {
        split_block(b, size);
        return ptr;
    }

    if (b->next && (b->next->flags & BLOCK_FREE)
        && (b->size + HDR_SIZE + b->next->size) >= size) {
        block_t *n = b->next;
        b->size += HDR_SIZE + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
        split_block(b, size);
        return ptr;
    }

    void *np = kmalloc(size);
    if (!np) return 0;
    uint8_t *s = (uint8_t *)ptr;
    uint8_t *d = (uint8_t *)np;
    size_t copy = b->size < size ? b->size : size;
    for (size_t i = 0; i < copy; i++) d[i] = s[i];
    kfree(ptr);
    return np;
}

void heap_stats(size_t *used, size_t *free_bytes, size_t *total) {
    size_t u = 0, f = 0;
    for (block_t *b = head; b; b = b->next) {
        if (b->flags & BLOCK_FREE) f += b->size;
        else                        u += b->size;
    }
    if (used)       *used = u;
    if (free_bytes) *free_bytes = f;
    if (total)      *total = heap_end - HEAP_BASE;
}
