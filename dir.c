
#include "fs_basic.h"
#include "fs_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
