#include "ramdisk.h"
#include "heap.h"

typedef struct {
    uint8_t *data;
    uint64_t bytes;
} ramdisk_t;

#define RAMDISK_SECTOR_SIZE 512

static int ramdisk_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    ramdisk_t *r = dev->private;
    uint64_t off = lba * RAMDISK_SECTOR_SIZE;
    uint64_t len = (uint64_t)count * RAMDISK_SECTOR_SIZE;
    if (off + len > r->bytes) return -1;
    uint8_t *d = (uint8_t *)buf;
    for (uint64_t i = 0; i < len; i++) d[i] = r->data[off + i];
    return 0;
}

static int ramdisk_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    ramdisk_t *r = dev->private;
    uint64_t off = lba * RAMDISK_SECTOR_SIZE;
    uint64_t len = (uint64_t)count * RAMDISK_SECTOR_SIZE;
    if (off + len > r->bytes) return -1;
    const uint8_t *s = (const uint8_t *)buf;
    for (uint64_t i = 0; i < len; i++) r->data[off + i] = s[i];
    return 0;
}

static const blockdev_ops_t ramdisk_ops = {
    .read  = ramdisk_read,
    .write = ramdisk_write,
};

blockdev_t *ramdisk_create(const char *name, uint64_t bytes) {
    if (!name || bytes == 0) return 0;
    bytes = (bytes + 511) & ~511ULL;

    blockdev_t *dev = (blockdev_t *)kmalloc(sizeof(blockdev_t));
    if (!dev) return 0;
    ramdisk_t *r = (ramdisk_t *)kmalloc(sizeof(ramdisk_t));
    if (!r) { kfree(dev); return 0; }
    r->data = (uint8_t *)kzalloc(bytes);
    if (!r->data) { kfree(r); kfree(dev); return 0; }
    r->bytes = bytes;

    uint8_t *p = (uint8_t *)dev;
    for (unsigned i = 0; i < sizeof(blockdev_t); i++) p[i] = 0;

    int i = 0;
    while (name[i] && i < BLKDEV_NAME_LEN - 1) { dev->name[i] = name[i]; i++; }
    dev->name[i] = 0;

    dev->sector_size  = RAMDISK_SECTOR_SIZE;
    dev->sector_count = bytes / RAMDISK_SECTOR_SIZE;
    dev->ops          = &ramdisk_ops;
    dev->private      = r;
    dev->flags        = 0;

    blockdev_register(dev);
    return dev;
}

void ramdisk_destroy(blockdev_t *dev) {
    if (!dev) return;
    blockdev_unregister(dev);
    ramdisk_t *r = (ramdisk_t *)dev->private;
    if (r) { kfree(r->data); kfree(r); }
    kfree(dev);
}
