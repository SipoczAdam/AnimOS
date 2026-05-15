#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      uint8_t;
typedef char               int8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;
typedef unsigned long      size_t;

struct multiboot_tag { uint32_t type; uint32_t size; };
struct multiboot_tag_framebuffer {
    uint32_t type; uint32_t size; uint64_t framebuffer_addr; uint32_t framebuffer_pitch;
    uint32_t framebuffer_width; uint32_t framebuffer_height; uint8_t framebuffer_bpp;
    uint8_t framebuffer_type; uint16_t reserved;
};

#endif
