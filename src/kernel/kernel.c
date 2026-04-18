// Saját típusok definiálása
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;

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

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[1];
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
    uint32_t* screen = (uint32_t*)fb->framebuffer_addr;
    uint32_t pitch = fb->framebuffer_pitch / 4;
    screen[y * pitch + x] = color;
}

void kernel_main(uint64_t multiboot_addr) {
    struct multiboot_tag_framebuffer* fb = 0;
    struct multiboot_tag_module* wallpaper_mod = 0;

    if (multiboot_addr & 7) return;

    struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8);
         tag->type != 0;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == 8) {
            fb = (struct multiboot_tag_framebuffer*)tag;
        } else if (tag->type == 3) {
            wallpaper_mod = (struct multiboot_tag_module*)tag;
        }
    }

    if (fb) {
        if (wallpaper_mod) {
            uint8_t* bmp_data = (uint8_t*)(uint64_t)wallpaper_mod->mod_start;
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

                uint32_t screen_w = fb->framebuffer_width;
                uint32_t screen_h = fb->framebuffer_height;

                // Végig megyünk a KÉPERNYŐ minden egyes pixelén
                for (uint32_t y = 0; y < screen_h; y++) {
                    for (uint32_t x = 0; x < screen_w; x++) {
                        // Kiszámoljuk, hogy a képernyő (x,y) koordinátája 
                        // hol van a forrás BMP-ben (arányosítás)
                        int32_t src_x = (x * bmp_w) / screen_w;
                        int32_t src_y_raw = (y * abs_bmp_h) / screen_h;

                        // BMP függőleges tükrözés kezelése
                        int32_t src_y = (bmp_h > 0) ? (abs_bmp_h - 1 - src_y_raw) : src_y_raw;

                        uint8_t* pixel_addr = pixels + (src_y * row_size) + (src_x * bytes_per_pixel);
                        
                        uint8_t b = pixel_addr[0];
                        uint8_t g = pixel_addr[1];
                        uint8_t r = pixel_addr[2];
                        
                        uint32_t color = (r << 16) | (g << 8) | b;
                        draw_pixel(x, y, color, fb);
                    }
                }
            }
        } else {
            // Ha nincs kép, marad a kék háttér
            for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
                for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
                    draw_pixel(x, y, 0x000033, fb);
                }
            }
        }
    }

    while(1) {
        __asm__ volatile("hlt");
    }
}
