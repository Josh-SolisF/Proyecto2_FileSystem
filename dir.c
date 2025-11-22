
#include "fs_basic.h"
#include "fs_utils.h"
#include "inode.h"
#include "block.h"
#include "fuse_functions.h"


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#define ROOT_INODE 0

void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name) {
    entry->inode_id = inode_id;
    strncpy(entry->name, name, sizeof(entry->name));
    entry->name[sizeof(entry->name)-1] = '\0';
}

void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode) {
    memset(block, 0, block_size);
    u32le_write(root_inode, &block[0]);
    strncpy((char*)&block[4], ".", 256); //los cuatro bytes del inodo mas los 256 del nombre
    u32le_write(root_inode, &block[264]); //inode en offset 264
    strncpy((char*)&block[268], "..", 256); //nonmbre de 256 en la posicion 268
}



int search_inode_by_path(qrfs_ctx *ctx, const char *path, u32 *inode_id_out) {
    if (!ctx || !path || !inode_id_out) return -EINVAL;

    // Si el path es "/" devolvemos el inodo raíz
    if (strcmp(path, "/") == 0) {
        *inode_id_out = ctx->root_inode;
        return 0;
    }

    // Copiar path y tokenizar
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';

    char *token = strtok(tmp, "/");
    u32 current_inode = ctx->root_inode;

    while (token) {
        // Leer inodo actual
        unsigned char in128[128];
        if (read_inode_block(ctx, current_inode, in128) != 0) {
            return -ENOENT;
        }

        // Deserializar inodo
        u32 inode_number, mode, uid, gid, links, size, direct[12], indirect1;
        inode_deserialize128(in128, &inode_number, &mode, &uid, &gid,
                              &links, &size, direct, &indirect1);

        if (!S_ISDIR(mode)) {
            return -ENOTDIR; // No es directorio
        }

        // Buscar token en las entradas del directorio
        int found = 0;
        for (int i = 0; i < 12 && direct[i] != 0; i++) {
            unsigned char block[4096];
            if (read_block(ctx->folder, direct[i], block, ctx->block_size) != 0) {
                return -EIO;
            }

            size_t entries = ctx->block_size / sizeof(dir_entry);
            dir_entry *entries_ptr = (dir_entry *)block;

            for (size_t j = 0; j < entries; j++) {
                if (entries_ptr[j].inode_id != 0 &&
                    strcmp(entries_ptr[j].name, token) == 0) {
                    current_inode = entries_ptr[j].inode_id;
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            return -ENOENT; // No se encontró el componente
        }

        token = strtok(NULL, "/");
    }

    *inode_id_out = current_inode;
    return 0;
}




void list_directory_block(const char *folder, u32 block_size, u32 dir_block_index) {
    char path[512]; // Construir ruta del bloque
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, dir_block_index);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error abriendo bloque de directorio: %s\n", strerror(errno));
        return;
    }

    unsigned char *buf = (unsigned char*)malloc(block_size);
    if (!buf) {
        fclose(fp);
        errno = ENOMEM;
        fprintf(stderr, "Memoria insuficiente\n");
        return;
    }

    size_t r = fread(buf, 1, block_size, fp);
    fclose(fp);
    if (r != block_size) {
        free(buf);
        fprintf(stderr, "Error leyendo bloque (bytes=%zu)\n", r);
        return;
    }


    size_t entry_size = 264;
    size_t max_entries = block_size / entry_size;// Cada entrada ocupa 264 bytes: 4 (inode) + 256 (nombre) + 4 (padding)

    printf("Contenido del directorio (bloque %u):\n", dir_block_index);
    for (size_t i = 0; i < max_entries; i++) {
        size_t offset = i * entry_size;
        u32 inode_id = u32le_read(&buf[offset]);
        const char *name = (const char*)&buf[offset + 4];

        // Si inode_id == 0 y nombre vacío, asumimos entrada libre
        if (inode_id == 0 && name[0] == '\0') continue;

        printf("  [%zu] inode=%u, name='%s'\n", i, inode_id, name);
    }

    free(buf);
}


int add_dir_entry_to_block(unsigned char *block, u32 block_size, const dir_entry *entry) {
    u32 max_entries = block_size / sizeof(dir_entry);
    dir_entry *entries = (dir_entry *)block;

    for (u32 i = 0; i < max_entries; i++) {
        if (entries[i].inode_id == 0) { // Espacio libre
            entries[i] = *entry;
            return 0;
        }
    }
    return -1; // No hay espacio
}


int write_directory_block(const char *folder, u32 block_index, const unsigned char *block, u32 block_size) {
    return write_block(folder, block_index, block, block_size);
}


//Solo soporta raíz y un nivel
int find_parent_dir_block(const char *path, u32 *block_index) {
    if (strcmp(path, "/") == 0) {
        *block_index = 0; // Bloque raíz
        return 0;
    }
    // Aquí deberiamos parsear el path y buscar en el directorio
    return -1; // por el momento x
}
