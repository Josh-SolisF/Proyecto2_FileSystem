#ifndef INODE_H
#define INODE_H
#include "fs_basic.h"

void init_inode(inode *node, u32 inode_id, mode_t mode, u32 size);
static void inode_serialize128(unsigned char out[128],u32 inode_number, u32 inode_mode, u32 user_id, u32 group_id,
    u32 links, u32 size,const u32 direct[12], u32 indirect1);
static void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode);
#endif