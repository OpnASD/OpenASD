/*
 * ASD Fast File System (FFS) - Inspired by OpenBSD FFS/UFS
 */

#ifndef ASD_FFS_H
#define ASD_FFS_H

#include "vfs.h"
#include "../block/block.h"

#define FFS_MAGIC 0x19960317
#define FFS_BLOCK_SIZE 4096
#define FFS_INODE_SIZE 128
#define FFS_DIRECT_BLOCKS 12

typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_table_start;
    uint32_t data_start;
} ffs_superblock_t;

typedef struct {
    uint16_t mode;
    uint16_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t blocks[FFS_DIRECT_BLOCKS];
    uint32_t indirect_block;
    uint32_t double_indirect_block;
} ffs_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} ffs_dirent_t;

/* Backend registration */
void ffs_init(block_dev_t *dev);

#endif
