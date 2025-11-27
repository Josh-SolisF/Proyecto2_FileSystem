

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include "fs_utils.h"

#include "fuse_functions.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>

#include "block.h"       // Para read_inode_block
#include "inode.h"          // Para inode_deserialize128
#include "dir.h"            // Para search_inode_by_path (o fs_utils.h si la pusiste ahí)
#include "bitmaps.h"
#include "fs_utils.h"
#include <sys/stat.h>

#include <inttypes.h>


int qrfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    fprintf(stderr, "[getattr] path='%s'\n", path);

    // Recuperar contexto
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    // Inicializar salida
    memset(stbuf, 0, sizeof(struct stat));

    // Caso raíz "/"
    if (strcmp(path, "/") == 0) {
        u32 mode = ctx->root_inode_mode;
        if ((mode & S_IFMT) != S_IFDIR) {
            mode = (S_IFDIR | 0755); // defensa si no viene marcado como directorio
        }

        stbuf->st_ino   = ctx->root_inode_number;
        stbuf->st_mode  = mode;                  // S_IFDIR | perms
        stbuf->st_uid   = ctx->root_uid;
        stbuf->st_gid   = ctx->root_gid;
        stbuf->st_nlink = (ctx->root_links == 0 ? 2 : ctx->root_links);
        stbuf->st_size  = ctx->root_size;

        return 0;
    }

    // Buscar inodo por path
    u32 inode_id = 0;
    int s_rc = search_inode_by_path(ctx, path, &inode_id);
    if (s_rc != 0) return -ENOENT;

    // Leer bloque del inodo (128 bytes)
    unsigned char raw[128];
    int r_rc = read_inode_block(ctx, inode_id, raw);
    if (r_rc != 0) return -EIO;

    // Deserializar inodo (retorna por parámetros)
    u32 ino=0, mode=0, uid=0, gid=0, links=0, size=0, direct[12], ind1=0;
    inode_deserialize128(raw, &ino, &mode, &uid, &gid, &links, &size, direct, &ind1);

    // Validar tipo soportado
    u32 type = (mode & S_IFMT);
    if (type != S_IFDIR && type != S_IFREG) {
        return -ENOENT;  // amplía cuando soportes más tipos
    }

    // Llenar atributos
    stbuf->st_ino   = ino;
    stbuf->st_mode  = mode;                   // S_IFDIR/S_IFREG | permisos
    stbuf->st_uid   = uid;
    stbuf->st_gid   = gid;
    stbuf->st_nlink = (links == 0 ? ((type == S_IFDIR) ? 2 : 1) : links);
    stbuf->st_size  = size;

    return 0;

}


int qrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi; // Por ahora no usamos file handle

    // Contexto
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    // Parsear path -> parent + nombre
    char path_copy[512];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy)-1] = '\0';
    char *parent_path = dirname(path_copy);

    char name_copy[512];
    strncpy(name_copy, path, sizeof(name_copy));
    name_copy[sizeof(name_copy)-1] = '\0';
    char *file_name = basename(name_copy);

    // Bloque del directorio padre
    u32 parent_block;
    if (find_parent_dir_block(ctx, parent_path, &parent_block) != 0) {
        return -ENOENT; // Directorio padre no encontrado
    }

    // Asignar nuevo inodo
    int inode_id = allocate_inode();
    if (inode_id < 0) {
        return -ENOSPC; // No hay espacio para inodos
    }

    // Inicializar inodo REG (asegúrate que mode incluye S_IFREG)
    inode new_inode;
    // Si 'mode' no incluye tipo, forzamos REG | 0644
    mode_t file_mode = (mode & S_IFMT) ? mode : (S_IFREG | 0644);
    init_inode(&new_inode, (u32)inode_id, file_mode, 0); // Tamaño inicial = 0
    new_inode.user_id = getuid();
    new_inode.group_id = getgid();
    new_inode.links_quaintities = 1;

    // Persistir inodo
    if (write_inode(folder, (u32)inode_id, &new_inode) != 0) {
        free_inode((int)inode_id);
        return -EIO;
    }

    // Leer bloque del directorio padre
    unsigned char *dir_block = (unsigned char*)calloc(1, block_size);
    if (!dir_block) {
        free_inode((int)inode_id);
        return -ENOMEM;
    }
    if (read_block(folder, parent_block, dir_block, block_size) != 0) {
        free(dir_block);
        free_inode((int)inode_id);
        return -EIO;
    }

    // Agregar entrada (layout 260 bytes por entrada)
    int add_rc = add_dir_entry_to_block(dir_block, block_size, (u32)inode_id, file_name);
    if (add_rc != 0) {
        free(dir_block);
        free_inode((u32)inode_id);
        return (add_rc == -EEXIST) ? -EEXIST : -ENOSPC;
    }

    // Persistir bloque actualizado
    if (write_directory_block(folder, parent_block, dir_block, block_size) != 0) {
        free(dir_block);
        free_inode((u32)inode_id);
        return -EIO;
    }

    free(dir_block);

    // Actualizar bitmaps (persistir en disco; idealmente usa ctx)
    if (update_bitmaps(ctx->folder) != 0) {
        return -EIO;
    }

    return 0; // Éxito
}



int qrfs_open(const char *path, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;

    // Buscar inodo por path

u32 inode_id;
if (search_inode_by_path(ctx, path, &inode_id) != 0) {
    return -ENOENT;
}
fi->fh = inode_id;


    return 0; // Éxito
}



int qrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    u32 inode_id = (u32)fi->fh;

    // Leer inodo
    inode node;
    if (read_inode(folder, inode_id, &node) != 0) {
        return -EIO;
    }

    // Si offset >= tamaño del archivo, no hay nada que leer
    if ((u32)offset >= node.inode_size) {
        return 0;
    }

    // Ajustar size si excede el tamaño del archivo
    if (offset + size > node.inode_size) {
        size = node.inode_size - offset;
    }

    size_t bytes_read = 0;
    size_t remaining = size;
    size_t current_offset = offset;

    // Calcular bloque inicial y desplazamiento
    u32 start_block = current_offset / block_size;
    u32 block_offset = current_offset % block_size;

    unsigned char block[block_size];

    // Leer bloques directos
    for (u32 i = start_block; i < 12 && remaining > 0; i++) {
        if (node.direct[i] == 0) break; // No hay más bloques

        if (read_block(folder, node.direct[i], block, block_size) != 0) {
            return -EIO;
        }

        size_t to_copy = block_size - block_offset;
        if (to_copy > remaining) to_copy = remaining;

        memcpy(buf + bytes_read, block + block_offset, to_copy);

        bytes_read += to_copy;
        remaining -= to_copy;
        block_offset = 0; // Solo aplica al primer bloque
    }

    return bytes_read; // Número de bytes leídos
}




int qrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi;
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    // Parse path
    char path_copy[512];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy)-1] = '\0';
    char *parent_path = dirname(path_copy);

    char name_copy[512];
    strncpy(name_copy, path, sizeof(name_copy));
    name_copy[sizeof(name_copy)-1] = '\0';
    char *file_name = basename(name_copy);

    // Find parent dir block
    u32 parent_block;
    if (find_parent_dir_block(ctx, parent_path, &parent_block) != 0) {
        return -ENOENT;
    }

    // Allocate inode
    int inode_id = allocate_inode();
    if (inode_id < 0) return -ENOSPC;

    // Allocate data block
    int data_block = allocate_block();
    if (data_block < 0) {
        free_inode(inode_id);
        return -ENOSPC;
    }

    // Create PNG for data block
    if (create_zero_block(folder, (u32)data_block, block_size) != 0) {
        free_block(data_block);
        free_inode(inode_id);
        return -EIO;
    }

    // Init inode
    inode new_inode;
    mode_t file_mode = (mode & S_IFMT) ? mode : (S_IFREG | 0644);
    init_inode(&new_inode, (u32)inode_id, file_mode, 0);
    new_inode.user_id = getuid();
    new_inode.group_id = getgid();
    new_inode.links_quaintities = 1;
    new_inode.direct[0] = (u32)data_block;

    // Persist inode
    if (write_inode(folder, (u32)inode_id, &new_inode) != 0) {
        free_block(data_block);
        free_inode(inode_id);
        return -EIO;
    }

    // Update parent dir block
    unsigned char *dir_block = (unsigned char*)calloc(1, block_size);
    if (!dir_block) return -ENOMEM;
    if (read_block(folder, parent_block, dir_block, block_size) != 0) {
        free(dir_block);
        return -EIO;
    }
    int add_rc = add_dir_entry_to_block(dir_block, block_size, (u32)inode_id, file_name);
    if (add_rc != 0) {
        free(dir_block);
        return (add_rc == -EEXIST) ? -EEXIST : -ENOSPC;
    }
    if (write_directory_block(folder, parent_block, dir_block, block_size) != 0) {
        free(dir_block);
        return -EIO;
    }
    free(dir_block);

    // Mark bitmap and persist
    spblock.data_bitmap[data_block] = '1';
    if (update_bitmaps(ctx->folder) != 0) return -EIO;

    return 0;
}


int qrfs_rename(const char *from, const char *to, unsigned int flags) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    //Buscar inodo del archivo origen

u32 inode_id;
if (search_inode_by_path(ctx, from, &inode_id) != 0) {
    return -ENOENT;
}


    //Parsear paths
    char from_copy[512], to_copy[512];
    strncpy(from_copy, from, sizeof(from_copy));
    strncpy(to_copy, to, sizeof(to_copy));

    char *from_parent = dirname(from_copy);
    char *from_name   = basename(from_copy);

    char *to_parent   = dirname(to_copy);
    char *to_name     = basename(to_copy);

    //Bloque del directorio padre origen
    u32 from_block, to_block;
    if (find_parent_dir_block(ctx,from_parent, &from_block) != 0) return -ENOENT;
    if (find_parent_dir_block(ctx, to_parent, &to_block) != 0) return -ENOENT;

    unsigned char block_from[block_size];
    unsigned char block_to[block_size];

    if (read_block(folder, from_block, block_from, block_size) != 0) return -EIO;
    if (read_block(folder, to_block, block_to, block_size) != 0) return -EIO;

    //Eliminar entrada en bloque origen
    u32 max_entries = block_size / sizeof(dir_entry);
    dir_entry *entries_from = (dir_entry *)block_from;
    int removed = 0;
    for (u32 i = 0; i < max_entries; i++) {
        if (entries_from[i].inode_id == inode_id && strcmp(entries_from[i].name, from_name) == 0) {
            entries_from[i].inode_id = 0; // Marcar como libre
            memset(entries_from[i].name, 0, sizeof(entries_from[i].name));
            removed = 1;
            break;
        }
    }
    if (!removed) return -ENOENT;

    // Agregar entrada en bloque destino
    dir_entry new_entry;
    init_dir_entry(&new_entry, inode_id, to_name);
    if (add_dir_entry_to_block(block_to, block_size, new_entry.inode_id, new_entry.name) != 0) {
        return -ENOSPC;
    }

    // Persistir cambios
    if (write_directory_block(folder, from_block, block_from, block_size) != 0) return -EIO;
    if (write_directory_block(folder, to_block, block_to, block_size) != 0) return -EIO;

    return 0; // Éxito
}




int qrfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    (void)offset; (void)fi; (void)flags;
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    // Solo soportamos raíz por ahora
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    // Emite "." y ".." únicamente, sin enumerar otros nombres
    filler(buf, ".",  NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    return 0;
}

// Estructura global con operaciones
struct fuse_operations qrfs_ops = {
    .getattr = qrfs_getattr,
    .create  = qrfs_create,

    .readdir = qrfs_readdir,

    .open    = qrfs_open,
    .read    = qrfs_read,
    .write   = qrfs_write,
    .rename  = qrfs_rename,
};
