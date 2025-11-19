#include "superblock.h"
#include "block.h"
#include "fs_utils.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

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
) {
    unsigned char *buf = (unsigned char*)calloc(1, block_size);
    if (!buf) { errno = ENOMEM; return -1; }

    buf[0]='Q'; buf[1]='R'; buf[2]='F'; buf[3]='S';
    u32le_write(1,              &buf[4]);   // version
    u32le_write(block_size,     &buf[8]);
    u32le_write(total_blocks,   &buf[12]);
    u32le_write(total_inodes,   &buf[16]);

    memcpy(&buf[20],  inode_bitmap_128, 128);
    memcpy(&buf[148], data_bitmap_128,  128);

    u32le_write(root_inode, &buf[276]);
    u32le_write(inode_bitmap_start,  &buf[280]);
    u32le_write(inode_bitmap_blocks, &buf[284]);
    u32le_write(data_bitmap_start,   &buf[288]);
    u32le_write(data_bitmap_blocks,  &buf[292]);
    u32le_write(inode_table_start,   &buf[296]);
    u32le_write(inode_table_blocks,  &buf[300]);
    u32le_write(data_region_start,   &buf[304]);

    int rc = write_block(folder, 0, buf, block_size);
    free(buf);
    return rc;
}