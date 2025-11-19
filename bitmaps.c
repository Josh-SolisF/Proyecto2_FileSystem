// bitmaps.c
#include "fs_basic.h"

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