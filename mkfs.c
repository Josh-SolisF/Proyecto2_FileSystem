
#define FUSE_USE_VERSION 31

#define _POSIX_C_SOURCE 200809L
#define FUSE_USE_VERSION 31


#define _DEFAULT_SOURCE           // o, alternativamente: #define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>               // realpath
#include <string.h>
#include <limits.h>               // PATH_MAX
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fuse3/fuse.h>

#include "fs_basic.h"
#include "superblock.h"
#include "fs_utils.h"

#include "block.h"
#include "inode.h"
#include "dir.h"
#include "fuse_functions.h" // donde está qrfs_ctx y qrfs_ops




int mkfs(int argc, char **argv) {
    const char *folder = (argc >= 3) ? argv[2] : "./qrfolder";

    u32 block_size   = 1024;
    u32 total_blocks = DEFAULT_TOTAL_BLOCKS;  // <=128
    u32 total_inodes = DEFAULT_TOTAL_INODES;  // <=128

    // Procesar argumentos opcionales
    for (int i = 2; i < argc; ++i) {
        if (strncmp(argv[i], "--blocks=", 9) == 0) {
            total_blocks = (u32)strtoul(argv[i] + 9, NULL, 10);
        } else if (strncmp(argv[i], "--inodes=", 9) == 0) {
            total_inodes = (u32)strtoul(argv[i] + 9, NULL, 10);
        } else if (strncmp(argv[i], "--blocksize=", 12) == 0) {
            block_size = (u32)strtoul(argv[i] + 12, NULL, 10);
        }
    }

    // Validaciones rápidas
    if (total_blocks > 128 || total_inodes > 128) {
        fprintf(stderr, "Por ahora mkfs.qrfs soporta como máximo 128 bloques e inodos.\n");
        return 2;
    }
    if (block_size < 512 || block_size > 65536) {
        fprintf(stderr, "block_size fuera de rango razonable (512..65536).\n");
        return 2;
    }

    // Preparar carpeta y bloques "zero"
    if (ensure_folder(folder) != 0) {
        fprintf(stderr, "No se pudo preparar la carpeta destino: %s\n", strerror(errno));
        return 1;
    }
    for (u32 i = 0; i < total_blocks; ++i) {
        if (create_zero_block(folder, i, block_size) != 0) {
            fprintf(stderr, "No se pudo crear block_%04u.png: %s\n", i, strerror(errno));
            return 1;
        }
    }

    // --- Offsets / layout ---
    u32 inode_bitmap_start  = 1;
    u32 inode_bitmap_blocks = 1;

    u32 data_bitmap_start   = inode_bitmap_start + inode_bitmap_blocks;
    u32 data_bitmap_blocks  = 1;

    u32 inode_table_start   = data_bitmap_start + data_bitmap_blocks;

    u32 inode_record_size   = 128;
    u32 inode_table_bytes   = total_inodes * inode_record_size;
    u32 inode_table_blocks  = ceil_div(inode_table_bytes, block_size);

    u32 data_region_start   = inode_table_start + inode_table_blocks;

    if (data_region_start >= total_blocks) {
        fprintf(stderr, "No hay espacio para región de datos.\n");
        return 1;
    }

    // --- Bitmaps (formato ASCII '0'/'1' de 128 bytes) ---
    unsigned char inode_bitmap[128];
    unsigned char data_bitmap[128];
    memset(inode_bitmap, '0', sizeof(inode_bitmap));
    memset(data_bitmap,  '0', sizeof(data_bitmap));

    // Inodo raíz
    u32 root_inode = 0;
    inode_bitmap[root_inode] = '1';

    // Marcar bloques usados en data_bitmap:
    //   - block 0: superbloque
    //   - rango del bitmap de inodos
    //   - rango del bitmap de datos
    //   - rango de la tabla de inodos
    //   - bloque del directorio raíz (primer bloque de la región de datos)
    data_bitmap[0] = '1'; // SB

    for (u32 b = inode_bitmap_start; b < inode_bitmap_start + inode_bitmap_blocks; ++b) {
        data_bitmap[b] = '1';
    }
    for (u32 b = data_bitmap_start; b < data_bitmap_start + data_bitmap_blocks; ++b) {
        data_bitmap[b] = '1';
    }
    for (u32 b = inode_table_start; b < inode_table_start + inode_table_blocks; ++b) {
        data_bitmap[b] = '1';
    }

    u32 root_dir_block = data_region_start;
    data_bitmap[root_dir_block] = '1';

    // --- Escribir bitmaps como BLOQUES completos (evita overflow) ---
    unsigned char *inode_bitmap_block = (unsigned char*)calloc(1, block_size);
    unsigned char *data_bitmap_block  = (unsigned char*)calloc(1, block_size);
    if (!inode_bitmap_block || !data_bitmap_block) {
        fprintf(stderr, "No hay memoria para buffers de bitmaps.\n");
        free(inode_bitmap_block); free(data_bitmap_block);
        return 1;
    }

    memcpy(inode_bitmap_block, inode_bitmap, sizeof(inode_bitmap));
    memcpy(data_bitmap_block,  data_bitmap,  sizeof(data_bitmap));

    if (write_block(folder, inode_bitmap_start, inode_bitmap_block, block_size) != 0 ||
        write_block(folder, data_bitmap_start,  data_bitmap_block,  block_size) != 0) {
        fprintf(stderr, "Error escribiendo bitmaps.\n");
        free(inode_bitmap_block); free(data_bitmap_block);
        return 1;
    }

    free(inode_bitmap_block);
    free(data_bitmap_block);

    // --- Tabla de inodos (registro del inodo raíz en el primer bloque de la tabla) ---
    unsigned char rec[128];
    u32 direct[12] = {0};
    direct[0] = root_dir_block;

    u32 mode_dir = 0040000 | 0755; // S_IFDIR | 0755
    u32 dir_size = 520;

    // uid=0, gid=0, links=2 ('.' y '..'), size=dir_size, direct[], indirect=0
    inode_serialize128(rec, root_inode, mode_dir, 0, 0, 2, dir_size, direct, 0);

    unsigned char *itbl_block0 = (unsigned char*)calloc(1, block_size);
    if (!itbl_block0) {
        fprintf(stderr, "No hay memoria para itbl_block0.\n");
        return 1;
    }
    memcpy(itbl_block0, rec, sizeof(rec));
    if (write_block(folder, inode_table_start, itbl_block0, block_size) != 0) {
        fprintf(stderr, "Error escribiendo tabla de inodos.\n");
        free(itbl_block0);
        return 1;
    }
    free(itbl_block0);

    // --- Directorio raíz (contenido del bloque de dir) ---
    unsigned char *dirblk = (unsigned char*)calloc(1, block_size);
    if (!dirblk) {
        fprintf(stderr, "No hay memoria para dirblk.\n");
        return 1;
    }
    build_root_dir_block(dirblk, block_size, root_inode);
    if (write_block(folder, root_dir_block, dirblk, block_size) != 0) {
        fprintf(stderr, "Error escribiendo directorio raíz.\n");
        free(dirblk);
        return 1;
    }
    free(dirblk);

    // --- Superbloque ---
    if (write_superblock_with_offsets(folder, block_size, total_blocks, total_inodes,
                                      inode_bitmap, data_bitmap, root_inode,
                                      inode_bitmap_start, inode_bitmap_blocks,
                                      data_bitmap_start, data_bitmap_blocks,
                                      inode_table_start, inode_table_blocks,
                                      data_region_start) != 0) {
        fprintf(stderr, "Error escribiendo superbloque.\n");
        return 1;
    }

    // --- Reporte ---
    printf("QRFS creado en '%s'\n", folder);
    printf("block_size=%u, total_blocks=%u, total_inodes=%u\n", block_size, total_blocks, total_inodes);
    printf("Layout:\n");
    printf("  SB               : block 0\n");
    printf("  inode_bitmap     : start=%u, blocks=%u\n", inode_bitmap_start, inode_bitmap_blocks);
    printf("  data_bitmap      : start=%u, blocks=%u\n", data_bitmap_start, data_bitmap_blocks);
    printf("  inode_table      : start=%u, blocks=%u (record_size=128)\n", inode_table_start, inode_table_blocks);
    printf("  data_region_start: %u\n", data_region_start);
    printf("  root inode       : %u  (direct[0]=%u, size=%u)\n", root_inode, root_dir_block, dir_size);

    return 0;
}



int fsck_qrfs(const char *folder) {
    u32 version, total_blocks, total_inodes;
    unsigned char inode_bitmap[128], data_bitmap[128];
    u32 root_inode;
    u32 ib_start, ib_blocks, db_start, db_blocks, it_start, it_blocks, data_start;

    // Leer superbloque
    if (read_superblock(folder, 1024, &version, &total_blocks, &total_inodes,
                        inode_bitmap, data_bitmap, &root_inode,
                        &ib_start, &ib_blocks, &db_start, &db_blocks,
                        &it_start, &it_blocks, &data_start) != 0) {
        fprintf(stderr, "Error: superbloque inválido.\n");
        return 1;
    }

    printf("Superbloque OK: version=%u, blocks=%u, inodes=%u\n", version, total_blocks, total_inodes);

    // Validar layout
    if (ib_start + ib_blocks > total_blocks ||
        db_start + db_blocks > total_blocks ||
        it_start + it_blocks > total_blocks ||
        data_start >= total_blocks) {
        fprintf(stderr, "Error: layout inconsistente.\n");
        return 1;
    }

    // Leer tabla de inodos (primer bloque)
    unsigned char buf[1024];
    if (read_block(folder, it_start, buf, 1024) != 0) {
        fprintf(stderr, "Error leyendo tabla de inodos.\n");
        return 1;
    }

    // Extraer inodo raíz
    unsigned char in[128];
    memcpy(in, buf, 128);

    u32 inode_number, inode_mode, user_id, group_id, links, size, indirect1;
    u32 direct[12];
    //deserializar los inodos
    inode_deserialize128(in, &inode_number, &inode_mode, &user_id, &group_id,
                         &links, &size, direct, &indirect1);

    printf("Inodo raíz: inode=%u, mode=%o, size=%u, links=%u\n",
           inode_number, inode_mode, size, links);

    if ((inode_mode & 0040000) == 0) {
        fprintf(stderr, "Error: inodo raíz no es directorio.\n");
        return 1;
    }
    if (links != 2) {
        fprintf(stderr, "Advertencia: inodo raíz links=%u (esperado 2).\n", links);
    }

    // Leer bloque del directorio raíz
    unsigned char dirbuf[1024];
    if (read_block(folder, direct[0], dirbuf, 1024) != 0) {
        fprintf(stderr, "Error leyendo bloque del directorio raíz.\n");
        return 1;
    }

	u32 root_dir_block = direct[0]; // El bloque del directorio raíz viene del inodo raíz

	list_directory_block(folder, 1024, root_dir_block);
    printf("Chequeo completado: QRFS parece consistente.\n");
    return 0;
}





int mount_qrfs(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s --mount <backend_folder> <mount_point>\n", argv[0]);
        return 1;
    }

    char backend_abs[PATH_MAX], mount_abs[PATH_MAX];


if (!realpath(argv[2], backend_abs)) {
    perror("No pude resolver ruta absoluta del backend_folder");
    return 1;
}
if (!realpath(argv[3], mount_abs)) {
    perror("No pude resolver ruta absoluta del mount_point");
    return 1;
}

    struct stat st;
    if (stat(mount_abs, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Mount point '%s' no existe o no es un directorio.\n", mount_abs);
        return 1;
    }
    if (access(mount_abs, W_OK) != 0) {
        perror("No tengo permiso de escritura sobre el mountpoint");
        return 1;
    }

    // Reservar memoria para el contexto
    qrfs_ctx *ctx = calloc(1, sizeof(qrfs_ctx));
    if (!ctx) {
        perror("No pude asignar memoria para el contexto");
        return 1;
    }

    ctx->folder = strdup(backend_abs);
    ctx->block_size = 1024; // OJO: si se almacena en el superblock, léelo de allí.

    u32 version, total_blocks, total_inodes;
    unsigned char inode_bitmap[128], data_bitmap[128];
    u32 root_inode;
    u32 inode_bitmap_start, inode_bitmap_blocks;
    u32 data_bitmap_start, data_bitmap_blocks;
    u32 inode_table_start, inode_table_blocks;
    u32 data_region_start;

    int rc = read_superblock(ctx->folder, ctx->block_size,
                             &version, &total_blocks, &total_inodes,
                             inode_bitmap, data_bitmap, &root_inode,
                             &inode_bitmap_start, &inode_bitmap_blocks,
                             &data_bitmap_start, &data_bitmap_blocks,
                             &inode_table_start, &inode_table_blocks,
                             &data_region_start);
    if (rc != 0) {
        fprintf(stderr, "[mount] Error leyendo superblock.\n");
        free(ctx->folder);
        free(ctx);
        return 1;
    }

    // Copiar datos al contexto
    ctx->version = version;
    ctx->total_blocks = total_blocks;
    ctx->total_inodes = total_inodes;
    ctx->inode_bitmap_start = inode_bitmap_start;
    ctx->inode_bitmap_blocks = inode_bitmap_blocks;
    ctx->data_bitmap_start = data_bitmap_start;
    ctx->data_bitmap_blocks = data_bitmap_blocks;
    ctx->inode_table_start = inode_table_start;
    ctx->inode_table_blocks = inode_table_blocks;
    ctx->data_region_start = data_region_start;
    ctx->root_inode = root_inode;


spblock.version = version;
spblock.blocksize = ctx->block_size;
spblock.total_blocks = total_blocks;
spblock.total_inodes = total_inodes;
spblock.root_inode = root_inode;
memcpy(spblock.inode_bitmap, inode_bitmap, sizeof(spblock.inode_bitmap));


    // Chequeos básicos
    if (ctx->block_size == 0 || (ctx->block_size % 128) != 0) {
        fprintf(stderr, "[mount] block_size=%u inválido.\n", ctx->block_size);
        free(ctx->folder);
        free(ctx);
        return 1;
    }
    if (ctx->inode_table_start + ctx->inode_table_blocks > ctx->total_blocks) {
        fprintf(stderr, "[mount] Tabla de inodos fuera de rango.\n");
        free(ctx->folder);
        free(ctx);
        return 1;
    }
    if (ctx->root_inode >= ctx->total_inodes) {
        fprintf(stderr, "[mount] root_inode fuera de rango.\n");
        free(ctx->folder);
        free(ctx);
        return 1;
    }

    // Leer inodo raíz
    unsigned char in128[128];

rc = read_inode_block(ctx, ctx->root_inode, in128);

    if (rc != 0) {
        fprintf(stderr, "[mount] Falló read_inode_block(root=%u).\n", ctx->root_inode);
        free(ctx->folder);
        free(ctx);
        return 1;
    }

    inode_deserialize128(in128,
                         &ctx->root_inode_number,
                         &ctx->root_inode_mode,
                         &ctx->root_uid,
                         &ctx->root_gid,
                         &ctx->root_links,
                         &ctx->root_size,
                         ctx->root_direct,
                         &ctx->root_indirect1);

    if (!S_ISDIR(ctx->root_inode_mode)) {
        fprintf(stderr, "[mount] El inodo raíz NO es un directorio.\n");
        free(ctx->folder);
        free(ctx);
        return 1;
    }

    // Logs
    fprintf(stderr, "QRFS: version=%u, blocks=%u, inodes=%u\n",
            ctx->version, ctx->total_blocks, ctx->total_inodes);
    fprintf(stderr, "QRFS: inode_table_start=%u blocks=%u, data_region_start=%u\n",
            ctx->inode_table_start, ctx->inode_table_blocks, ctx->data_region_start);
    fprintf(stderr, "QRFS: root_inode=%u size=%u links=%u uid=%u gid=%u\n",
            ctx->root_inode_number, ctx->root_size, ctx->root_links, ctx->root_uid, ctx->root_gid);

    // Preparar argumentos para FUSE

char *fuse_argv[] = { "qrfs", "-f", "-d", "-s", mount_abs };

int fuse_argc = (int)(sizeof(fuse_argv) / sizeof(fuse_argv[0]));




    // Lanzar FUSE con el contexto
    int ret = fuse_main(fuse_argc, fuse_argv, &qrfs_ops, ctx);

    // Liberar recursos al desmontar
    free(ctx->folder);
    free(ctx);
    return ret;
}



