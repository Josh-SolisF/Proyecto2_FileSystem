// block_io.h
#ifndef BLOCK_H
#define BLOCK_H
#include "fs_basic.h"

int ensure_folder(const char *folder);
int create_zero_block(const char *folder, u32 index, u32 block_size);
int write_block(const char *folder, u32 index, const void *buf, u32 len);
int read_block(const char *folder, u32 block_index, unsigned char *buf, u32 block_size);

int read_inode_block(qrfs_ctx *ctx, u32 inode_id, unsigned char out128[128]);

#endif
