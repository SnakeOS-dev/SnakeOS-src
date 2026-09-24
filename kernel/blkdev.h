#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>
#include <stddef.h>

#define BLKDEV_NAME_LEN 16

struct blockdev;

typedef struct blockdev_ops {
    int (*read)(struct blockdev *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write)(struct blockdev *dev, uint64_t lba, uint32_t count, const void *buf);
    void (*sync)(struct blockdev *dev);
} blockdev_ops_t;

typedef struct blockdev {
    char name[BLKDEV_NAME_LEN];
    uint64_t sector_size;                                           
    uint64_t sector_count;                         
    uint32_t flags;
    const blockdev_ops_t *ops;
    void *private;
    struct blockdev *next;
} blockdev_t;

#define BLKDEV_RDONLY 0x01

void        blockdev_register(blockdev_t *dev);
void        blockdev_unregister(blockdev_t *dev);
blockdev_t *blockdev_find(const char *name);
blockdev_t *blockdev_first(void);
blockdev_t *blockdev_next(blockdev_t *dev);
size_t      blockdev_count(void);

int blockdev_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf);
int blockdev_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf);

#endif
