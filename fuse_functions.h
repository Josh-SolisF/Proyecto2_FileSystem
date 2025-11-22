
#ifndef FUSE_FUNCTIONS_H
#define FUSE_FUNCTIONS_H


#include <sys/stat.h>
#include <unistd.h>
#include "fs_basic.h"

#include <stdio.h>
#include <stdint.h>

// Declaración de funciones FUSE
int qrfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int qrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int qrfs_open(const char *path, struct fuse_file_info *fi);
int qrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int qrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int qrfs_rename(const char *from, const char *to, unsigned int flags);

// Estructura con operaciones FUSE
extern struct fuse_operations qrfs_ops;




typedef struct {
    char *folder;
    u32   block_size;

    // Superblock fields
    u32 version, total_blocks, total_inodes;
    u32 inode_bitmap_start, inode_bitmap_blocks;
    u32 data_bitmap_start,  data_bitmap_blocks;
    u32 inode_table_start,  inode_table_blocks;
    u32 data_region_start;

    // Root
    u32 root_inode;

    // Cache root inode (opcional)
    u32 root_inode_number;
    u32 root_inode_mode;
    u32 root_uid, root_gid;
    u32 root_links;
    u32 root_size;
    u32 root_direct[12];
    u32 root_indirect1;
} qrfs_ctx;


#endif
