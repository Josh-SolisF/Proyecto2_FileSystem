#ifndef BITMAPS_H
#define BITMAPS_H
#include "fs_basic.h"

int  allocate_inode(void);
void free_inode(int inode_id);
int  allocate_block(void);
void free_block(int block_num);

