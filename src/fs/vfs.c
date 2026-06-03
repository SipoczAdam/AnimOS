#include "vfs.h"
#include "fat32.h"

extern int memcmp_custom(const void* s1, const void* s2, uint32_t n);

int vfs_init() {
    return fat32_init();
}

uint8_t vfs_get_boot_drive() {
    return fat32_get_current_drive();
}

int vfs_read_file(const char* path, uint8_t* buffer) {
    // Basic "Sysroot:/" mapping
    if (memcmp_custom(path, "Sysroot:/", 9) == 0) {
        const char* internal_path = path + 9;
        return fat32_read_file(internal_path, buffer);
    }
    return -1;
}

uint32_t vfs_get_file_size(const char* path) {
    if (memcmp_custom(path, "Sysroot:/", 9) == 0) {
        const char* internal_path = path + 9;
        return fat32_get_file_size(internal_path);
    }
    return 0;
}

int vfs_list_dir(const char* path, char* buffer, uint32_t max_size) {
    if (memcmp_custom(path, "Sysroot:/", 9) == 0) {
        const char* internal_path = path + 9;
        return fat32_list_dir(internal_path, buffer, max_size);
    }
    return -1;
}

uint32_t vfs_get_used_space_mb() {
    return fat32_get_used_space_mb();
}

