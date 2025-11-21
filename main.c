
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse_functions.h"


int fsck_qrfs(const char *folder);
int mkfs(int argc, char **argv);

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--fsck") == 0) {
        if (fsck_qrfs("/dev/qrfs") != 0) {
            fprintf(stderr, "Error en fsck_qrfs\n");
            return 1;
        }
        argc--;
        argv++;
    }

    if (argc > 1 && strcmp(argv[1], "--mkfs") == 0) {
        if (mkfs(argc, argv) != 0) {
            fprintf(stderr, "Error en mkfs\n");
            return 1;
        }
        argc--;
        argv++;
    }

    return fuse_main(argc, argv, &qrfs_ops, NULL);
}
