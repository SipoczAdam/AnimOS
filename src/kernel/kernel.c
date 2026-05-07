#include "types.h"
#include "io.h"
#include "../drivers/pci.h"
#include "../drivers/e1000.h"
#include "../net/net.h"
#include "../fs/vfs.h"

struct multiboot_tag { uint32_t type; uint32_t size; };
struct multiboot_tag_framebuffer {
    uint32_t type; uint32_t size; uint64_t framebuffer_addr; uint32_t framebuffer_pitch;
    uint32_t framebuffer_width; uint32_t framebuffer_height; uint8_t framebuffer_bpp;
    uint8_t framebuffer_type; uint16_t reserved;
};

struct idt_entry {
    uint16_t base_low; uint16_t selector; uint8_t ist; uint8_t flags;
    uint16_t base_mid; uint32_t base_high; uint32_t reserved;
} __attribute__((packed));
struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));

extern uint8_t cursor_data_embedded[];
extern uint8_t wallpaper_data_embedded[];
extern uint8_t power_icon_data_embedded[];
extern uint8_t arial_font_data_embedded[];
extern uint8_t arial_font_xml_data_embedded[];
extern uint8_t offline_icon_data_embedded[];
extern uint8_t online_icon_data_embedded[];

uint8_t* wallpaper_data = 0;
uint8_t* cursor_data = 0;
uint8_t* power_icon_data = 0;
uint8_t* arial_font_data = 0;
uint8_t* arial_font_xml_data = 0;
uint8_t* offline_icon_data = 0;
uint8_t* online_icon_data = 0;

uint32_t screen_w = 1024;
uint32_t screen_h = 768;
int32_t cursor_w = 32;
int32_t cursor_h = 32;
int net_status = 0; // 0: None, 1: Found, 2: Sent, 3: Synced

void msleep(uint32_t ms);
void init_wallpaper_info();

int memcmp_custom(const void* s1, const void* s2, uint32_t n) {
    const uint8_t *p1 = s1, *p2 = s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

void reboot() {
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    // Triple fault fallback
    static struct idt_ptr zero_idtp = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(zero_idtp));
    __asm__ volatile("int $3");
    while(1) __asm__ volatile("hlt");
}

void shutdown(uint64_t multiboot_addr) {
    // QEMU/Bochs
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    // VirtualBox
    outw(0x4004, 0x3400);

    // ACPI próbálkozás (Multiboot 2 tag-ek alapján)
    struct multiboot_tag* tag;
    uint8_t* rsdp = 0;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8); tag->type != 0; tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 14 || tag->type == 15) { // RSDP v1 vagy v2
            rsdp = (uint8_t*)tag + 8;
            break;
        }
    }

    if (rsdp) {
        uint32_t rsdt_addr = *(uint32_t*)(rsdp + 16);
        uint8_t* rsdt = (uint8_t*)(uint64_t)rsdt_addr;
        if (memcmp_custom(rsdt, "RSDT", 4) == 0) {
            uint32_t entries = (*(uint32_t*)(rsdt + 4) - 36) / 4;
            uint32_t* table_ptr = (uint32_t*)(rsdt + 36);
            for (uint32_t i = 0; i < entries; i++) {
                uint8_t* table = (uint8_t*)(uint64_t)table_ptr[i];
                if (memcmp_custom(table, "FACP", 4) == 0) {
                    uint32_t pm1a_cnt = *(uint32_t*)(table + 64);
                    outw((uint16_t)pm1a_cnt, 0x2000 | (5 << 10)); 
                    msleep(100);
                    outw((uint16_t)pm1a_cnt, 0x3400); 
                }
            }
        }
    }

    while(1) __asm__ volatile("hlt");
}

void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xF9); outb(0xA1, 0xEF);
}

struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_mid = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel; idt[num].ist = 0; idt[num].flags = flags; idt[num].reserved = 0;
}

extern void idt_load(struct idt_ptr* ptr);
extern void isr_mouse_stub(void);
extern void isr_keyboard_stub(void);

volatile int32_t mouse_x = 512, mouse_y = 384;
volatile uint8_t mouse_left_button = 0;
volatile uint8_t mouse_clicked = 0;
volatile int power_menu_open = 0;
volatile int power_menu_progress = 0;

void keyboard_handler_main() {
    static int e0_received = 0;
    uint8_t scancode = inb(0x60);
    if (scancode == 0xE0) {
        e0_received = 1;
    } else {
        if (scancode == 0x5B || scancode == 0x5C) { // Windows key Make
            power_menu_open = !power_menu_open;
        }
        e0_received = 0;
    }
    outb(0x20, 0x20);
}
uint8_t mouse_cycle = 0;
uint8_t mouse_byte[3];

void mouse_wait(uint8_t a_type) {
    uint32_t timeout = 100000;
    if (a_type == 0) { while (timeout--) { if (inb(0x64) & 1) return; } }
    else { while (timeout--) { if (!(inb(0x64) & 2)) return; } }
}

void ps2_flush() {
    uint32_t timeout = 1000;
    while (timeout-- && (inb(0x64) & 1)) inb(0x60);
}

void mouse_write(uint8_t data) {
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, data);
}

uint8_t mouse_read() {
    uint32_t timeout = 100000;
    while (timeout--) { if (inb(0x64) & 1) return inb(0x60); }
    return 0;
}

void mouse_init() {
    __asm__ volatile("cli");
    ps2_flush();
    
    mouse_wait(1); outb(0x64, 0xA8); 
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); uint8_t conf = (inb(0x60) | 2) & ~0x20;
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, conf);
    
    mouse_write(0xF6); mouse_read(); // Set Defaults
    mouse_write(0xF4); mouse_read(); // Enable Scanning
    
    ps2_flush();
    __asm__ volatile("sti");
}

void mouse_handler_main() {
    uint8_t status = inb(0x64);
    while (status & 1) {
        uint8_t data = inb(0x60);
        if (status & 0x20) {
            switch (mouse_cycle) {
                case 0:
                    if (data & 0x08) {
                        mouse_byte[0] = data;
                        mouse_cycle = 1;
                    }
                    break;
                case 1:
                    mouse_byte[1] = data;
                    mouse_cycle = 2;
                    break;
                case 2:
                    mouse_byte[2] = data;
                    mouse_cycle = 0;
                    
                    int32_t dx = (int32_t)mouse_byte[1];
                    int32_t dy = (int32_t)mouse_byte[2];
                    
                    if (mouse_byte[0] & 0x10) dx -= 256;
                    if (mouse_byte[0] & 0x20) dy -= 256;
                    
                    mouse_x += dx;
                    mouse_y -= dy;
                    
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x > (int32_t)screen_w - 5) mouse_x = screen_w - 5;
                    if (mouse_y > (int32_t)screen_h - 5) mouse_y = screen_h - 5;

                    uint8_t current_left = mouse_byte[0] & 1;
                    if (current_left && !mouse_left_button) {
                        mouse_clicked = 1;
                    }
                    mouse_left_button = current_left;
                    break;
            }
        }
        status = inb(0x64);
    }
    outb(0x20, 0x20); outb(0xA0, 0x20);
}

#pragma pack(push, 1)
struct bmp_file_header { uint16_t bfType; uint32_t bfSize; uint16_t bfReserved1, bfReserved2; uint32_t bfOffBits; };
struct bmp_info_header {
    uint32_t biSize; int32_t biWidth, biHeight; uint16_t biPlanes, biBitCount;
    uint32_t biCompression, biSizeImage; int32_t biXPelsPerMeter, biYPelsPerMeter; uint32_t biClrUsed, biClrImportant;
};
#pragma pack(pop)

uint32_t sqrt_int(uint32_t n) {
    if (n < 2) return n;
    uint32_t x = n / 2 + 1, y = (x + n / x) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (x >= fb->framebuffer_width || y >= fb->framebuffer_height) return;
    uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
    uint32_t offset = y * fb->framebuffer_pitch + x * (fb->framebuffer_bpp / 8);
    if (fb->framebuffer_bpp == 32) { *(uint32_t*)(screen + offset) = color; }
    else if (fb->framebuffer_bpp == 24) { screen[offset] = color & 0xFF; screen[offset+1] = (color>>8) & 0xFF; screen[offset+2] = (color>>16) & 0xFF; }
}

uint32_t blend_colors(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg; if (alpha == 255) return fg;
    uint32_t rb = (bg >> 16) & 0xFF, gb = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint32_t rf = (fg >> 16) & 0xFF, gf = (fg >> 8) & 0xFF, bf = fg & 0xFF;
    return (((rf * alpha + rb * (255 - alpha)) / 255) << 16) | (((gf * alpha + gb * (255 - alpha)) / 255) << 8) | ((bf * alpha + bb * (255 - alpha)) / 255);
}

uint32_t cursor_buffer[64 * 64];
uint32_t menu_area_buffer[200 * 300]; 
int32_t last_mouse_x = -1, last_mouse_y = -1;

struct wallpaper_info {
    uint8_t* pixels;
    int32_t width, height;
    uint32_t row_size;
    int32_t bpp;
};
struct wallpaper_info wall_info;

void init_wallpaper_info() {
    if (!wallpaper_data) return;
    struct bmp_file_header* bfh = (struct bmp_file_header*)wallpaper_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(wallpaper_data + sizeof(struct bmp_file_header));
    wall_info.pixels = wallpaper_data + bfh->bfOffBits;
    wall_info.width = bih->biWidth;
    wall_info.height = bih->biHeight;
    wall_info.bpp = bih->biBitCount;
    wall_info.row_size = (wall_info.width * (wall_info.bpp / 8) + 3) & ~3;
}

extern uint8_t kernel_end[];
static uint8_t* bump_ptr = 0;

void* malloc_custom(uint32_t size) {
    if (bump_ptr == 0) bump_ptr = kernel_end;
    // Align to 8 bytes
    bump_ptr = (uint8_t*)(((uint64_t)bump_ptr + 7) & ~7);
    void* ptr = bump_ptr;
    bump_ptr += size;
    return ptr;
}

uint8_t* load_asset(const char* path) {
    uint32_t size = vfs_get_file_size(path);
    if (size == 0) return 0;
    uint8_t* buffer = malloc_custom(size);
    if (vfs_read_file(path, buffer) != 0) return 0;
    return buffer;
}

uint32_t get_wallpaper_pixel_fast(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb) {
    if (!wall_info.pixels) return 0;
    int32_t abs_bmp_h = wall_info.height < 0 ? -wall_info.height : wall_info.height;
    int32_t src_y = (wall_info.height > 0) ? (abs_bmp_h - 1 - (int32_t)(y * abs_bmp_h / fb->framebuffer_height)) : (int32_t)(y * abs_bmp_h / fb->framebuffer_height);
    uint8_t* p = wall_info.pixels + (src_y * wall_info.row_size) + ((int32_t)(x * wall_info.width / fb->framebuffer_width) * (wall_info.bpp / 8));
    return (p[2] << 16) | (p[1] << 8) | p[0];
}

void draw_cursor(int32_t mx, int32_t my, struct multiboot_tag_framebuffer* fb) {
    if (mx == last_mouse_x && my == last_mouse_y) return;
    uint8_t* dib = cursor_data + 6 + 16;
    struct bmp_info_header* bih = (struct bmp_info_header*)dib;
    uint8_t* pixels = dib + bih->biSize;
    int32_t w = bih->biWidth, h = bih->biHeight / 2;
    if (last_mouse_x != -1) {
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) { draw_pixel(last_mouse_x + x, last_mouse_y + y, cursor_buffer[y * w + x], fb); }
        }
    }
    last_mouse_x = mx; last_mouse_y = my;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t px = mx + x, py = my + y;
            uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
            uint32_t offset = py * fb->framebuffer_pitch + px * (fb->framebuffer_bpp / 8);
            if (fb->framebuffer_bpp == 32) cursor_buffer[y * w + x] = *(uint32_t*)(screen + offset);
            int32_t src_y = h - 1 - y;
            uint8_t* p = pixels + (src_y * w * 4) + (x * 4);
            if (p[3] > 128) draw_pixel(px, py, (p[2] << 16) | (p[1] << 8) | p[0], fb);
        }
    }
}

void draw_icon_scaled(uint32_t x, uint32_t y, uint32_t target_w, uint32_t target_h, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb) {
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = bmp_data + bfh->bfOffBits;
    int32_t w = bih->biWidth, h = bih->biHeight, abs_h = h < 0 ? -h : h;
    uint32_t bpp = bih->biBitCount;
    uint32_t row_size = (w * (bpp / 8) + 3) & ~3;

    for (uint32_t iy = 0; iy < target_h; iy++) {
        for (uint32_t ix = 0; ix < target_w; ix++) {
            uint32_t x_start = ix * w / target_w;
            uint32_t x_end = (ix + 1) * w / target_w;
            uint32_t y_start_raw = iy * abs_h / target_h;
            uint32_t y_end_raw = (iy + 1) * abs_h / target_h;

            if (x_end <= x_start) x_end = x_start + 1;
            if (y_end_raw <= y_start_raw) y_end_raw = y_start_raw + 1;

            uint64_t r_sq_sum = 0, g_sq_sum = 0, b_sq_sum = 0, a_sum = 0;
            uint32_t count = 0;

            for (uint32_t sy_raw = y_start_raw; sy_raw < y_end_raw; sy_raw++) {
                for (uint32_t sx = x_start; sx < x_end; sx++) {
                    int32_t src_y = (h > 0) ? (abs_h - 1 - (int32_t)sy_raw) : (int32_t)sy_raw;
                    uint8_t* p = pixels + (src_y * row_size) + (sx * (bpp / 8));
                    
                    // Perceptual averaging: sum of squares for better detail retention
                    uint32_t b = p[0], g = p[1], r = p[2];
                    b_sq_sum += b * b;
                    g_sq_sum += g * g;
                    r_sq_sum += r * r;
                    
                    if (bpp == 32) a_sum += p[3]; else a_sum += 255;
                    count++;
                }
            }

            if (count == 0) continue;
            
            uint32_t r_avg = sqrt_int(r_sq_sum / count);
            uint32_t g_avg = sqrt_int(g_sq_sum / count);
            uint32_t b_avg = sqrt_int(b_sq_sum / count);
            uint32_t avg_color = (r_avg << 16) | (g_avg << 8) | b_avg;
            uint8_t avg_alpha = (uint8_t)(a_sum / count);

            if (avg_alpha == 0) continue;
            if (avg_alpha < 255) {
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                uint32_t offset = (y + iy) * fb->framebuffer_pitch + (x + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                avg_color = blend_colors(bg, avg_color, avg_alpha);
            }
            draw_pixel(x + ix, y + iy, avg_color, fb);
        }
    }
}

void draw_icon(uint32_t x, uint32_t y, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb) {
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = bmp_data + bfh->bfOffBits;
    int32_t w = bih->biWidth, h = bih->biHeight, abs_h = h < 0 ? -h : h;
    uint32_t bpp = bih->biBitCount;
    uint32_t row_size = (w * (bpp / 8) + 3) & ~3;
    for (int32_t iy = 0; iy < abs_h; iy++) {
        for (int32_t ix = 0; ix < w; ix++) {
            int32_t src_y = (h > 0) ? (abs_h - 1 - iy) : iy;
            uint8_t* p = pixels + (src_y * row_size) + (ix * (bpp / 8));
            uint32_t color = (p[2] << 16) | (p[1] << 8) | p[0];
            if (bpp == 32) {
                uint8_t alpha = p[3]; if (alpha == 0) continue;
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                uint32_t offset = (y + iy) * fb->framebuffer_pitch + (x + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                color = blend_colors(bg, color, alpha);
            }
            draw_pixel(x + ix, y + iy, color, fb);
        }
    }
}

uint8_t read_rtc_reg(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

const char* strstr_custom(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return haystack;
        }
    }
    return 0;
}

// Segédfüggvény a környezet azonosításához
int is_qemu() {
    uint32_t eax, ebx, ecx, edx;
    char brand[49];
    brand[48] = 0;

    // A processzor nevének lekérése (Brand String)
    for (uint32_t i = 0; i < 3; i++) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ((uint32_t*)brand)[i * 4 + 0] = eax;
        ((uint32_t*)brand)[i * 4 + 1] = ebx;
        ((uint32_t*)brand)[i * 4 + 2] = ecx;
        ((uint32_t*)brand)[i * 4 + 3] = edx;
    }

    // Ha a processzor nevében benne van a QEMU vagy KVM, akkor emulátorban vagyunk
    if (strstr_custom(brand, "QEMU") || strstr_custom(brand, "KVM")) return 1;
    
    // Hypervisor azonosító ellenőrzése (ha a brand string nem lenne egyértelmű)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000));
    if (ebx == 0x4b564d4b || ebx == 0x47435447) return 1; // "KVMK" vagy "TCGT"
    
    return 0;
}

void get_time(uint8_t* h, uint8_t* m) {
    uint64_t ntp_time = ntp_get_time();
    if (ntp_time != 0) {
        *h = (ntp_time / 3600) % 24;
        *m = (ntp_time / 60) % 60;
        // Helyi idő korrekció (például +2 óra)
        *h = (*h + 2) % 24;
        return;
    }

    // Várakozás, amíg az RTC frissít
    while (read_rtc_reg(0x0A) & 0x80);

    *m = read_rtc_reg(0x02);
    *h = read_rtc_reg(0x04);
    uint8_t registerB = read_rtc_reg(0x0B);

    // BCD átalakítás binárissá, ha szükséges
    if (!(registerB & 0x04)) {
        *m = (*m & 0x0F) + ((*m / 16) * 10);
        *h = ((*h & 0x0F) + (((*h & 0x70) / 16) * 10)) | (*h & 0x80);
    }

    // 12 órás formátum átalakítása 24 órásra, ha szükséges
    if (!(registerB & 0x02) && (*h & 0x80)) {
        *h = ((*h & 0x7F) + 12) % 24;
    }

    if (is_qemu()) {
        *h = (*h + 2) % 24;
    }
}

int32_t atoi_custom(const char* s) {
    int32_t res = 0, sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { res = res * 10 + (*s - '0'); s++; }
    return res * sign;
}

int32_t get_attr_value(const char* tag, const char* attr) {
    const char* p = strstr_custom(tag, attr);
    if (!p) return 0;
    while (*p && *p != '\"') p++;
    if (*p == '\"') return atoi_custom(p + 1);
    return 0;
}

int32_t draw_char(uint32_t x, uint32_t y, char c, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    char search[16] = "<char id=\"";
    int i = 10;
    uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else { search[i++] = code + '0'; }
    search[i++] = '\"'; search[i] = 0;

    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    if (!tag) return 0;

    int32_t cx = get_attr_value(tag, " x=");
    int32_t cy = get_attr_value(tag, " y=");
    int32_t cw = get_attr_value(tag, " width=");
    int32_t ch = get_attr_value(tag, " height=");
    int32_t ox = get_attr_value(tag, " xoffset=");
    int32_t oy = get_attr_value(tag, " yoffset=");
    int32_t xa = get_attr_value(tag, " xadvance=");

    struct bmp_file_header* bfh = (struct bmp_file_header*)arial_font_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(arial_font_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = arial_font_data + bfh->bfOffBits;
    uint32_t bpp = bih->biBitCount;
    uint32_t row_size = (bih->biWidth * (bpp / 8) + 3) & ~3;
    int32_t img_h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;

    for (int32_t iy = 0; iy < ch; iy++) {
        for (int32_t ix = 0; ix < cw; ix++) {
            int32_t src_y = (bih->biHeight > 0) ? (img_h - 1 - (cy + iy)) : (cy + iy);
            uint8_t* p = pixels + (src_y * row_size) + ((cx + ix) * (bpp / 8));
            uint8_t alpha = (bpp == 32) ? p[3] : ((p[0] > 20 || p[1] > 20 || p[2] > 20) ? 255 : 0);
            if (alpha > 20) {
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                uint32_t offset = (y + oy + iy) * fb->framebuffer_pitch + (x + ox + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                draw_pixel(x + ox + ix, y + oy + iy, blend_colors(bg, color, alpha), fb);
            }
        }
    }
    return xa;
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    while (*str) {
        x += draw_char(x, y, *str, color, fb);
        str++;
    }
}

void uint_to_hex(uint64_t n, char* out, int digits) {
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) {
        out[digits - 1 - i] = hex[n & 0xF];
        n >>= 4;
    }
    out[digits] = 0;
}

int32_t get_char_width(char c) {
    char search[16] = "<char id=\"";
    int i = 10;
    uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else { search[i++] = code + '0'; }
    search[i++] = '\"'; search[i] = 0;
    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    if (!tag) return 0;
    return get_attr_value(tag, " xadvance=");
}

uint32_t get_string_width(const char* str) {
    uint32_t w = 0;
    while (*str) { w += get_char_width(*str); str++; }
    return w;
}

void draw_dock(struct multiboot_tag_framebuffer* fb) {
    uint32_t margin = 20;
    uint32_t dock_h = 55, dock_x = margin, dock_w = fb->framebuffer_width - 2 * margin, dock_y = fb->framebuffer_height - dock_h - margin, radius = 25, dock_color = 0xFFFFFF;
    for (uint32_t y = dock_y; y < dock_y + dock_h; y++) {
        for (uint32_t x = dock_x; x < dock_x + dock_w; x++) {
            uint8_t alpha = 200; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (x < dock_x + radius && y < dock_y + radius) { dx = (dock_x + radius) - x; dy = (dock_y + radius) - y; is_corner = 1; }
            else if (x > dock_x + dock_w - radius && y < dock_y + radius) { dx = x - (dock_x + dock_w - radius); dy = (dock_y + radius) - y; is_corner = 1; }
            else if (x < dock_x + radius && y > dock_y + dock_h - radius) { dx = (dock_x + radius) - x; dy = y - (dock_y + dock_h - radius); is_corner = 1; }
            else if (x > dock_x + dock_w - radius && y > dock_y + dock_h - radius) { dx = x - (dock_x + dock_w - radius); dy = y - (dock_y + dock_h - radius); is_corner = 1; }
            if (is_corner) {
                uint32_t dist_sq = dx*dx + dy*dy;
                uint32_t r_sq = radius * radius;
                uint32_t inner_r_sq = (radius - 1) * (radius - 1);
                if (dist_sq >= r_sq) alpha = 0;
                else if (dist_sq > inner_r_sq) alpha = (200 * (r_sq - dist_sq)) / (r_sq - inner_r_sq);
            }
            if (alpha > 0) draw_pixel(x, y, blend_colors(get_wallpaper_pixel_fast(x, y, fb), dock_color, alpha), fb);
        }
    }
    struct bmp_info_header* icon_bih = (struct bmp_info_header*)(power_icon_data + sizeof(struct bmp_file_header));
    int32_t icon_h = icon_bih->biHeight < 0 ? -icon_bih->biHeight : icon_bih->biHeight;
    draw_icon(dock_x + 15, dock_y + (dock_h - icon_h) / 2, power_icon_data, fb);

    uint8_t h, m; get_time(&h, &m);
    char time_str[6] = { (h/10)+'0', (h%10)+'0', ':', (m/10)+'0', (m%10)+'0', 0 };
    uint32_t time_x = dock_x + dock_w - 80;
    uint32_t time_y = dock_y + 17;
    draw_string(time_x, time_y, time_str, 0x333333, fb);

    extern int received_any;
    const char* status_str = "";
    uint32_t status_color = 0x555555;

    if (net_status == 3) { status_str = "NTP OK"; status_color = 0x00AA00; }
    else if (net_status == 7) { status_str = "DHCP..."; status_color = 0xAAAA00; }
    else if (net_status == 8) { status_str = "DHCP REQ"; status_color = 0xAAAA00; }
    else if (net_status == 2) {
        if (received_any) { status_str = "RECV ANY"; status_color = 0xAA00AA; }
        else { status_str = "SEND NTP"; status_color = 0xAAAA00; }
    }
    else if (net_status == 1) { status_str = "LINK OK"; status_color = 0xAA0000; }
    else if (net_status == 6) { status_str = "NO LINK"; status_color = 0xAA5500; }
    else if (net_status == 4) { status_str = "UNKNOWN PCI"; status_color = 0x0000AA; }
    else if (net_status == 5) { status_str = "PCI SCAN"; status_color = 0x777777; }
    else { status_str = "NO NIC"; status_color = 0x555555; }

    uint32_t status_x = time_x - get_string_width(status_str) - 20;
    draw_string(status_x, time_y, status_str, status_color, fb);

    if (net_dhcp_ok()) {
        uint32_t ip = net_get_ip();
        char ip_str[20];
        int pos = 0;
        for(int i=0; i<4; i++) {
            uint8_t part = (ip >> (i*8)) & 0xFF;
            if(part >= 100) ip_str[pos++] = (part/100)+'0';
            if(part >= 10) ip_str[pos++] = ((part/10)%10)+'0';
            ip_str[pos++] = (part%10)+'0';
            if(i < 3) ip_str[pos++] = '.';
        }
        ip_str[pos] = 0;
        uint32_t ip_x = status_x - get_string_width(ip_str) - 30;
        draw_string(ip_x, time_y, ip_str, 0x333333, fb);
    }
}

void draw_status_bar(struct multiboot_tag_framebuffer* fb) {
    uint32_t margin = 20;
    uint32_t bar_h = 36, bar_w = 200, bar_x = fb->framebuffer_width - margin - bar_w, bar_y = margin, radius = 18, bar_color = 0xFFFFFF;
    for (uint32_t y = bar_y; y < bar_y + bar_h; y++) {
        for (uint32_t x = bar_x; x < bar_x + bar_w; x++) {
            uint8_t alpha = 200; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (x < bar_x + radius && y < bar_y + radius) { dx = (bar_x + radius) - x; dy = (bar_y + radius) - y; is_corner = 1; }
            else if (x > bar_x + bar_w - radius && y < bar_y + radius) { dx = x - (bar_x + bar_w - radius); dy = (bar_y + radius) - y; is_corner = 1; }
            else if (x < bar_x + radius && y > bar_y + bar_h - radius) { dx = (bar_x + radius) - x; dy = y - (bar_y + bar_h - radius); is_corner = 1; }
            else if (x > bar_x + bar_w - radius && y > bar_y + bar_h - radius) { dx = x - (bar_x + bar_w - radius); dy = y - (bar_y + bar_h - radius); is_corner = 1; }
            if (is_corner) {
                uint32_t dist_sq = dx*dx + dy*dy;
                uint32_t r_sq = radius * radius;
                uint32_t inner_r_sq = (radius - 1) * (radius - 1);
                if (dist_sq >= r_sq) alpha = 0;
                else if (dist_sq > inner_r_sq) alpha = (200 * (r_sq - dist_sq)) / (r_sq - inner_r_sq);
            }
            if (alpha > 0) draw_pixel(x, y, blend_colors(get_wallpaper_pixel_fast(x, y, fb), bar_color, alpha), fb);
        }
    }

    uint8_t* icon = (net_dhcp_ok() && net_status == 3) ? online_icon_data : offline_icon_data;
    uint32_t icon_size = 24;
    draw_icon_scaled(bar_x + bar_w - icon_size - 5, bar_y + (bar_h - icon_size) / 2, icon_size, icon_size, icon, fb);
}

void hide_cursor(struct multiboot_tag_framebuffer* fb) {
    if (last_mouse_x == -1) return;
    uint8_t* dib = cursor_data + 6 + 16;
    struct bmp_info_header* bih = (struct bmp_info_header*)dib;
    int32_t w = bih->biWidth, h = bih->biHeight / 2;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) { draw_pixel(last_mouse_x + x, last_mouse_y + y, cursor_buffer[y * w + x], fb); }
    }
    last_mouse_x = -1; last_mouse_y = -1;
}

void msleep(uint32_t ms) {
    if (ms == 0) return;
    for (uint32_t i = 0; i < ms; i++) {
        outb(0x43, 0xB0); // Channel 2, LSB/MSB, Mode 0
        outb(0x42, 1193 & 0xFF); // ~1ms (1193.18 Hz)
        outb(0x42, (1193 >> 8) & 0xFF);
        uint8_t ctrl = inb(0x61) & 0xFC;
        outb(0x61, ctrl);       // Gate 2 low
        outb(0x61, ctrl | 1);   // Gate 2 high to start
        while (!(inb(0x61) & 0x20)); // Wait for OUT2 (Bit 5) to go HIGH
    }
}

void draw_power_menu(struct multiboot_tag_framebuffer* fb, int progress) {
    uint32_t margin = 20;
    uint32_t dock_h = 55, dock_x = margin, dock_w = fb->framebuffer_width - 2 * margin, dock_y = fb->framebuffer_height - dock_h - margin;
    uint32_t menu_w = 200, menu_h = 120, menu_x = dock_x, radius = 15;
    int32_t target_y = dock_y - menu_h - 10, start_y = dock_y;
    int32_t current_y = start_y + (target_y - start_y) * progress / 100;
    int32_t area_y = target_y, area_h = start_y - target_y;

    hide_cursor(fb);

    for (int32_t y = 0; y < area_h; y++) {
        for (int32_t x = 0; x < (int32_t)menu_w; x++) {
            menu_area_buffer[y * menu_w + x] = get_wallpaper_pixel_fast(menu_x + x, area_y + y, fb);
        }
    }

    if (progress > 0) {
        int32_t menu_rel_y = current_y - area_y;
        for (int32_t y = 0; y < (int32_t)menu_h; y++) {
            int32_t buffer_y = menu_rel_y + y;
            if (buffer_y < 0 || buffer_y >= area_h) continue;
            for (int32_t x = 0; x < (int32_t)menu_w; x++) {
                uint8_t alpha = 220; uint32_t dx = 0, dy = 0; int is_corner = 0;
                if (x < (int32_t)radius && y < (int32_t)radius) { dx = radius - x; dy = radius - y; is_corner = 1; }
                else if (x > (int32_t)menu_w - (int32_t)radius && y < (int32_t)radius) { dx = x - (menu_w - radius); dy = radius - y; is_corner = 1; }
                else if (x < (int32_t)radius && y > (int32_t)menu_h - (int32_t)radius) { dx = radius - x; dy = y - (menu_h - radius); is_corner = 1; }
                else if (x > (int32_t)menu_w - (int32_t)radius && y > (int32_t)menu_h - (int32_t)radius) { dx = x - (menu_w - radius); dy = y - (menu_h - radius); is_corner = 1; }
                if (is_corner) {
                    uint32_t dist_sq = dx*dx + dy*dy;
                    if (dist_sq >= radius * radius) alpha = 0;
                    else if (dist_sq > (radius-1)*(radius-1)) alpha = (220 * (radius*radius - dist_sq)) / (radius*radius - (radius-1)*(radius-1));
                }
                if (alpha > 0) {
                    uint32_t color = 0xFFFFFF;
                    if (is_corner) { if (dx*dx+dy*dy > (radius-2)*(radius-2)) color = 0xCCCCCC; }
                    else if (x == 0 || x == (int32_t)menu_w - 1 || y == 0 || y == (int32_t)menu_h - 1) color = 0xCCCCCC;
                    menu_area_buffer[buffer_y * menu_w + x] = blend_colors(menu_area_buffer[buffer_y * menu_w + x], color, alpha);
                }
            }
        }
    }
    for (int32_t y = 0; y < area_h; y++) {
        for (int32_t x = 0; x < (int32_t)menu_w; x++) {
            draw_pixel(menu_x + x, area_y + y, menu_area_buffer[y * menu_w + x], fb);
        }
    }
    // Szövegeket CSAK a puffer kirajzolása UTÁN rajzoljuk
    if (progress == 100) {
        draw_string(menu_x + 20, current_y + 20, "Restart", 0x333333, fb);
        for(uint32_t x = menu_x + 10; x < menu_x + menu_w - 10; x++) draw_pixel(x, current_y + 60, 0xBBBBBB, fb);
        draw_string(menu_x + 20, current_y + 80, "Shutdown", 0x333333, fb);
    }
}

void draw_dialog(struct multiboot_tag_framebuffer* fb, const char* title, const char* msg) {
    uint32_t msg_w = get_string_width(msg);
    uint32_t w = msg_w + 80; if (w < 350) w = 350;
    uint32_t h = 220, x = (fb->framebuffer_width - w) / 2, y = (fb->framebuffer_height - h) / 2, radius = 15;
    for (uint32_t iy = y; iy < y + h; iy++) {
        for (uint32_t ix = x; ix < x + w; ix++) {
            uint8_t alpha = 245; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (ix < x + radius && iy < y + radius) { dx = (x + radius) - ix; dy = (y + radius) - iy; is_corner = 1; }
            else if (ix > x + w - radius && iy < y + radius) { dx = ix - (x + w - radius); dy = (y + radius) - iy; is_corner = 1; }
            else if (ix < x + radius && iy > y + h - radius) { dx = (x + radius) - ix; dy = iy - (y + h - radius); is_corner = 1; }
            else if (ix > x + w - radius && iy > y + h - radius) { dx = ix - (x + w - radius); dy = iy - (y + h - radius); is_corner = 1; }
            if (is_corner) { if (dx*dx + dy*dy >= radius * radius) alpha = 0; }
            if (alpha > 0) draw_pixel(ix, iy, blend_colors(get_wallpaper_pixel_fast(ix, iy, fb), 0xFFFFFF, alpha), fb);
        }
    }
    draw_string(x + 25, y + 25, title, 0x222222, fb);
    for(uint32_t ix = x + 10; ix < x + w - 10; ix++) draw_pixel(ix, y + 65, 0xBBBBBB, fb);
    draw_string(x + (w - msg_w) / 2, y + 95, msg, 0x444444, fb);
    
    // Gombok: Yes (Pirosas), Cancel (Szürke) - Középre rendezve
    uint32_t btn_w = 120, btn_h = 45, spacing = 20;
    uint32_t total_btns_w = 2 * btn_w + spacing;
    uint32_t start_btn_x = x + (w - total_btns_w) / 2;
    uint32_t btn_y = y + 145;

    // YES gomb
    for(uint32_t iy = 0; iy < btn_h; iy++) for(uint32_t ix = 0; ix < btn_w; ix++) draw_pixel(start_btn_x + ix, btn_y + iy, 0xFF5555, fb);
    uint32_t yes_w = get_string_width("Yes");
    draw_string(start_btn_x + (btn_w - yes_w) / 2, btn_y + (btn_h - 18) / 2, "Yes", 0xFFFFFF, fb);

    // CANCEL gomb
    for(uint32_t iy = 0; iy < btn_h; iy++) for(uint32_t ix = 0; ix < btn_w; ix++) draw_pixel(start_btn_x + btn_w + spacing + ix, btn_y + iy, 0xDDDDDD, fb);
    uint32_t cancel_w = get_string_width("Cancel");
    draw_string(start_btn_x + btn_w + spacing + (btn_w - cancel_w) / 2, btn_y + (btn_h - 18) / 2, "Cancel", 0x333333, fb);
}

void kernel_main(uint64_t multiboot_addr) {
    struct multiboot_tag_framebuffer* fb = 0;
    struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8); tag->type != 0; tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 8) fb = (struct multiboot_tag_framebuffer*)tag;
    }
    if (fb && fb->framebuffer_addr != 0) {
        screen_w = fb->framebuffer_width; screen_h = fb->framebuffer_height;
        mouse_x = screen_w / 2; mouse_y = screen_h / 2;
        pic_remap();
        for (int i = 0; i < 256; i++) idt_set_gate(i, 0, 0, 0);
        idt_set_gate(33, (uint64_t)isr_keyboard_stub, 0x08, 0x8E);
        idt_set_gate(44, (uint64_t)isr_mouse_stub, 0x08, 0x8E);
        idtp.limit = sizeof(idt) - 1; idtp.base = (uint64_t)&idt;
        idt_load(&idtp);
        mouse_init();

        // Alapértelmezett beágyazott assetek beállítása fallback-nek
        wallpaper_data = wallpaper_data_embedded;
        cursor_data = cursor_data_embedded;
        power_icon_data = power_icon_data_embedded;
        arial_font_data = arial_font_data_embedded;
        arial_font_xml_data = arial_font_xml_data_embedded;
        offline_icon_data = offline_icon_data_embedded;
        online_icon_data = online_icon_data_embedded;
        init_wallpaper_info();

        vfs_init();

        // Dynamically load assets from disk if available
        uint8_t* new_wall = load_asset("Sysroot:/AnimOS/assets/wallpapers/bubble.bmp");
        if (new_wall) { wallpaper_data = new_wall; init_wallpaper_info(); }
        
        uint8_t* new_cursor = load_asset("Sysroot:/AnimOS/assets/cursor/Default/Normal Select.cur");
        if (new_cursor) cursor_data = new_cursor;

        uint8_t* new_power = load_asset("Sysroot:/AnimOS/assets/taskbar/power.bmp");
        if (new_power) power_icon_data = new_power;

        uint8_t* new_font = load_asset("Sysroot:/AnimOS/assets/fonts/arial_black/arial_black.bmp");
        if (new_font) arial_font_data = new_font;

        uint8_t* new_font_xml = load_asset("Sysroot:/AnimOS/assets/fonts/arial_black/arial_black.xml");
        if (new_font_xml) arial_font_xml_data = new_font_xml;

        uint8_t* new_offline = load_asset("Sysroot:/AnimOS/assets/ui/offline.bmp");
        if (new_offline) offline_icon_data = new_offline;

        uint8_t* new_online = load_asset("Sysroot:/AnimOS/assets/ui/online.bmp");
        if (new_online) online_icon_data = new_online;

        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            for (uint32_t x = 0; x < fb->framebuffer_width; x++) { draw_pixel(x, y, get_wallpaper_pixel_fast(x, y, fb), fb); }
        }
        draw_dock(fb);
        draw_status_bar(fb);

        // Hálózat inicializálása
        net_status = 5; draw_dock(fb); // PCI Scan Start (Fehér)
        struct pci_device net_dev;
        if (pci_find_device(0xFFFF, 0xFFFF, &net_dev)) {
            if (net_dev.vendor_id == 0x8086 && (net_dev.device_id == 0x100E || net_dev.device_id == 0x100F || net_dev.device_id == 0x10D3)) {
                if (e1000_init(&net_dev) == 0) {
                    net_status = 1; draw_dock(fb); // Intel E1000 kész (Piros)
                    net_init((10) | (0 << 8) | (2 << 16) | (15 << 24));
                    msleep(500); // Várjunk, amíg a link stabilizálódik a bridge-en
                }
            } else {
                net_status = 4; draw_dock(fb); // Más kártya (Kék)
            }
        } else {
            net_status = 0; draw_dock(fb); // Semmi (Nincs pötty)
        }
        uint32_t margin = 20;
        uint32_t dock_h = 55, dock_x = margin, dock_w = fb->framebuffer_width - 2 * margin, dock_y = fb->framebuffer_height - dock_h - margin;
        struct bmp_info_header* icon_bih = (struct bmp_info_header*)(power_icon_data + sizeof(struct bmp_file_header));
        int32_t icon_h = icon_bih->biHeight < 0 ? -icon_bih->biHeight : icon_bih->biHeight;
        int32_t icon_w = icon_bih->biWidth;
        int32_t icon_x = dock_x + 15, icon_y = dock_y + (dock_h - icon_h) / 2;
        int dialog_state = 0; // 0: None, 1: Restart, 2: Shutdown
        uint8_t last_min = 255;
        last_mouse_x = -1; last_mouse_y = -1; // Reset state

        uint32_t ntp_retry_timer = 0;
        int last_displayed_status = -1;
        uint32_t link_stable_count = 0;

        while(1) { 
            // Link állapot ellenőrzése stabilitási számlálóval
            int current_link = e1000_link_up();
            if (current_link) {
                if (link_stable_count < 10) link_stable_count++;
            } else {
                link_stable_count = 0;
            }

            if (net_status != 0 && net_status != 4 && net_status != 5) {
                if (link_stable_count < 5) {
                    if (net_status != 6) net_status = 6;
                } else {
                    if (net_status == 6) net_status = 1;
                }
            }

            // NTP szinkronizáció / DHCP folyamat (ritkított próbálkozás)
            if (net_status == 1 || (net_status == 2 && ntp_retry_timer == 0) || (net_status == 3 && ntp_retry_timer == 0) || (net_status == 7 && ntp_retry_timer == 0) || (net_status == 8 && ntp_retry_timer == 0)) {
                if (link_stable_count >= 5) {
                    if (!net_dhcp_ok()) {
                        if (net_status != 8) net_status = 7;
                    }
                    ntp_sync((162) | (159 << 8) | (200 << 16) | (1 << 24));
                    if (net_status == 1) net_status = 2;
                    ntp_retry_timer = 500; // 500 * 10ms = 5 másodperc várakozás
                }
            }
            if (ntp_retry_timer > 0) ntp_retry_timer--;

            // Külön ellenőrzés: Ha a DHCP OK, de még a REQUEST fázisban vagyunk a UI szerint
            if (net_dhcp_ok() && net_status == 8) {
                // Várunk egy picit, hátha jön az NTP, de legalább ne DHCP REQ-et írjunk
                // net_status = 2; // Vissza 'SEND NTP' állapotba (vagy egy új 'DHCP OK' állapotba)
            }

            uint8_t h, m;
            get_time(&h, &m);
            
            // Frissítés, ha változik a perc VAGY a hálózati állapot
            if (m != last_min || net_status != last_displayed_status) {
                last_min = m;
                last_displayed_status = net_status;
                hide_cursor(fb);
                draw_dock(fb);
                draw_status_bar(fb);
                if (dialog_state == 1) draw_dialog(fb, "Restart", "Are you sure you want to restart the system?");
                else if (dialog_state == 2) draw_dialog(fb, "Shutdown", "Are you sure you want to shutdown the system?");
                draw_cursor(mouse_x, mouse_y, fb);
            }

            int32_t mx = mouse_x, my = mouse_y;
            if (mouse_clicked) {
                mouse_clicked = 0;
                uint32_t menu_w = 200, menu_h = 120, menu_x = dock_x, menu_y = dock_y - menu_h - 10;
                
                if (dialog_state != 0) {
                    const char* msg = (dialog_state == 1) ? "Are you sure you want to restart the system?" : "Are you sure you want to shutdown the system?";
                    uint32_t msg_w = get_string_width(msg);
                    uint32_t dw = msg_w + 80; if (dw < 350) dw = 350;
                    uint32_t dh = 220, dx = (fb->framebuffer_width - dw) / 2, dy = (fb->framebuffer_height - dh) / 2;
                    uint32_t btn_w = 120, btn_h = 45, spacing = 20;
                    uint32_t total_btns_w = 2 * btn_w + spacing;
                    uint32_t start_btn_x = dx + (dw - total_btns_w) / 2;
                    uint32_t btn_y = dy + 145;

                    // YES gomb
                    if (mx >= (int32_t)start_btn_x && mx <= (int32_t)(start_btn_x + btn_w) && my >= (int32_t)btn_y && my <= (int32_t)(btn_y + btn_h)) {
                        hide_cursor(fb);
                        for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                        if (dialog_state == 1) {
                            const char* msg = "Restarting...";
                            draw_string((screen_w - get_string_width(msg)) / 2, screen_h / 2, msg, 0xFFFFFF, fb);
                            msleep(500);
                            reboot();
                        } else {
                            const char* logoff_msg = "Logging off...";
                            draw_string((screen_w - get_string_width(logoff_msg)) / 2, screen_h / 2, logoff_msg, 0xFFFFFF, fb);
                            msleep(800);
                            for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                            const char* shutdown_msg = "AnimOS is shutting down...";
                            draw_string((screen_w - get_string_width(shutdown_msg)) / 2, screen_h / 2, shutdown_msg, 0xFFFFFF, fb);
                            msleep(800);
                            shutdown(multiboot_addr);
                            // Ha az ACPI/emulator shutdown sikertelen:
                            for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                            const char* safe_msg = "It is now safe to turn off your computer.";
                            draw_string((screen_w - get_string_width(safe_msg)) / 2, screen_h / 2, safe_msg, 0xFFFFFF, fb);
                        }
                        while(1) __asm__ volatile("hlt");
                    }
                    // CANCEL gomb
                    else if (mx >= (int32_t)(start_btn_x + btn_w + spacing) && mx <= (int32_t)(start_btn_x + 2 * btn_w + spacing) && my >= (int32_t)btn_y && my <= (int32_t)(btn_y + btn_h)) {
                        dialog_state = 0;
                        hide_cursor(fb);
                        for(uint32_t y = dy; y < dy + dh; y++) for(uint32_t x = dx; x < dx + dw; x++) draw_pixel(x, y, get_wallpaper_pixel_fast(x, y, fb), fb);
                        draw_dock(fb);
                    }
                }
                else if (mx >= icon_x && mx <= icon_x + icon_w && my >= icon_y && my <= icon_y + icon_h) power_menu_open = !power_menu_open;
                else if (power_menu_open) {
                    // Restart opció: current_y + 20, hit area
                    if (mx >= (int32_t)menu_x && mx <= (int32_t)(menu_x + menu_w) && my >= (int32_t)(menu_y + 10) && my <= (int32_t)(menu_y + 60)) {
                        dialog_state = 1; power_menu_open = 0; power_menu_progress = 0;
                        hide_cursor(fb);
                        draw_power_menu(fb, 0);
                        draw_dialog(fb, "Restart", "Are you sure you want to restart the system?");
                    }
                    // Shutdown opció: current_y + 80, hit area
                    else if (mx >= (int32_t)menu_x && mx <= (int32_t)(menu_x + menu_w) && my >= (int32_t)(menu_y + 70) && my <= (int32_t)(menu_y + 120)) {
                        dialog_state = 2; power_menu_open = 0; power_menu_progress = 0;
                        hide_cursor(fb);
                        draw_power_menu(fb, 0);
                        draw_dialog(fb, "Shutdown", "Are you sure you want to shutdown the system?");
                    }
                    else if (!(mx >= (int32_t)menu_x && mx <= (int32_t)(menu_x + menu_w) && my >= (int32_t)menu_y && my <= (int32_t)(menu_y + menu_h))) power_menu_open = 0;
                }
            }
            if (power_menu_open && power_menu_progress < 100) {
                power_menu_progress += 20; if (power_menu_progress > 100) power_menu_progress = 100;
                hide_cursor(fb);
                draw_power_menu(fb, power_menu_progress);
                draw_cursor(mx, my, fb);
                msleep(5);
            } else if (!power_menu_open && power_menu_progress > 0) {
                power_menu_progress -= 20; if (power_menu_progress < 0) power_menu_progress = 0;
                hide_cursor(fb);
                draw_power_menu(fb, power_menu_progress);
                draw_cursor(mx, my, fb);
                msleep(5);
            }
            draw_cursor(mx, my, fb); 
            net_poll();
            msleep(10); 
        }
    }
    while(1) { __asm__ volatile("hlt"); }
}
