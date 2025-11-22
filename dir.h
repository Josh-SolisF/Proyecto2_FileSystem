#ifndef DIR_H
#define DIR_H
#include "fs_basic.h"

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name);
void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode);
void list_directory_block(const char *folder, u32 block_size, u32 dir_block_index);

int add_dir_entry_to_block(unsigned char *block, u32 block_size, const dir_entry *entry);
int write_directory_block(const char *folder, u32 block_index, const unsigned char *block, u32 block_size);

int find_parent_dir_block(const char *path, u32 *block_index);

#endif