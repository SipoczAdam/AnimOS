#ifndef API_H
#define API_H

#include "types.h"

struct multiboot_tag_framebuffer;

typedef struct {
    void (*draw_pixel)(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb);
    uint32_t (*blend_colors)(uint32_t bg, uint32_t fg, uint8_t alpha);
    uint32_t (*get_wallpaper_pixel)(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb);
    void (*draw_string_scaled)(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale_pct, struct multiboot_tag_framebuffer* fb);
    uint32_t (*get_string_width_scaled)(const char* str, int scale_pct);
    void (*draw_icon_scaled)(uint32_t x, uint32_t y, uint32_t target_w, uint32_t target_h, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb);
    
    // Assets pointers
    uint8_t* close_icon;
    uint8_t* maximize_icon;
    uint8_t* minimize_icon;
    uint8_t* boot_logo;
    uint32_t* window_buffer;

    void (*draw_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, struct multiboot_tag_framebuffer* fb);
    void (*draw_rounded_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color, struct multiboot_tag_framebuffer* fb);
    void (*blit_buffer)(uint32_t* src_buffer, struct multiboot_tag_framebuffer* fb);
    void (*get_mouse_pos)(int32_t* mx, int32_t* my, uint8_t* clicked, int32_t* wheel);
    void (*yield)();
    int (*list_dir)(const char* path, char* buffer, uint32_t max_size);
    void (*set_wallpaper)(const char* name);
    uint32_t (*net_get_ip)();
    uint32_t (*net_get_subnet)();
    uint32_t (*net_get_gateway)();
    uint32_t (*net_get_dns_primary)();
    uint32_t (*net_get_dns_secondary)();
    const char* (*net_get_nic_name)();
    void (*net_get_mac)(uint8_t* mac);
    uint64_t (*net_get_last_sync_time)();
    const char* (*net_get_ntp_server)();
    const char* (*get_timezone)();
    int (*get_timezone_offset)();
    void (*set_timezone_offset)(int offset);

    // System Info
    char cpu_brand[49];
    char current_wallpaper[256];
    uint32_t ram_size_mb;
    uint32_t disk_size_gb;
    uint32_t disk_size_mb;
    int window_maximized;
} kernel_api_t;

typedef enum {
    APP_EVENT_INIT,
    APP_EVENT_TICK,
    APP_EVENT_CLICK,
    APP_EVENT_CLOSE
} app_event_t;

typedef void (*app_entry_t)(kernel_api_t* api, struct multiboot_tag_framebuffer* fb, app_event_t event);

#endif
