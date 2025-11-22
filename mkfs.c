#include "fs_basic.h"
#include "fs_utils.h"
#include "block.h"
#include "superblock.h"
#include "inode.h"
#include "dir.h"
#include "fuse_functions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits.h>

#include <errno.h>

#include <stdlib.h>
#include <limits.h>

#define FUSE_USE_VERSION 30
#include <fuse3/fuse.h>


#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#endif


int mkfs(int argc, char **argv) {
const char *folder = (argc >= 3) ? argv[2] : "./qrfolder";
u32 block_size   = 1024;
    u32 total_blocks = DEFAULT_TOTAL_BLOCKS;  // <=128
    u32 total_inodes = DEFAULT_TOTAL_INODES;  // <=128

    // Procesar argumentos opcionales
    for (int i = 2; i < argc; ++i) {
        if (strncmp(argv[i], "--blocks=", 9) == 0) {total_blocks = (u32)strtoul(argv[i] + 9, NULL, 10);}
        else if (strncmp(argv[i], "--inodes=", 9) == 0) {total_inodes = (u32)strtoul(argv[i] + 9, NULL, 10);}
        else if (strncmp(argv[i], "--blocksize=", 12) == 0) {block_size = (u32)strtoul(argv[i] + 12, NULL, 10);}
    }

    // Esto lo podemos quitar si el profe quieremás
    if (total_blocks > 128 || total_inodes > 128) {
        fprintf(stderr, "Por ahora mkfs.qrfs soporta como máximo 128 bloques e inodos.\n");
        return 2;
    }
    if (block_size < 512 || block_size > 65536) {
        fprintf(stderr, "block_size fuera de rango razonable (512..65536).\n");
        return 2;
    }

    //  Crear carpeta y bloques
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

    // Offsets
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

    // Bitmaps
    unsigned char inode_bitmap[128];
    unsigned char data_bitmap[128];
    memset(inode_bitmap, '0', sizeof(inode_bitmap));
    memset(data_bitmap,  '0', sizeof(data_bitmap));

    u32 root_inode = 0;
    inode_bitmap[root_inode] = '1';

    data_bitmap[0] = '1'; // SB
    for (u32 b = inode_bitmap_start; b < inode_bitmap_start + inode_bitmap_blocks; ++b) data_bitmap[b] = '1';
    for (u32 b = data_bitmap_start;  b < data_bitmap_start + data_bitmap_blocks;  ++b) data_bitmap[b] = '1';
    for (u32 b = inode_table_start;  b < inode_table_start + inode_table_blocks;  ++b) data_bitmap[b] = '1';

    u32 root_dir_block = data_region_start;
    data_bitmap[root_dir_block] = '1';

    // Escribir bitmaps
    if (write_block(folder, inode_bitmap_start, inode_bitmap, block_size) != 0 ||
        write_block(folder, data_bitmap_start, data_bitmap, block_size) != 0) {
        fprintf(stderr, "Error escribiendo bitmaps.\n");
        return 1;
    }

    //  Tabla de inodos (inodo raíz)
    unsigned char rec[128];
    u32 direct[12] = {0};
    direct[0] = root_dir_block;
    u32 mode_dir = 0040000 | 0755; // S_IFDIR | 0755
    u32 dir_size = 520;

    inode_serialize128(rec, root_inode, mode_dir, 0, 0, 2, dir_size, direct, 0);

    unsigned char *itbl_block0 = (unsigned char*)calloc(1, block_size);
    memcpy(itbl_block0, rec, 128);
    if (write_block(folder, inode_table_start, itbl_block0, block_size) != 0) {
        fprintf(stderr, "Error escribiendo tabla de inodos.\n");
        free(itbl_block0);
        return 1;
    }
    free(itbl_block0);

    // Directorio raíz
    unsigned char *dirblk = (unsigned char*)calloc(1, block_size);
    build_root_dir_block(dirblk, block_size, root_inode);
    if (write_block(folder, root_dir_block, dirblk, block_size) != 0) {
        fprintf(stderr, "Error escribiendo directorio raíz.\n");
        free(dirblk);
        return 1;
    }
    free(dirblk);

    // Superbloque
    if (write_superblock_with_offsets(folder, block_size, total_blocks, total_inodes,
                                      inode_bitmap, data_bitmap, root_inode,
                                      inode_bitmap_start, inode_bitmap_blocks,
                                      data_bitmap_start, data_bitmap_blocks,
                                      inode_table_start, inode_table_blocks,
                                      data_region_start) != 0) {
        fprintf(stderr, "Error escribiendo superbloque.\n");
        return 1;
    }

    //Reporte
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

    qrfs_ctx ctx = {0};
    ctx.folder     = strdup(backend_abs);
    if (!ctx.folder) {
        fprintf(stderr, "Fallo strdup para backend_abs\n");
        return 1;
    }

    // ⚠️ Si el tamaño de bloque es fijo en tu formato, déjalo aquí;
    // si se almacena en el superblock, LEELO desde allí y no lo hardcodees.
    ctx.block_size = 1024;

    // 1) Leer SUPERBLOQUE
    u32 version, total_blocks, total_inodes;
    unsigned char inode_bitmap[128], data_bitmap[128];
    u32 root_inode;
    u32 inode_bitmap_start, inode_bitmap_blocks;
    u32 data_bitmap_start,  data_bitmap_blocks;
    u32 inode_table_start,  inode_table_blocks;
    u32 data_region_start;

    int rc = read_superblock(ctx.folder, ctx.block_size,
                             &version, &total_blocks, &total_inodes,
                             inode_bitmap, data_bitmap,
                             &root_inode,
                             &inode_bitmap_start, &inode_bitmap_blocks,
                             &data_bitmap_start,  &data_bitmap_blocks,
                             &inode_table_start,  &inode_table_blocks,
                             &data_region_start);
    if (rc != 0) {
        fprintf(stderr, "[mount] Falló read_superblock (rc=%d). Abortando.\n", rc);
        free(ctx.folder);
        return 1;
    }

    // 2) Copiar al ctx
    ctx.version            = version;
    ctx.total_blocks       = total_blocks;
    ctx.total_inodes       = total_inodes;
    ctx.inode_bitmap_start = inode_bitmap_start;
    ctx.inode_bitmap_blocks= inode_bitmap_blocks;
    ctx.data_bitmap_start  = data_bitmap_start;
    ctx.data_bitmap_blocks = data_bitmap_blocks;
    ctx.inode_table_start  = inode_table_start;
    ctx.inode_table_blocks = inode_table_blocks;
    ctx.data_region_start  = data_region_start;
    ctx.root_inode         = root_inode;

    // (Opcional) cachear bitmaps:
    // memcpy(ctx.inode_bitmap, inode_bitmap, 128);
    // memcpy(ctx.data_bitmap,  data_bitmap, 128);

    // 2.1) Chequeos de integridad básicos (evita lecturas fuera de rango)
    if (ctx.block_size == 0 || (ctx.block_size % 128) != 0) {
        fprintf(stderr, "[mount] block_size=%u inválido para inodos de 128 bytes.\n", ctx.block_size);
        free(ctx.folder);
        return 1;
    }
    if (ctx.inode_table_start + ctx.inode_table_blocks > ctx.total_blocks) {
        fprintf(stderr, "[mount] Tabla de inodos fuera de rango: start=%u blocks=%u total_blocks=%u\n",
                ctx.inode_table_start, ctx.inode_table_blocks, ctx.total_blocks);
        free(ctx.folder);
        return 1;
    }
    if (ctx.root_inode >= ctx.total_inodes) {
        fprintf(stderr, "[mount] root_inode=%u fuera de rango total_inodes=%u\n",
                ctx.root_inode, ctx.total_inodes);
        free(ctx.folder);
        return 1;
    }

    // 3) Leer y deserializar el INODO RAÍZ desde la TABLA DE INODOS
    unsigned char in128[128];
    rc = read_inode_block(ctx.folder,
                          ctx.root_inode,        // inode_id
                          in128,
                          ctx.block_size,
                          ctx.inode_table_start, // start de la tabla
                          ctx.total_inodes);
    if (rc != 0) {
        fprintf(stderr, "[mount] Falló read_inode_block(root=%u). rc=%d\n", ctx.root_inode, rc);
        free(ctx.folder);
        return 1;
    }

    inode_deserialize128(in128,
                         &ctx.root_inode_number,
                         &ctx.root_inode_mode,
                         &ctx.root_uid,
                         &ctx.root_gid,
                         &ctx.root_links,
                         &ctx.root_size,
                         ctx.root_direct,
                         &ctx.root_indirect1);

    // Verificar que el número coincida con el id
    if (ctx.root_inode_number != ctx.root_inode) {
        fprintf(stderr, "[mount] Inconsistencia: superblock.root=%u pero inode.number=%u\n",
                ctx.root_inode, ctx.root_inode_number);
        // No abortamos necesariamente, pero logueamos.
    }

    // Verificar que sea directorio
    if (!S_ISDIR(ctx.root_inode_mode)) {
        fprintf(stderr, "[mount] El inodo raíz NO es un directorio (mode=0x%08x).\n",
                ctx.root_inode_mode);
        free(ctx.folder);
        return 1;
    }

    // Logs útiles
    fprintf(stderr, "QRFS: version=%u, blocks=%u, inodes=%u\n",
            ctx.version, ctx.total_blocks, ctx.total_inodes);
    fprintf(stderr, "QRFS: inode_table_start=%u blocks=%u, data_region_start=%u\n",
            ctx.inode_table_start, ctx.inode_table_blocks, ctx.data_region_start);
    fprintf(stderr, "QRFS: root_inode=%u size=%u links=%u uid=%u gid=%u\n",
            ctx.root_inode_number, ctx.root_size, ctx.root_links, ctx.root_uid, ctx.root_gid);

    // **Minimal fuse3**: programa, opciones, mountpoint al final
    char *fuse_argv[] = { "qrfs", "-f", mount_abs };
    int   fuse_argc   = (int)(sizeof(fuse_argv)/sizeof(fuse_argv[0]));

    for (int i = 0; i < fuse_argc; ++i) {
        fprintf(stderr, "fuse_argv[%d] = '%s'\n", i, fuse_argv[i]);
    }
    fprintf(stderr, "backend_folder = '%s'\n", ctx.folder);

    // 4) Lanzar FUSE con el ctx lleno
    int ret = fuse_main(fuse_argc, fuse_argv, &qrfs_ops, &ctx);

    free(ctx.folder);
    return ret;
}


