
#define FUSE_USE_VERSION 31

#include "fs_basic.h"
#include <string.h>
#include <unistd.h>
#include <time.h>

const u32 block_size = 1024;
const u32 DEFAULT_TOTAL_BLOCKS = 100;
const u32 DEFAULT_TOTAL_INODES = 10;

superblock spblock;

void initialize_superblock(void) {
    spblock.version = 1;
    spblock.blocksize = block_size;
    spblock.total_blocks = DEFAULT_TOTAL_BLOCKS;
    spblock.total_inodes = DEFAULT_TOTAL_INODES;
    memset(spblock.data_bitmap,  '0', sizeof(spblock.data_bitmap));
    memset(spblock.inode_bitmap, '0', sizeof(spblock.inode_bitmap));
    spblock.root_inode = 0;
}



void u32le_write(u32 v, unsigned char *p) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

u32 u32le_read(const unsigned char *p) {
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}


u32 ceil_div(u32 a, u32 b) {return (a + b - 1) / b;}