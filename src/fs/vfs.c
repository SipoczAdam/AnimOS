#include "vfs.h"
#include "fat32.h"

extern int memcmp_custom(const void* s1, const void* s2, uint32_t n);

int vfs_init() {
    return fat32_init();
}

int vfs_read_file(const char* path, uint8_t* buffer) {
    // Basic "Sysroot:/" mapping
    if (memcmp_custom(path, "Sysroot:/", 9) == 0) {
        const char* internal_path = path + 9;
        return fat32_read_file(internal_path, buffer);
    }
    return -1;
}
