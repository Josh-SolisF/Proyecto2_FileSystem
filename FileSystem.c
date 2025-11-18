

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>



typedef uint32_t u32;

static const u32 block_size  = 1024;
static const u32 DEFAULT_TOTAL_BLOCKS= 100;   // <= 128
static const u32 DEFAULT_TOTAL_INODES= 10;    // <= 128


typedef struct superblock {
  u32 version;

  //Info of file system
  u32 blocksize; //size of the block
  u32 total_blocks;
  u32 total_inodes;

  //How to offsets the blocks

  char inode_bitmap[128];
  char data_bitmap[128];
  unsigned int root_inode;



} superblock;

typedef struct inode {
  //Identity
  u32 inode_number;
  mode_t inode_mode;
  u32 user_id;
  u32 group_id; //
  u32 links_quaintities;

  //sizes
  u32 inode_size;
  struct timespec last_access_time;
  struct timespec last_modification_time;
  struct timespec metadata_last_change_time;
  u32  direct[12];
  u32 indirect1;



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

void init_inode(inode *node, u32 inode_id, mode_t mode, u32 size) {
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


void init_dir_entry(dir_entry *entry, u32 inode_id, const char *name) {
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


static inline void u32le_write(u32 v, unsigned char *p) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}


static inline u32 ceil_div(u32 a, u32 b) { return (a + b - 1) / b; } //preguntar para que es esto


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


static int ensure_folder(const char *folder) {
    struct stat st;
    if (stat(folder, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR; return -1;
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", folder);
    int rc = system(cmd);
    (void)rc;
    return 0;
}


static int create_zero_block (const char *folder, u32 index, u32 block_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, index);
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    unsigned char *zeros = (unsigned char*)calloc(1, block_size);
    if (!zeros) { fclose(fp); errno = ENOMEM; return -1; }
    size_t w = fwrite(zeros, 1, block_size, fp);
    free(zeros);
    fclose(fp);
    return (w == block_size) ? 0 : -1;
}


static int write_block(const char *folder, u32 index, const void *buf, u32 len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, index);
    FILE *fp = fopen(path, "r+b");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_SET);
    size_t w = fwrite(buf, 1, len, fp);
    fclose(fp);
    return (w == len) ? 0 : -1;
}


/* ---- Serialización del superbloque con offsets ---- */
static int write_superblock_with_offsets(
    const char *folder,
    u32 block_size,
    u32 total_blocks,
    u32 total_inodes,
    const unsigned char *inode_bitmap_128,     // 128 bytes
    const unsigned char *data_bitmap_128,      // 128 bytes
    u32 root_inode,
    // offsets:
    u32 inode_bitmap_start, u32 inode_bitmap_blocks,
    u32 data_bitmap_start,  u32 data_bitmap_blocks,
    u32 inode_table_start,  u32 inode_table_blocks,
    u32 data_region_start
) {
    unsigned char *buf = (unsigned char*)calloc(1, block_size);
    if (!buf) { errno = ENOMEM; return -1; }

    // Magic y cabecera
    buf[0]='Q'; buf[1]='R'; buf[2]='F'; buf[3]='S';
    u32le_write(1,              &buf[4]);   // version=1
    u32le_write(block_size,     &buf[8]);
    u32le_write(total_blocks,   &buf[12]);
    u32le_write(total_inodes,   &buf[16]);

    // Bitmaps (copiados crudos al SB, 128 bytes c/u)
    memcpy(&buf[20],  inode_bitmap_128, 128);
    memcpy(&buf[148], data_bitmap_128,  128);

    // root inode
    u32le_write(root_inode, &buf[276]);

    // Offsets y tamaños de regiones
    u32le_write(inode_bitmap_start,  &buf[280]);
    u32le_write(inode_bitmap_blocks, &buf[284]);
    u32le_write(data_bitmap_start,   &buf[288]);
    u32le_write(data_bitmap_blocks,  &buf[292]);
    u32le_write(inode_table_start,   &buf[296]);
    u32le_write(inode_table_blocks,  &buf[300]);
    u32le_write(data_region_start,   &buf[304]);

    // Escribir bloque 0
    int rc = write_block(folder, 0, buf, block_size);
    free(buf);
    return rc;
}


/* ---- Serialización de inodo (registro 128 bytes) ----
 * Formato de 128 bytes:
 *  [0..3]   inode_number (u32 LE)
 *  [4..7]   inode_mode   (u32 LE)
 *  [8..11]  user_id      (u32 LE)
 *  [12..15] group_id     (u32 LE)
 *  [16..19] links        (u32 LE)
 *  [20..23] size         (u32 LE)
 *  [24..71] direct[12]   (12 * u32 LE = 48 bytes)
 *  [72..75] indirect1    (u32 LE)
 *  [76..127] reservado (0)
 */
static void inode_serialize128(
    unsigned char out[128],
    u32 inode_number, u32 inode_mode, u32 user_id, u32 group_id,
    u32 links, u32 size,
    const u32 direct[12], u32 indirect1
) {
    memset(out, 0, 128);
    u32le_write(inode_number, &out[0]);
    u32le_write(inode_mode,   &out[4]);
    u32le_write(user_id,      &out[8]);
    u32le_write(group_id,     &out[12]);
    u32le_write(links,        &out[16]);
    u32le_write(size,         &out[20]);
    for (int i=0;i<12;i++) u32le_write(direct[i], &out[24 + i*4]);
    u32le_write(indirect1,    &out[72]);
}

/* ---- Directorio en disco (bloque de 1024):
 * Usamos entradas fijas tipo (u32 inode_id + 256 bytes name) = 260 bytes c/u.
 * Dos entradas: "." y ".." ocupan 520 bytes.
 */
static void build_root_dir_block(unsigned char *block, u32 block_size, u32 root_inode) {
    memset(block, 0, block_size);
    // entrada "."
    u32le_write(root_inode, &block[0]);                // inode_id
    const char *dot = ".";
    strncpy((char*)&block[4], dot, 256);
    // entrada ".."
    u32le_write(root_inode, &block[264]);              // inode_id
    const char *dotdot = "..";
    strncpy((char*)&block[268], dotdot, 256);
    // resto queda en cero
}

/*
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
*/

/* ---- mkfs principal ---- */
int main(int argc, char **argv) {
    const char *folder = (argc >= 2) ? argv[1] : "./qrfolder";
    u32 block_size   = block_size;
    u32 total_blocks = DEFAULT_TOTAL_BLOCKS;  // <=128
    u32 total_inodes = DEFAULT_TOTAL_INODES;  // <=128

    // Opcionales: --blocks N --inodes M --blocksize B
    for (int i=2; i<argc; ++i) {
        if (strncmp(argv[i], "--blocks=", 9)==0) {
            total_blocks = (u32)strtoul(argv[i]+9, NULL, 10);
        } else if (strncmp(argv[i], "--inodes=", 9)==0) {
            total_inodes = (u32)strtoul(argv[i]+9, NULL, 10);
        } else if (strncmp(argv[i], "--blocksize=", 12)==0) {
            block_size = (u32)strtoul(argv[i]+12, NULL, 10);
        }
    }
    if (total_blocks > 128 || total_inodes > 128) {
        fprintf(stderr, "Por ahora mkfs.qrfs soporta como máximo 128 bloques e inodos (para bitmaps en 1 bloque).\n");
        return 2;
    }
    if (block_size < 512 || block_size > 65536) {
        fprintf(stderr, "block_size fuera de rango razonable (512..65536).\n");
        return 2;
    }

    /* 1) Crear carpeta y todos los archivos de bloque */
    if (ensure_folder(folder) != 0) {
        fprintf(stderr, "No se pudo preparar la carpeta destino: %s\n", strerror(errno));
        return 1;
    }
    for (u32 i=0; i<total_blocks; ++i) {
        if (create_zero_block(folder, i, block_size) != 0) {
            fprintf(stderr, "No se pudo crear block_%04u.png: %s\n", i, strerror(errno));
            return 1;
        }
    }

    /* 2) Definir offsets de regiones (todos relativos a numeración de bloques) */
    u32 inode_bitmap_start  = 1;
    u32 inode_bitmap_blocks = 1;                   // cabe en 1 bloque (128 bytes <= 1024)
    u32 data_bitmap_start   = inode_bitmap_start + inode_bitmap_blocks;  // 2
    u32 data_bitmap_blocks  = 1;                   // cabe en 1 bloque (128 bytes <= 1024)
    u32 inode_table_start   = data_bitmap_start + data_bitmap_blocks;    // 3

    // Cada inodo ocupa 128 bytes en disco:
    u32 inode_record_size   = 128;
    u32 inode_table_bytes   = total_inodes * inode_record_size;
    u32 inode_table_blocks  = ceil_div(inode_table_bytes, block_size);   // p.ej., 10->1280 -> 2 bloques

    u32 data_region_start   = inode_table_start + inode_table_blocks;    // 5 si B=100, I=10

    if (data_region_start >= total_blocks) {
        fprintf(stderr, "No hay espacio para región de datos (data_region_start=%u, total_blocks=%u).\n",
                data_region_start, total_blocks);
        return 1;
    }

    /* 3) Construir bitmaps en RAM (128 bytes cada uno) */
    unsigned char inode_bitmap[128];
    unsigned char data_bitmap[128];
    memset(inode_bitmap, '0', sizeof(inode_bitmap));
    memset(data_bitmap,  '0', sizeof(data_bitmap));

    // Inodo raíz:
    u32 root_inode = 0;
    inode_bitmap[root_inode] = '1';

    // Bloques reservados (no disponibles para datos): SB + metadatos + tabla inodos
    data_bitmap[0] = '1'; // superbloque
    for (u32 b=inode_bitmap_start; b<inode_bitmap_start+inode_bitmap_blocks; ++b) data_bitmap[b] = '1';
    for (u32 b=data_bitmap_start;  b<data_bitmap_start+data_bitmap_blocks;  ++b) data_bitmap[b] = '1';
    for (u32 b=inode_table_start;  b<inode_table_start+inode_table_blocks;  ++b) data_bitmap[b] = '1';

    // Asignar 1 bloque para el directorio raíz en región de datos:
    u32 root_dir_block = data_region_start;        // tomamos el primero disponible
    data_bitmap[root_dir_block] = '1';

    /* 4) Escribir bitmaps en sus bloques */
    // Nota: aunque el bitmap real mida total_inodes/total_blocks, aquí usamos 128 bytes (relleno de '0'/'1').
    if (write_block(folder, inode_bitmap_start, inode_bitmap, block_size) != 0) {
        fprintf(stderr, "Error escribiendo bitmap de inodos: %s\n", strerror(errno));
        return 1;
    }
    if (write_block(folder, data_bitmap_start, data_bitmap, block_size) != 0) {
        fprintf(stderr, "Error escribiendo bitmap de bloques: %s\n", strerror(errno));
        return 1;
    }

    /* 5) Escribir tabla de inodos con el inodo raíz */
    // Construimos el registro 0 (raíz)
    unsigned char rec[128];
    u32 direct[12]; for (int i=0;i<12;i++) direct[i]=0;
    direct[0] = root_dir_block;    // apunta al bloque de directorio raíz

    // Modo directorio 0755 (usa S_IFDIR|0755); aquí grabamos numérico directo:
    u32 mode_dir = 0040000 | 0755; // 0040000 = S_IFDIR en octal

    // Tamaño lógico del dir: 2 entradas * 260 bytes = 520
    u32 dir_size = 520;

    inode_serialize128(rec, root_inode, mode_dir,
                       0, 0,           // uid/gid (0 por ahora)
                       2,              // links (. y ..)
                       dir_size,
                       direct, 0);

    // Escribir el registro 0 dentro del primer bloque de la tabla (offset 0)
    unsigned char *itbl_block0 = (unsigned char*)calloc(1, block_size);
    if (!itbl_block0) { errno = ENOMEM; return 1; }
    memcpy(&itbl_block0[0], rec, 128);

    if (write_block(folder, inode_table_start, itbl_block0, block_size) != 0) {
        free(itbl_block0);
        fprintf(stderr, "Error escribiendo tabla de inodos (bloque 0): %s\n", strerror(errno));
        return 1;
    }
    free(itbl_block0);

    // Si la tabla ocupa más de 1 bloque, los demás ya están en cero por create_zero_block()

    /* 6) Escribir bloque del directorio raíz */
    unsigned char *dirblk = (unsigned char*)calloc(1, block_size);
    if (!dirblk) { errno = ENOMEM; return 1; }
    build_root_dir_block(dirblk, block_size, root_inode);
    if (write_block(folder, root_dir_block, dirblk, block_size) != 0) {
        free(dirblk);
        fprintf(stderr, "Error escribiendo directorio raíz: %s\n", strerror(errno));
        return 1;
    }
    free(dirblk);

    /* 7) Escribir el superbloque con offsets */
    if (write_superblock_with_offsets(
            folder, block_size, total_blocks, total_inodes,
            inode_bitmap, data_bitmap, root_inode,
            inode_bitmap_start, inode_bitmap_blocks,
            data_bitmap_start,  data_bitmap_blocks,
            inode_table_start,  inode_table_blocks,
            data_region_start) != 0) {
        fprintf(stderr, "Error escribiendo superbloque: %s\n", strerror(errno));
        return 1;
    }

    /* 8) Reporte */
    printf("QRFS creado en '%s'\n", folder);
    printf("block_size=%u, total_blocks=%u, total_inodes=%u\n",
           block_size, total_blocks, total_inodes);
    printf("Layout:\n");
    printf("  SB               : block 0\n");
    printf("  inode_bitmap     : start=%u, blocks=%u\n", inode_bitmap_start, inode_bitmap_blocks);
    printf("  data_bitmap      : start=%u, blocks=%u\n", data_bitmap_start,  data_bitmap_blocks);
    printf("  inode_table      : start=%u, blocks=%u (record_size=128)\n", inode_table_start, inode_table_blocks);
    printf("  data_region_start: %u\n", data_region_start);
    printf("  root inode       : %u  (direct[0]=%u, size=%u)\n", root_inode, root_dir_block, dir_size);

    return 0;
}
