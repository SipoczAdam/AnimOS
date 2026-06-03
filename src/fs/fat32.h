#ifndef FAT32_H
#define FAT32_H

#include "../kernel/types.h"

#pragma pack(push, 1)
struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_short;
    uint8_t  media_type;
    uint16_t sectors_per_fat_short;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_long;
    uint32_t sectors_per_fat_long;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
};

struct fat32_directory_entry {
    char     name[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t cluster_high;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t cluster_low;
    uint32_t size;
};
#pragma pack(pop)

int fat32_init();
uint8_t fat32_get_current_drive();
int fat32_read_file(const char* path, uint8_t* buffer);
uint32_t fat32_get_file_size(const char* path);
int fat32_list_dir(const char* path, char* buffer, uint32_t max_size);
uint32_t fat32_get_used_space_mb();

#endif
