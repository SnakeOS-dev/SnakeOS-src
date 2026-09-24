#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "blkdev.h"

struct vfs_fs;

typedef struct vfs_node vfs_node_t;

typedef struct vfs_node_ops {
    int     (*open)(vfs_node_t *node, int flags);
    int     (*close)(vfs_node_t *node);
    int64_t (*read)(vfs_node_t *node, void *buf, uint64_t offset, uint64_t count);
    int64_t (*write)(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t count);
    vfs_node_t *(*lookup)(vfs_node_t *dir, const char *name);
    int     (*create)(vfs_node_t *dir, const char *name, uint32_t type);
    vfs_node_t *(*readdir)(vfs_node_t *dir, uint64_t index);
    uint64_t (*size)(vfs_node_t *node);
    int     (*remove)(vfs_node_t *dir, const char *name);
} vfs_node_ops_t;

struct vfs_node {
    char     name[64];
    uint32_t flags;
    uint32_t mask;
    uint64_t length;
    blockdev_t *dev;
    const vfs_node_ops_t *ops;
    const struct vfs_fs  *fs;                                           
    vfs_node_t *mount;                                                 
    vfs_node_t *parent;
    vfs_node_t *children;
    vfs_node_t *next_sib;
    void *private;
    uint32_t refcount;
};

typedef struct vfs_fs {
    const char *name;
    int (*mount)(blockdev_t *dev, vfs_node_t **out_root);
    int (*unmount)(vfs_node_t *root);
} vfs_fs_t;

#define VFS_FILE     0x01
#define VFS_DIR      0x02
#define VFS_MOUNT    0x04
#define VFS_CHARDEV  0x08

                                    
#define VFS_OK       0
#define VFS_EINVAL  -1
#define VFS_ENOENT  -2
#define VFS_ENODEV  -3
#define VFS_ENOMEM  -4
#define VFS_ENOTDIR -5
#define VFS_EBUSY   -6
#define VFS_ENOSYS  -7
#define VFS_EIO     -8
#define VFS_EEXIST  -9
#define VFS_EACCESS -10

int vfs_init(void);
int vfs_register_fs(const vfs_fs_t *fs);

vfs_node_t *vfs_node_create(const char *name, uint32_t flags);
void        vfs_node_destroy(vfs_node_t *node);

vfs_node_t *vfs_root(void);
vfs_node_t *vfs_lookup(const char *path);
vfs_node_t *vfs_mkdir_p(const char *path);

int vfs_mount(const char *dev_name, const char *path, const char *fstype);
int vfs_unmount(const char *path);
int     vfs_open(vfs_node_t *node, int flags);
int     vfs_close(vfs_node_t *node);
int64_t vfs_read(vfs_node_t *node, void *buf, uint64_t offset, uint64_t count);
int64_t vfs_write(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t count);
vfs_node_t *vfs_readdir(vfs_node_t *dir, uint64_t index);
uint64_t    vfs_size(vfs_node_t *node);
int vfs_remove(vfs_node_t *node);
void vfs_dev_populate(void);
#endif
