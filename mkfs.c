// mkfs.c
#include "fs_basic.h"
#include "fs_utils.h"
#include "bitmaps.h"
#include "inode.h"
#include "dir.h"
#include <stdio.h>

int main(void) {
    printf("Inicializando superblock...\n");
    initialize_superblock();
    printf("Version: %u, Blocksize: %u, Total inodes: %u, Total blocks: %u\n",
           spblock.version, spblock.blocksize, spblock.total_inodes, spblock.total_blocks);

    printf("\nAsignando un inodo...\n");
    int inode_id = allocate_inode();
    printf("Inodo asignado: %d\n", inode_id);

    printf("Liberando el inodo...\n");
    free_inode(inode_id);
    printf("Inodo %d liberado.\n", inode_id);

    printf("\nAsignando un bloque...\n");
    int blk = allocate_block();
    printf("Bloque asignado: %d\n", blk);

    printf("Liberando el bloque...\n");
    free_block(blk);
    printf("Bloque %d liberado.\n", blk);

    printf("\nCreando un inodo en memoria...\n");
    inode myinode;
    init_inode(&myinode, 1, 0100000 | 0644, 512);

    printf("\nCreando entrada de directorio...\n");
    dir_entry entry;
    init_dir_entry(&entry, 1, "archivo.txt");
    printf("Entrada: %s -> inodo %u\n", entry.name, entry.inode_id);

    printf("\nTodo listo. (Este main no monta FUSE, solo prueba funciones)\n");
    return 0;
}
