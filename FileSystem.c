
#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define block_size 1024
typedef unsigned int uint ;


typedef struct superblock {
  uint version;

  //Info of file system
  uint blocksize; //size of the block
  uint total_blocks;
  uint total_inodes;

  //How to offsets the blocks

  char inode_bitmap[128];
  char data_bitmap[128];
  unsigned int root_ino;



} superblock;

typedef struct inode {
  //Identity
  uint inode_number;
  mode_t inode_mode;
  uint user_id;
  uint group_id; //
  uint links_quaintities;

  //sizes
  uint inode_size;
  struct timespec last_access_time;
  struct timespec last_modification_time;
  struct timespec metadata_last_change_time;
  uint  direct[12];
  uint indirect1;



}inode;
typedef struct dir_entry {
  unsigned int ino;
  char name[256];
} dir_entry;


superblock spblock;

void initialize_superblock(){

  memset(spblock.data_bitmap, '0', 100*sizeof(char));
  memset(spblock.inode_bitmap, '0', 100*sizeof(char));
}