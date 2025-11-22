#ifndef INODE_H
#define INODE_H
#include "fs_basic.h"

void init_inode(inode *node, u32 inode_id, mode_t mode, u32 size);
void inode_serialize128(unsigned char out[128], u32 inode_number, u32 inode_mode,
                        u32 user_id, u32 group_id, u32 links, u32 size,
                        const u32 direct[12], u32 indirect1);
void inode_deserialize128(const unsigned char in[128],u32 *inode_number, u32 *inode_mode, u32 *user_id, u32 *group_id,
    u32 *links, u32 *size,u32 direct[12], u32 *indirect1);
int write_inode(const char *folder, u32 inode_id, const inode *node);
int read_inode(const char *folder, u32 inode_id, inode *node) ;
#endif