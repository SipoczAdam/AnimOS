// Saját típusok definiálása
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;

extern uint8_t wallpaper_data[];

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t reserved;
};

#pragma pack(push, 1)
struct bmp_file_header {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct bmp_info_header {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

void draw_pixel(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (x >= fb->framebuffer_width || y >= fb->framebuffer_height) return;

    if (fb->framebuffer_bpp == 32) {
        uint32_t* screen = (uint32_t*)fb->framebuffer_addr;
        uint32_t pitch = fb->framebuffer_pitch / 4;
        screen[y * pitch + x] = color;
    } else if (fb->framebuffer_bpp == 24) {
        uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
        uint32_t offset = y * fb->framebuffer_pitch + x * 3;
        screen[offset] = color & 0xFF;
        screen[offset + 1] = (color >> 8) & 0xFF;
        screen[offset + 2] = (color >> 16) & 0xFF;
    }
}

void kernel_main(uint64_t multiboot_addr) {
    struct multiboot_tag_framebuffer* fb = 0;

    // Keresés előtt egy fix fehér pont rajzolása (Debug)
    // VirtualBox VBoxSVGA framebuffer címe gyakran 0xE0000000 környékén van
    // Ezzel teszteljük, hogy egyáltalán eljutunk-e ide.
    uint32_t* debug_ptr = (uint32_t*)0xE0000000;
    for(int i = 0; i < 100; i++) debug_ptr[i] = 0xFFFFFF;

    struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8);
         tag->type != 0;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == 8) {
            fb = (struct multiboot_tag_framebuffer*)tag;
        }
    }

    if (fb) {
        // Ha megtaláltuk, töröljük a debug pontot és rajzoljuk a háttérképet
        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
                draw_pixel(x, y, 0x000000, fb);
            }
        }

        uint8_t* bmp_data = wallpaper_data;
        struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data;
        struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));

        if (bfh->bfType == 0x4D42) {
            uint8_t* pixels = bmp_data + bfh->bfOffBits;
            int32_t bmp_w = bih->biWidth;
            int32_t bmp_h = bih->biHeight;
            int32_t abs_bmp_h = bmp_h < 0 ? -bmp_h : bmp_h;
            uint32_t bpp = bih->biBitCount;
            uint32_t bytes_per_pixel = bpp / 8;
            uint32_t row_size = (bmp_w * bytes_per_pixel + 3) & ~3;

            for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
                for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
                    int32_t src_x = (x * bmp_w) / fb->framebuffer_width;
                    int32_t src_y_raw = (y * abs_bmp_h) / fb->framebuffer_height;
                    int32_t src_y = (bmp_h > 0) ? (abs_bmp_h - 1 - src_y_raw) : src_y_raw;

                    uint8_t* p = pixels + (src_y * row_size) + (src_x * bytes_per_pixel);
                    uint32_t color = (p[2] << 16) | (p[1] << 8) | p[0];
                    draw_pixel(x, y, color, fb);
                }
            }
        }
    } else {
        // Ha nincs fb, legalább vga-n írjunk valamit
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        const char* msg = "DEBUG: Kernel Started, No FB";
        for(int i = 0; msg[i]; i++) vga[i] = (uint16_t)msg[i] | 0x2F00;
    }

    while(1) {
        __asm__ volatile("hlt");
    }
}
