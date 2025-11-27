#include "fs_basic.h"
#include "superblock.h"
#include "fs_utils.h"


#define FUSE_USE_VERSION 31

int allocate_inode(void) {
    for (int i = 0; i < (int)spblock.total_inodes; i++) {
        if (spblock.inode_bitmap[i] == '0') {
            spblock.inode_bitmap[i] = '1';
            return i;
        }
    }
    return -1;
}

void free_inode(int inode_id) {
    if (inode_id >= 0 && inode_id < (int)spblock.total_inodes) {
        spblock.inode_bitmap[inode_id] = '0';
    }
}

int allocate_block(void) {
    for (int i = 0; i < (int)spblock.total_blocks; i++) {
        if (spblock.data_bitmap[i] == '0') {
            spblock.data_bitmap[i] = '1';
            return i;
        }
    }
    return -1;
}

void free_block(int block_num) {
    if (block_num >= 0 && block_num < (int)spblock.total_blocks) {
        spblock.data_bitmap[block_num] = '0';
    }
}



int update_bitmaps(const char *folder) {
    u32 inode_bitmap_start  = 1;
    u32 inode_bitmap_blocks = 1;

    u32 data_bitmap_start   = inode_bitmap_start + inode_bitmap_blocks;
    u32 data_bitmap_blocks  = 1;

    u32 inode_table_start   = data_bitmap_start + data_bitmap_blocks;
    u32 inode_record_size   = 128;
    u32 inode_table_bytes   = spblock.total_inodes * inode_record_size;
    u32 inode_table_blocks  = ceil_div(inode_table_bytes, spblock.blocksize);

    u32 data_region_start   = inode_table_start + inode_table_blocks;

    return write_superblock_with_offsets(
        folder,
        spblock.blocksize,
        spblock.total_blocks,
        spblock.total_inodes,
        (unsigned char *)spblock.inode_bitmap,
        (unsigned char *)spblock.data_bitmap,
        spblock.root_inode,
        inode_bitmap_start, inode_bitmap_blocks,
        data_bitmap_start,  data_bitmap_blocks,
        inode_table_start,  inode_table_blocks,
        data_region_start
    );
}


