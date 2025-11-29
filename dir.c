
#define FUSE_USE_VERSION 31



#include "fs_utils.h"
#include <string.h>
#include <libgen.h>
#include "fs_basic.h"
#include "fs_utils.h"
#include "inode.h"
#include "block.h"
#include "fuse_functions.h"
#include <limits.h>
#include "dir.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#define ROOT_INODE 0

#define QRFS_DIR_NAME_MAX      256

//Analiza struct dir_entry
void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name) {
    entry->inode_id = inode_id;
    strncpy(entry->name, name, sizeof(entry->name));
    entry->name[sizeof(entry->name)-1] = '\0';
}


//Escribe una entrada de directorio dentro del buffer de bloque blk a partir de offset:
void direntry_write(unsigned char *blk, u32 offset, u32 inode, const char *name) {
    u32le_write(inode, &blk[offset]); // 4 bytes LE
    // nombre con terminador y padding a 256
    size_t nlen = strnlen(name, QRFS_DIR_NAME_MAX - 1);
    memset(&blk[offset + 4], 0, QRFS_DIR_NAME_MAX);
    memcpy(&blk[offset + 4], name, nlen);
}

//Lee una entrada del directorio desde el bloque
void direntry_read(const unsigned char *blk, u32 offset, u32 *inode_out, char *name_out) {
    *inode_out = u32le_read(&blk[offset]); // 4 bytes LE → u32 host
    memcpy(name_out, &blk[offset + 4], QRFS_DIR_NAME_MAX);
    name_out[QRFS_DIR_NAME_MAX - 1] = '\0'; // asegurar terminador

}

//Iniializa el directorio raíz
void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode) {
    memset(block, 0, block_size);
    direntry_write(block, 0, root_inode, "."); //Apunta al mismo directorio
    direntry_write(block, QRFS_DIR_ENTRY_SIZE, root_inode, ".."); //Apunta al padre que la verdad no ocupamos porque es solo uno por el momenot
}

//Busca dentro de un bloque de directorio una entrada cuyo nombre conincida con el name
static int find_in_block(const unsigned char *blk, u32 block_size, const char *name, u32 *inode_out) {
    for (u32 off = 0; off + QRFS_DIR_ENTRY_SIZE <= block_size; off += QRFS_DIR_ENTRY_SIZE) {
        u32 ent_inode;
        char ent_name[QRFS_DIR_NAME_MAX];
        direntry_read(blk, off, &ent_inode, ent_name);
        if (ent_inode == 0) continue; // slot vacío
        if (strcmp(ent_name, name) == 0) {
            *inode_out = ent_inode;
            return 0;
        }
    }
    return -ENOENT;
}


//Busqueda de inodo por ruta
int search_inode_by_path(qrfs_ctx *ctx, const char *path, u32 *inode_id_out) {
    if (!ctx || !path || !inode_id_out) {
        fprintf(stderr, "[SEARCH] invalid args ctx=%p path=%p inode_id_out=%p\n", (void*)ctx, (void*)path, (void*)inode_id_out);
        fflush(stderr);
        return -EINVAL;
    }

    fprintf(stderr, "[SEARCH] path='%s'\n", path); fflush(stderr);
//Si es raiz lo devuelve
    if (strcmp(path, "/") == 0) {
        *inode_id_out = ctx->root_inode;
        fprintf(stderr, "[SEARCH] root => inode=%u\n", *inode_id_out);
        fflush(stderr);
        return 0;
    }

    const char *name = strrchr(path, '/');
    name = (name) ? name + 1 : path; //saca el basename del path
    fprintf(stderr, "[SEARCH] basename='%s'\n", name); fflush(stderr);
    u32 dir_block = ctx->root_direct[0]; //Carga el primer bloque directo del directorio raíz
    fprintf(stderr, "[SEARCH] dir_block(root_direct[0])=%u\n", dir_block); fflush(stderr);
    unsigned char *blk = calloc(1, ctx->block_size);
    if (!blk) {
        fprintf(stderr, "[SEARCH] ENOMEM allocating %u bytes\n", ctx->block_size);
        fflush(stderr);
        return -ENOMEM;
    }

    int rb = read_block(ctx->folder, dir_block, blk, ctx->block_size);
    fprintf(stderr, "[SEARCH] read_block rc=%d\n", rb); fflush(stderr);
    if (rb != 0) {
        free(blk);
        fprintf(stderr, "[SEARCH] read_block failed for %u\n", dir_block); fflush(stderr);
        return -EIO;
    }

    // Iterar entradas de directorio de ese bloque si encuenta un nombre con el basename retorna id
    for (u32 offset = 0; offset + QRFS_DIR_ENTRY_SIZE <= ctx->block_size; offset += QRFS_DIR_ENTRY_SIZE) {
        u32 inode_id = 0;
        char name_out[QRFS_DIR_NAME_MAX];
        direntry_read(blk, offset, &inode_id, name_out);


        if (inode_id != 0 || name_out[0] != '\0') {
            fprintf(stderr, "[SEARCH] off=%u inode=%u name='%s'\n", offset, inode_id, name_out);
            fflush(stderr);
        }

        if (inode_id != 0 && strcmp(name_out, name) == 0) {
            *inode_id_out = inode_id;
            fprintf(stderr, "[SEARCH] FOUND '%s' => inode=%u at off=%u\n", name, inode_id, offset);
            fflush(stderr);
            free(blk);
            return 0;
        }
    }

    free(blk);
    fprintf(stderr, "[SEARCH] NOT FOUND '%s' in dir block=%u\n", name, dir_block);
    fflush(stderr);
    return -ENOENT;
}





void list_directory_block(const char *folder, u32 block_size, u32 dir_block_index) {
    char path[512]; // Construir ruta del bloque
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, dir_block_index);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error abriendo bloque de directorio: %s\n", strerror(errno));
        return;
    }

    unsigned char *buf = (unsigned char*)malloc(block_size); //Lee block_size bytes.
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
    for (size_t i = 0; i < max_entries; i++) { //Itera todas las entradas del bloque y las imprime como inode y name, saltando las que estén vacías
        size_t offset = i * entry_size;
        u32 inode_id = u32le_read(&buf[offset]);
        const char *name = (const char*)&buf[offset + 4];

        // Si inode_id == 0 y nombre vacío, asumimos entrada libre
        if (inode_id == 0 && name[0] == '\0') continue;

        printf("  [%zu] inode=%u, name='%s'\n", i, inode_id, name);
    }

    free(buf);
}





int add_dir_entry_to_block(unsigned char *blk, u32 block_size, u32 inode_id, const char *name) {
    // Ignoramos duplicados
    for (u32 off = 0; off + QRFS_DIR_ENTRY_SIZE <= block_size; off += QRFS_DIR_ENTRY_SIZE) {
        u32 ent_inode; char ent_name[QRFS_DIR_NAME_MAX];
        direntry_read(blk, off, &ent_inode, ent_name);
        if (ent_inode != 0 && strcmp(ent_name, name) == 0) {
            return -EEXIST; // name already present
        }
    }
    // Trata de encontrar un slot vacio en los bitmpas osea inode = 0
    for (u32 off = 0; off + QRFS_DIR_ENTRY_SIZE <= block_size; off += QRFS_DIR_ENTRY_SIZE) {
        u32 ent_inode; char ent_name[QRFS_DIR_NAME_MAX];
        direntry_read(blk, off, &ent_inode, ent_name);
        if (ent_inode == 0) {
            direntry_write(blk, off, inode_id, name);
            return 0;
        }
    }
    return -ENOSPC; // no free entry
}

//Wrappper sencillo para write block
int write_directory_block(const char *folder, u32 block_index, const unsigned char *block, u32 block_size) {
    return write_block(folder, block_index, block, block_size);
}


int find_parent_dir_block(qrfs_ctx *ctx,const char *parent_path,u32 *parent_block_out){
    if (!ctx || !parent_path || !parent_block_out) return -EINVAL;

    // Caso raíz /
    if (strcmp(parent_path, "/") == 0) {
        *parent_block_out = ctx->root_direct[0];
        return 0;
    }

    // Resolver el inodo del directorio padre
    u32 parent_inode;
    int rc = search_inode_by_path(ctx, parent_path, &parent_inode);
    if (rc != 0) return rc;

    // Leer el registro 128 del inodo padre
    unsigned char in128[128];
    rc = read_inode_block(ctx, parent_inode, in128);
    if (rc != 0) return rc;

    u32 ino, mode, uid, gid, links, size, direct[12], ind1;
    inode_deserialize128(in128, &ino, &mode, &uid, &gid, &links, &size, direct, &ind1);

    if (!S_ISDIR(mode)) return -ENOTDIR;
    if (direct[0] == 0) return -EIO; // directorio sin bloque directo 0

    *parent_block_out = direct[0];
    return 0;
}


