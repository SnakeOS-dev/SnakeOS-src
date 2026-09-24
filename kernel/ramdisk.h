#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>
#include <stddef.h>
#include "blkdev.h"

blockdev_t *ramdisk_create(const char *name, uint64_t bytes);
void        ramdisk_destroy(blockdev_t *dev);

#endif
