#include "ffs.h"
#include "../mm/mm.h"
#include "string.h"
#include <stddef.h>

typedef struct {
    block_dev_t *dev;
    ffs_superblock_t sb;
} ffs_info_t;

typedef struct {
    ffs_info_t *ffs;
    ffs_inode_t inode;
    uint32_t inode_num;
    uint64_t offset;
} ffs_file_t;

static int ffs_read_block(ffs_info_t *ffs, uint32_t block, void *buf) {
    return ffs->dev->read(ffs->dev, (uint64_t)block * (FFS_BLOCK_SIZE / ffs->dev->sector_size), 
                          FFS_BLOCK_SIZE / ffs->dev->sector_size, buf);
}

static int ffs_read_inode(ffs_info_t *ffs, uint32_t ino, ffs_inode_t *out) {
    uint32_t block = ffs->sb.inode_table_start + (ino * FFS_INODE_SIZE) / FFS_BLOCK_SIZE;
    uint32_t offset = (ino * FFS_INODE_SIZE) % FFS_BLOCK_SIZE;
    
    uint8_t buf[FFS_BLOCK_SIZE];
    if (ffs_read_block(ffs, block, buf) != 0) return -1;
    
    memcpy(out, buf + offset, FFS_INODE_SIZE);
    return 0;
}

/* VFS Operations Implementation */

static vfs_bctx_t ffs_open_impl(vfs_backend_t *be, const char *path, int flags) {
    (void)flags;
    ffs_info_t *ffs = (ffs_info_t *)be->ctx;
    // Simple root-only implementation for now as a placeholder
    if (strcmp(path, "/") != 0 && strcmp(path, "") != 0) return NULL;

    ffs_file_t *f = kmalloc(sizeof(ffs_file_t));
    if (!f) return NULL;

    f->ffs = ffs;
    f->inode_num = 2; // Root inode is usually 2
    if (ffs_read_inode(ffs, f->inode_num, &f->inode) != 0) {
        kfree(f);
        return NULL;
    }
    f->offset = 0;
    return (vfs_bctx_t)f;
}

static void ffs_close(vfs_backend_t *be, vfs_bctx_t bctx) {
    (void)be;
    if (bctx) kfree(bctx);
}

static int ffs_read(vfs_backend_t *be, vfs_bctx_t bctx, void *buf, size_t n, size_t *got) {
    (void)be;
    ffs_file_t *f = (ffs_file_t *)bctx;
    if (f->offset >= f->inode.size) {
        if (got) *got = 0;
        return 0;
    }

    size_t remain = f->inode.size - f->offset;
    size_t to_read = (n < remain) ? n : (size_t)remain;
    
    // Simplified: only direct blocks for now
    uint32_t b_idx = f->offset / FFS_BLOCK_SIZE;
    uint32_t b_off = f->offset % FFS_BLOCK_SIZE;
    
    if (b_idx >= FFS_DIRECT_BLOCKS) return VFS_ERR;

    uint8_t kbuf[FFS_BLOCK_SIZE];
    if (ffs_read_block(f->ffs, f->inode.blocks[b_idx], kbuf) != 0) return VFS_ERR;
    
    size_t chunk = FFS_BLOCK_SIZE - b_off;
    if (chunk > to_read) chunk = to_read;
    
    memcpy(buf, kbuf + b_off, chunk);
    f->offset += chunk;
    if (got) *got = chunk;
    
    return 0;
}

static int ffs_stat(vfs_backend_t *be, const char *path, stat_info_t *out) {
    (void)be; (void)path;
    // Placeholder for stat
    memset(out, 0, sizeof(stat_info_t));
    return 0;
}

/* More operations would be implemented here... */

static vfs_ops_t ffs_ops = {
    .open = ffs_open_impl,
    .close = ffs_close,
    .read = ffs_read,
    .stat = ffs_stat,
    /* ... other ops ... */
};

void ffs_init(block_dev_t *dev) {
    uint8_t buf[FFS_BLOCK_SIZE];
    // Read superblock (block 1)
    if (dev->read(dev, FFS_BLOCK_SIZE / dev->sector_size, FFS_BLOCK_SIZE / dev->sector_size, buf) != 0) return;
    
    ffs_superblock_t *sb = (ffs_superblock_t *)buf;
    if (sb->magic != FFS_MAGIC) return;

    ffs_info_t *ffs = kmalloc(sizeof(ffs_info_t));
    ffs->dev = dev;
    memcpy(&ffs->sb, sb, sizeof(ffs_superblock_t));

    vfs_mount("/root", ffs_ops, ffs);
}
