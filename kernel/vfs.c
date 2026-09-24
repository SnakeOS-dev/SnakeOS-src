#include "vfs.h"
#include "heap.h"

#define VFS_MAX_FS 8

static const vfs_fs_t *fs_registry[VFS_MAX_FS];
static int fs_count = 0;
static vfs_node_t *vfs_root_node = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

vfs_node_t *vfs_node_create(const char *name, uint32_t flags) {
    vfs_node_t *n = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!n) return 0;
    int i = 0;
    while (name && name[i] && i < 63) { n->name[i] = name[i]; i++; }
    n->name[i] = 0;
    n->flags = flags;
    n->refcount = 1;
    return n;
}

void vfs_node_destroy(vfs_node_t *n) {
    if (n) kfree(n);
}

vfs_node_t *vfs_root(void) { return vfs_root_node; }

                                                                       

static vfs_node_t *gendir_lookup(vfs_node_t *dir, const char *name) {
    for (vfs_node_t *c = dir->children; c; c = c->next_sib)
        if (str_eq(c->name, name)) return c;
    return 0;
}

static vfs_node_t *gendir_readdir(vfs_node_t *dir, uint64_t index) {
    uint64_t i = 0;
    for (vfs_node_t *c = dir->children; c; c = c->next_sib, i++)
        if (i == index) return c;
    return 0;
}

static int gendir_create(vfs_node_t *dir, const char *name, uint32_t type) {
    if (gendir_lookup(dir, name)) return VFS_EEXIST;
    uint32_t flags = (type & VFS_DIR) ? VFS_DIR : VFS_FILE;
    vfs_node_t *n = vfs_node_create(name, flags);
    if (!n) return VFS_ENOMEM;
    if (flags & VFS_DIR) n->ops = dir->ops;                   
    n->parent   = dir;
    n->next_sib = dir->children;
    dir->children = n;
    return VFS_OK;
}

static const vfs_node_ops_t gendir_ops = {
    .lookup  = gendir_lookup,
    .create  = gendir_create,
    .readdir = gendir_readdir,
};

                                                          

int vfs_init(void) {
    fs_count = 0;
    vfs_root_node = vfs_node_create("/", VFS_DIR);
    if (!vfs_root_node) return VFS_ENOMEM;
    vfs_root_node->ops = &gendir_ops;
    return VFS_OK;
}

int vfs_register_fs(const vfs_fs_t *fs) {
    if (!fs || !fs->name || !fs->mount) return VFS_EINVAL;
    if (fs_count >= VFS_MAX_FS) return VFS_ENOMEM;
    for (int i = 0; i < fs_count; i++)
        if (str_eq(fs_registry[i]->name, fs->name)) return VFS_EEXIST;
    fs_registry[fs_count++] = fs;
    return VFS_OK;
}

static const vfs_fs_t *vfs_find_fs(const char *name) {
    for (int i = 0; i < fs_count; i++)
        if (str_eq(fs_registry[i]->name, name)) return fs_registry[i];
    return 0;
}

                                    

static vfs_node_t *node_effective(vfs_node_t *n) {
    return (n && (n->flags & VFS_MOUNT) && n->mount) ? n->mount : n;
}

vfs_node_t *vfs_lookup(const char *path) {
    if (!path || path[0] != '/' || !vfs_root_node) return 0;
    if (path[1] == 0) return vfs_root_node;

    vfs_node_t *cur = vfs_root_node;
    const char *p = path + 1;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) return cur;

        char token[64];
        int len = 0;
        while (p[len] && p[len] != '/' && len < 63) { token[len] = p[len]; len++; }
        token[len] = 0;

        vfs_node_t *dir  = node_effective(cur);
        vfs_node_t *next = (dir->ops && dir->ops->lookup)
                         ? dir->ops->lookup(dir, token) : 0;
        if (!next) return 0;
        cur = next;
        p += len;
    }
    return cur;
}

vfs_node_t *vfs_mkdir_p(const char *path) {
    if (!path || path[0] != '/' || !vfs_root_node) return 0;
    if (path[1] == 0) return vfs_root_node;

    vfs_node_t *cur = vfs_root_node;
    const char *p = path + 1;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) return cur;

        char token[64];
        int len = 0;
        while (p[len] && p[len] != '/' && len < 63) { token[len] = p[len]; len++; }
        token[len] = 0;

        vfs_node_t *dir  = node_effective(cur);
        vfs_node_t *next = (dir->ops && dir->ops->lookup)
                         ? dir->ops->lookup(dir, token) : 0;

        if (!next) {
            if (!(dir->flags & VFS_DIR)) return 0;
            if (!dir->ops || !dir->ops->create) return 0;
            if (dir->ops->create(dir, token, VFS_DIR) != VFS_OK) return 0;
            next = dir->ops->lookup(dir, token);
            if (!next) return 0;
        } else if (!(next->flags & VFS_DIR)) {
            return 0;
        }
        cur = next;
        p += len;
    }
    return cur;
}

                                           

int vfs_mount(const char *dev_name, const char *path, const char *fstype) {
    if (!path || path[0] != '/') return VFS_EINVAL;

    blockdev_t *dev = blockdev_find(dev_name);
    if (!dev) return VFS_ENODEV;

    const vfs_fs_t *fs = vfs_find_fs(fstype);
    if (!fs) return VFS_ENOSYS;                                         

    vfs_node_t *mp = vfs_lookup(path);
    if (!mp) {
        mp = vfs_mkdir_p(path);
        if (!mp) return VFS_ENOENT;
    }
    if (!(mp->flags & VFS_DIR)) return VFS_ENOTDIR;
    if (mp->mount) return VFS_EBUSY;

    vfs_node_t *root = 0;
    if (fs->mount(dev, &root) != VFS_OK || !root) return VFS_EIO;

    root->fs = fs;
    mp->mount = root;
    mp->flags |= VFS_MOUNT;
    return VFS_OK;
}

int vfs_unmount(const char *path) {
    vfs_node_t *mp = vfs_lookup(path);
    if (!mp) return VFS_ENOENT;
    if (!(mp->flags & VFS_MOUNT) || !mp->mount) return VFS_EINVAL;

    vfs_node_t *root = mp->mount;
    if (root->fs && root->fs->unmount && root->fs->unmount(root) != VFS_OK)
        return VFS_EIO;

    mp->mount = 0;
    mp->flags &= ~VFS_MOUNT;
    return VFS_OK;
}

                                            

int vfs_open(vfs_node_t *node, int flags) {
    if (!node) return VFS_EINVAL;
    node = node_effective(node);
    if (node->ops && node->ops->open) return node->ops->open(node, flags);
    node->refcount++;
    return VFS_OK;
}

int vfs_close(vfs_node_t *node) {
    if (!node) return VFS_EINVAL;
    node = node_effective(node);
    if (node->ops && node->ops->close) return node->ops->close(node);
    if (node->refcount > 0) node->refcount--;
    return VFS_OK;
}

int64_t vfs_read(vfs_node_t *node, void *buf, uint64_t offset, uint64_t count) {
    if (!node) return VFS_EINVAL;
    node = node_effective(node);
    if (!node->ops || !node->ops->read) return VFS_ENOSYS;
    return node->ops->read(node, buf, offset, count);
}

int64_t vfs_write(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t count) {
    if (!node) return VFS_EINVAL;
    node = node_effective(node);
    if (!node->ops || !node->ops->write) return VFS_ENOSYS;
    return node->ops->write(node, buf, offset, count);
}

vfs_node_t *vfs_readdir(vfs_node_t *dir, uint64_t index) {
    if (!dir) return 0;
    dir = node_effective(dir);
    if (!dir->ops || !dir->ops->readdir) return 0;
    return dir->ops->readdir(dir, index);
}

uint64_t vfs_size(vfs_node_t *node) {
    if (!node) return 0;
    node = node_effective(node);
    if (node->ops && node->ops->size) return node->ops->size(node);
    return node->length;
}

                                                                  

static int64_t blkdev_node_read(vfs_node_t *node, void *buf,
                                uint64_t offset, uint64_t count) {
    blockdev_t *dev = node->dev;
    if (!dev) return VFS_ENODEV;
    if (offset % dev->sector_size || count % dev->sector_size)
        return VFS_EINVAL;                                       
    if (blockdev_read(dev, offset / dev->sector_size,
                      (uint32_t)(count / dev->sector_size), buf) < 0)
        return VFS_EIO;
    return (int64_t)count;
}

static int64_t blkdev_node_write(vfs_node_t *node, const void *buf,
                                 uint64_t offset, uint64_t count) {
    blockdev_t *dev = node->dev;
    if (!dev) return VFS_ENODEV;
    if (offset % dev->sector_size || count % dev->sector_size)
        return VFS_EINVAL;
    if (blockdev_write(dev, offset / dev->sector_size,
                       (uint32_t)(count / dev->sector_size), buf) < 0)
        return VFS_EIO;
    return (int64_t)count;
}

static const vfs_node_ops_t blkdev_node_ops = {
    .read  = blkdev_node_read,
    .write = blkdev_node_write,
};

void vfs_dev_populate(void) {
    if (!vfs_root_node) return;
    vfs_node_t *devdir = vfs_mkdir_p("/dev");
    if (!devdir) return;

    for (blockdev_t *d = blockdev_first(); d; d = blockdev_next(d)) {
        if (devdir->ops && devdir->ops->lookup
            && devdir->ops->lookup(devdir, d->name))
            continue;
        vfs_node_t *n = vfs_node_create(d->name, VFS_FILE);
        if (!n) continue;
        n->dev    = d;
        n->ops    = &blkdev_node_ops;
        n->length = d->sector_count * d->sector_size;
        n->parent   = devdir;
        n->next_sib = devdir->children;
        devdir->children = n;
    }
}
int vfs_remove(vfs_node_t *node) {
    if (!node || !node->parent) return VFS_EINVAL;
    vfs_node_t *dir = node_effective(node->parent);
    if (!dir->ops || !dir->ops->remove) return VFS_ENOSYS;
    return dir->ops->remove(dir, node->name);
}
