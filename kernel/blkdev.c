#include "blkdev.h"

static blockdev_t *dev_list = 0;
static size_t      dev_count = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

void blockdev_register(blockdev_t *dev) {
    if (!dev || !dev->ops) return;
    dev->next = dev_list;
    dev_list  = dev;
    dev_count++;
}

void blockdev_unregister(blockdev_t *dev) {
    if (!dev) return;
    blockdev_t **pp = &dev_list;
    while (*pp && *pp != dev) pp = &(*pp)->next;
    if (*pp) {
        *pp = dev->next;
        dev_count--;
    }
}

blockdev_t *blockdev_find(const char *name) {
    for (blockdev_t *d = dev_list; d; d = d->next)
        if (str_eq(d->name, name)) return d;
    return 0;
}

blockdev_t *blockdev_first(void)      { return dev_list; }
blockdev_t *blockdev_next(blockdev_t *d) { return d ? d->next : 0; }
size_t      blockdev_count(void)      { return dev_count; }

int blockdev_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    if (lba + count > dev->sector_count) return -1;
    return dev->ops->read(dev, lba, count, buf);
}

int blockdev_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->ops || !dev->ops->write) return -1;
    if (dev->flags & BLKDEV_RDONLY) return -1;
    if (lba + count > dev->sector_count) return -1;
    return dev->ops->write(dev, lba, count, buf);
}
