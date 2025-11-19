#ifndef FS_UTILS_H
#define FS_UTILS_H
#include "fs_basic.h"
#include <time.h>

void initialize_superblock(void);
static inline void now_timespec(struct timespec *ts); // puedes dejarla inline en el .h

static inline void u32le_write(u32 v, unsigned char *p);
static inline u32 ceil_div(u32 a, u32 b);

#endif
