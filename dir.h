#ifndef DIR_H
#define DIR_H
#include "fs_basic.h"
#include "fuse_functions.h"

#define QRFS_DIR_NAME_MAX      256
#define QRFS_DIR_ENTRY_SIZE    (4 + QRFS_DIR_NAME_MAX)

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name);
void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode);
void list_directory_block(const char *folder, u32 block_size, u32 dir_block_index);

int add_dir_entry_to_block(unsigned char *blk, u32 block_size, u32 inode_id, const char *name);
int write_directory_block(const char *folder, u32 block_index, const unsigned char *block, u32 block_size);
int search_inode_by_path(qrfs_ctx *ctx, const char *path, u32 *inode_id_out) ;

int find_parent_dir_block(qrfs_ctx *ctx,
                          const char *parent_path,
                          u32 *parent_block_out);
;
void direntry_write(unsigned char *blk, uint32_t offset, uint32_t inode, const char *name);
void direntry_read(const unsigned char *blk, uint32_t offset, uint32_t *inode_out, char *name_out) ;


#endif