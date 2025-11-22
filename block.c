#include "block.h"
#include "fuse_functions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>


int ensure_folder(const char *folder) {
    struct stat st;
    if (stat(folder, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", folder);
    int rc = system(cmd);
    (void)rc; // ignoramos el código de retorno
    return 0;
}
//Bloque nulo
int create_zero_block(const char *folder, u32 index, u32 block_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, index);
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    unsigned char *zeros = (unsigned char*)calloc(1, block_size);
    if (!zeros) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    size_t w = fwrite(zeros, 1, block_size, fp);
    free(zeros);
    fclose(fp);
    return (w == block_size) ? 0 : -1;
}

// Escribe datos
int write_block(const char *folder, u32 index, const void *buf, u32 len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, index);
    FILE *fp = fopen(path, "r+b");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_SET);
    size_t w = fwrite(buf, 1, len, fp);
    fclose(fp);
    return (w == len) ? 0 : -1;
}

//Lee datos de un bloque


int read_block(const char *folder, u32 block_index, unsigned char *buf, u32 block_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, block_index);

    fprintf(stderr, "[read_block] path='%s'\n", path);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error abriendo bloque %u: %s (errno=%d)\n", block_index, strerror(errno), errno);
        return -1;
    }

    size_t r = fread(buf, 1, block_size, fp);
    fclose(fp);

    if (r != block_size) {
        fprintf(stderr, "Error leyendo bloque %u (bytes leídos=%zu, esperado=%u)\n",
                block_index, r, block_size);
        return -1;
    }
    return 0;
}




int read_inode_block(qrfs_ctx *ctx, u32 inode_id, unsigned char out128[128]) {
    const u32 inodes_per_block = ctx->block_size / 128;
    if (inodes_per_block == 0) return -1;
    if (inode_id >= ctx->total_inodes) return -1;

    const u32 rel_block = inode_id / inodes_per_block;
    const u32 offset    = (inode_id % inodes_per_block) * 128;
    const u32 abs_block = ctx->inode_table_start + rel_block;

    unsigned char block[4096];
    if (ctx->block_size > sizeof(block)) return -1;

    if (read_block(ctx->folder, abs_block, block, ctx->block_size) != 0) {
        return -1;
    }
    memcpy(out128, block + offset, 128);
    return 0;
}



