#ifndef SUPERBLOCK_H
#define SUPERBLOCK_H
#include "fs_basic.h"

int write_superblock_with_offsets(
    const char *folder,
    u32 block_size,
    u32 total_blocks,
    u32 total_inodes,
    const unsigned char *inode_bitmap_128,
    const unsigned char *data_bitmap_128,
    u32 root_inode,
    u32 inode_bitmap_start, u32 inode_bitmap_blocks,
    u32 data_bitmap_start,  u32 data_bitmap_blocks,
    u32 inode_table_start,  u32 inode_table_blocks,
    u32 data_region_start
);
int read_superblock(const char *folder, u32 block_size,u32 *version, u32 *total_blocks, u32 *total_inodes,
                    unsigned char inode_bitmap[128],unsigned char data_bitmap[128],u32 *root_inode,
                    u32 *inode_bitmap_start, u32 *inode_bitmap_blocks,u32 *data_bitmap_start,  u32 *data_bitmap_blocks,
                    u32 *inode_table_start,  u32 *inode_table_blocks,u32 *data_region_start);
#endif
