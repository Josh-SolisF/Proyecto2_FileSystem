
#include "fuse_functions.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

int qrfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = 1024; // tamaño simulado
    }
    return 0;
}

int qrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    printf("Crear archivo: %s\n", path);
    return 0;
}

int qrfs_open(const char *path, struct fuse_file_info *fi) {
    printf("Abrir archivo: %s\n", path);
    return 0;
}

int qrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    const char *contenido = "Hola desde QRFS!\n";
    size_t len = strlen(contenido);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, contenido + offset, size);
    } else {
        size = 0;
    }
    return size;
}

int qrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("Escribir en archivo: %s, datos: %.*s\n", path, (int)size, buf);
    return size;
}

int qrfs_rename(const char *from, const char *to) {
    printf("Renombrar: %s -> %s\n", from, to);
    return 0;
}

// Estructura global con operaciones
struct fuse_operations qrfs_ops = {
    .getattr = qrfs_getattr,
    .create  = qrfs_create,
    .open    = qrfs_open,
    .read    = qrfs_read,
    .write   = qrfs_write,
    .rename  = qrfs_rename,
};
