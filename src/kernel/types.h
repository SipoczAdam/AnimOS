#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      uint8_t;
typedef char               int8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;
typedef unsigned long      size_t;
typedef unsigned long long uintptr_t;

struct multiboot_tag { uint32_t type; uint32_t size; };
struct multiboot_tag_framebuffer {
    uint32_t type; uint32_t size; uint64_t framebuffer_addr; uint32_t framebuffer_pitch;
    uint32_t framebuffer_width; uint32_t framebuffer_height; uint8_t framebuffer_bpp;
    uint8_t framebuffer_type; uint16_t reserved;
};

struct multiboot_tag_basic_meminfo {
    uint32_t type; uint32_t size; uint32_t mem_lower; uint32_t mem_upper;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[0];
} __attribute__((packed));

#endif
