
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/stat.h>
#define block_size 1024
typedef unsigned int uint ;

typedef struct superblock {
  uint version;

  //Info of file system
  uint blocksize; //size of the block
  uint total_blocks;
  uint total_inodes;

  //How to offsets the blocks

  char inode_bitmap[128];
  char data_bitmap[128];
  unsigned int root_ino;



} superblock;

typedef struct inode {
  //Identity
  uint inode_number;
  mode_t inode_mode;
  uint user_id;
  uint group_id; //
  uint links_quaintities;

  //sizes
  uint inode_size;
  struct timespec last_access_time;
  struct timespec last_modification_time;
  struct timespec metadata_last_change_time;
  uint  direct[12];
  uint indirect1;



}inode;
typedef struct dir_entry {
  unsigned int ino;
  char name[256];
} dir_entry;


superblock spblock;


void initialize_superblock() {
  spblock.version = 1;
  spblock.blocksize = block_size;
  spblock.total_blocks = 100;
  spblock.total_inodes = 10;
  memset(spblock.data_bitmap, '0', sizeof(spblock.data_bitmap));
  memset(spblock.inode_bitmap, '0', sizeof(spblock.inode_bitmap));
  spblock.root_ino = 0;
}

int allocate_inode() {
    for (int i = 0; i < spblock.total_inodes; i++) {
        if (spblock.inode_bitmap[i] == '0') {
            spblock.inode_bitmap[i] = '1';
            return i; // número de inodo asignado
        }
    }
    return -1; // No hay inodos libres
}

void free_inode(int ino) {
    if (ino >= 0 && ino < spblock.total_inodes) {
        spblock.inode_bitmap[ino] = '0';
    }
}

int allocate_block() {
    for (int i = 0; i < spblock.total_blocks; i++) {
        if (spblock.data_bitmap[i] == '0') {
            spblock.data_bitmap[i] = '1';
            return i;
        }
    }
    return -1; // No hay bloques libres
}

void free_block(int block_num) {
    if (block_num >= 0 && block_num < spblock.total_blocks) {
        spblock.data_bitmap[block_num] = '0';
    }
}

static inline void now_timespec(struct timespec *ts) {
    // C11 estándar
    timespec_get(ts, TIME_UTC); // no necesita POSIX ni -lrt
}

void init_inode(inode *node, uint ino, mode_t mode, uint size) {
    node->inode_number = ino;
    node->inode_mode = mode;
    node->inode_size = size;
    node->links_quaintities = 1;
    node->user_id = getuid();
    node->group_id = getgid();

    now_timespec(&node->last_access_time);
    node->last_modification_time = node->last_access_time;
    node->metadata_last_change_time = node->last_access_time;
    memset(node->direct, 0, sizeof(node->direct));
    node->indirect1 = 0;
}


void init_dir_entry(dir_entry *entry, uint ino, const char *name) {
    entry->ino = ino;
    strncpy(entry->name, name, sizeof(entry->name));
}


int main() {
    printf("Inicializando superblock...\n");
    initialize_superblock();
    printf("Version: %u, Blocksize: %u, Total inodes: %u\n",
           spblock.version, spblock.blocksize, spblock.total_inodes);

    printf("\nAsignando un inodo...\n");
    int ino = allocate_inode();
    printf("Inodo asignado: %d\n", ino);

    printf("Liberando el inodo...\n");
    free_inode(ino);
    printf("Inodo %d liberado.\n", ino);

    printf("\nAsignando un bloque...\n");
    int blk = allocate_block();
    printf("Bloque asignado: %d\n", blk);

    printf("Liberando el bloque...\n");
    free_block(blk);
    printf("Bloque %d liberado.\n", blk);

    printf("\nCreando un inodo en memoria...\n");
    inode myinode;
// Archivo regular + permisos 0644, sin usar S_IFREG
init_inode(&myinode, 1, 0100000 | 0644, 512);    printf("\nCreando entrada de directorio...\n");
    dir_entry entry;
    init_dir_entry(&entry, 1, "archivo.txt");
    printf("Entrada: %s -> inodo %u\n", entry.name, entry.ino);

    printf("\nTodo listo. (Este main no monta FUSE, solo prueba funciones)\n");
    return 0;
}
