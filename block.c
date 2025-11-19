#include "block.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* Crea la carpeta si no existe */
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

/* Crea un bloque vacío (relleno con ceros) */
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

/* Escribe datos en un bloque existente */
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

/* Lee datos desde un bloque */
int read_block(const char *folder, u32 index, void *buf, u32 len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/block_%04u.png", folder, index);
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_SET);
    size_t r = fread(buf, 1, len, fp);
    fclose(fp);
    return (r == len) ? 0 : -1;
}

