
#include "fuse_functions.h"
#include <stdio.h>
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
    (void) fi; // No se usa por ahora

    // Recuperar contexto
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    // Parsear path para obtener nombre y directorio padre
    char path_copy[512];
    strncpy(path_copy, path, sizeof(path_copy));
    char *parent_path = dirname(path_copy);

    char name_copy[512];
    strncpy(name_copy, path, sizeof(name_copy));
    char *file_name = basename(name_copy);

    // Buscar bloque del directorio padre
    u32 parent_block;
    if (find_parent_dir_block(parent_path, &parent_block) != 0) {
        return -ENOENT; // Directorio padre no encontrado
    }

    // Asignar nuevo inodo
    int inode_id = allocate_inode();
    if (inode_id < 0) {
        return -ENOSPC; // No hay espacio para inodos
    }

    // Inicializar inodo
    inode new_inode;
    init_inode(&new_inode, inode_id, mode, 0); // Tamaño inicial = 0
    new_inode.user_id = getuid();
    new_inode.group_id = getgid();
    new_inode.links_quaintities = 1;

    // Persistir inodo en disco
    if (write_inode(folder, inode_id, &new_inode) != 0) {
        free_inode(inode_id);
        return -EIO;
    }

    //Crear entrada de directorio
    dir_entry entry;
    init_dir_entry(&entry, inode_id, file_name);

    // Leer bloque del directorio padre
    unsigned char dir_block[block_size];
    if (read_block(folder, parent_block, dir_block, block_size) != 0) {
        free_inode(inode_id);
        return -EIO;
    }

    // Agregar entrada al bloque
    if (add_dir_entry_to_block(dir_block, block_size, &entry) != 0) {
        free_inode(inode_id);
        return -ENOSPC; // Directorio lleno
    }

    // Persistir bloque actualizado
    if (write_directory_block(folder, parent_block, dir_block, block_size) != 0) {
        free_inode(inode_id);
        return -EIO;
    }

    //Actualizar bitmaps
    if (update_bitmaps(folder) != 0) {
        return -EIO;
    }

    return 0; // Éxito
}



int qrfs_open(const char *path, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;

    // Buscar inodo por path
    u32 inode_id = search_inode_by_path(ctx->folder, path, ctx->block_size);
    if ((int)inode_id < 0) {
        return -ENOENT; // Archivo no existe
    }

    // Guardar inode_id en file handle para usar en read
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



int qrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    u32 inode_id = (u32)fi->fh;

    // Leer inodo
    inode node;
    if (read_inode(folder, inode_id, &node) != 0) {
        return -EIO;
    }

    size_t bytes_written = 0;
    size_t remaining = size;
    size_t current_offset = offset;

    // Calcular bloque inicial y desplazamiento
    u32 start_block = current_offset / block_size;
    u32 block_offset = current_offset % block_size;

    unsigned char block[block_size];

    for (u32 i = start_block; i < 12 && remaining > 0; i++) {
        // Si el bloque no existe, asignarlo
        if (node.direct[i] == 0) {
            int new_block = allocate_block();
            if (new_block < 0) {
                break; // No hay espacio
            }
            node.direct[i] = new_block;
        }

        // Leer bloque actual (para no sobrescribir datos previos)
        if (read_block(folder, node.direct[i], block, block_size) != 0) {
            return -EIO;
        }

        // Calcular cuánto escribir en este bloque
        size_t to_write = block_size - block_offset;
        if (to_write > remaining) to_write = remaining;

        memcpy(block + block_offset, buf + bytes_written, to_write);

        // Escribir bloque actualizado
        if (write_block(folder, node.direct[i], block, block_size) != 0) {
            return -EIO;
        }

        bytes_written += to_write;
        remaining -= to_write;
        block_offset = 0; // Solo aplica al primer bloque
    }

    // Actualizar tamaño del archivo si creció
    if ((u32)(offset + bytes_written) > node.inode_size) {
        node.inode_size = offset + bytes_written;
    }

    // Persistir inodo actualizado
    if (write_inode(folder, inode_id, &node) != 0) {
        return -EIO;
    }

    // Actualizar bitmaps
    if (update_bitmaps(folder) != 0) {
        return -EIO;
    }

    return bytes_written; // Número de bytes escritos
}


int qrfs_rename(const char *from, const char *to, unsigned int flags) {
    qrfs_ctx *ctx = (qrfs_ctx *)fuse_get_context()->private_data;
    const char *folder = ctx->folder;
    u32 block_size = ctx->block_size;

    // 1. Buscar inodo del archivo origen
    u32 inode_id = search_inode_by_path(folder, from, block_size);
    if ((int)inode_id < 0) {
        return -ENOENT;
    }

    // 2. Parsear paths
    char from_copy[512], to_copy[512];
    strncpy(from_copy, from, sizeof(from_copy));
    strncpy(to_copy, to, sizeof(to_copy));

    char *from_parent = dirname(from_copy);
    char *from_name   = basename(from_copy);

    char *to_parent   = dirname(to_copy);
    char *to_name     = basename(to_copy);

    // 3. Bloque del directorio padre origen
    u32 from_block, to_block;
    if (find_parent_dir_block(from_parent, &from_block) != 0) return -ENOENT;
    if (find_parent_dir_block(to_parent, &to_block) != 0) return -ENOENT;

    unsigned char block_from[block_size];
    unsigned char block_to[block_size];

    if (read_block(folder, from_block, block_from, block_size) != 0) return -EIO;
    if (read_block(folder, to_block, block_to, block_size) != 0) return -EIO;

    // 4. Eliminar entrada en bloque origen
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

    // 5. Agregar entrada en bloque destino
    dir_entry new_entry;
    init_dir_entry(&new_entry, inode_id, to_name);
    if (add_dir_entry_to_block(block_to, block_size, &new_entry) != 0) {
        return -ENOSPC;
    }

    // 6. Persistir cambios
    if (write_directory_block(folder, from_block, block_from, block_size) != 0) return -EIO;
    if (write_directory_block(folder, to_block, block_to, block_size) != 0) return -EIO;

    return 0; // Éxito
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
