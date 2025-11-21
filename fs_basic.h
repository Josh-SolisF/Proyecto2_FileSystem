
#ifndef FS_BASIC_H
#define FS_BASIC_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

typedef uint32_t u32;

extern const u32 block_size;              // aqui hay que cambiarlo conforme lo que dijo el profe de los qr
extern const u32 DEFAULT_TOTAL_BLOCKS;
extern const u32 DEFAULT_TOTAL_INODES;

typedef struct superblock {
    u32 version;
    u32 blocksize;
    u32 total_blocks;
    u32 total_inodes;
    char inode_bitmap[128];
    char data_bitmap[128];
    unsigned int root_inode;
} superblock;

typedef struct inode {
    u32   inode_number;
    mode_t inode_mode;
    u32   user_id;
    u32   group_id;
    u32   links_quaintities;
    u32   inode_size;
    struct timespec last_access_time;
    struct timespec last_modification_time;
    struct timespec metadata_last_change_time;
    u32   direct[12];
    u32   indirect1;
} inode;

typedef struct dir_entry {
    unsigned int inode_id;
    char name[256];
} dir_entry;

//global
extern superblock spblock;

#endif
