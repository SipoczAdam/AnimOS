#ifndef VFS_H
#define VFS_H

#include "../kernel/types.h"

int vfs_init();
uint8_t vfs_get_boot_drive();
int vfs_read_file(const char* path, uint8_t* buffer);
uint32_t vfs_get_file_size(const char* path);

#endif
