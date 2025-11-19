#ifndef FS_UTILS_H
#define FS_UTILS_H
#include "fs_basic.h"
#include <time.h>

void initialize_superblock(void);
void now_timespec(struct timespec *ts);
void u32le_write(u32 v, unsigned char *p);
u32 ceil_div(u32 a, u32 b);

#endif