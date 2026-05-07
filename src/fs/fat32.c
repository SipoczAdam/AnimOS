#include "fat32.h"
#include "../drivers/ata.h"

static struct fat32_bpb bpb;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint8_t current_drive = 0;

static uint8_t sector_buffer[512];

static int fat32_init_drive(uint8_t drive) {
    if (ata_read_sectors(drive, 0, 1, sector_buffer) != 0) return -1;
    
    uint8_t* src = sector_buffer;
    uint8_t* dest = (uint8_t*)&bpb;
    for(uint32_t i = 0; i < sizeof(struct fat32_bpb); i++) dest[i] = src[i];

    if (bpb.boot_signature != 0x29 && bpb.boot_signature != 0x28) {
        if (sector_buffer[510] == 0x55 && sector_buffer[511] == 0xAA) {
            uint32_t p1_lba = *(uint32_t*)&sector_buffer[454];
            if (p1_lba != 0) {
                if (ata_read_sectors(drive, p1_lba, 1, sector_buffer) != 0) return -1;
                for(uint32_t i = 0; i < sizeof(struct fat32_bpb); i++) dest[i] = sector_buffer[i];
                if (bpb.boot_signature != 0x29 && bpb.boot_signature != 0x28) return -1;

                fat_start_sector = p1_lba + bpb.reserved_sectors;
                data_start_sector = p1_lba + bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat_long);
                current_drive = drive;
                return 0;
            }
        }
        return -1;
    }

    fat_start_sector = bpb.reserved_sectors;
    data_start_sector = bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat_long);
    current_drive = drive;
    return 0;
}

int fat32_init() {
    if (fat32_init_drive(0) == 0) return 0;
    if (fat32_init_drive(1) == 0) return 0;
    return -1;
}

static uint32_t get_sector_for_cluster(uint32_t cluster) {
    return data_start_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t get_next_cluster(uint32_t cluster) {
    uint32_t fat_sector = fat_start_sector + (cluster * 4) / 512;
    uint32_t fat_offset = (cluster * 4) % 512;
    
    static uint8_t fat_buffer[512];
    if (ata_read_sectors(current_drive, fat_sector, 1, fat_buffer) != 0) return 0x0FFFFFFF;
    
    return (*(uint32_t*)&fat_buffer[fat_offset]) & 0x0FFFFFFF;
}

static void filename_to_fat(const char* input, char* output) {
    int i, j;
    for (i = 0; i < 11; i++) output[i] = ' ';
    for (i = 0, j = 0; input[i] != '.' && input[i] != 0 && j < 8; i++, j++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        output[j] = c;
    }
    if (input[i] == '.') {
        i++;
        for (j = 8; input[i] != 0 && j < 11; i++, j++) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[j] = c;
        }
    }
}

static int find_entry(const char* path, struct fat32_directory_entry* out_entry) {
    uint32_t current_cluster = bpb.root_cluster;
    const char* p = path;
    if (*p == '/') p++;

    while (*p) {
        char component[13];
        int i = 0;
        while (*p && *p != '/' && i < 12) component[i++] = *p++;
        component[i] = 0;
        if (*p == '/') p++;

        char fat_name[11];
        filename_to_fat(component, fat_name);

        int found_in_path_step = 0;
        uint32_t dir_cluster = current_cluster;
        
        while (dir_cluster < 0x0FFFFFF8) {
            for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
                uint32_t sector = get_sector_for_cluster(dir_cluster) + s;
                if (ata_read_sectors(current_drive, sector, 1, sector_buffer) != 0) return -1;

                struct fat32_directory_entry* entries = (struct fat32_directory_entry*)sector_buffer;
                for (int j = 0; j < 16; j++) {
                    if (entries[j].name[0] == 0) return -1; // No more entries in this directory
                    if (entries[j].name[0] == 0xE5) continue; // Deleted
                    
                    int match = 1;
                    for (int k = 0; k < 11; k++) {
                        if (entries[j].name[k] != fat_name[k]) { match = 0; break; }
                    }
                    if (match) {
                        current_cluster = entries[j].cluster_low | (entries[j].cluster_high << 16);
                        if (*p == 0) {
                            if (out_entry) *out_entry = entries[j];
                            return 0;
                        }
                        found_in_path_step = 1;
                        goto next_component;
                    }
                }
            }
            dir_cluster = get_next_cluster(dir_cluster);
        }
        
        next_component:
        if (!found_in_path_step) return -1;
    }
    return -1;
}

int fat32_read_file(const char* path, uint8_t* buffer) {
    struct fat32_directory_entry entry;
    if (find_entry(path, &entry) != 0) return -1;

    uint32_t cluster = entry.cluster_low | (entry.cluster_high << 16);
    uint32_t size = entry.size;
    uint32_t bytes_read = 0;

    while (cluster < 0x0FFFFFF8 && bytes_read < size) {
        uint32_t sector = get_sector_for_cluster(cluster);
        
        for (uint32_t s = 0; s < bpb.sectors_per_cluster && bytes_read < size; s++) {
            if (ata_read_sectors(current_drive, sector + s, 1, sector_buffer) != 0) return -1;
            
            uint32_t to_copy = 512;
            if (size - bytes_read < 512) to_copy = size - bytes_read;
            
            for (uint32_t i = 0; i < to_copy; i++) buffer[bytes_read + i] = sector_buffer[i];
            bytes_read += to_copy;
        }
        
        cluster = get_next_cluster(cluster);
    }

    return 0;
}

uint32_t fat32_get_file_size(const char* path) {
    struct fat32_directory_entry entry;
    if (find_entry(path, &entry) == 0) {
        return entry.size;
    }
    return 0;
}
