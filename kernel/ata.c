#include "ata.h"
#include "blkdev.h"
#include "io.h"

#define ATA_MAX_DEVICES    4
#define ATA_SECTOR_SIZE    512

#define ATA_PRIMARY_BASE   0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_BASE 0x170
#define ATA_SECONDARY_CTRL 0x376

#define REG_DATA   0x00
#define REG_ERR    0x01
#define REG_SECT   0x02
#define REG_LBA0   0x03
#define REG_LBA1   0x04
#define REG_LBA2   0x05
#define REG_DRIVE  0x06
#define REG_STATUS 0x07
#define REG_CMD    0x07

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

#define ATA_SR_ERR  0x01
#define ATA_SR_DRQ  0x08
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY  0x80

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

typedef struct {
    uint16_t base;
    uint16_t ctrl;
    uint8_t  drive;                            
} ata_drive_t;

static ata_drive_t ata_drives[ATA_MAX_DEVICES];
static blockdev_t  ata_devs[ATA_MAX_DEVICES];
static int         ata_count = 0;

static int ata_wait_bsy_clear(uint16_t base) {
    for (int i = 0; i < 10000000; i++) {
        if (!(inb(base + REG_STATUS) & ATA_SR_BSY)) return 0;
        io_wait();
    }
    return -1;
}

static int ata_wait_drq(uint16_t base) {
    for (int i = 0; i < 10000000; i++) {
        uint8_t s = inb(base + REG_STATUS);
        if (s & ATA_SR_ERR)  return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
        io_wait();
    }
    return -1;
}

static void ata_select(ata_drive_t *d) {
    outb(d->base + REG_DRIVE, (uint8_t)(0xA0 | (d->drive << 4)));
    for (int i = 0; i < 4; i++) inb(d->ctrl);            
}

static int ata_identify(ata_drive_t *d, uint16_t *id                ) {
    ata_select(d);
    outb(d->base + REG_SECT, 0);
    outb(d->base + REG_LBA0, 0);
    outb(d->base + REG_LBA1, 0);
    outb(d->base + REG_LBA2, 0);
    outb(d->base + REG_CMD, ATA_CMD_IDENTIFY);

    if (inb(d->base + REG_STATUS) == 0) return -1;

    int ready = 0;
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(d->base + REG_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY)) {
            ready = 1;
            break;
        }
        io_wait();
    }
    if (!ready) return -1;

    uint8_t lba1 = inb(d->base + REG_LBA1);
    uint8_t lba2 = inb(d->base + REG_LBA2);
    if ((lba1 == 0x14 && lba2 == 0xEB) ||
        (lba1 == 0x69 && lba2 == 0x96))
        return -1;
    if (!(inb(d->base + REG_STATUS) & ATA_SR_DRQ)) return -1;

    for (int i = 0; i < 256; i++)
        id[i] = inw(d->base + REG_DATA);
    return 0;
}

static int ata_read28(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    ata_drive_t *d = dev->private;
    if (count == 0 || count > 256 || lba > 0x0FFFFFFF) return -1;

    if (ata_wait_bsy_clear(d->base) < 0) return -1;
    ata_select(d);
    outb(d->base + REG_DRIVE, (uint8_t)(0xE0 | (d->drive << 4)
                                       | ((lba >> 24) & 0x0F)));
    for (int i = 0; i < 4; i++) inb(d->ctrl);
    outb(d->base + REG_SECT, count == 256 ? 0 : (uint8_t)count); io_wait();
    outb(d->base + REG_LBA0, (uint8_t)lba); io_wait();
    outb(d->base + REG_LBA1, (uint8_t)(lba >> 8)); io_wait();
    outb(d->base + REG_LBA2, (uint8_t)(lba >> 16)); io_wait();
    outb(d->base + REG_CMD, ATA_CMD_READ_SECTORS);

    uint16_t *dst = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_drq(d->base) < 0) return -1;
        for (int i = 0; i < 256; i++)
            dst[i] = inw(d->base + REG_DATA);
        dst += 256;
    }
    return 0;
}

static int ata_write28(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    ata_drive_t *d = dev->private;
    if (count == 0 || count > 256 || lba > 0x0FFFFFFF) return -1;

    ata_select(d);
    outb(d->base + REG_DRIVE, (uint8_t)(0xE0 | (d->drive << 4)
                                       | ((lba >> 24) & 0x0F)));
    outb(d->base + REG_SECT, count == 256 ? 0 : (uint8_t)count);
    outb(d->base + REG_LBA0, (uint8_t)lba);
    outb(d->base + REG_LBA1, (uint8_t)(lba >> 8));
    outb(d->base + REG_LBA2, (uint8_t)(lba >> 16));
    outb(d->base + REG_CMD, ATA_CMD_WRITE_SECTORS);

    const uint16_t *src = (const uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_drq(d->base) < 0) return -1;
        for (int i = 0; i < 256; i++)
            outw(d->base + REG_DATA, src[i]);
        src += 256;
    }

    outb(d->base + REG_CMD, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy_clear(d->base);
    return 0;
}

static const blockdev_ops_t ata_ops = {
    .read  = ata_read28,
    .write = ata_write28,
};

void ata_init(void) {
    static const struct { uint16_t base, ctrl; } buses[2] = {
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL   },
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL },
    };
    uint16_t id[256];
    ata_count = 0;

	for (int b = 0; b < 2 && ata_count < ATA_MAX_DEVICES; b++) {
        for (int drv = 0; drv < 2 && ata_count < ATA_MAX_DEVICES; drv++) {
            ata_drive_t *d = &ata_drives[ata_count];
            d->base  = buses[b].base;
            d->ctrl  = buses[b].ctrl;
            d->drive = (uint8_t)drv;

            if (ata_identify(d, id) < 0) continue;
	    if (id[0] & 0x8000) continue;
            uint32_t sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
            if (sectors == 0) continue;

            blockdev_t *dev = &ata_devs[ata_count];
            uint8_t *p = (uint8_t *)dev;
            for (unsigned i = 0; i < sizeof(blockdev_t); i++) p[i] = 0;

            dev->sector_size  = ATA_SECTOR_SIZE;
            dev->sector_count = sectors;
            dev->ops          = &ata_ops;
            dev->private      = d;

                                        
            dev->name[0] = 'h';
            dev->name[1] = 'd';
            dev->name[2] = (char)('a' + ata_count);
            dev->name[3] = '0';
            dev->name[4] = 0;

            blockdev_register(dev);
            ata_count++;
        }
    }
}

int ata_device_count(void) { return ata_count; }
