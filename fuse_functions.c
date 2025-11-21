
#include "fuse_functions.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>



int qrfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi; // not used
    memset(stbuf, 0, sizeof(struct stat));

    // Recuperar contexto (folder y block_size)
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;

    // Buscar el inodo correspondiente al path
    u32 inode_id = search_inode_by_path(ctx->folder, path, ctx->block_size);
    if ((int)inode_id < 0) {
        return -ENOENT; // Path not found
    }

    // Leer el inodo desde disco
    unsigned char raw[128];
    if (read_inode_block(ctx->folder, inode_id, raw, ctx->block_size) != 0) {
        return -EIO; // I/O error
    }

    // Deserializar atributos del inodo
    u32 ino, mode, uid, gid, links, size, direct[12], ind1;
    inode_deserialize128(raw, &ino, &mode, &uid, &gid, &links, &size, direct, &ind1);

    // Llenar struct stat
    stbuf->st_mode  = mode;      // Contiene S_IFDIR o S_IFREG + permisos
    stbuf->st_uid   = uid;       // Usuario propietario
    stbuf->st_gid   = gid;       // Grupo propietario
    stbuf->st_nlink = links;     // Número de enlaces
    stbuf->st_size  = size;      // Tamaño del archivo


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


int qrfs_rename(const char *from, const char *to, unsigned int flags) {
    (void) flags; // si no lo usas
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
