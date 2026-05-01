#include "fat32.h"
#include "../drivers/ata.h"

static struct fat32_bpb bpb;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;

// Simple memory management for sector buffers
static uint8_t sector_buffer[512];

int fat32_init() {
    // Read the MBR or the first sector into the temporary buffer first to avoid overflow
    if (ata_read_sectors(0, 1, sector_buffer) != 0) return -1;
    
    // Copy only the BPB part to our struct
    uint8_t* src = sector_buffer;
    uint8_t* dest = (uint8_t*)&bpb;
    for(uint32_t i = 0; i < sizeof(struct fat32_bpb); i++) dest[i] = src[i];

    // Check if it's actually FAT32
    if (bpb.boot_signature != 0x29) {
        // ... (Optional: MBR parsing could be added here)
    }

    fat_start_sector = bpb.reserved_sectors;
    data_start_sector = bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat_long);

    return 0;
}

static uint32_t get_sector_for_cluster(uint32_t cluster) {
    return data_start_sector + (cluster - 2) * bpb.sectors_per_cluster;
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

int fat32_read_file(const char* path, uint8_t* buffer) {
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

        uint32_t sector = get_sector_for_cluster(current_cluster);
        if (ata_read_sectors(sector, 1, sector_buffer) != 0) return -1;

        struct fat32_directory_entry* entries = (struct fat32_directory_entry*)sector_buffer;
        int found = 0;
        for (int j = 0; j < 16; j++) {
            int match = 1;
            for (int k = 0; k < 11; k++) {
                if (entries[j].name[k] != fat_name[k]) { match = 0; break; }
            }
            if (match) {
                current_cluster = entries[j].cluster_low | (entries[j].cluster_high << 16);
                if (*p == 0) { // Last component, must be a file
                    uint32_t size = entries[j].size;
                    uint32_t sectors_to_read = (size + 511) / 512;
                    return ata_read_sectors(get_sector_for_cluster(current_cluster), sectors_to_read, buffer);
                }
                found = 1;
                break;
            }
        }
        if (!found) return -1;
    }

    return -1;
}
