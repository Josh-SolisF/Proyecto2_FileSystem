
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse_functions.h"


int fsck_qrfs(const char *folder);
int mkfs(int argc, char **argv);
int mount_qrfs(argc, argv);

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--fsck") == 0) {
        return fsck_qrfs(argv[2]);
    }
    if (argc > 1 && strcmp(argv[1], "--mkfs") == 0) {
        return mkfs(argc, argv);
    }
    if (argc > 1 && strcmp(argv[1], "--mount") == 0) {
        return mount_qrfs(argc,  **argv) ;
    }

    fprintf(stderr, "Uso: %s --mkfs <folder> | --fsck <folder> | --mount <backend_folder> <mount_point>\n", argv[0]);
    return 1;
}

/*
 *
*
gcc -Wall -Wextra -D_FILE_OFFSET_BITS=64 `pkg-config fuse3 --cflags` \
  -o programa \
  bitmaps.c block.c dir.c fs_utils.c inode.c superblock.c mkfs.c main.c fuse_functions.c \
  `pkg-config fuse3 --libs`

 *
 **/
