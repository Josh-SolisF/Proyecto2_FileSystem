
#define FUSE_USE_VERSION 31

#include "superblock.h"
#include "block.h"
#include "fs_utils.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

int write_superblock_with_offsets(
    const char *folder,
    u32 block_size,
    u32 total_blocks,
    u32 total_inodes,
    const unsigned char *inode_bitmap_128,
    const unsigned char *data_bitmap_128,
    u32 root_inode,
    u32 inode_bitmap_start, u32 inode_bitmap_blocks,
    u32 data_bitmap_start,  u32 data_bitmap_blocks,
    u32 inode_table_start,  u32 inode_table_blocks,
    u32 data_region_start
) {
    unsigned char *buf = (unsigned char*)calloc(1, block_size);
    if (!buf) { errno = ENOMEM; return -1; }

    buf[0]='Q'; buf[1]='R'; buf[2]='F'; buf[3]='S';
    u32le_write(1,              &buf[4]);   // version
    u32le_write(block_size,     &buf[8]);
    u32le_write(total_blocks,   &buf[12]);
    u32le_write(total_inodes,   &buf[16]);

    memcpy(&buf[20],  inode_bitmap_128, 128);
    memcpy(&buf[148], data_bitmap_128,  128);

    u32le_write(root_inode, &buf[276]);
    u32le_write(inode_bitmap_start,  &buf[280]);
    u32le_write(inode_bitmap_blocks, &buf[284]);
    u32le_write(data_bitmap_start,   &buf[288]);
    u32le_write(data_bitmap_blocks,  &buf[292]);
    u32le_write(inode_table_start,   &buf[296]);
    u32le_write(inode_table_blocks,  &buf[300]);
    u32le_write(data_region_start,   &buf[304]);

    int rc = write_block(folder, 0, buf, block_size);
    free(buf);
    return rc;
}


int read_superblock(const char *folder, u32 block_size,
                    u32 *version, u32 *total_blocks, u32 *total_inodes,
                    unsigned char inode_bitmap[128],
                    unsigned char data_bitmap[128],
                    u32 *root_inode,
                    u32 *inode_bitmap_start, u32 *inode_bitmap_blocks,
                    u32 *data_bitmap_start,  u32 *data_bitmap_blocks,
                    u32 *inode_table_start,  u32 *inode_table_blocks,
                    u32 *data_region_start)
{
    // Construir ruta del bloque 0
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, 0);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error abriendo superbloque: %s\n", strerror(errno));
        return -1;
    }

    unsigned char *buf = (unsigned char*)malloc(block_size);
    if (!buf) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    size_t r = fread(buf, 1, block_size, fp);
    fclose(fp);
    if (r != block_size) {
        free(buf);
        fprintf(stderr, "Error leyendo superbloque (bytes leídos=%zu)\n", r);
        return -1;
    }

    // Validar magic
    if (buf[0] != 'Q' || buf[1] != 'R' || buf[2] != 'F' || buf[3] != 'S') {
        free(buf);
        fprintf(stderr, "Magic inválido: no es QRFS\n");
        return -1;
    }

    *version       = u32le_read(&buf[4]);
    u32 blk_size   = u32le_read(&buf[8]);
    *total_blocks  = u32le_read(&buf[12]);
    *total_inodes  = u32le_read(&buf[16]);

    if (blk_size != block_size) {
        fprintf(stderr, "Advertencia: block_size esperado=%u, en SB=%u\n", block_size, blk_size);
    }

    memcpy(inode_bitmap, &buf[20], 128);
    memcpy(data_bitmap,  &buf[148], 128);

    *root_inode          = u32le_read(&buf[276]);
    *inode_bitmap_start  = u32le_read(&buf[280]);
    *inode_bitmap_blocks = u32le_read(&buf[284]);
    *data_bitmap_start   = u32le_read(&buf[288]);
    *data_bitmap_blocks  = u32le_read(&buf[292]);
    *inode_table_start   = u32le_read(&buf[296]);
    *inode_table_blocks  = u32le_read(&buf[300]);
    *data_region_start   = u32le_read(&buf[304]);

    free(buf);
    return 0;
}
