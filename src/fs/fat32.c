#include "fat32.h"
#include "../drivers/ata.h"

static struct fat32_bpb bpb;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint8_t current_drive = 0;

static uint8_t sector_buffer[512] __attribute__((aligned(16)));

static int fat32_init_drive(uint8_t drive) {
    if (ata_read_sectors(drive, 0, 1, sector_buffer) != 0) return -1;
    
    // Check if sector 0 is a BPB or an MBR
    // FAT32 BPB has 0x28 or 0x29 at offset 66
    int is_bpb = (sector_buffer[66] == 0x28 || sector_buffer[66] == 0x29);
    uint32_t p1_lba = 0;

    if (!is_bpb && sector_buffer[510] == 0x55 && sector_buffer[511] == 0xAA) {
        p1_lba = *(uint32_t*)&sector_buffer[454]; // Corrected LBA offset
        if (p1_lba != 0) {
            if (ata_read_sectors(drive, p1_lba, 1, sector_buffer) != 0) return -1;
            is_bpb = (sector_buffer[66] == 0x28 || sector_buffer[66] == 0x29);
        }
    }

    if (!is_bpb) return -1;

    uint8_t* src = sector_buffer;
    uint8_t* dest = (uint8_t*)&bpb;
    for(uint32_t i = 0; i < sizeof(struct fat32_bpb); i++) dest[i] = src[i];

    fat_start_sector = p1_lba + bpb.reserved_sectors;
    data_start_sector = fat_start_sector + (bpb.fat_count * bpb.sectors_per_fat_long);
    current_drive = drive;
    
    return 0;
}

int fat32_init() {
    if (fat32_init_drive(0) == 0) return 0;
    if (fat32_init_drive(1) == 0) return 0;
    return -1;
}

uint8_t fat32_get_current_drive() {
    return current_drive;
}

static uint32_t get_sector_for_cluster(uint32_t cluster) {
    return data_start_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t get_next_cluster(uint32_t cluster) {
    uint32_t fat_sector = fat_start_sector + (cluster * 4) / 512;
    uint32_t fat_offset = (cluster * 4) % 512;
    
    static uint8_t fat_buffer[512] __attribute__((aligned(16)));
    if (ata_read_sectors(current_drive, fat_sector, 1, fat_buffer) != 0) {
        return 0x0FFFFFFF;
    }
    
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

#pragma pack(push, 1)
struct fat32_lfn_entry {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attributes;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
};
#pragma pack(pop)

static int find_entry(const char* path, struct fat32_directory_entry* out_entry) {
    uint32_t current_cluster = bpb.root_cluster;
    const char* p = path;
    if (*p == '/') p++;

    int component_depth = 0;
    while (*p) {
        char component[256];
        int i = 0;
        while (*p && *p != '/' && i < 255) component[i++] = *p++;
        component[i] = 0;
        if (*p == '/') p++;
        
        component_depth++;

        char fat_name[11];
        filename_to_fat(component, fat_name);

        int found_in_path_step = 0;
        uint32_t dir_cluster = current_cluster;
        if (dir_cluster == 0) dir_cluster = bpb.root_cluster;
        
        char lfn_buffer[256];
        lfn_buffer[0] = 0;

        while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8) {
            for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
                uint32_t sector = get_sector_for_cluster(dir_cluster) + s;
                
                if (ata_read_sectors(current_drive, sector, 1, sector_buffer) != 0) return -1;

                for (int j = 0; j < 16; j++) {
                    uint8_t* entry_ptr = sector_buffer + (j * 32);
                    if (entry_ptr[0] == 0) goto cluster_done;
                    
                    if (entry_ptr[0] == 0xE5) { lfn_buffer[0] = 0; continue; }

                    if (entry_ptr[11] == 0x0F) {
                        struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)entry_ptr;
                        int sequence = (lfn->order & 0x3F);
                        if (sequence > 0 && sequence <= 20) {
                            int index = (sequence - 1) * 13;
                            uint16_t* n1 = lfn->name1; uint16_t* n2 = lfn->name2; uint16_t* n3 = lfn->name3;
                            for(int k=0; k<5; k++) lfn_buffer[index + k] = (n1[k] == 0 || n1[k] == 0xFFFF) ? 0 : (char)(n1[k] & 0xFF);
                            for(int k=0; k<6; k++) lfn_buffer[index + 5 + k] = (n2[k] == 0 || n2[k] == 0xFFFF) ? 0 : (char)(n2[k] & 0xFF);
                            for(int k=0; k<2; k++) lfn_buffer[index + 11 + k] = (n3[k] == 0 || n3[k] == 0xFFFF) ? 0 : (char)(n3[k] & 0xFF);
                            if (lfn->order & 0x40) lfn_buffer[index + 13] = 0; // Ensure some termination
                        }
                        continue;
                    }

                    struct fat32_directory_entry* entry = (struct fat32_directory_entry*)entry_ptr;
                    if (entry->attributes & 0x08) { lfn_buffer[0] = 0; continue; }

                    int match = 0;
                    if (lfn_buffer[0] != 0) {
                        match = 1;
                        for (int k = 0; ; k++) {
                            char c1 = component[k]; if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
                            char c2 = lfn_buffer[k]; if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
                            if (c1 != c2) { match = 0; break; }
                            if (c1 == 0) break;
                        }
                    }

                    if (!match) {
                        match = 1;
                        for (int k = 0; k < 8; k++) {
                            if (fat_name[k] == ' ') break;
                            if (entry->name[k] != fat_name[k]) { match = 0; break; }
                        }
                    }

                    if (match) {
                        current_cluster = entry->cluster_low | (entry->cluster_high << 16);
                        if (current_cluster == 0) current_cluster = bpb.root_cluster;
                        
                        if (*p == 0) { if (out_entry) *out_entry = *entry; return 0; }
                        found_in_path_step = 1;
                        // Reset LFN for next component
                        for(int k=0; k<255; k++) lfn_buffer[k] = 0;
                        goto next_component;
                    }
                    for(int k=0; k<255; k++) lfn_buffer[k] = 0;
                }
            }
            dir_cluster = get_next_cluster(dir_cluster);
        }
        
        cluster_done:
        next_component:
        if (!found_in_path_step) {
            return -1;
        }
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
            
            // Allow kernel to refresh UI (cursor) during large file reads
            extern void kernel_ui_refresh_simple();
            extern void kernel_ui_refresh_scaling();
            extern int is_system_busy;
            static uint32_t refresh_counter = 0;
            if (refresh_counter++ % 32 == 0) {
                if (is_system_busy) kernel_ui_refresh_scaling();
                else kernel_ui_refresh_simple();
            }

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

int fat32_list_dir(const char* path, char* buffer, uint32_t max_size) {
    struct fat32_directory_entry entry;
    uint32_t dir_cluster;

    if (path[0] == '/' && path[1] == 0) {
        dir_cluster = bpb.root_cluster;
    } else {
        if (find_entry(path, &entry) != 0) return -1;
        if (!(entry.attributes & 0x10)) return -1; // Not a directory
        dir_cluster = entry.cluster_low | (entry.cluster_high << 16);
        if (dir_cluster == 0) dir_cluster = bpb.root_cluster;
    }

    uint32_t buffer_offset = 0;
    char lfn_buffer[256];
    for (int k = 0; k < 256; k++) lfn_buffer[k] = 0;

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8) {
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            uint32_t sector = get_sector_for_cluster(dir_cluster) + s;
            if (ata_read_sectors(current_drive, sector, 1, sector_buffer) != 0) return buffer_offset;

            for (int j = 0; j < 16; j++) {
                uint8_t* entry_ptr = sector_buffer + (j * 32);
                if (entry_ptr[0] == 0) return buffer_offset;
                if (entry_ptr[0] == 0xE5) { for(int k=0; k<256; k++) lfn_buffer[k] = 0; continue; }

                if (entry_ptr[11] == 0x0F) {
                    struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)entry_ptr;
                    int sequence = (lfn->order & 0x3F);
                    if (sequence > 0 && sequence <= 20) {
                        int index = (sequence - 1) * 13;
                        uint16_t* n1 = lfn->name1; uint16_t* n2 = lfn->name2; uint16_t* n3 = lfn->name3;
                        for(int k=0; k<5; k++) lfn_buffer[index + k] = (n1[k] == 0 || n1[k] == 0xFFFF) ? 0 : (char)(n1[k] & 0xFF);
                        for(int k=0; k<6; k++) lfn_buffer[index + 5 + k] = (n2[k] == 0 || n2[k] == 0xFFFF) ? 0 : (char)(n2[k] & 0xFF);
                        for(int k=0; k<2; k++) lfn_buffer[index + 11 + k] = (n3[k] == 0 || n3[k] == 0xFFFF) ? 0 : (char)(n3[k] & 0xFF);
                        if (lfn->order & 0x40) lfn_buffer[index + 13] = 0;
                    }
                    continue;
                }

                struct fat32_directory_entry* d_entry = (struct fat32_directory_entry*)entry_ptr;
                if (d_entry->attributes & 0x08) { for(int k=0; k<256; k++) lfn_buffer[k] = 0; continue; }

                char name[256];
                if (lfn_buffer[0] != 0) {
                    int k = 0;
                    while (lfn_buffer[k] && k < 255) { name[k] = lfn_buffer[k]; k++; }
                    name[k] = 0;
                } else {
                    int k, l = 0;
                    for (k = 0; k < 8 && d_entry->name[k] != ' '; k++) name[l++] = d_entry->name[k];
                    if (d_entry->ext[0] != ' ') {
                        name[l++] = '.';
                        for (k = 0; k < 3 && d_entry->ext[k] != ' '; k++) name[l++] = d_entry->ext[k];
                    }
                    name[l] = 0;
                }
                
                if (!(name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))) {
                    int name_len = 0;
                    while (name[name_len]) name_len++;
                    if (buffer_offset + name_len + 1 < max_size) {
                        for (int k = 0; k < name_len; k++) buffer[buffer_offset++] = name[k];
                        buffer[buffer_offset++] = '\n';
                    } else {
                        return buffer_offset;
                    }
                }

                for(int k=0; k<256; k++) lfn_buffer[k] = 0;
            }
        }
        dir_cluster = get_next_cluster(dir_cluster);
    }
    return buffer_offset;
}

