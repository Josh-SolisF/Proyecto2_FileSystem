#ifndef DIR_H
#define DIR_H
#include "fs_basic.h"

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name);
void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode);

#endif