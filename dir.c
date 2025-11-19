
#include "fs_basic.h"
#include <string.h>

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name) {
    entry->inode_id = inode_id;
    strncpy(entry->name, name, sizeof(entry->name));
    entry->name[sizeof(entry->name)-1] = '\0';
}