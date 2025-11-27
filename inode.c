
#define FUSE_USE_VERSION 31

#include "fs_basic.h"
#include "fs_utils.h"
#include "fuse_functions.h"

#include "inode.h"
#include "block.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <time.h>
void init_inode(inode *node, u32 inode_id, mode_t mode, u32 size) {
    node->inode_number = inode_id;
    node->inode_mode   = mode;
    node->inode_size   = size;
    node->links_quaintities = 1;
    node->user_id = (u32)getuid();
    node->group_id = (u32)getgid();

    now_timespec(&node->last_access_time);
    node->last_modification_time    = node->last_access_time;
    node->metadata_last_change_time = node->last_access_time;

    memset(node->direct, 0, sizeof(node->direct));
    node->indirect1 = 0;
}

 void inode_serialize128(unsigned char out[128],u32 inode_number, u32 inode_mode, u32 user_id, u32 group_id,
    u32 links, u32 size,const u32 direct[12], u32 indirect1) {
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



void inode_deserialize128(const unsigned char in[128],
    u32 *inode_number, u32 *inode_mode, u32 *user_id, u32 *group_id,
    u32 *links, u32 *size, u32 direct[12], u32 *indirect1)
{
    *inode_number = u32le_read(&in[0]);
    *inode_mode   = u32le_read(&in[4]);
    *user_id      = u32le_read(&in[8]);
    *group_id     = u32le_read(&in[12]);
    *links        = u32le_read(&in[16]);
    *size         = u32le_read(&in[20]);

    for (int i = 0; i < 12; i++) {
        direct[i] = u32le_read(&in[24 + i * 4]);
    }
    *indirect1 = u32le_read(&in[72]);
}





int write_inode(qrfs_ctx *ctx, u32 inode_id, const inode *node) {
    if (!ctx || !node) return -EINVAL;

    const u32 rec_size = 128; // tamaño fijo del inodo serializado
    const u32 bs = ctx->block_size;
    const u32 inode_table_start = ctx->inode_table_start;

    // Serializar el inodo en 128 bytes
    unsigned char rec[rec_size];
    inode_serialize128(rec,
                       node->inode_number, node->inode_mode,
                       node->user_id, node->group_id,
                       node->links_quaintities, node->inode_size,
                       node->direct, node->indirect1);

    // Calcular ubicación del registro en la tabla
    u32 rec_off = inode_id * rec_size;                  // offset en bytes desde inicio de la tabla
    u32 blk_idx = inode_table_start + (rec_off / bs);   // bloque donde está el inodo
    u32 off_in_blk = rec_off % bs;                      // offset dentro del bloque

    // Leer el bloque actual
    unsigned char *blk = (unsigned char *)calloc(1, bs);
    if (!blk) return -ENOMEM;

    if (read_block(ctx->folder, blk_idx, blk, bs) != 0) {
        free(blk);
        return -EIO;
    }

    // Sobrescribir el registro del inodo en el bloque
    memcpy(blk + off_in_blk, rec, rec_size);

    // Escribir el bloque actualizado
    if (write_block(ctx->folder, blk_idx, blk, bs) != 0) {
        free(blk);
        return -EIO;
    }

    free(blk);
    return 0;
}




int read_inode(const char *folder, u32 inode_id, inode *node) {
    unsigned char buf[128];
    u32 inode_table_start = spblock.total_blocks - spblock.total_inodes; // Ajusta según tu diseño
    u32 block_index = inode_table_start + inode_id;

    if (read_block(folder, block_index, buf, sizeof(buf)) != 0) {
        return -1;
    }

    inode_deserialize128(buf,
        &node->inode_number, &node->inode_mode, &node->user_id, &node->group_id,
        &node->links_quaintities, &node->inode_size, node->direct, &node->indirect1);

    return 0;
}



