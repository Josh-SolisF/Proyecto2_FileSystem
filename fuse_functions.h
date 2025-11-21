
#ifndef FUSE_FUNCTIONS_H
#define FUSE_FUNCTIONS_H

#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <sys/stat.h>

// Declaración de funciones FUSE
int qrfs_getattr(const char *path, struct stat *stbuf);
int qrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int qrfs_open(const char *path, struct fuse_file_info *fi);
int qrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int qrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int qrfs_rename(const char *from, const char *to);

// Estructura con operaciones FUSE
extern struct fuse_operations qrfs_ops;

#endif
