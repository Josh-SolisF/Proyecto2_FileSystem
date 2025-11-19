
#include "fs_basic.h"
#include "fs_utils.h"

#include <string.h>

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name) {
    entry->inode_id = inode_id;
    strncpy(entry->name, name, sizeof(entry->name));
    entry->name[sizeof(entry->name)-1] = '\0';
}

void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode) {
    memset(block, 0, block_size);
    u32le_write(root_inode, &block[0]);
    strncpy((char*)&block[4], ".", 256);
    u32le_write(root_inode, &block[264]);
    strncpy((char*)&block[268], "..", 256);
}