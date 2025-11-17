
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
  unsigned int root_inode;



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
  unsigned int inode_id;
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
  spblock.root_inode = 0;
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

void free_inode(int inode_id) {
    if (inode_id >= 0 && inode_id < spblock.total_inodes) {
        spblock.inode_bitmap[inode_id] = '0';
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

void init_inode(inode *node, uint inode_id, mode_t mode, uint size) {
    node->inode_number = inode_id;
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


void init_dir_entry(dir_entry *entry, uint inode_id, const char *name) {
    entry->inode_id = inode_id;
    strncpy(entry->name, name, sizeof(entry->name));
}


int writeblock(const char *folder, int index, const void *buf, size_t len){
  char path[256];
  snprintf(path, sizeof(path), "%s/block_%04d.pngs", folder, index);
  FILE *fp = fopen(path, "r+b");
  if (fp == NULL) {return -1;}
  fseek(fp, 0, SEEK_SET); //starting position
  fwrite(buf, len, 1, fp);
  fclose(fp);
 return 0;
}

int readblock(const char *folder, int index, void *buf, size_t len){
  char path[256];
  snprintf(path, sizeof(path), "%s/block_%04d.pngs", folder, index);
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {return -1;}
  fread(buf, 1, len,fp);
  fclose(fp);
  return 0;

}


static inline void u32le_write(uint v, unsigned char *p) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}


int write_superblock_png0(const char *folder, const superblock *sb) {
    if (!folder || !sb) { errno = EINVAL; return -1; }

    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04d.png", folder, 0);

    // Crear carpeta si no existe
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", folder);
    system(cmd);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Error creando %s: %s\n", path, strerror(errno));
        return -1;
    }

    unsigned char buf[block_size];
    memset(buf, 0, sizeof(buf));

    // Magic
    buf[0] = 'Q'; buf[1] = 'R'; buf[2] = 'F'; buf[3] = 'S';
    u32le_write(sb->version,      &buf[4]);
    u32le_write(sb->blocksize,    &buf[8]);
    u32le_write(sb->total_blocks, &buf[12]);
    u32le_write(sb->total_inodes, &buf[16]);

    memcpy(&buf[20],  sb->inode_bitmap, sizeof(sb->inode_bitmap));
    memcpy(&buf[148], sb->data_bitmap,  sizeof(sb->data_bitmap));
    u32le_write(sb->root_inode, &buf[276]);

    size_t written = fwrite(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (written != sizeof(buf)) {
        fprintf(stderr, "Escritura incompleta: %zu/%zu\n", written, sizeof(buf));
        return -1;
    }

    printf("Superbloque escrito en %s (%d bytes)\n", path, block_size);
    return 0;
}




int main() {
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
  // Archivo regular + permisos 0644, sin usar S_IFREG
  init_inode(&myinode, 1, 0100000 | 0644, 512);
  printf("Inodo %u listo. size=%u, uid=%u, gid=%u\n",
         myinode.inode_number, myinode.inode_size, myinode.user_id, myinode.group_id);

  printf("\nCreando entrada de directorio...\n");
  dir_entry entry;
  init_dir_entry(&entry, 1, "archivo.txt");
  printf("Entrada: %s -> inodo %u\n", entry.name, entry.inode_id);

  printf("\nEscribiendo superbloque en block_0000.png (contenedor binario)...\n");
  if (write_superblock_png0("./qrfolder", &spblock) != 0) {
    fprintf(stderr, "Error al escribir superbloque: %s\n", strerror(errno));
    return 1;
  }

  printf("\nTodo listo. (Este main no monta FUSE, solo prueba funciones)\n");
  return 0;
}
