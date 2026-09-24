#include "ext2.h"
#include "heap.h"
#include "kernel.h"
static const vfs_node_ops_t ext2_node_ops;
static const vfs_fs_t        ext2_fs;
#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_VALID_FS   1
#define EXT2_ERROR_FS   2

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR     2
#define EXT2_FT_CHRDEV  3
#define EXT2_FT_BLKDEV  4
#define EXT2_FT_FIFO    5
#define EXT2_FT_SOCK    6
#define EXT2_FT_SYMLINK 7

#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

#define EXT2_S_IRUSR 0x0100
#define EXT2_S_IWUSR 0x0080
#define EXT2_S_IXUSR 0x0040
#define EXT2_S_IRGRP 0x0020
#define EXT2_S_IWGRP 0x0010
#define EXT2_S_IXGRP 0x0008
#define EXT2_S_IROTH 0x0004
#define EXT2_S_IWOTH 0x0002
#define EXT2_S_IXOTH 0x0001

struct ext2_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint32_t s_prealloc_blocks;
    uint32_t s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint16_t pad[255];
} __attribute__((packed));

struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed));

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

typedef struct {
    blockdev_t *dev;
    struct ext2_super_block sb;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t groups_block;
    uint32_t first_data_block;
    uint8_t *block_buf;
    uint8_t *gd_buf;
    uint32_t gd_sectors;
    uint32_t gd_block;
    uint32_t itable_block;
    uint32_t bmap_block;
    uint32_t imap_block;
    uint32_t root_inode;
} ext2_fs_t;

typedef struct {
    ext2_fs_t *fs;
    uint32_t ino;
    struct ext2_inode inode;
    int dirty;
} ext2_file_t;

static uint32_t ext2_block_size(ext2_fs_t *fs) {
    return 1024U << fs->sb.s_log_block_size;
}

static uint32_t ext2_sectors_per_block(ext2_fs_t *fs) {
    return ext2_block_size(fs) / 512;
}

static int ext2_read_block(ext2_fs_t *fs, uint32_t block, void *buf) {
    uint64_t lba = (uint64_t)block * ext2_sectors_per_block(fs);
    return blockdev_read(fs->dev, lba, ext2_sectors_per_block(fs), buf);
}

static int ext2_write_block(ext2_fs_t *fs, uint32_t block, const void *buf) {
    uint64_t lba = (uint64_t)block * ext2_sectors_per_block(fs);
    return blockdev_write(fs->dev, lba, ext2_sectors_per_block(fs), buf);
}

static uint32_t ext2_group_of_inode(ext2_fs_t *fs, uint32_t ino) {
    return (ino - 1) / fs->inodes_per_group;
}

static uint32_t ext2_index_in_group(ext2_fs_t *fs, uint32_t ino) {
    return (ino - 1) % fs->inodes_per_group;
}

static uint32_t ext2_inode_table_block(ext2_fs_t *fs, uint32_t group) {
    uint32_t gd_per_block = ext2_block_size(fs) / sizeof(struct ext2_group_desc);
    uint32_t gd_block = fs->groups_block + group / gd_per_block;
    uint32_t idx = group % gd_per_block;
    ext2_read_block(fs, gd_block, fs->gd_buf);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)fs->gd_buf;
    return gd[idx].bg_inode_table;
}

static int ext2_read_inode(ext2_fs_t *fs, uint32_t ino, struct ext2_inode *out) {
    uint32_t group = ext2_group_of_inode(fs, ino);
    uint32_t idx = ext2_index_in_group(fs, ino);
    uint32_t itable = ext2_inode_table_block(fs, group);
    uint32_t per_block = ext2_block_size(fs) / fs->inode_size;
    uint32_t block = itable + idx / per_block;
    uint32_t off = (idx % per_block) * fs->inode_size;
    if (ext2_read_block(fs, block, fs->block_buf) < 0) return -1;
    uint8_t *p = fs->block_buf + off;
    for (uint32_t i = 0; i < sizeof(struct ext2_inode); i++)
        ((uint8_t *)out)[i] = p[i];
    return 0;
}

static int ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const struct ext2_inode *in) {
    uint32_t group = ext2_group_of_inode(fs, ino);
    uint32_t idx = ext2_index_in_group(fs, ino);
    uint32_t itable = ext2_inode_table_block(fs, group);
    uint32_t per_block = ext2_block_size(fs) / fs->inode_size;
    uint32_t block = itable + idx / per_block;
    uint32_t off = (idx % per_block) * fs->inode_size;
    if (ext2_read_block(fs, block, fs->block_buf) < 0) return -1;
    uint8_t *p = fs->block_buf + off;
    for (uint32_t i = 0; i < sizeof(struct ext2_inode); i++)
        p[i] = ((const uint8_t *)in)[i];
    return ext2_write_block(fs, block, fs->block_buf);
}

static uint32_t ext2_inode_block(ext2_fs_t *fs, struct ext2_inode *in,
                                 uint32_t file_block) {
    uint32_t bs = ext2_block_size(fs);
    uint32_t ptrs = bs / 4;
    if (file_block < 12) {
        return in->i_block[file_block];
    }
    file_block -= 12;
    if (file_block < ptrs) {
        if (in->i_block[12] == 0) return 0;
        ext2_read_block(fs, in->i_block[12], fs->block_buf);
        return ((uint32_t *)fs->block_buf)[file_block];
    }
    file_block -= ptrs;
    if (file_block < ptrs * ptrs) {
        if (in->i_block[13] == 0) return 0;
        ext2_read_block(fs, in->i_block[13], fs->block_buf);
        uint32_t l1 = ((uint32_t *)fs->block_buf)[file_block / ptrs];
        if (l1 == 0) return 0;
        ext2_read_block(fs, l1, fs->block_buf);
        return ((uint32_t *)fs->block_buf)[file_block % ptrs];
    }
    file_block -= ptrs * ptrs;
    if (file_block < ptrs * ptrs * ptrs) {
        if (in->i_block[14] == 0) return 0;
        ext2_read_block(fs, in->i_block[14], fs->block_buf);
        uint32_t l1 = ((uint32_t *)fs->block_buf)[file_block / (ptrs * ptrs)];
        if (l1 == 0) return 0;
        ext2_read_block(fs, l1, fs->block_buf);
        uint32_t rem = file_block % (ptrs * ptrs);
        uint32_t l2 = ((uint32_t *)fs->block_buf)[rem / ptrs];
        if (l2 == 0) return 0;
        ext2_read_block(fs, l2, fs->block_buf);
        return ((uint32_t *)fs->block_buf)[rem % ptrs];
    }
    return 0;
}

static int ext2_set_inode_block(ext2_fs_t *fs, struct ext2_inode *in,
                                uint32_t file_block, uint32_t phys_block) {
    uint32_t bs = ext2_block_size(fs);
    uint32_t ptrs = bs / 4;
    if (file_block < 12) {
        in->i_block[file_block] = phys_block;
        return 0;
    }
    file_block -= 12;
    if (file_block < ptrs) {
        if (in->i_block[12] == 0) return -1;
        ext2_read_block(fs, in->i_block[12], fs->block_buf);
        ((uint32_t *)fs->block_buf)[file_block] = phys_block;
        return ext2_write_block(fs, in->i_block[12], fs->block_buf);
    }
    file_block -= ptrs;
    if (file_block < ptrs * ptrs) {
        if (in->i_block[13] == 0) return -1;
        ext2_read_block(fs, in->i_block[13], fs->block_buf);
        uint32_t l1 = ((uint32_t *)fs->block_buf)[file_block / ptrs];
        if (l1 == 0) return -1;
        ext2_read_block(fs, l1, fs->block_buf);
        ((uint32_t *)fs->block_buf)[file_block % ptrs] = phys_block;
        return ext2_write_block(fs, l1, fs->block_buf);
    }
    file_block -= ptrs * ptrs;
    if (file_block < ptrs * ptrs * ptrs) {
        if (in->i_block[14] == 0) return -1;
        ext2_read_block(fs, in->i_block[14], fs->block_buf);
        uint32_t l1 = ((uint32_t *)fs->block_buf)[file_block / (ptrs * ptrs)];
        if (l1 == 0) return -1;
        ext2_read_block(fs, l1, fs->block_buf);
        uint32_t rem = file_block % (ptrs * ptrs);
        uint32_t l2 = ((uint32_t *)fs->block_buf)[rem / ptrs];
        if (l2 == 0) return -1;
        ext2_read_block(fs, l2, fs->block_buf);
        ((uint32_t *)fs->block_buf)[rem % ptrs] = phys_block;
        return ext2_write_block(fs, l2, fs->block_buf);
    }
    return -1;
}

static uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return 0;
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t gd_per_block = bs / sizeof(struct ext2_group_desc);
        uint32_t gd_block = fs->groups_block + g / gd_per_block;
        uint32_t idx = g % gd_per_block;
        ext2_read_block(fs, gd_block, fs->gd_buf);
        struct ext2_group_desc *gd = (struct ext2_group_desc *)fs->gd_buf;
        if (gd[idx].bg_free_blocks_count == 0) continue;
        uint32_t bmap = gd[idx].bg_block_bitmap;
        ext2_read_block(fs, bmap, buf);
        uint32_t total = fs->blocks_per_group;
        if (g == fs->group_count - 1) {
            uint32_t rem = fs->sb.s_blocks_count - fs->first_data_block
                         - g * fs->blocks_per_group;
            if (rem < total) total = rem;
        }
        for (uint32_t i = 0; i < total; i++) {
            if (!(buf[i >> 3] & (1 << (i & 7)))) {
                buf[i >> 3] |= (1 << (i & 7));
                ext2_write_block(fs, bmap, buf);
                gd[idx].bg_free_blocks_count--;
                ext2_write_block(fs, gd_block, fs->gd_buf);
                fs->sb.s_free_blocks_count--;
                uint32_t sb_block = fs->sb.s_log_block_size == 0 ? 1 : 0;
                ext2_write_block(fs, sb_block, &fs->sb);
                kfree(buf);
                return fs->first_data_block + g * fs->blocks_per_group + i;
            }
        }
    }
    kfree(buf);
    return 0;
}

static void ext2_free_block(ext2_fs_t *fs, uint32_t block) {
    uint32_t bs = ext2_block_size(fs);
    uint32_t g = (block - fs->first_data_block) / fs->blocks_per_group;
    uint32_t i = (block - fs->first_data_block) % fs->blocks_per_group;
    uint32_t gd_per_block = bs / sizeof(struct ext2_group_desc);
    uint32_t gd_block = fs->groups_block + g / gd_per_block;
    uint32_t idx = g % gd_per_block;
    ext2_read_block(fs, gd_block, fs->gd_buf);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)fs->gd_buf;
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return;
    ext2_read_block(fs, gd[idx].bg_block_bitmap, buf);
    buf[i >> 3] &= ~(1 << (i & 7));
    ext2_write_block(fs, gd[idx].bg_block_bitmap, buf);
    gd[idx].bg_free_blocks_count++;
    ext2_write_block(fs, gd_block, fs->gd_buf);
    fs->sb.s_free_blocks_count++;
    uint32_t sb_block = fs->sb.s_log_block_size == 0 ? 1 : 0;
    ext2_write_block(fs, sb_block, &fs->sb);
    kfree(buf);
}

static uint32_t ext2_alloc_inode(ext2_fs_t *fs) {
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return 0;
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t gd_per_block = bs / sizeof(struct ext2_group_desc);
        uint32_t gd_block = fs->groups_block + g / gd_per_block;
        uint32_t idx = g % gd_per_block;
        ext2_read_block(fs, gd_block, fs->gd_buf);
        struct ext2_group_desc *gd = (struct ext2_group_desc *)fs->gd_buf;
        if (gd[idx].bg_free_inodes_count == 0) continue;
        uint32_t imap = gd[idx].bg_inode_bitmap;
        ext2_read_block(fs, imap, buf);
        for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
            if (!(buf[i >> 3] & (1 << (i & 7)))) {
                buf[i >> 3] |= (1 << (i & 7));
                ext2_write_block(fs, imap, buf);
                gd[idx].bg_free_inodes_count--;
                ext2_write_block(fs, gd_block, fs->gd_buf);
                fs->sb.s_free_inodes_count--;
                uint32_t sb_block = fs->sb.s_log_block_size == 0 ? 1 : 0;
                ext2_write_block(fs, sb_block, &fs->sb);
                kfree(buf);
                return g * fs->inodes_per_group + i + 1;
            }
        }
    }
    kfree(buf);
    return 0;
}

static void ext2_free_inode(ext2_fs_t *fs, uint32_t ino) {
    uint32_t bs = ext2_block_size(fs);
    uint32_t g = (ino - 1) / fs->inodes_per_group;
    uint32_t i = (ino - 1) % fs->inodes_per_group;
    uint32_t gd_per_block = bs / sizeof(struct ext2_group_desc);
    uint32_t gd_block = fs->groups_block + g / gd_per_block;
    uint32_t idx = g % gd_per_block;
    ext2_read_block(fs, gd_block, fs->gd_buf);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)fs->gd_buf;
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return;
    ext2_read_block(fs, gd[idx].bg_inode_bitmap, buf);
    buf[i >> 3] &= ~(1 << (i & 7));
    ext2_write_block(fs, gd[idx].bg_inode_bitmap, buf);
    gd[idx].bg_free_inodes_count++;
    ext2_write_block(fs, gd_block, fs->gd_buf);
    fs->sb.s_free_inodes_count++;
    uint32_t sb_block = fs->sb.s_log_block_size == 0 ? 1 : 0;
    ext2_write_block(fs, sb_block, &fs->sb);
    kfree(buf);
}

static int ext2_ensure_block(ext2_fs_t *fs, struct ext2_inode *in,
                             uint32_t file_block) {
    uint32_t existing = ext2_inode_block(fs, in, file_block);
    if (existing) return (int)existing;
    uint32_t bs = ext2_block_size(fs);
    uint32_t ptrs = bs / 4;
    uint32_t phys = ext2_alloc_block(fs);
    if (!phys) return -1;
    uint8_t *z = (uint8_t *)kmalloc(bs);
    if (!z) { ext2_free_block(fs, phys); return -1; }
    for (uint32_t i = 0; i < bs; i++) z[i] = 0;
    ext2_write_block(fs, phys, z);
    kfree(z);
    if (file_block < 12) {
        in->i_block[file_block] = phys;
        return (int)phys;
    }
    uint32_t fb = file_block - 12;
    if (fb < ptrs) {
        if (in->i_block[12] == 0) {
            uint32_t b = ext2_alloc_block(fs);
            if (!b) { ext2_free_block(fs, phys); return -1; }
            uint8_t *zz = (uint8_t *)kmalloc(bs);
            for (uint32_t i = 0; i < bs; i++) zz[i] = 0;
            ext2_write_block(fs, b, zz);
            kfree(zz);
            in->i_block[12] = b;
        }
        ext2_read_block(fs, in->i_block[12], fs->block_buf);
        ((uint32_t *)fs->block_buf)[fb] = phys;
        ext2_write_block(fs, in->i_block[12], fs->block_buf);
        return (int)phys;
    }
    fb -= ptrs;
    if (fb < ptrs * ptrs) {
        if (in->i_block[13] == 0) {
            uint32_t b = ext2_alloc_block(fs);
            if (!b) { ext2_free_block(fs, phys); return -1; }
            uint8_t *zz = (uint8_t *)kmalloc(bs);
            for (uint32_t i = 0; i < bs; i++) zz[i] = 0;
            ext2_write_block(fs, b, zz);
            kfree(zz);
            in->i_block[13] = b;
        }
        ext2_read_block(fs, in->i_block[13], fs->block_buf);
        uint32_t l1 = ((uint32_t *)fs->block_buf)[fb / ptrs];
        if (l1 == 0) {
            uint32_t b = ext2_alloc_block(fs);
            if (!b) { ext2_free_block(fs, phys); return -1; }
            uint8_t *zz = (uint8_t *)kmalloc(bs);
            for (uint32_t i = 0; i < bs; i++) zz[i] = 0;
            ext2_write_block(fs, b, zz);
            kfree(zz);
            l1 = b;
            ((uint32_t *)fs->block_buf)[fb / ptrs] = b;
            ext2_write_block(fs, in->i_block[13], fs->block_buf);
        }
        ext2_read_block(fs, l1, fs->block_buf);
        ((uint32_t *)fs->block_buf)[fb % ptrs] = phys;
        ext2_write_block(fs, l1, fs->block_buf);
        return (int)phys;
    }
    ext2_free_block(fs, phys);
    return -1;
}

static int ext2_dir_lookup(ext2_fs_t *fs, uint32_t dir_ino, const char *name,
                           uint32_t *out_ino, uint8_t *out_ft) {
    struct ext2_inode din;
    if (ext2_read_inode(fs, dir_ino, &din) < 0) return -1;
    if (!(din.i_mode & EXT2_S_IFDIR)) return -1;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return -1;
    uint64_t pos = 0;
    while (pos < din.i_size) {
        uint32_t fb = (uint32_t)(pos / bs);
        uint32_t pb = ext2_inode_block(fs, &din, fb);
        if (!pb) { pos += bs; continue; }
        if (ext2_read_block(fs, pb, buf) < 0) { kfree(buf); return -1; }
        uint32_t off = 0;
        while (off < bs) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                int match = 1;
                if (de->name_len != 0) {
                    uint32_t nl = de->name_len;
                    for (uint32_t i = 0; i < nl; i++) {
                        if (!name[i] || name[i] != de->name[i]) { match = 0; break; }
                    }
                    if (name[nl] != 0) match = 0;
                }
                if (match) {
                    *out_ino = de->inode;
                    if (out_ft) *out_ft = de->file_type;
                    kfree(buf);
                    return 0;
                }
            }
            off += de->rec_len;
        }
        pos += bs;
    }
    kfree(buf);
    return -1;
}

static int ext2_dir_add(ext2_fs_t *fs, uint32_t dir_ino, uint32_t ino,
                        const char *name, uint8_t ft) {
    struct ext2_inode din;
    if (ext2_read_inode(fs, dir_ino, &din) < 0) return -1;
    uint32_t bs = ext2_block_size(fs);
    uint32_t nlen = 0;
    while (name[nlen]) nlen++;
    uint32_t need = 8 + nlen;
    need = (need + 3) & ~3U;
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return -1;
    uint64_t pos = 0;
    while (pos < din.i_size) {
        uint32_t fb = (uint32_t)(pos / bs);
        uint32_t pb = ext2_inode_block(fs, &din, fb);
        if (!pb) {
            int nb = ext2_ensure_block(fs, &din, fb);
            if (nb < 0) { kfree(buf); return -1; }
            pb = (uint32_t)nb;
        }
        if (ext2_read_block(fs, pb, buf) < 0) { kfree(buf); return -1; }
        uint32_t off = 0;
        while (off < bs) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode == 0 && de->rec_len >= need) {
                de->inode = ino;
                de->name_len = (uint8_t)nlen;
                de->file_type = ft;
                for (uint32_t i = 0; i < nlen; i++) de->name[i] = name[i];
                ext2_write_block(fs, pb, buf);
                ext2_write_inode(fs, dir_ino, &din);
                kfree(buf);
                return 0;
            }
            uint32_t real = 8 + de->name_len;
            real = (real + 3) & ~3U;
            uint32_t slack = de->rec_len - real;
            if (slack >= need) {
                uint32_t old_len = de->rec_len;
                de->rec_len = real;
                struct ext2_dir_entry *nd = (struct ext2_dir_entry *)(buf + off + real);
                nd->inode = ino;
                nd->rec_len = old_len - real;
                nd->name_len = (uint8_t)nlen;
                nd->file_type = ft;
                for (uint32_t i = 0; i < nlen; i++) nd->name[i] = name[i];
                ext2_write_block(fs, pb, buf);
                ext2_write_inode(fs, dir_ino, &din);
                kfree(buf);
                return 0;
            }
            off += de->rec_len;
        }
        pos += bs;
    }
    int nb = ext2_ensure_block(fs, &din, (uint32_t)(din.i_size / bs));
    if (nb < 0) { kfree(buf); return -1; }
    for (uint32_t i = 0; i < bs; i++) buf[i] = 0;
    struct ext2_dir_entry *de = (struct ext2_dir_entry *)buf;
    de->inode = ino;
    de->rec_len = bs;
    de->name_len = (uint8_t)nlen;
    de->file_type = ft;
    for (uint32_t i = 0; i < nlen; i++) de->name[i] = name[i];
    ext2_write_block(fs, (uint32_t)nb, buf);
    din.i_size += bs;
    ext2_write_inode(fs, dir_ino, &din);
    kfree(buf);
    return 0;
}

static int ext2_dir_remove(ext2_fs_t *fs, uint32_t dir_ino, const char *name) {
    struct ext2_inode din;
    if (ext2_read_inode(fs, dir_ino, &din) < 0) return -1;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return -1;
    uint64_t pos = 0;
    while (pos < din.i_size) {
        uint32_t fb = (uint32_t)(pos / bs);
        uint32_t pb = ext2_inode_block(fs, &din, fb);
        if (!pb) { pos += bs; continue; }
        if (ext2_read_block(fs, pb, buf) < 0) { kfree(buf); return -1; }
        uint32_t off = 0;
        while (off < bs) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0) {
                uint32_t nl = de->name_len;
                int match = 1;
                for (uint32_t i = 0; i < nl; i++) {
                    if (!name[i] || name[i] != de->name[i]) { match = 0; break; }
                }
                if (name[nl] != 0) match = 0;
                if (match) {
                    de->inode = 0;
                    de->name_len = 0;
                    de->file_type = 0;
                    ext2_write_block(fs, pb, buf);
                    kfree(buf);
                    return 0;
                }
            }
            off += de->rec_len;
        }
        pos += bs;
    }
    kfree(buf);
    return -1;
}

static int ext2_dir_is_empty(ext2_fs_t *fs, uint32_t dir_ino) {
    struct ext2_inode din;
    if (ext2_read_inode(fs, dir_ino, &din) < 0) return -1;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return -1;
    uint64_t pos = 0;
    while (pos < din.i_size) {
        uint32_t fb = (uint32_t)(pos / bs);
        uint32_t pb = ext2_inode_block(fs, &din, fb);
        if (!pb) { pos += bs; continue; }
        if (ext2_read_block(fs, pb, buf) < 0) { kfree(buf); return -1; }
        uint32_t off = 0;
        while (off < bs) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                if (de->name[0] != '.' ) { kfree(buf); return 0; }
                if (de->name_len > 1) { kfree(buf); return 0; }
            }
            off += de->rec_len;
        }
        pos += bs;
    }
    kfree(buf);
    return 1;
}

static int ext2_truncate(ext2_fs_t *fs, struct ext2_inode *in, uint32_t ino,
                         uint64_t new_size) {
    uint32_t bs = ext2_block_size(fs);
    uint64_t old_blocks = (in->i_size + bs - 1) / bs;
    uint64_t new_blocks = (new_size + bs - 1) / bs;
    if (new_blocks < old_blocks) {
        for (uint64_t b = new_blocks; b < old_blocks; b++) {
            uint32_t pb = ext2_inode_block(fs, in, (uint32_t)b);
            if (pb) ext2_free_block(fs, pb);
        }
        uint32_t ptrs = bs / 4;
        if (old_blocks > 12 && new_blocks <= 12) {
            if (in->i_block[12]) { ext2_free_block(fs, in->i_block[12]); in->i_block[12] = 0; }
        }
        if (old_blocks > 12 + ptrs && new_blocks <= 12 + ptrs) {
            if (in->i_block[13]) {
                uint8_t *buf = (uint8_t *)kmalloc(bs);
                ext2_read_block(fs, in->i_block[13], buf);
                for (uint32_t i = 0; i < ptrs; i++) {
                    uint32_t l1 = ((uint32_t *)buf)[i];
                    if (l1) ext2_free_block(fs, l1);
                }
                kfree(buf);
                ext2_free_block(fs, in->i_block[13]);
                in->i_block[13] = 0;
            }
        }
    }
    in->i_size = (uint32_t)new_size;
    in->i_blocks = (uint32_t)(new_blocks * (bs / 512));
    return ext2_write_inode(fs, ino, in);
}

static int64_t ext2_file_read(vfs_node_t *node, void *buf, uint64_t offset,
                              uint64_t count) {
    ext2_file_t *f = node->private;
    ext2_fs_t *fs = f->fs;
    if (offset >= f->inode.i_size) return 0;
    if (offset + count > f->inode.i_size)
        count = f->inode.i_size - offset;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *tmp = (uint8_t *)kmalloc(bs);
    if (!tmp) return VFS_ENOMEM;
    uint8_t *out = (uint8_t *)buf;
    uint64_t done = 0;
    while (done < count) {
        uint64_t fpos = offset + done;
        uint32_t fb = (uint32_t)(fpos / bs);
        uint32_t foff = (uint32_t)(fpos % bs);
        uint32_t chunk = bs - foff;
        if (chunk > count - done) chunk = (uint32_t)(count - done);
        uint32_t pb = ext2_inode_block(fs, &f->inode, fb);
        if (!pb) {
            for (uint32_t i = 0; i < chunk; i++) out[done + i] = 0;
        } else {
            if (ext2_read_block(fs, pb, tmp) < 0) { kfree(tmp); return VFS_EIO; }
            for (uint32_t i = 0; i < chunk; i++) out[done + i] = tmp[foff + i];
        }
        done += chunk;
    }
    kfree(tmp);
    return (int64_t)done;
}

static int64_t ext2_file_write(vfs_node_t *node, const void *buf, uint64_t offset,
                               uint64_t count) {
    ext2_file_t *f = node->private;
    ext2_fs_t *fs = f->fs;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *tmp = (uint8_t *)kmalloc(bs);
    if (!tmp) return VFS_ENOMEM;
    const uint8_t *in = (const uint8_t *)buf;
    uint64_t done = 0;
    while (done < count) {
        uint64_t fpos = offset + done;
        uint32_t fb = (uint32_t)(fpos / bs);
        uint32_t foff = (uint32_t)(fpos % bs);
        uint32_t chunk = bs - foff;
        if (chunk > count - done) chunk = (uint32_t)(count - done);
        int pb = ext2_ensure_block(fs, &f->inode, fb);
        if (pb < 0) { kfree(tmp); return VFS_ENOMEM; }
        if (foff == 0 && chunk == bs) {
            for (uint32_t i = 0; i < chunk; i++) tmp[i] = in[done + i];
            if (ext2_write_block(fs, (uint32_t)pb, tmp) < 0) { kfree(tmp); return VFS_EIO; }
        } else {
            if (ext2_read_block(fs, (uint32_t)pb, tmp) < 0) { kfree(tmp); return VFS_EIO; }
            for (uint32_t i = 0; i < chunk; i++) tmp[foff + i] = in[done + i];
            if (ext2_write_block(fs, (uint32_t)pb, tmp) < 0) { kfree(tmp); return VFS_EIO; }
        }
        done += chunk;
    }
    if (offset + count > f->inode.i_size) {
        f->inode.i_size = (uint32_t)(offset + count);
        f->inode.i_blocks = (uint32_t)(((f->inode.i_size + bs - 1) / bs) * (bs / 512));
    }
    ext2_write_inode(fs, f->ino, &f->inode);
    node->length = f->inode.i_size;
    kfree(tmp);
    return (int64_t)done;
}

static vfs_node_t *ext2_lookup(vfs_node_t *dir, const char *name) {
    ext2_file_t *d = dir->private;
    ext2_fs_t *fs = d->fs;
    uint32_t ino;
    uint8_t ft;
    if (ext2_dir_lookup(fs, d->ino, name, &ino, &ft) < 0) return 0;
    struct ext2_inode ei;
    if (ext2_read_inode(fs, ino, &ei) < 0) return 0;
    vfs_node_t *n = vfs_node_create(name, 0);
    if (!n) return 0;
    ext2_file_t *f = (ext2_file_t *)kmalloc(sizeof(ext2_file_t));
    if (!f) { vfs_node_destroy(n); return 0; }
    f->fs = fs;
    f->ino = ino;
    f->inode = ei;
    f->dirty = 0;
    n->private = f;
    n->length = ei.i_size;
    if (ei.i_mode & EXT2_S_IFDIR) n->flags = VFS_DIR;
    else if (ei.i_mode & EXT2_S_IFREG) n->flags = VFS_FILE;
    else n->flags = VFS_FILE;
    n->mask = ei.i_mode & 0xFFF;
    n->ops = dir->ops;
    return n;
}

static int ext2_create(vfs_node_t *dir, const char *name, uint32_t type) {
    ext2_file_t *d = dir->private;
    ext2_fs_t *fs = d->fs;
    uint32_t existing;
    if (ext2_dir_lookup(fs, d->ino, name, &existing, 0) == 0)
        return VFS_EEXIST;
    uint32_t ino = ext2_alloc_inode(fs);
    if (!ino) return VFS_ENOMEM;
    struct ext2_inode ni;
    uint8_t *p = (uint8_t *)&ni;
    for (uint32_t i = 0; i < sizeof(ni); i++) p[i] = 0;
    if (type & VFS_DIR) {
        ni.i_mode = EXT2_S_IFDIR | 0755;
        uint32_t bs = ext2_block_size(fs);
        int pb = ext2_alloc_block(fs);
        if (!pb) { ext2_free_inode(fs, ino); return VFS_ENOMEM; }
        uint8_t *buf = (uint8_t *)kmalloc(bs);
        for (uint32_t i = 0; i < bs; i++) buf[i] = 0;
        struct ext2_dir_entry *de = (struct ext2_dir_entry *)buf;
        de->inode = ino;
        de->name_len = 1;
        de->file_type = EXT2_FT_DIR;
        de->name[0] = '.';
        uint32_t rl = 12;
        struct ext2_dir_entry *de2 = (struct ext2_dir_entry *)(buf + rl);
        de2->inode = d->ino;
        de2->rec_len = bs - rl;
        de2->name_len = 2;
        de2->file_type = EXT2_FT_DIR;
        de2->name[0] = '.';
        de2->name[1] = '.';
        ext2_write_block(fs, (uint32_t)pb, buf);
        kfree(buf);
        ni.i_block[0] = (uint32_t)pb;
        ni.i_size = bs;
        ni.i_blocks = bs / 512;
        ni.i_links_count = 2;
    } else {
        ni.i_mode = EXT2_S_IFREG | 0644;
        ni.i_size = 0;
        ni.i_blocks = 0;
        ni.i_links_count = 1;
    }
    ext2_write_inode(fs, ino, &ni);
    uint8_t ft = (type & VFS_DIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (ext2_dir_add(fs, d->ino, ino, name, ft) < 0) {
        ext2_free_inode(fs, ino);
        return VFS_EIO;
    }
    if (type & VFS_DIR) {
        struct ext2_inode pin;
        if (ext2_read_inode(fs, d->ino, &pin) == 0) {
            pin.i_links_count++;
            ext2_write_inode(fs, d->ino, &pin);
        }
    }
    return VFS_OK;
}

static vfs_node_t *ext2_readdir(vfs_node_t *dir, uint64_t index) {
    ext2_file_t *d = dir->private;
    ext2_fs_t *fs = d->fs;
    struct ext2_inode din = d->inode;
    uint32_t bs = ext2_block_size(fs);
    uint8_t *buf = (uint8_t *)kmalloc(bs);
    if (!buf) return 0;
    uint64_t pos = 0;
    uint64_t count = 0;
    while (pos < din.i_size) {
        uint32_t fb = (uint32_t)(pos / bs);
        uint32_t pb = ext2_inode_block(fs, &din, fb);
        if (!pb) { pos += bs; continue; }
        if (ext2_read_block(fs, pb, buf) < 0) { kfree(buf); return 0; }
        uint32_t off = 0;
        while (off < bs) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                if (count == index) {
                    char name[64];
                    uint32_t nl = de->name_len;
                    if (nl > 63) nl = 63;
                    for (uint32_t i = 0; i < nl; i++) name[i] = de->name[i];
                    name[nl] = 0;
                    vfs_node_t *n = vfs_node_create(name, 0);
                    if (!n) { kfree(buf); return 0; }
                    struct ext2_inode ei;
                    if (ext2_read_inode(fs, de->inode, &ei) == 0) {
                        ext2_file_t *f = (ext2_file_t *)kmalloc(sizeof(ext2_file_t));
                        if (f) {
                            f->fs = fs;
                            f->ino = de->inode;
                            f->inode = ei;
                            f->dirty = 0;
                            n->private = f;
                            n->length = ei.i_size;
                            if (ei.i_mode & EXT2_S_IFDIR) n->flags = VFS_DIR;
                            else n->flags = VFS_FILE;
                            n->mask = ei.i_mode & 0xFFF;
                        }
                    }
                    n->ops = dir->ops;
                    kfree(buf);
                    return n;
                }
                count++;
            }
            off += de->rec_len;
        }
        pos += bs;
    }
    kfree(buf);
    return 0;
}

static uint64_t ext2_size(vfs_node_t *node) {
    ext2_file_t *f = node->private;
    return f->inode.i_size;
}


static int ext2_mount(blockdev_t *dev, vfs_node_t **out_root) {
    if (!dev || dev->sector_size != 512) return VFS_EINVAL;
    ext2_fs_t *fs = (ext2_fs_t *)kzalloc(sizeof(ext2_fs_t));
    if (!fs) return VFS_ENOMEM;
    fs->dev = dev;
    uint8_t *sbuf = (uint8_t *)kmalloc(1024);
    if (!sbuf) { kfree(fs); return VFS_ENOMEM; }
    if (blockdev_read(dev, 2, 2, sbuf) < 0) { kfree(sbuf); kfree(fs); return VFS_EIO; }
    for (uint32_t i = 0; i < sizeof(struct ext2_super_block); i++)
        ((uint8_t *)&fs->sb)[i] = sbuf[i];
    kfree(sbuf);
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) { kfree(fs); return VFS_EINVAL; }
    if (fs->sb.s_rev_level == 0) fs->inode_size = 128;
    else fs->inode_size = fs->sb.s_inode_size;
    if (fs->inode_size < 128 || fs->inode_size > 1024) { kfree(fs); return VFS_EINVAL; }
    if (fs->sb.s_log_block_size > 2) { kfree(fs); return VFS_EINVAL; }
    fs->block_size = ext2_block_size(fs);
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->first_data_block = fs->sb.s_first_data_block;
    fs->group_count = (fs->sb.s_blocks_count - fs->sb.s_first_data_block
                     + fs->blocks_per_group - 1) / fs->blocks_per_group;
    if (fs->sb.s_log_block_size == 0) {
        fs->groups_block = 2;
    } else {
        fs->groups_block = 1;
    }
    fs->block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!fs->block_buf) { kfree(fs); return VFS_ENOMEM; }
    fs->gd_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!fs->gd_buf) { kfree(fs->block_buf); kfree(fs); return VFS_ENOMEM; }
    vfs_node_t *root = vfs_node_create("/", VFS_DIR);
    if (!root) { kfree(fs->gd_buf); kfree(fs->block_buf); kfree(fs); return VFS_ENOMEM; }
    ext2_file_t *rf = (ext2_file_t *)kmalloc(sizeof(ext2_file_t));
    if (!rf) { vfs_node_destroy(root); kfree(fs->gd_buf); kfree(fs->block_buf); kfree(fs); return VFS_ENOMEM; }
    rf->fs = fs;
    rf->ino = 2;
    rf->dirty = 0;
    if (ext2_read_inode(fs, 2, &rf->inode) < 0) {
        kfree(rf); vfs_node_destroy(root); kfree(fs->gd_buf); kfree(fs->block_buf); kfree(fs);
        return VFS_EIO;
    }
    root->private = rf;
    root->length = rf->inode.i_size;
    root->mask = rf->inode.i_mode & 0xFFF;
    root->ops = &ext2_node_ops;
    *out_root = root;
    return VFS_OK;
}

static int ext2_unmount(vfs_node_t *root) {
    if (!root || !root->private) return VFS_EINVAL;
    ext2_file_t *f = root->private;
    ext2_fs_t *fs = f->fs;
    kfree(fs->gd_buf);
    kfree(fs->block_buf);
    kfree(fs);
    kfree(f);
    vfs_node_destroy(root);
    return VFS_OK;
}
static int ext2_remove(vfs_node_t *dir, const char *name) {
    ext2_file_t *d = dir->private;
    ext2_fs_t *fs = d->fs;

    uint32_t ino; uint8_t ft;
    if (ext2_dir_lookup(fs, d->ino, name, &ino, &ft) < 0) return VFS_ENOENT;

    struct ext2_inode ei;
    if (ext2_read_inode(fs, ino, &ei) < 0) return VFS_EIO;

    if (ei.i_mode & EXT2_S_IFDIR) {
        if (!ext2_dir_is_empty(fs, ino)) return VFS_EBUSY;
        ext2_truncate(fs, &ei, ino, 0);
        ext2_free_inode(fs, ino);
        struct ext2_inode pin;
        if (ext2_read_inode(fs, d->ino, &pin) == 0) {
            if (pin.i_links_count) pin.i_links_count--;
            ext2_write_inode(fs, d->ino, &pin);
        }
    } else {
        if (ei.i_links_count > 0) ei.i_links_count--;
        if (ei.i_links_count == 0) {
            ext2_truncate(fs, &ei, ino, 0);
            ext2_free_inode(fs, ino);
        } else {
            ext2_write_inode(fs, ino, &ei);
        }
    }

    return ext2_dir_remove(fs, d->ino, name) < 0 ? VFS_EIO : VFS_OK;
}
static const vfs_fs_t ext2_fs = {
    .name    = "ext2",
    .mount   = ext2_mount,
    .unmount = ext2_unmount,
};

static const vfs_node_ops_t ext2_node_ops = {
    .read    = ext2_file_read,
    .write   = ext2_file_write,
    .lookup  = ext2_lookup,
    .create  = ext2_create,
    .remove  = ext2_remove,
    .readdir = ext2_readdir,
    .size    = ext2_size,
};

int ext2_register(void) {
    return vfs_register_fs(&ext2_fs);
}
