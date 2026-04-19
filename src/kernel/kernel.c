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
    uint16_t reserved;
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

// Egyszerű egész számú négyzetgyökvonás az élsimításhoz
uint32_t sqrt_int(uint32_t n) {
    if (n < 2) return n;
    uint32_t x = n / 2 + 1;
    uint32_t y = (x + n / x) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (x >= fb->framebuffer_width || y >= fb->framebuffer_height) return;

    uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
    uint32_t offset = y * fb->framebuffer_pitch + x * (fb->framebuffer_bpp / 8);

    if (fb->framebuffer_bpp == 32) {
        uint32_t* p = (uint32_t*)(screen + offset);
        *p = color;
    } else if (fb->framebuffer_bpp == 24) {
        screen[offset] = color & 0xFF;
        screen[offset + 1] = (color >> 8) & 0xFF;
        screen[offset + 2] = (color >> 16) & 0xFF;
    } else if (fb->framebuffer_bpp == 16) {
        uint16_t* p = (uint16_t*)(screen + offset);
        uint16_t r = (color >> 19) & 0x1F;
        uint16_t g = (color >> 10) & 0x3F;
        uint16_t b = (color >> 3) & 0x1F;
        *p = (r << 11) | (g << 5) | b;
    }
}

uint32_t blend_colors(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;

    uint32_t rb = (bg >> 16) & 0xFF;
    uint32_t gb = (bg >> 8) & 0xFF;
    uint32_t bb = bg & 0xFF;

    uint32_t rf = (fg >> 16) & 0xFF;
    uint32_t gf = (fg >> 8) & 0xFF;
    uint32_t bf = fg & 0xFF;

    uint32_t r = (rf * alpha + rb * (255 - alpha)) / 255;
    uint32_t g = (gf * alpha + gb * (255 - alpha)) / 255;
    uint32_t b = (bf * alpha + bb * (255 - alpha)) / 255;

    return (r << 16) | (g << 8) | b;
}

uint32_t get_wallpaper_pixel(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb) {
    uint8_t* bmp_data = wallpaper_data;
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));

    if (bfh->bfType != 0x4D42) return 0;

    uint8_t* pixels = bmp_data + bfh->bfOffBits;
    int32_t bmp_w = bih->biWidth;
    int32_t bmp_h = bih->biHeight;
    int32_t abs_bmp_h = bmp_h < 0 ? -bmp_h : bmp_h;
    uint32_t bytes_per_pixel = bih->biBitCount / 8;
    uint32_t row_size = (bmp_w * bytes_per_pixel + 3) & ~3;

    int32_t src_x = (x * bmp_w) / fb->framebuffer_width;
    int32_t src_y_raw = (y * abs_bmp_h) / fb->framebuffer_height;
    int32_t src_y = (bmp_h > 0) ? (abs_bmp_h - 1 - src_y_raw) : src_y_raw;

    uint8_t* p = pixels + (src_y * row_size) + (src_x * bytes_per_pixel);
    return (p[2] << 16) | (p[1] << 8) | p[0];
}

void kernel_main(uint64_t multiboot_addr) {
    struct multiboot_tag_framebuffer* fb = 0;

    struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8);
         tag->type != 0;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == 8) {
            fb = (struct multiboot_tag_framebuffer*)tag;
        }
    }

    if (fb && fb->framebuffer_addr != 0) {
        // 1. Háttérkép
        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
                uint32_t color = get_wallpaper_pixel(x, y, fb);
                draw_pixel(x, y, color, fb);
            }
        }

        // 2. Tálca (Dock) beállításai
        uint32_t dock_h = 55; // Keskenyebb magasság
        uint32_t dock_w = (fb->framebuffer_width * 85) / 100; // 85% szélesség
        uint32_t dock_x = (fb->framebuffer_width - dock_w) / 2;
        uint32_t dock_y = fb->framebuffer_height - dock_h - 20; // 20 pixel alulról
        uint32_t radius = 25; // Hozzáigazított sugár
        uint32_t dock_color = 0xFFFFFF;
        uint8_t base_alpha = 150;

        for (uint32_t y = dock_y; y < dock_y + dock_h; y++) {
            for (uint32_t x = dock_x; x < dock_x + dock_w; x++) {
                
                uint8_t pixel_alpha = base_alpha;
                uint32_t dx = 0, dy = 0;
                int is_corner = 0;

                // Sarok koordináták meghatározása
                if (x < dock_x + radius && y < dock_y + radius) {
                    dx = (dock_x + radius) - x; dy = (dock_y + radius) - y; is_corner = 1;
                } else if (x > dock_x + dock_w - radius && y < dock_y + radius) {
                    dx = x - (dock_x + dock_w - radius); dy = (dock_y + radius) - y; is_corner = 1;
                } else if (x < dock_x + radius && y > dock_y + dock_h - radius) {
                    dx = (dock_x + radius) - x; dy = y - (dock_y + dock_h - radius); is_corner = 1;
                } else if (x > dock_x + dock_w - radius && y > dock_y + dock_h - radius) {
                    dx = x - (dock_x + dock_w - radius); dy = y - (dock_y + dock_h - radius); is_corner = 1;
                }

                if (is_corner) {
                    uint32_t dist_sq = dx*dx + dy*dy;
                    uint32_t r_sq = radius*radius;
                    if (dist_sq > r_sq) {
                        pixel_alpha = 0; // Kívül van
                    } else {
                        // Élsimítás: távolság alapú alpha csökkentés a széleken
                        uint32_t dist = sqrt_int(dist_sq);
                        if (dist > radius - 2) {
                            // Az utolsó 2 pixelnél lineárisan halványítunk
                            uint32_t edge_dist = radius - dist; // 0, 1 vagy 2
                            pixel_alpha = (base_alpha * edge_dist) / 2;
                        }
                    }
                }

                if (pixel_alpha > 0) {
                    uint32_t bg_color = get_wallpaper_pixel(x, y, fb);
                    uint32_t blended = blend_colors(bg_color, dock_color, pixel_alpha);
                    draw_pixel(x, y, blended, fb);
                }
            }
        }
    }

    while(1) {
        __asm__ volatile("hlt");
    }
}
