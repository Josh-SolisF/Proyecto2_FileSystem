

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

if (write_inode(ctx, inode_id, &new_inode) != 0) {
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



int qrfs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    return 0; // Ignorar timestamps por ahora
}



int qrfs_open(const char *path, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    u32 inode_id;
    if (search_inode_by_path(ctx, path, &inode_id) != 0) {
        return -ENOENT;
    }

    // Leer inodo para validar tipo
    unsigned char raw[128];
    if (read_inode_block(ctx, inode_id, raw) != 0) {
        return -EIO;
    }

    u32 ino=0, mode=0, uid=0, gid=0, links=0, size=0, direct[12], ind1=0;
    inode_deserialize128(raw, &ino, &mode, &uid, &gid, &links, &size, direct, &ind1);

    if ((mode & S_IFMT) != S_IFREG) {
        return -EISDIR; // No abrir directorios como archivos
    }


    fi->fh = inode_id; // Guardar el inodo como file handle
    return 0;
}


int qrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    u32 inode_id = (u32)fi->fh;

    unsigned char raw[128];
    if (read_inode_block(ctx, inode_id, raw) != 0) return -EIO;

    inode node;
    inode_deserialize128(raw, &node.inode_number, &node.inode_mode,
                         &node.user_id, &node.group_id, &node.links_quaintities,
                         &node.inode_size, node.direct, &node.indirect1);

    if ((node.inode_mode & S_IFMT) != S_IFREG) return -EISDIR;
    if ((u32)offset >= node.inode_size) return 0;
    if (offset + size > node.inode_size) size = node.inode_size - offset;

    size_t bytes_read = 0;
    size_t remaining = size;
    size_t current_offset = offset;

    u32 start_block = current_offset / ctx->block_size;
    u32 block_offset = current_offset % ctx->block_size;

    unsigned char *block = (unsigned char*)calloc(1, ctx->block_size);
    if (!block) return -ENOMEM;

    for (u32 i = start_block; i < 12 && remaining > 0; i++) {
        if (node.direct[i] == 0) break;

        if (read_block(ctx->folder, node.direct[i], block, ctx->block_size) != 0) {
            free(block);
            return -EIO;
        }

        size_t to_read = ctx->block_size - block_offset;
        if (to_read > remaining) to_read = remaining;

        memcpy(buf + bytes_read, block + block_offset, to_read);

        bytes_read += to_read;
        remaining -= to_read;
        block_offset = 0;
    }

    free(block);

    now_timespec(&node.last_access_time);
    write_inode(ctx, inode_id, &node);

    return bytes_read;
}




int qrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;
    u32 inode_id = (u32)fi->fh;

    // Leer inodo desde la tabla QRFS
    unsigned char raw[128];
    if (read_inode_block(ctx, inode_id, raw) != 0) return -EIO;

    inode node;
    inode_deserialize128(raw, &node.inode_number, &node.inode_mode,
                         &node.user_id, &node.group_id, &node.links_quaintities,
                         &node.inode_size, node.direct, &node.indirect1);

    // Validar tipo de archivo
    if ((node.inode_mode & S_IFMT) != S_IFREG) return -EISDIR;

    size_t bytes_written = 0;
    size_t remaining = size;
    size_t current_offset = offset;

    // Calcular bloque inicial y offset dentro del bloque
    u32 start_block = current_offset / block_size;
    u32 block_offset = current_offset % block_size;

    unsigned char *block = (unsigned char*)calloc(1, block_size);
    if (!block) return -ENOMEM;

    // Escribir en bloques directos
    for (u32 i = start_block; i < 12 && remaining > 0; i++) {
        // Si el bloque no existe, asignarlo
        if (node.direct[i] == 0) {
            int new_block = allocate_block();
            if (new_block < 0) break; // No hay espacio
            node.direct[i] = new_block;

            // Inicializar bloque vacío
            if (create_zero_block(folder, node.direct[i], block_size) != 0) {
                free(block);
                return -EIO;
            }
        }

        // Leer bloque actual
        if (read_block(folder, node.direct[i], block, block_size) != 0) {
            free(block);
            return -EIO;
        }

        // Calcular cuánto escribir
        size_t to_write = block_size - block_offset;
        if (to_write > remaining) to_write = remaining;

        memcpy(block + block_offset, buf + bytes_written, to_write);

        // Escribir bloque actualizado
        if (write_block(folder, node.direct[i], block, block_size) != 0) {
            free(block);
            return -EIO;
        }

        bytes_written += to_write;
        remaining -= to_write;
        block_offset = 0; // Solo aplica al primer bloque
    }

    free(block);

    // Actualizar tamaño y timestamps
    if ((u32)(offset + bytes_written) > node.inode_size)
        node.inode_size = offset + bytes_written;

    now_timespec(&node.last_modification_time);
    now_timespec(&node.metadata_last_change_time);

    // Persistir inodo actualizado
    if (write_inode(ctx, inode_id, &node) != 0) return -EIO;

    // Actualizar bitmaps
    if (update_bitmaps(folder) != 0) return -EIO;

    return bytes_written;
}



int find_dir_entry_in_block(const unsigned char *blk, u32 block_size,
                                   const char *name, u32 *inode_id_out,
                                   u32 *offset_out)
{
    u32 offset = 0;
    while (offset + sizeof(dir_entry) <= block_size) {
        u32 inode_tmp = 0;
        char name_tmp[256] = {0};
        direntry_read(blk, offset, &inode_tmp, name_tmp);

        if (inode_tmp != 0 && strcmp(name_tmp, name) == 0) {
            if (inode_id_out) *inode_id_out = inode_tmp;
            if (offset_out)   *offset_out   = offset;
            return 0;
        }
        offset += sizeof(dir_entry);
    }
    return -ENOENT;
}

int qrfs_rename(const char *from, const char *to, unsigned int flags) {
    (void)flags; // ignoramos flags por simplicidad

    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    // *** Asumimos un único directorio raíz ***
    // Normalizamos 'from' y 'to' para extraer solo el basename (sin subdirectorios)
    char from_copy[512];
    strncpy(from_copy, from, sizeof(from_copy));
    from_copy[sizeof(from_copy)-1] = '\0';
    char *from_name = basename(from_copy);

    char to_copy[512];
    strncpy(to_copy, to, sizeof(to_copy));
    to_copy[sizeof(to_copy)-1] = '\0';
    char *to_name = basename(to_copy);

    // Bloque del directorio raíz (usamos root_direct[0])
    u32 dir_block_index = ctx->root_direct[0];

    // Leer bloque del directorio
    unsigned char *dir_blk = (unsigned char*)calloc(1, block_size);
    if (!dir_blk) return -ENOMEM;

    if (read_block(folder, dir_block_index, dir_blk, block_size) != 0) {
        free(dir_blk);
        return -EIO;
    }

    // Buscar entrada origen
    u32 src_inode_id = 0, src_offset = 0;
    int rc = find_dir_entry_in_block(dir_blk, block_size, from_name, &src_inode_id, &src_offset);
    if (rc != 0) {
        free(dir_blk);
        return -ENOENT; // no existe el origen
    }

fprintf(stderr, "[RENAME] from='%s' to='%s'\n", from_name, to_name);
fprintf(stderr, "[RENAME] dir_block=%u\n", dir_block_index);

    // Comprobar si destino ya existe (política: no sobrescribir)
    u32 dst_inode_id = 0, dst_offset = 0;
    int dst_exists = (find_dir_entry_in_block(dir_blk, block_size, to_name, &dst_inode_id, &dst_offset) == 0);
    if (dst_exists) {
        free(dir_blk);
        return -EEXIST;
    }

    // Renombrar in-place: escribir el mismo inode_id con el nuevo nombre en el mismo offset
    direntry_write(dir_blk, src_offset, src_inode_id, to_name);

    // Persistir bloque de directorio
    if (write_directory_block(folder, dir_block_index, dir_blk, block_size) != 0) {
        free(dir_blk);
        return -EIO;
    }
    free(dir_blk);

    // Actualizar ctime del inodo (opcional pero recomendable)
    unsigned char raw[128];
    if (read_inode_block(ctx, src_inode_id, raw) == 0) {
        inode node;
        inode_deserialize128(raw, &node.inode_number, &node.inode_mode,
                             &node.user_id, &node.group_id, &node.links_quaintities,
                             &node.inode_size, node.direct, &node.indirect1);

        now_timespec(&node.metadata_last_change_time);
        write_inode(ctx, src_inode_id, &node);
    }

    return 0;
}

int qrfs_rmdir(const char *path) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    if (!ctx) return -EIO;

    // Proteger raíz por claridad (aunque FUSE normalmente no llama rmdir("/") )
    if (strcmp(path, "/") == 0) {
        return -EBUSY;  // no se puede borrar la raíz
    }

    // No soportamos directorios (no hay mkdir -> no existen subdirectorios)
    return -ENOTSUP;    // o -EOPNOTSUPP
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
    .utimens = qrfs_utimens,
    .rmdir  = qrfs_rmdir,
};
