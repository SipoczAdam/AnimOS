#include "types.h"
#include "io.h"
#include "../drivers/pci.h"
#include "../drivers/e1000.h"
#include "../drivers/ata.h"
#include "../net/net.h"
#include "../fs/vfs.h"
#include "api.h"

/* --- Includes and Global Variables --- */

kernel_api_t kernel_api;

struct idt_entry {
    uint16_t base_low; uint16_t selector; uint8_t ist; uint8_t flags;
    uint16_t base_mid; uint32_t base_high; uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit; uint64_t base;
} __attribute__((packed));

struct idt_entry idt[256] __attribute__((aligned(16)));
struct idt_ptr idtp;

#pragma pack(push, 1)
struct bmp_file_header { uint16_t bfType; uint32_t bfSize; uint16_t bfReserved1, bfReserved2; uint32_t bfOffBits; };
struct bmp_info_header {
    uint32_t biSize; int32_t biWidth, biHeight; uint16_t biPlanes, biBitCount;
    uint32_t biCompression, biSizeImage; int32_t biXPelsPerMeter, biYPelsPerMeter; uint32_t biClrUsed, biClrImportant;
};
#pragma pack(pop)

struct wallpaper_info {
    uint8_t* pixels;
    int32_t width, height;
    uint32_t row_size;
    int32_t bpp;
};

struct wallpaper_info wall_info;
struct multiboot_tag_framebuffer* global_fb = 0;

uint8_t* wallpaper_data = 0;
uint32_t* scaled_wallpaper = 0;
uint32_t* screen_backbuffer = 0;
uint8_t* cursor_data = 0;
uint8_t* normal_cursor_data = 0;
uint8_t* working_cursor_data = 0;
uint32_t working_frames_offsets[64];
int working_frames_count = 0;
int current_working_frame = 0;
int is_system_busy = 0;
uint8_t* power_icon_data = 0;
uint8_t* arial_font_data = 0;
uint8_t* arial_font_xml_data = 0;
uint8_t* offline_icon_data = 0;
uint8_t* online_icon_data = 0;
uint8_t* file_explorer_icon_data = 0;
uint8_t* preferences_icon_data = 0;
uint8_t* boot_logo_data = 0;
uint8_t* close_icon_data = 0;
uint8_t* minimize_icon_data = 0;
uint8_t* maximize_icon_data = 0;

uint32_t screen_w = 1024;
uint32_t screen_h = 768;
int32_t cursor_w = 32;
int32_t cursor_h = 32;
int net_status = 0; 
int selected_icon = -1; 
int hover_icon = -1;    
int preferences_window_open = 0;
int file_explorer_window_open = 0;
int active_app = -1;
int app_minimized[2] = {0, 0};
int app_maximized[2] = {1, 1};
uint32_t last_click_time = 0;
uint32_t last_clicked_icon = -1;
int dialog_state = 0; 
int preferences_needs_init = 0;
int file_explorer_needs_init = 0;
uint32_t global_ram_mb = 0;
static int desktop_ready = 0;

volatile int32_t mouse_x = 512, mouse_y = 384;
volatile int32_t mouse_speed = 10; // 1.0x speed
volatile int32_t mouse_scroll_speed = 3; // 3 lines per notch
volatile int32_t mouse_primary_button = 0; // 0 = Left, 1 = Right
volatile uint8_t mouse_left_button = 0;
volatile uint32_t mouse_click_count = 0;
volatile int32_t mouse_wheel = 0;
volatile int power_menu_open = 0;
volatile int power_menu_progress = 0;

static int32_t frame_mx, frame_my, frame_wheel;
static uint8_t frame_clicked;
static uint32_t last_kernel_click_count = 0;

uint32_t cursor_buffer[64 * 64];
uint32_t menu_area_buffer[200 * 300];
uint32_t icon_area_buffer[150 * 150]; 
uint32_t preferences_window_buffer[1024 * 768]; 

static uint8_t* app_bin_cache[2] = {0, 0};
static uint32_t app_bin_size[2] = {0, 0};
static const char* app_bin_paths[2] = {"Sysroot:/AnimOS/apps/file_explorer.bin", "Sysroot:/AnimOS/apps/preferences.bin"};
static int currently_loaded_app = -1;
static uint8_t* app_state_buffers[2] = {0, 0};
static int app_state_saved[2] = {0, 0};

/* --- Forward Declarations --- */

const char* kernel_get_timezone();
int kernel_get_timezone_offset();

void msleep(uint32_t ms);
void* malloc_custom(uint32_t size);
void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color, struct multiboot_tag_framebuffer* fb);
void draw_pixel(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb);
void strcpy_custom(char* dst, const char* src);
void strcat_custom(char* dst, const char* src);
uint32_t blend_colors(uint32_t bg, uint32_t fg, uint8_t alpha);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, struct multiboot_tag_framebuffer* fb);
uint32_t get_wallpaper_pixel_fast(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb);
void draw_string_scaled(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale_pct, struct multiboot_tag_framebuffer* fb);
uint32_t get_string_width(const char* str);
uint32_t get_string_width_scaled(const char* str, int scale_pct);
void draw_icon_scaled(uint32_t x, uint32_t y, uint32_t target_w, uint32_t target_h, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb);
void draw_dock(struct multiboot_tag_framebuffer* fb);
void draw_status_bar(struct multiboot_tag_framebuffer* fb);
void draw_desktop_icons(struct multiboot_tag_framebuffer* fb);
void draw_power_menu(struct multiboot_tag_framebuffer* fb, int progress);
void draw_dialog(struct multiboot_tag_framebuffer* fb, const char* title, const char* msg);
void draw_preferences_window(struct multiboot_tag_framebuffer* fb, app_event_t event);
void init_kernel_api();
void compose_frame(struct multiboot_tag_framebuffer* real_fb, uint64_t multiboot_addr);
void redraw_desktop(struct multiboot_tag_framebuffer* fb);
void blit_buffer(uint32_t* src_buffer, struct multiboot_tag_framebuffer* fb);
void draw_cursor_simple(int32_t mx, int32_t my, struct multiboot_tag_framebuffer* fb);
void kernel_ui_refresh_simple();
void kernel_ui_refresh_scaling();
void mouse_init();
void pic_remap();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void reboot();
void shutdown(uint64_t multiboot_addr);

/* --- Basic Helper Functions --- */

int memcmp_custom(const void* s1, const void* s2, uint32_t n) {
    const uint8_t *p1 = s1, *p2 = s2;
    for (uint32_t i = 0; i < n; i++) if (p1[i] != p2[i]) return p1[i] - p2[i];
    return 0;
}

uint32_t sqrt_int(uint32_t n) {
    if (n < 2) return n;
    uint32_t x = n / 2 + 1, y = (x + n / x) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

int32_t atoi_custom(const char* s) {
    int32_t res = 0, sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { res = res * 10 + (*s - '0'); s++; }
    return res * sign;
}

const char* strstr_custom(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
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

void msleep(uint32_t ms) {
    if (ms == 0) return;
    for (uint32_t i = 0; i < ms; i++) {
        // Garantáltan nem blokkoló, port-alapú várakozás (kb. 1ms)
        for (volatile int j = 0; j < 1250; j++) {
            inb(0x80); // Standard IO delay port (~1us per hívás)
        }
    }
}

void kernel_panic(const char* msg) {
    __asm__ volatile("cli");
    if (global_fb) {
        draw_rect(0, 0, screen_w, screen_h, 0xFF0000, global_fb);
        draw_string(50, 50, "--- KERNEL PANIC (EXCEPTION) ---", 0xFFFFFF, global_fb);
        draw_string(50, 80, msg, 0xFFFFFF, global_fb);
    }
    while(1) __asm__ volatile("hlt");
}

void exception_handler_main(uint64_t vec, uint64_t err, uint64_t rip) {
    char buf[256];
    strcpy_custom(buf, "Exception: 0x");
    uint8_t v = (uint8_t)vec;
    buf[13] = "0123456789ABCDEF"[(v >> 4) & 0xF];
    buf[14] = "0123456789ABCDEF"[v & 0xF];
    buf[15] = 0;
    strcat_custom(buf, ", Error: 0x");
    
    uint32_t e = (uint32_t)err;
    char hex_err[9];
    for(int i=0; i<8; i++) hex_err[7-i] = "0123456789ABCDEF"[(e >> (i*4)) & 0xF];
    hex_err[8] = 0;
    strcat_custom(buf, hex_err);

    strcat_custom(buf, ", RIP: 0x");
    char hex_rip[17];
    for(int i=0; i<16; i++) hex_rip[15-i] = "0123456789ABCDEF"[(rip >> (i*4)) & 0xF];
    hex_rip[16] = 0;
    strcat_custom(buf, hex_rip);
    
    kernel_panic(buf);
}

extern uint8_t kernel_end[];
static uint8_t* bump_ptr = 0;

void* malloc_custom(uint32_t size) {
    if (bump_ptr == 0) bump_ptr = kernel_end;
    bump_ptr = (uint8_t*)(((uint64_t)bump_ptr + 15) & ~15); // 16-byte alignment (SSE miatt fontos!)
    
    // Safety: 4GB-ig engedjük a foglalást (identitás leképezés 16GB)
    if ((uint64_t)bump_ptr > 0xFFFFFFFF) {
        return 0;
    }
    
    void* ptr = bump_ptr;
    bump_ptr += size;
    return ptr;
}

uint8_t read_rtc_reg(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

int is_qemu() {
    uint32_t eax, ebx, ecx, edx;
    char brand[49]; brand[48] = 0;
    for (uint32_t i = 0; i < 3; i++) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ((uint32_t*)brand)[i * 4 + 0] = eax; ((uint32_t*)brand)[i * 4 + 1] = ebx;
        ((uint32_t*)brand)[i * 4 + 2] = ecx; ((uint32_t*)brand)[i * 4 + 3] = edx;
    }
    if (strstr_custom(brand, "QEMU") || strstr_custom(brand, "KVM")) return 1;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000));
    if (ebx == 0x4b564d4b || ebx == 0x47435447) return 1;
    return 0;
}

void get_cpu_brand(char* brand) {
    uint32_t eax, ebx, ecx, edx;
    for (uint32_t i = 0; i < 3; i++) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ((uint32_t*)brand)[i * 4 + 0] = eax; ((uint32_t*)brand)[i * 4 + 1] = ebx;
        ((uint32_t*)brand)[i * 4 + 2] = ecx; ((uint32_t*)brand)[i * 4 + 3] = edx;
    }
    brand[48] = 0;
    // Trim leading spaces
    char* src = brand;
    while (*src == ' ') src++;
    if (src != brand) {
        char* dst = brand;
        while (*src) *dst++ = *src++;
        *dst = 0;
    }
}

struct tm_custom {
    int sec, min, hour, mday, mon, year, wday;
};

void unix_to_tm(uint64_t ts, struct tm_custom* t) {
    t->sec = ts % 60;
    t->min = (ts / 60) % 60;
    t->hour = (ts / 3600) % 24;
    uint32_t days = ts / 86400;
    t->wday = (days + 4) % 7;
    int y = 1970;
    while (1) {
        int diy = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < (uint32_t)diy) break;
        days -= diy; y++;
    }
    t->year = y;
    int m_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) m_days[1] = 29;
    int m = 0;
    while (days >= (uint32_t)m_days[m]) { days -= m_days[m]; m++; }
    t->mon = m + 1; t->mday = days + 1;
}

int is_dst_eu(int y, int m, int d, int h) {
    if (m < 3 || m > 10) return 0;
    if (m > 3 && m < 10) return 1;
    // Last Sunday of March
    int last_sun_march = 31 - ((5 * y / 4 + 4) % 7);
    if (m == 3) return (d > last_sun_march || (d == last_sun_march && h >= 1));
    // Last Sunday of October
    int last_sun_oct = 31 - ((5 * y / 4 + 1) % 7);
    if (m == 10) return (d < last_sun_oct || (d == last_sun_oct && h < 1));
    return 0;
}

int is_dst_us(int y, int m, int d, int h) {
    if (m < 3 || m > 11) return 0;
    if (m > 3 && m < 11) return 1;
    // Second Sunday of March
    int second_sun_march = 14 - ((5 * y / 4 + 1) % 7);
    if (m == 3) return (d > second_sun_march || (d == second_sun_march && h >= 2));
    // First Sunday of November
    int first_sun_nov = 7 - ((5 * y / 4 + 1) % 7);
    if (m == 11) return (d < first_sun_nov || (d == first_sun_nov && h < 2));
    return 0;
}

void get_time(uint8_t* h, uint8_t* m) {
    uint64_t ntp_time = ntp_get_time();
    int offset = kernel_get_timezone_offset(); // This already includes DST

    if (ntp_time != 0) {
        struct tm_custom t;
        unix_to_tm(ntp_time, &t);
        *h = (t.hour + offset + 24) % 24;
        *m = t.min;
        return;
    }

    // RTC Fallback
    *m = read_rtc_reg(0x02);
    *h = read_rtc_reg(0x04);
    uint8_t registerB = read_rtc_reg(0x0B);
    if (!(registerB & 0x04)) {
        *m = (*m & 0x0F) + ((*m / 16) * 10);
        *h = ((*h & 0x0F) + (((*h & 0x70) / 16) * 10)) | (*h & 0x80);
    }
    if (!(registerB & 0x02) && (*h & 0x80)) *h = ((*h & 0x7F) + 12) % 24;
    
    // Apply unified offset (base + DST)
    *h = (*h + offset + 24) % 24;
}

/* --- Rendering Primitives --- */

void draw_pixel(uint32_t x, uint32_t y, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (!fb) fb = global_fb;
    if (!fb || x >= fb->framebuffer_width || y >= fb->framebuffer_height) return;
    uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
    uint32_t offset = y * fb->framebuffer_pitch + x * (fb->framebuffer_bpp / 8);
    if (fb->framebuffer_bpp == 32) *(uint32_t*)(screen + offset) = color;
    else if (fb->framebuffer_bpp == 24) { screen[offset] = color & 0xFF; screen[offset+1] = (color>>8) & 0xFF; screen[offset+2] = (color>>16) & 0xFF; }
}

uint32_t blend_colors(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg; if (alpha == 255) return fg;
    uint32_t rb = (bg >> 16) & 0xFF, gb = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint32_t rf = (fg >> 16) & 0xFF, gf = (fg >> 8) & 0xFF, bf = fg & 0xFF;
    return (((rf * alpha + rb * (255 - alpha)) / 255) << 16) | (((gf * alpha + gb * (255 - alpha)) / 255) << 8) | ((bf * alpha + bb * (255 - alpha)) / 255);
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (!fb) fb = global_fb;
    for (uint32_t iy = y; iy < y + h; iy++) {
        if (iy >= fb->framebuffer_height) break;
        uint8_t* screen = (uint8_t*)fb->framebuffer_addr + iy * fb->framebuffer_pitch;
        if (fb->framebuffer_bpp == 32) { uint32_t* row32 = (uint32_t*)screen; for (uint32_t ix = x; ix < x + w; ix++) { if (ix >= fb->framebuffer_width) break; row32[ix] = color; } }
        else if (fb->framebuffer_bpp == 24) { for (uint32_t ix = x; ix < x + w; ix++) { if (ix >= fb->framebuffer_width) break; uint32_t offset = ix * 3; screen[offset] = color & 0xFF; screen[offset+1] = (color >> 8) & 0xFF; screen[offset+2] = (color >> 16) & 0xFF; } }
    }
}

void blit_buffer(uint32_t* src_buffer, struct multiboot_tag_framebuffer* fb) {
    if (!fb) fb = global_fb; if (!src_buffer) return;
    for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
        uint8_t* dest = (uint8_t*)fb->framebuffer_addr + y * fb->framebuffer_pitch; uint32_t* src = src_buffer + y * fb->framebuffer_width;
        if (fb->framebuffer_bpp == 32) { uint32_t* dest32 = (uint32_t*)dest; for (uint32_t x = 0; x < fb->framebuffer_width; x++) dest32[x] = src[x]; }
        else if (fb->framebuffer_bpp == 24) { for (uint32_t x = 0; x < fb->framebuffer_width; x++) { uint32_t color = src[x]; dest[x*3] = color & 0xFF; dest[x*3+1] = (color >> 8) & 0xFF; dest[x*3+2] = (color >> 16) & 0xFF; } }
    }
}

/* --- Asset Loading and Wallpaper Handling --- */

uint8_t* load_asset(const char* path) {
    uint32_t size = vfs_get_file_size(path);
    if (size == 0) return 0;
    uint8_t* buffer = malloc_custom(size + 1);
    if (!buffer) return 0;
    if (vfs_read_file(path, buffer) != 0) return 0;
    buffer[size] = 0; return buffer;
}

void strcpy_custom(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

void strcat_custom(char* dst, const char* src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

void init_wallpaper_info() {
    if (!wallpaper_data) return;
    struct bmp_file_header* bfh = (struct bmp_file_header*)wallpaper_data;
    if (bfh->bfType != 0x4D42) return;
    struct bmp_info_header* bih = (struct bmp_info_header*)(wallpaper_data + sizeof(struct bmp_file_header));
    wall_info.pixels = wallpaper_data + bfh->bfOffBits;
    wall_info.width = bih->biWidth; wall_info.height = bih->biHeight; wall_info.bpp = bih->biBitCount;
    wall_info.row_size = (wall_info.width * (wall_info.bpp / 8) + 3) & ~3;
}

void kernel_ui_refresh_simple() {
    if (!desktop_ready || !global_fb || !screen_backbuffer) return;
    
    // Save area under cursor from screen_backbuffer
    int32_t cursor_saved_x = mouse_x;
    int32_t cursor_saved_y = mouse_y;
    int32_t cursor_saved_w = 0;
    int32_t cursor_saved_h = 0;
    
    if (cursor_data) {
        uint8_t* actual_cursor = cursor_data;
        if (is_system_busy && working_cursor_data && working_frames_count > 0) {
            actual_cursor = working_cursor_data + working_frames_offsets[current_working_frame];
        }
        uint8_t* dib = actual_cursor + 6 + 16;
        struct bmp_info_header* bih = (struct bmp_info_header*)dib;
        cursor_saved_w = bih->biWidth;
        cursor_saved_h = bih->biHeight / 2;
        if (cursor_saved_w > 64) cursor_saved_w = 64;
        if (cursor_saved_h > 64) cursor_saved_h = 64;
        
        for (int32_t y = 0; y < cursor_saved_h; y++) {
            for (int32_t x = 0; x < cursor_saved_w; x++) {
                int32_t px = cursor_saved_x + x;
                int32_t py = cursor_saved_y + y;
                if (px >= 0 && px < (int32_t)screen_w && py >= 0 && py < (int32_t)screen_h) {
                    cursor_buffer[y * cursor_saved_w + x] = screen_backbuffer[py * screen_w + px];
                }
            }
        }
    }

    struct multiboot_tag_framebuffer back_fb = *global_fb;
    back_fb.framebuffer_addr = (uint64_t)screen_backbuffer;
    back_fb.framebuffer_pitch = global_fb->framebuffer_width * 4;
    back_fb.framebuffer_bpp = 32;
    
    draw_cursor_simple(mouse_x, mouse_y, &back_fb);
    
    blit_buffer(screen_backbuffer, global_fb);
    
    if (cursor_saved_w > 0 && cursor_saved_h > 0) {
        for (int32_t y = 0; y < cursor_saved_h; y++) {
            for (int32_t x = 0; x < cursor_saved_w; x++) {
                int32_t px = cursor_saved_x + x;
                int32_t py = cursor_saved_y + y;
                if (px >= 0 && px < (int32_t)screen_w && py >= 0 && py < (int32_t)screen_h) {
                    screen_backbuffer[py * screen_w + px] = cursor_buffer[y * cursor_saved_w + x];
                }
            }
        }
    }
}

static int32_t scaling_last_mx = -1;
static int32_t scaling_last_my = -1;

void kernel_ui_refresh_scaling() {
    if (!global_fb || !screen_backbuffer) return;
    
    // Erase old cursor by blitting the 32x32 area from the clean backbuffer
    if (scaling_last_mx != -1) {
        for (int32_t y = 0; y < 32; y++) {
            int32_t py = scaling_last_my + y;
            if (py < 0 || py >= (int32_t)global_fb->framebuffer_height) continue;
            
            uint8_t* dest = (uint8_t*)global_fb->framebuffer_addr + py * global_fb->framebuffer_pitch;
            uint32_t* src_row = screen_backbuffer + py * screen_w;

            for (int32_t x = 0; x < 32; x++) {
                int32_t px = scaling_last_mx + x;
                if (px < 0 || px >= (int32_t)global_fb->framebuffer_width) continue;

                uint32_t color = src_row[px];
                if (global_fb->framebuffer_bpp == 32) {
                    ((uint32_t*)dest)[px] = color;
                } else if (global_fb->framebuffer_bpp == 24) {
                    dest[px*3] = color & 0xFF; dest[px*3+1] = (color >> 8) & 0xFF; dest[px*3+2] = (color >> 16) & 0xFF;
                }
            }
        }
    }

    // Advance working frame if busy (with speed divider)
    static uint32_t animation_speed_divider = 0;
    static int last_busy_state = 0;
    if (is_system_busy && working_frames_count > 0) {
        if (!last_busy_state) {
            animation_speed_divider = 0;
            current_working_frame = 0;
        }

        if (++animation_speed_divider >= 32) {
            animation_speed_divider = 0;
            current_working_frame = (current_working_frame + 1) % working_frames_count;
        }
    } else {
        animation_speed_divider = 0;
    }
    last_busy_state = is_system_busy;

    // Draw new cursor and save its position
    draw_cursor_simple(mouse_x, mouse_y, global_fb);
    scaling_last_mx = mouse_x;
    scaling_last_my = mouse_y;
}

void precompute_scaled_wallpaper(struct multiboot_tag_framebuffer* fb) {
    if (!wallpaper_data || !wall_info.pixels || !fb) return;
    if (!scaled_wallpaper)
        scaled_wallpaper = (uint32_t*)malloc_custom(fb->framebuffer_width * fb->framebuffer_height * sizeof(uint32_t));
    
    // Initialize scaling cursor tracking with current position to erase the click "ghost"
    scaling_last_mx = mouse_x;
    scaling_last_my = mouse_y;

    for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
        for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
            int32_t abs_bmp_h = wall_info.height < 0 ? -wall_info.height : wall_info.height;
            int32_t src_y = (wall_info.height > 0) ? (abs_bmp_h - 1 - (int32_t)(y * abs_bmp_h / fb->framebuffer_height)) : (int32_t)(y * abs_bmp_h / fb->framebuffer_height);
            uint8_t* p = wall_info.pixels + (src_y * wall_info.row_size) + ((int32_t)(x * wall_info.width / fb->framebuffer_width) * (wall_info.bpp / 8));
            scaled_wallpaper[y * fb->framebuffer_width + x] = (p[2] << 16) | (p[1] << 8) | p[0];
        }
        if (y % 4 == 0) kernel_ui_refresh_scaling();
    }
    if (!screen_backbuffer)
        screen_backbuffer = (uint32_t*)malloc_custom(fb->framebuffer_width * fb->framebuffer_height * sizeof(uint32_t));
}

static uint8_t* raw_wallpaper_buffer = 0;
static uint32_t raw_wallpaper_buffer_size = 0;

void set_wallpaper(const char* name) {
    char path[512];
    strcpy_custom(path, "Sysroot:/AnimOS/assets/wallpapers/");
    strcat_custom(path, name);
    
    uint32_t size = vfs_get_file_size(path);
    if (size == 0) return;

    is_system_busy = 1;
    current_working_frame = 0;

    if (!raw_wallpaper_buffer || size > raw_wallpaper_buffer_size) {
        raw_wallpaper_buffer = (uint8_t*)malloc_custom(size + 1);
        raw_wallpaper_buffer_size = size;
    }

    if (raw_wallpaper_buffer && vfs_read_file(path, raw_wallpaper_buffer) == 0) {
        raw_wallpaper_buffer[size] = 0;
        wallpaper_data = raw_wallpaper_buffer;
        init_wallpaper_info();
        precompute_scaled_wallpaper(global_fb);
        
        // Update current_wallpaper in API
        strcpy_custom(kernel_api.current_wallpaper, name);
    }
    is_system_busy = 0;
}

uint32_t get_wallpaper_pixel_fast(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb) {
    if (scaled_wallpaper) {
        if (x >= fb->framebuffer_width || y >= fb->framebuffer_height) return 0;
        return scaled_wallpaper[y * fb->framebuffer_width + x];
    }
    if (!wall_info.pixels) return 0;
    int32_t abs_bmp_h = wall_info.height < 0 ? -wall_info.height : wall_info.height;
    int32_t src_y = (wall_info.height > 0) ? (abs_bmp_h - 1 - (int32_t)(y * abs_bmp_h / fb->framebuffer_height)) : (int32_t)(y * abs_bmp_h / fb->framebuffer_height);
    uint8_t* p = wall_info.pixels + (src_y * wall_info.row_size) + ((int32_t)(x * wall_info.width / fb->framebuffer_width) * (wall_info.bpp / 8));
    return (p[2] << 16) | (p[1] << 8) | p[0];
}

void draw_background(struct multiboot_tag_framebuffer* fb) {
    if (scaled_wallpaper) {
        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            uint8_t* row = (uint8_t*)fb->framebuffer_addr + y * fb->framebuffer_pitch;
            uint32_t* src_row = scaled_wallpaper + y * screen_w;
            if (fb->framebuffer_bpp == 32) { uint32_t* row32 = (uint32_t*)row; for (uint32_t x = 0; x < fb->framebuffer_width; x++) row32[x] = src_row[x]; }
            else if (fb->framebuffer_bpp == 24) {
                for (uint32_t x = 0; x < fb->framebuffer_width; x++) {
                    uint32_t color = src_row[x]; row[x*3] = color & 0xFF; row[x*3+1] = (color >> 8) & 0xFF; row[x*3+2] = (color >> 16) & 0xFF;
                }
            }
        }
    } else {
        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            for (uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, get_wallpaper_pixel_fast(x, y, fb), fb);
        }
    }
}

/* --- Font and String Rendering --- */

int32_t get_attr_value(const char* tag, const char* attr) {
    const char* p = strstr_custom(tag, attr);
    if (!p) return 0;
    while (*p && *p != '\"') p++;
    if (*p == '\"') return atoi_custom(p + 1);
    return 0;
}

int32_t draw_char(uint32_t x, uint32_t y, char c, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (!arial_font_xml_data || !arial_font_data) return 0;
    char search[16] = "<char id=\""; int i = 10; uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else search[i++] = code + '0';
    search[i++] = '\"'; search[i] = 0;
    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    if (!tag) return 0;
    int32_t cx = get_attr_value(tag, " x="), cy = get_attr_value(tag, " y="), cw = get_attr_value(tag, " width="), ch = get_attr_value(tag, " height="), ox = get_attr_value(tag, " xoffset="), oy = get_attr_value(tag, " yoffset="), xa = get_attr_value(tag, " xadvance=");
    struct bmp_file_header* bfh = (struct bmp_file_header*)arial_font_data; struct bmp_info_header* bih = (struct bmp_info_header*)(arial_font_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = arial_font_data + bfh->bfOffBits; uint32_t bpp = bih->biBitCount, row_size = (bih->biWidth * (bpp / 8) + 3) & ~3;
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
    if (!str) return;
    while (*str) { x += draw_char(x, y, *str, color, fb); str++; }
}

int32_t draw_char_scaled(uint32_t x, uint32_t y, char c, uint32_t color, int scale_pct, struct multiboot_tag_framebuffer* fb) {
    if (!arial_font_xml_data || !arial_font_data || scale_pct <= 0) return 0;
    char search[16] = "<char id=\""; int i = 10; uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else search[i++] = code + '0';
    search[i++] = '\"'; search[i] = 0;
    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    if (!tag) return 0;
    int32_t cx = get_attr_value(tag, " x="), cy = get_attr_value(tag, " y="), cw = get_attr_value(tag, " width="), ch = get_attr_value(tag, " height="), ox = get_attr_value(tag, " xoffset="), oy = get_attr_value(tag, " yoffset="), xa = get_attr_value(tag, " xadvance=");
    struct bmp_file_header* bfh = (struct bmp_file_header*)arial_font_data; struct bmp_info_header* bih = (struct bmp_info_header*)(arial_font_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = arial_font_data + bfh->bfOffBits; uint32_t bpp = bih->biBitCount, row_size = (bih->biWidth * (bpp / 8) + 3) & ~3;
    int32_t img_h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
    int32_t sw = (cw * scale_pct) / 100, sh = (ch * scale_pct) / 100, sox = (ox * scale_pct) / 100, soy = (oy * scale_pct) / 100;
    for (int32_t iy = 0; iy < sh; iy++) {
        for (int32_t ix = 0; ix < sw; ix++) {
            uint32_t src_x_start = (ix * 100) / scale_pct, src_x_end = ((ix + 1) * 100) / scale_pct;
            uint32_t src_y_start = (iy * 100) / scale_pct, src_y_end = ((iy + 1) * 100) / scale_pct;
            if (src_x_end <= src_x_start) src_x_end = src_x_start + 1;
            if (src_y_end <= src_y_start) src_y_end = src_y_start + 1;
            uint32_t a_sum = 0, count = 0;
            for (uint32_t sy = src_y_start; sy < src_y_end; sy++) {
                for (uint32_t sx = src_x_start; sx < src_x_end; sx++) {
                    int32_t real_y = (bih->biHeight > 0) ? (img_h - 1 - (cy + sy)) : (cy + sy);
                    uint8_t* p = pixels + (real_y * row_size) + ((cx + sx) * (bpp / 8));
                    a_sum += (bpp == 32) ? p[3] : ((p[0] > 20 || p[1] > 20 || p[2] > 20) ? 255 : 0);
                    count++;
                }
            }
            uint8_t alpha = a_sum / count;
            if (alpha > 20) {
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                uint32_t offset = (y + soy + iy) * fb->framebuffer_pitch + (x + sox + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                draw_pixel(x + sox + ix, y + soy + iy, blend_colors(bg, color, alpha), fb);
            }
        }
    }
    return (xa * scale_pct) / 100;
}

void draw_string_scaled(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale_pct, struct multiboot_tag_framebuffer* fb) {
    if (!str) return;
    while (*str) { x += draw_char_scaled(x, y, *str, color, scale_pct, fb); str++; }
}

int32_t get_char_width(char c) {
    if (!arial_font_xml_data) return 0;
    char search[16] = "<char id=\""; int i = 10; uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else search[i++] = code + '0';
    search[i++] = '\"'; search[i] = 0;
    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    return tag ? get_attr_value(tag, " xadvance=") : 0;
}

uint32_t get_string_width(const char* str) {
    if (!str) return 0; uint32_t w = 0;
    while (*str) { w += get_char_width(*str); str++; }
    return w;
}

int32_t get_char_width_scaled(char c, int scale_pct) {
    if (!arial_font_xml_data || scale_pct <= 0) return 0;
    char search[16] = "<char id=\""; int i = 10; uint8_t code = (uint8_t)c;
    if (code >= 100) { search[i++] = (code / 100) + '0'; search[i++] = ((code / 10) % 10) + '0'; search[i++] = (code % 10) + '0'; }
    else if (code >= 10) { search[i++] = (code / 10) + '0'; search[i++] = (code % 10) + '0'; }
    else search[i++] = code + '0';
    search[i++] = '\"'; search[i] = 0;
    const char* tag = strstr_custom((const char*)arial_font_xml_data, search);
    return tag ? (get_attr_value(tag, " xadvance=") * scale_pct) / 100 : 0;
}

uint32_t get_string_width_scaled(const char* str, int scale_pct) {
    if (!str || scale_pct <= 0) return 0; uint32_t w = 0;
    while (*str) { w += get_char_width_scaled(*str, scale_pct); str++; }
    return w;
}

/* --- UI Components --- */

void draw_icon(uint32_t x, uint32_t y, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb) {
    if (!bmp_data) return;
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data; struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = bmp_data + bfh->bfOffBits; int32_t w = bih->biWidth, h = bih->biHeight, abs_h = h < 0 ? -h : h; uint32_t bpp = bih->biBitCount, row_size = (w * (bpp / 8) + 3) & ~3;
    for (int32_t iy = 0; iy < abs_h; iy++) {
        for (int32_t ix = 0; ix < w; ix++) {
            int32_t src_y = (h > 0) ? (abs_h - 1 - iy) : iy; uint8_t* p = pixels + (src_y * row_size) + (ix * (bpp / 8));
            uint32_t color = (p[2] << 16) | (p[1] << 8) | p[0];
            if (bpp == 32) {
                uint8_t alpha = p[3]; if (alpha == 0) continue;
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr; uint32_t offset = (y + iy) * fb->framebuffer_pitch + (x + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                color = blend_colors(bg, color, alpha);
            }
            draw_pixel(x + ix, y + iy, color, fb);
        }
    }
}

void draw_icon_scaled(uint32_t x, uint32_t y, uint32_t target_w, uint32_t target_h, uint8_t* bmp_data, struct multiboot_tag_framebuffer* fb) {
    if (!bmp_data) return;
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data; struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = bmp_data + bfh->bfOffBits; int32_t w = bih->biWidth, h = bih->biHeight, abs_h = h < 0 ? -h : h; uint32_t bpp = bih->biBitCount, row_size = (w * (bpp / 8) + 3) & ~3;
    for (uint32_t iy = 0; iy < target_h; iy++) {
        for (uint32_t ix = 0; ix < target_w; ix++) {
            uint32_t x_start = ix * w / target_w, x_end = (ix + 1) * w / target_w, y_start_raw = iy * abs_h / target_h, y_end_raw = (iy + 1) * abs_h / target_h;
            if (x_end <= x_start) x_end = x_start + 1; if (y_end_raw <= y_start_raw) y_end_raw = y_start_raw + 1;
            uint64_t r_sq_sum = 0, g_sq_sum = 0, b_sq_sum = 0, a_sum = 0; uint32_t count = 0;
            for (uint32_t sy_raw = y_start_raw; sy_raw < y_end_raw; sy_raw++) {
                for (uint32_t sx = x_start; sx < x_end; sx++) {
                    int32_t src_y = (h > 0) ? (abs_h - 1 - (int32_t)sy_raw) : (int32_t)sy_raw;
                    uint8_t* p = pixels + (src_y * row_size) + (sx * (bpp / 8));
                    b_sq_sum += p[0] * p[0]; g_sq_sum += p[1] * p[1]; r_sq_sum += p[2] * p[2]; a_sum += (bpp == 32) ? p[3] : 255; count++;
                }
            }
            if (count == 0) continue;
            uint32_t avg_color = (sqrt_int(r_sq_sum / count) << 16) | (sqrt_int(g_sq_sum / count) << 8) | sqrt_int(b_sq_sum / count); uint8_t avg_alpha = (uint8_t)(a_sum / count);
            if (avg_alpha == 0) continue;
            if (avg_alpha < 255) {
                uint8_t* screen = (uint8_t*)fb->framebuffer_addr; uint32_t offset = (y + iy) * fb->framebuffer_pitch + (x + ix) * (fb->framebuffer_bpp / 8);
                uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                avg_color = blend_colors(bg, avg_color, avg_alpha);
            }
            draw_pixel(x + ix, y + iy, avg_color, fb);
        }
    }
}

void draw_boot_progress_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int progress, struct multiboot_tag_framebuffer* fb) {
    uint32_t bg_color = 0x222222, border_color = 0x444444;
    for (uint32_t iy = 0; iy < h; iy++) {
        for (uint32_t ix = 0; ix < w; ix++) {
            uint32_t color = bg_color; if (ix == 0 || ix == w - 1 || iy == 0 || iy == h - 1) color = border_color;
            draw_pixel(x + ix, y + iy, color, fb);
        }
    }
    if (progress > 0) {
        if (progress > 100) progress = 100;
        uint32_t fill_w = (w - 4) * progress / 100;
        for (uint32_t iy = 2; iy < h - 2; iy++) {
            for (uint32_t ix = 0; ix < fill_w; ix++) {
                uint8_t alpha = (ix * 255) / (w - 4); uint32_t color = blend_colors(0x00FFFF, 0x800080, alpha);
                draw_pixel(x + 2 + ix, y + iy, color, fb);
            }
        }
    }
}

void draw_dock(struct multiboot_tag_framebuffer* fb) {
    uint32_t margin = 20, dock_h = 55, dock_x = margin, dock_w = fb->framebuffer_width - 2 * margin, dock_y = fb->framebuffer_height - dock_h - margin, radius = 25, dock_color = 0xFFFFFF;
    for (uint32_t y = dock_y; y < dock_y + dock_h; y++) {
        for (uint32_t x = dock_x; x < dock_x + dock_w; x++) {
            uint8_t alpha = 200; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (x < dock_x + radius && y < dock_y + radius) { dx = (dock_x + radius) - x; dy = (dock_y + radius) - y; is_corner = 1; }
            else if (x > dock_x + dock_w - radius && y < dock_y + radius) { dx = x - (dock_x + dock_w - radius); dy = (dock_y + radius) - y; is_corner = 1; }
            else if (x < dock_x + radius && y > dock_y + dock_h - radius) { dx = (dock_x + radius) - x; dy = y - (dock_y + dock_h - radius); is_corner = 1; }
            else if (x > dock_x + dock_w - radius && y > dock_y + dock_h - radius) { dx = x - (dock_x + dock_w - radius); dy = y - (dock_y + dock_h - radius); is_corner = 1; }
            if (is_corner) { uint32_t dist_sq = dx*dx + dy*dy, r_sq = radius * radius, inner_r_sq = (radius - 1) * (radius - 1); if (dist_sq >= r_sq) alpha = 0; else if (dist_sq > inner_r_sq) alpha = (200 * (r_sq - dist_sq)) / (r_sq - inner_r_sq); }
            if (alpha > 0) draw_pixel(x, y, blend_colors(get_wallpaper_pixel_fast(x, y, fb), dock_color, alpha), fb);
        }
    }
    if (power_icon_data) {
        struct bmp_info_header* icon_bih = (struct bmp_info_header*)(power_icon_data + sizeof(struct bmp_file_header));
        int32_t icon_h = icon_bih->biHeight < 0 ? -icon_bih->biHeight : icon_bih->biHeight;
        draw_icon(dock_x + 15, dock_y + (dock_h - icon_h) / 2, power_icon_data, fb);
    }
    uint8_t h, m; get_time(&h, &m); char time_str[6] = { (h/10)+'0', (h%10)+'0', ':', (m/10)+'0', (m%10)+'0', 0 };
    draw_string(dock_x + dock_w - 80, dock_y + 17, time_str, 0x333333, fb);

    // App Icons in the middle
    {
        uint8_t* app_icons[2] = { file_explorer_icon_data, preferences_icon_data };
        int app_open_flags[2] = { file_explorer_window_open, preferences_window_open };
        int icon_count = 0;
        for (int i = 0; i < 2; i++) if (app_open_flags[i] && app_icons[i]) icon_count++;
        if (icon_count > 0) {
            uint32_t icon_size = 32;
            uint32_t gap = 12;
            uint32_t total_w = icon_count * icon_size + (icon_count - 1) * gap;
            uint32_t start_x = dock_x + (dock_w - total_w) / 2;
            int icon_index = 0;
            for (int i = 0; i < 2; i++) {
                if (!app_open_flags[i] || !app_icons[i]) continue;
                uint32_t icon_x = start_x + icon_index * (icon_size + gap);
                uint32_t icon_y = dock_y + (dock_h - icon_size) / 2;
                draw_icon_scaled(icon_x, icon_y, icon_size, icon_size, app_icons[i], fb);
                // Draw indicator stripe: longer blue for visible active app, shorter gray for minimized/inactive
                if (active_app == i && !app_minimized[i]) {
                    // Visible active app: longer blue stripe
                    draw_rect(icon_x + icon_size / 2 - 5, dock_y + dock_h - 6, 10, 2, 0x0078D7, fb);
                } else if (app_minimized[i] || active_app != i) {
                    // Minimized or inactive: shorter gray stripe
                    draw_rect(icon_x + icon_size / 2 - 3, dock_y + dock_h - 6, 6, 2, 0x888888, fb);
                }
                icon_index++;
            }
        }
    }
}

void draw_status_bar(struct multiboot_tag_framebuffer* fb) {
    uint32_t margin = 20, bar_h = 36, bar_w = 200, bar_x = fb->framebuffer_width - margin - bar_w, bar_y = margin, radius = 18, bar_color = 0xFFFFFF;
    for (uint32_t y = bar_y; y < bar_y + bar_h; y++) {
        for (uint32_t x = bar_x; x < bar_x + bar_w; x++) {
            uint8_t alpha = 200; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (x < bar_x + radius && y < bar_y + radius) { dx = (bar_x + radius) - x; dy = (bar_y + radius) - y; is_corner = 1; }
            else if (x > bar_x + bar_w - radius && y < bar_y + radius) { dx = x - (bar_x + bar_w - radius); dy = (bar_y + radius) - y; is_corner = 1; }
            else if (x < bar_x + radius && y > bar_y + bar_h - radius) { dx = (bar_x + radius) - x; dy = y - (bar_y + bar_h - radius); is_corner = 1; }
            else if (x > bar_x + bar_w - radius && y > bar_y + bar_h - radius) { dx = x - (bar_x + bar_w - radius); dy = y - (bar_y + bar_h - radius); is_corner = 1; }
            if (is_corner) { uint32_t dist_sq = dx*dx + dy*dy, r_sq = radius * radius, inner_r_sq = (radius - 1) * (radius - 1); if (dist_sq >= r_sq) alpha = 0; else if (dist_sq > inner_r_sq) alpha = (200 * (r_sq - dist_sq)) / (r_sq - inner_r_sq); }
            if (alpha > 0) draw_pixel(x, y, blend_colors(get_wallpaper_pixel_fast(x, y, fb), bar_color, alpha), fb);
        }
    }
    uint8_t* icon = (net_dhcp_ok() && net_status == 3) ? online_icon_data : offline_icon_data; uint32_t icon_size = 24;
    draw_icon_scaled(bar_x + bar_w - icon_size - 5, bar_y + (bar_h - icon_size) / 2, icon_size, icon_size, icon, fb);
}

void draw_desktop_icons(struct multiboot_tag_framebuffer* fb) {
    if ((preferences_window_open || file_explorer_window_open) && kernel_api.window_maximized && !kernel_api.window_minimized) return;
    uint32_t icon_xs[] = {30, 30}, icon_ys[] = {30, 130}; const char* labels[] = {"File explorer", "Preferences"}; uint8_t* icon_datas[] = {file_explorer_icon_data, preferences_icon_data};
    for (int i = 0; i < 2; i++) {
        if (!icon_datas[i]) continue;
        uint32_t icon_x = icon_xs[i], icon_y = icon_ys[i], icon_w = 48, icon_h = 48, label_w = get_string_width_scaled(labels[i], 65);
        uint32_t total_w = (label_w > icon_w) ? label_w + 20 : icon_w + 20, total_h = icon_h + 5 + 18 + 10, rect_x = icon_x + icon_w/2 - total_w/2, rect_y = icon_y - 5;
        if (total_w > 150) total_w = 150; if (total_h > 150) total_h = 150;
        struct multiboot_tag_framebuffer temp_fb = *fb; temp_fb.framebuffer_addr = (uint64_t)icon_area_buffer; temp_fb.framebuffer_width = total_w; temp_fb.framebuffer_height = total_h; temp_fb.framebuffer_pitch = total_w * (fb->framebuffer_bpp / 8);
        for (uint32_t y = 0; y < total_h; y++) {
            for (uint32_t x = 0; x < total_w; x++) {
                uint32_t bg = get_wallpaper_pixel_fast(rect_x + x, rect_y + y, fb);
                if (selected_icon == i || hover_icon == i) {
                    uint32_t radius = 10, dx = 0, dy = 0; int is_corner = 0; uint8_t a = 100;
                    if (x < radius && y < radius) { dx = radius - x; dy = radius - y; is_corner = 1; }
                    else if (x > total_w - radius && y < radius) { dx = x - (total_w - radius); dy = radius - y; is_corner = 1; }
                    else if (x < radius && y > total_h - radius) { dx = radius - x; dy = y - (total_h - radius); is_corner = 1; }
                    else if (x > total_w - radius && y > total_h - radius) { dx = x - (total_w - radius); dy = y - (total_h - radius); is_corner = 1; }
                    if (is_corner && dx*dx + dy*dy >= radius * radius) a = 0;
                    if (a > 0) bg = blend_colors(bg, 0xAAAAAA, a);
                }
                uint32_t offset = y * temp_fb.framebuffer_pitch + x * (fb->framebuffer_bpp / 8);
                if (fb->framebuffer_bpp == 32) *(uint32_t*)((uint8_t*)temp_fb.framebuffer_addr + offset) = bg;
                else { uint8_t* p = (uint8_t*)temp_fb.framebuffer_addr + offset; p[0] = bg & 0xFF; p[1] = (bg >> 8) & 0xFF; p[2] = (bg >> 16) & 0xFF; }
            }
        }
        draw_icon_scaled(icon_x - rect_x, icon_y - rect_y, icon_w, icon_h, icon_datas[i], &temp_fb);
        draw_string_scaled((uint32_t)((int32_t)(icon_x - rect_x) + (int32_t)(icon_w / 2) - (int32_t)(label_w / 2)), icon_y - rect_y + icon_h + 5, labels[i], 0xFFFFFF, 65, &temp_fb);
        for (uint32_t y = 0; y < total_h; y++) {
            for (uint32_t x = 0; x < total_w; x++) {
                uint32_t offset = y * temp_fb.framebuffer_pitch + x * (fb->framebuffer_bpp / 8), color;
                if (fb->framebuffer_bpp == 32) color = *(uint32_t*)((uint8_t*)temp_fb.framebuffer_addr + offset);
                else { uint8_t* p = (uint8_t*)temp_fb.framebuffer_addr + offset; color = (p[2] << 16) | (p[1] << 8) | p[0]; }
                draw_pixel(rect_x + x, rect_y + y, color, fb);
            }
        }
    }
}

void draw_power_menu(struct multiboot_tag_framebuffer* fb, int progress) {
    uint32_t margin = 20, dock_h = 55, dock_x = margin, dock_y = fb->framebuffer_height - dock_h - margin;
    uint32_t menu_w = 200, menu_h = 120, menu_x = dock_x, radius = 15;
    int32_t target_y = dock_y - menu_h - 10, start_y = dock_y, current_y = start_y + (target_y - start_y) * progress / 100, area_y = target_y, area_h = start_y - target_y;
    for (int32_t y = 0; y < area_h; y++) {
        for (int32_t x = 0; x < (int32_t)menu_w; x++) {
            uint32_t sx = menu_x + x, sy = area_y + y;
            if (screen_backbuffer && sx < screen_w && sy < screen_h) menu_area_buffer[y * menu_w + x] = screen_backbuffer[sy * screen_w + sx];
            else menu_area_buffer[y * menu_w + x] = get_wallpaper_pixel_fast(sx, sy, fb);
        }
    }
    if (progress > 0) {
        int32_t menu_rel_y = current_y - area_y;
        for (int32_t y = 0; y < (int32_t)menu_h; y++) {
            int32_t buffer_y = menu_rel_y + y; if (buffer_y < 0 || buffer_y >= area_h) continue;
            for (int32_t x = 0; x < (int32_t)menu_w; x++) {
                uint8_t alpha = 220; uint32_t dx = 0, dy = 0; int is_corner = 0;
                if (x < (int32_t)radius && y < (int32_t)radius) { dx = radius - x; dy = radius - y; is_corner = 1; }
                else if (x > (int32_t)menu_w - (int32_t)radius && y < (int32_t)radius) { dx = x - (menu_w - radius); dy = radius - y; is_corner = 1; }
                else if (x < (int32_t)radius && y > (int32_t)menu_h - (int32_t)radius) { dx = radius - x; dy = y - (menu_h - radius); is_corner = 1; }
                else if (x > (int32_t)menu_w - (int32_t)radius && y > (int32_t)menu_h - (int32_t)radius) { dx = x - (menu_w - radius); dy = y - (menu_h - radius); is_corner = 1; }
                if (is_corner) { uint32_t dist_sq = dx*dx + dy*dy; if (dist_sq >= radius * radius) alpha = 0; else if (dist_sq > (radius-1)*(radius-1)) alpha = (220 * (radius*radius - dist_sq)) / (radius*radius - (radius-1)*(radius-1)); }
                if (alpha > 0) {
                    uint32_t color = 0xFFFFFF; if (is_corner) { if (dx*dx+dy*dy > (radius-2)*(radius-2)) color = 0xCCCCCC; }
                    else if (x == 0 || x == (int32_t)menu_w - 1 || y == 0 || y == (int32_t)menu_h - 1) color = 0xCCCCCC;
                    menu_area_buffer[buffer_y * menu_w + x] = blend_colors(menu_area_buffer[buffer_y * menu_w + x], color, alpha);
                }
            }
        }
    }
    for (int32_t y = 0; y < area_h; y++) for (int32_t x = 0; x < (int32_t)menu_w; x++) draw_pixel(menu_x + x, area_y + y, menu_area_buffer[y * menu_w + x], fb);
    if (progress == 100) { draw_string(menu_x + 20, current_y + 20, "Restart", 0x333333, fb); for(uint32_t x = menu_x + 10; x < menu_x + menu_w - 10; x++) draw_pixel(x, current_y + 60, 0xBBBBBB, fb); draw_string(menu_x + 20, current_y + 80, "Shutdown", 0x333333, fb); }
}

void draw_dialog(struct multiboot_tag_framebuffer* fb, const char* title, const char* msg) {
    uint32_t msg_w = get_string_width(msg), w = msg_w + 80; if (w < 350) w = 350;
    uint32_t h = 220, x = (fb->framebuffer_width - w) / 2, y = (fb->framebuffer_height - h) / 2, radius = 15;
    for (uint32_t iy = y; iy < y + h; iy++) {
        for (uint32_t ix = x; ix < x + w; ix++) {
            uint8_t alpha = 245; uint32_t dx = 0, dy = 0; int is_corner = 0;
            if (ix < x + radius && iy < y + radius) { dx = (x + radius) - ix; dy = (y + radius) - iy; is_corner = 1; }
            else if (ix > x + w - radius && iy < y + radius) { dx = ix - (x + w - radius); dy = (y + radius) - iy; is_corner = 1; }
            else if (ix < x + radius && iy > y + h - radius) { dx = (x + radius) - ix; dy = iy - (y + h - radius); is_corner = 1; }
            else if (ix > x + w - radius && iy > y + h - radius) { dx = ix - (x + w - radius); dy = iy - (y + h - radius); is_corner = 1; }
            if (is_corner && dx*dx + dy*dy >= radius * radius) alpha = 0;
            if (alpha > 0) {
                uint32_t bg = (screen_backbuffer && ix < screen_w && iy < screen_h) ? screen_backbuffer[iy * screen_w + ix] : get_wallpaper_pixel_fast(ix, iy, fb);
                draw_pixel(ix, iy, blend_colors(bg, 0xFFFFFF, alpha), fb);
            }
        }
    }
    draw_string(x + 25, y + 25, title, 0x222222, fb); for(uint32_t ix = x + 10; ix < x + w - 10; ix++) draw_pixel(ix, y + 65, 0xBBBBBB, fb);
    draw_string(x + (w - msg_w) / 2, y + 95, msg, 0x444444, fb); uint32_t btn_w = 120, btn_h = 45, spacing = 20, total_btns_w = 2 * btn_w + spacing, start_btn_x = x + (w - total_btns_w) / 2, btn_y = y + 145;
    for(uint32_t iy = 0; iy < btn_h; iy++) for(uint32_t ix = 0; ix < btn_w; ix++) draw_pixel(start_btn_x + ix, btn_y + iy, 0xFF5555, fb);
    draw_string(start_btn_x + (btn_w - get_string_width("Yes")) / 2, btn_y + (btn_h - 18) / 2, "Yes", 0xFFFFFF, fb);
    for(uint32_t iy = 0; iy < btn_h; iy++) for(uint32_t ix = 0; ix < btn_w; ix++) draw_pixel(start_btn_x + btn_w + spacing + ix, btn_y + iy, 0xDDDDDD, fb);
    draw_string(start_btn_x + btn_w + spacing + (btn_w - get_string_width("Cancel")) / 2, btn_y + (btn_h - 18) / 2, "Cancel", 0x333333, fb);
}

/* --- Window and App Management --- */

void get_mouse_pos(int32_t* mx, int32_t* my, uint8_t* clicked, int32_t* wheel) { 
    *mx = frame_mx; *my = frame_my; *clicked = frame_clicked;
    *wheel = frame_wheel;
    if (!kernel_api.window_maximized && (preferences_window_open || file_explorer_window_open)) {
        uint32_t win_w = 800, win_h = 600;
        *mx -= (screen_w - win_w) / 2;
        *my -= (screen_h - win_h) / 2;
    }
}

void ntp_sync(uint32_t server_ip); void net_poll();
static uint32_t ntp_retry_timer = 0; static uint32_t link_stable_count = 0;

void kernel_yield() {
    net_poll(); int current_link = e1000_link_up();
    if (current_link) { if (link_stable_count < 10) link_stable_count++; } else link_stable_count = 0;
    if (net_status != 0 && net_status != 4 && net_status != 5) { if (link_stable_count < 5) { if (net_status != 6) net_status = 6; } else if (net_status == 6) net_status = 1; }
    if (ntp_retry_timer > 0) ntp_retry_timer--;
    if (net_status == 1 || (net_status == 2 && ntp_retry_timer == 0) || (net_status == 3 && ntp_retry_timer == 0) || (net_status >= 7 && ntp_retry_timer == 0)) {
        if (link_stable_count >= 5) { if (!net_dhcp_ok()) { if (net_status != 8) net_status = 7; } ntp_sync((162) | (159 << 8) | (200 << 16) | (1 << 24)); if (net_status == 1) net_status = 2; ntp_retry_timer = 500; }
    }
}

void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color, struct multiboot_tag_framebuffer* fb) {
    if (!fb) fb = global_fb;
    for (uint32_t iy = y; iy < y + h; iy++) {
        if (iy >= fb->framebuffer_height) break;
        for (uint32_t ix = x; ix < x + w; ix++) {
            if (ix >= fb->framebuffer_width) break;
            
            uint32_t dx = 0, dy = 0;
            int is_corner = 0;
            if (ix < x + radius && iy < y + radius) { dx = (x + radius) - ix; dy = (y + radius) - iy; is_corner = 1; }
            else if (ix > x + w - radius && iy < y + radius) { dx = ix - (x + w - radius); dy = (y + radius) - iy; is_corner = 1; }
            else if (ix < x + radius && iy > y + h - radius) { dx = (x + radius) - ix; dy = iy - (y + h - radius); is_corner = 1; }
            else if (ix > x + w - radius && iy > y + h - radius) { dx = ix - (x + w - radius); dy = iy - (y + h - radius); is_corner = 1; }
            
            if (is_corner) {
                uint32_t dist_sq = dx*dx + dy*dy;
                uint32_t r_sq = radius * radius;
                uint32_t inner_r_sq = (radius > 0) ? (radius - 1) * (radius - 1) : 0;
                
                if (dist_sq >= r_sq) continue;
                if (dist_sq > inner_r_sq) {
                    uint8_t alpha = (255 * (r_sq - dist_sq)) / (r_sq - inner_r_sq);
                    uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                    uint32_t offset = iy * fb->framebuffer_pitch + ix * (fb->framebuffer_bpp / 8);
                    uint32_t bg = (fb->framebuffer_bpp == 32) ? *(uint32_t*)(screen + offset) : (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                    draw_pixel(ix, iy, blend_colors(bg, color, alpha), fb);
                } else {
                    draw_pixel(ix, iy, color, fb);
                }
            } else {
                draw_pixel(ix, iy, color, fb);
            }
        }
    }
}

static int current_timezone_offset = 1;
static int ntp_automatic_mode = 1;

void set_timezone_offset(int offset) {
    if (offset == -999) { 
        ntp_automatic_mode = 1;
        current_timezone_offset = 1; // Default to GMT+1 (Budapest/Paris base)
    } else {
        ntp_automatic_mode = 0;
        current_timezone_offset = offset;
    }
}

void kernel_set_mouse_speed(int speed) {
    mouse_speed = speed;
}

int kernel_get_mouse_speed() {
    return mouse_speed;
}

void kernel_set_scroll_speed(int lines) {
    mouse_scroll_speed = lines;
}

int kernel_get_scroll_speed() {
    return mouse_scroll_speed;
}

void kernel_set_primary_button(int button) {
    mouse_primary_button = button;
}

int kernel_get_primary_button() {
    return mouse_primary_button;
}

uint8_t kernel_get_mouse_button_state() {
    return mouse_left_button;
}

const char* kernel_get_timezone() {
    if (ntp_automatic_mode) return "NTP (Automatic)";
    static char tz_buf[16];
    if (current_timezone_offset >= 0) {
        tz_buf[0] = 'G'; tz_buf[1] = 'M'; tz_buf[2] = 'T'; tz_buf[3] = '+';
        int off = current_timezone_offset;
        tz_buf[4] = (off / 10) + '0'; tz_buf[5] = (off % 10) + '0'; tz_buf[6] = 0;
    } else {
        tz_buf[0] = 'G'; tz_buf[1] = 'M'; tz_buf[2] = 'T'; tz_buf[3] = '-';
        int off = -current_timezone_offset;
        tz_buf[4] = (off / 10) + '0'; tz_buf[5] = (off % 10) + '0'; tz_buf[6] = 0;
    }
    return tz_buf;
}

static int last_dst_check_hour = -1;
static int cached_dst = 0;

int kernel_get_timezone_offset() {
    int offset = current_timezone_offset;
    uint64_t ntp_time = ntp_get_time();
    int year, mon, day, hour;

    if (ntp_time != 0) {
        struct tm_custom t;
        unix_to_tm(ntp_time, &t);
        year = t.year; mon = t.mon; day = t.mday; hour = t.hour;
    } else {
        if (read_rtc_reg(0x0A) & 0x80) return offset + cached_dst;
        hour = read_rtc_reg(0x04);
        day = read_rtc_reg(0x07);
        mon = read_rtc_reg(0x08);
        int y_b = read_rtc_reg(0x09);
        uint8_t regB = read_rtc_reg(0x0B);
        if (!(regB & 0x04)) {
            hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
            day = (day & 0x0F) + ((day / 16) * 10);
            mon = (mon & 0x0F) + ((mon / 16) * 10);
            y_b = (y_b & 0x0F) + ((y_b / 16) * 10);
        }
        if (!(regB & 0x02) && (hour & 0x80)) hour = ((hour & 0x7F) + 12) % 24;
        year = 2000 + y_b;
    }

    if (hour != last_dst_check_hour) {
        last_dst_check_hour = hour;
        cached_dst = 0;
        if (offset == 0 || offset == 1 || offset == 2) {
            if (is_dst_eu(year, mon, day, hour)) cached_dst = 1;
        } else if (offset == -5 || offset == -8) {
            if (is_dst_us(year, mon, day, hour)) cached_dst = 1;
        }
    }
    return offset + cached_dst;
}
void init_kernel_api() {
    kernel_api.draw_pixel = draw_pixel; kernel_api.blend_colors = blend_colors; kernel_api.get_wallpaper_pixel = get_wallpaper_pixel_fast;
    kernel_api.draw_string_scaled = draw_string_scaled; kernel_api.get_string_width_scaled = get_string_width_scaled; kernel_api.draw_icon_scaled = draw_icon_scaled;
    kernel_api.close_icon = close_icon_data; kernel_api.maximize_icon = maximize_icon_data; kernel_api.minimize_icon = minimize_icon_data;
    kernel_api.boot_logo = boot_logo_data;
    kernel_api.window_buffer = preferences_window_buffer; kernel_api.draw_rect = draw_rect; 
    kernel_api.draw_rounded_rect = draw_rounded_rect;
    kernel_api.blit_buffer = blit_buffer;
    kernel_api.get_mouse_pos = get_mouse_pos; kernel_api.yield = kernel_yield;
    kernel_api.list_dir = vfs_list_dir;
    kernel_api.set_wallpaper = set_wallpaper;
    kernel_api.net_get_ip = net_get_ip;
    kernel_api.net_get_subnet = net_get_subnet;
    kernel_api.net_get_gateway = net_get_gateway;
    kernel_api.net_get_dns_primary = net_get_dns_primary;
    kernel_api.net_get_dns_secondary = net_get_dns_secondary;
    kernel_api.net_get_nic_name = net_get_nic_name;
    kernel_api.net_get_mac = net_get_mac;
    kernel_api.net_get_last_sync_time = net_get_last_sync_time;
    kernel_api.net_get_ntp_server = net_get_ntp_server;
    kernel_api.get_timezone = kernel_get_timezone;
    kernel_api.get_timezone_offset = kernel_get_timezone_offset;
    kernel_api.set_timezone_offset = set_timezone_offset;
    kernel_api.set_mouse_speed = kernel_set_mouse_speed;
    kernel_api.get_mouse_speed = kernel_get_mouse_speed;
    kernel_api.set_scroll_speed = kernel_set_scroll_speed;
    kernel_api.get_scroll_speed = kernel_get_scroll_speed;
    kernel_api.set_primary_button = kernel_set_primary_button;
    kernel_api.get_primary_button = kernel_get_primary_button;
    kernel_api.get_mouse_button_state = kernel_get_mouse_button_state;
    kernel_api.load_asset = load_asset;

    get_cpu_brand(kernel_api.cpu_brand);
    
    // Set current wallpaper name
    const char* default_wall = "bubble.bmp";
    int wall_idx = 0;
    while(default_wall[wall_idx] && wall_idx < 255) {
        kernel_api.current_wallpaper[wall_idx] = default_wall[wall_idx];
        wall_idx++;
    }
    kernel_api.current_wallpaper[wall_idx] = 0;

    kernel_api.ram_size_mb = global_ram_mb;
    
    uint8_t boot_drive = vfs_get_boot_drive();
    kernel_api.disk_size_gb = ata_get_size_gb(boot_drive);
    kernel_api.disk_size_mb = ata_get_size_mb(boot_drive);
    kernel_api.disk_used_mb = vfs_get_used_space_mb();
    kernel_api.window_maximized = 1;
    kernel_api.window_minimized = 0;
}

static uint8_t* preferences_bin_cache = 0; static uint32_t preferences_bin_size = 0;

static int get_app_cache_index(const char* path) {
    for (int i = 0; i < 2; i++) {
        const char* a = app_bin_paths[i];
        const char* b = path;
        int equal = 1;
        while (*a && *b) {
            if (*a != *b) { equal = 0; break; }
            a++; b++;
        }
        if (equal && *a == 0 && *b == 0) return i;
    }
    return -1;
}

void run_app(const char* path, struct multiboot_tag_framebuffer* fb, app_event_t event) {
    int idx = get_app_cache_index(path);
    if (idx < 0) return;

    int is_fe_init = (idx == 0 && event == APP_EVENT_INIT);
    if (is_fe_init) {
        is_system_busy = 1;
        current_working_frame = 0;
    }

    uint8_t* app_memory = (uint8_t*)0x8000000;
    if (!app_bin_cache[idx]) {
        uint32_t size = vfs_get_file_size(path);
        if (size == 0) {
            if (is_fe_init) is_system_busy = 0;
            return;
        }
        app_bin_cache[idx] = (uint8_t*)malloc_custom(size);
        if (vfs_read_file(path, app_bin_cache[idx]) != 0) {
            if (is_fe_init) is_system_busy = 0;
            return;
        }
        app_bin_size[idx] = size;
    }
    
    if (!app_state_buffers[idx]) {
        app_state_buffers[idx] = (uint8_t*)malloc_custom(0x100000);
    }
    
    if (currently_loaded_app != idx) {
        // Save the state of the currently loaded app if it was valid
        if (currently_loaded_app >= 0 && app_state_buffers[currently_loaded_app]) {
            uint8_t* src = (uint8_t*)0x8000000;
            uint8_t* dest = app_state_buffers[currently_loaded_app];
            for (uint32_t i = 0; i < 0x100000; i++) {
                dest[i] = src[i];
            }
            app_state_saved[currently_loaded_app] = 1;
        }
        
        // Load the new app's saved state, or initialize it fresh
        if (app_state_saved[idx] && event != APP_EVENT_INIT) {
            uint8_t* src = app_state_buffers[idx];
            uint8_t* dest = (uint8_t*)0x8000000;
            for (uint32_t i = 0; i < 0x100000; i++) {
                dest[i] = src[i];
            }
            currently_loaded_app = idx;
        } else {
            for (uint32_t i = 0; i < 0x100000; i++) app_memory[i] = 0;
            uint64_t* src64 = (uint64_t*)app_bin_cache[idx];
            uint64_t* dest64 = (uint64_t*)app_memory;
            uint32_t blocks = app_bin_size[idx] / 8;
            for (uint32_t i = 0; i < blocks; i++) dest64[i] = src64[i];
            for (uint32_t i = blocks * 8; i < app_bin_size[idx]; i++) app_memory[i] = app_bin_cache[idx][i];
            currently_loaded_app = idx;
            app_state_saved[idx] = 1;
            event = APP_EVENT_INIT; // Force initialization
        }
    } else if (event == APP_EVENT_INIT) {
        for (uint32_t i = 0; i < 0x100000; i++) app_memory[i] = 0;
        uint64_t* src64 = (uint64_t*)app_bin_cache[idx];
        uint64_t* dest64 = (uint64_t*)app_memory;
        uint32_t blocks = app_bin_size[idx] / 8;
        for (uint32_t i = 0; i < blocks; i++) dest64[i] = src64[i];
        for (uint32_t i = blocks * 8; i < app_bin_size[idx]; i++) app_memory[i] = app_bin_cache[idx][i];
        app_state_saved[idx] = 1;
    }
    
    app_entry_t entry = (app_entry_t)app_memory;
    entry(&kernel_api, fb, event);

    if (is_fe_init) {
        is_system_busy = 0;
    }
}

void draw_preferences_window(struct multiboot_tag_framebuffer* fb, app_event_t event) { 
    if (kernel_api.window_maximized) {
        run_app("Sysroot:/AnimOS/apps/preferences.bin", fb, event);
    } else {
        uint32_t win_w = 800, win_h = 600;
        uint32_t win_x = (fb->framebuffer_width - win_w) / 2, win_y = (fb->framebuffer_height - win_h) / 2;
        
        struct multiboot_tag_framebuffer vfb = *fb;
        vfb.framebuffer_width = win_w; vfb.framebuffer_height = win_h;
        vfb.framebuffer_addr += win_y * fb->framebuffer_pitch + win_x * (fb->framebuffer_bpp / 8);
        run_app("Sysroot:/AnimOS/apps/preferences.bin", &vfb, event);
    }
}

void draw_file_explorer_window(struct multiboot_tag_framebuffer* fb, app_event_t event) {
    if (kernel_api.window_maximized) {
        run_app("Sysroot:/AnimOS/apps/file_explorer.bin", fb, event);
    } else {
        uint32_t win_w = 800, win_h = 600;
        uint32_t win_x = (fb->framebuffer_width - win_w) / 2, win_y = (fb->framebuffer_height - win_h) / 2;
        
        struct multiboot_tag_framebuffer vfb = *fb;
        vfb.framebuffer_width = win_w; vfb.framebuffer_height = win_h;
        vfb.framebuffer_addr += win_y * fb->framebuffer_pitch + win_x * (fb->framebuffer_bpp / 8);
        run_app("Sysroot:/AnimOS/apps/file_explorer.bin", &vfb, event);
    }
}

/* --- Cursor Handling --- */

void find_ani_frames(uint8_t* data, uint32_t size) {
    working_frames_count = 0;
    if (!data) return;
    if (memcmp_custom(data, "RIFF", 4) != 0) return;
    if (memcmp_custom(data + 8, "ACON", 4) != 0) return;
    
    uint32_t pos = 12;
    while (pos < size - 8) {
        if (memcmp_custom(data + pos, "icon", 4) == 0) {
            uint32_t chunk_size = *(uint32_t*)(data + pos + 4);
            if (working_frames_count < 64) {
                working_frames_offsets[working_frames_count++] = pos + 8;
            }
            pos += 8 + chunk_size;
            if (chunk_size % 2) pos++; 
        } else if (memcmp_custom(data + pos, "LIST", 4) == 0) {
            pos += 12; 
        } else {
            uint32_t chunk_size = *(uint32_t*)(data + pos + 4);
            pos += 8 + chunk_size;
            if (chunk_size % 2) pos++;
        }
    }
}

void draw_cursor_simple(int32_t mx, int32_t my, struct multiboot_tag_framebuffer* fb) {
    if (!cursor_data) return;
    uint8_t* actual_cursor = cursor_data;
    if (is_system_busy && working_cursor_data && working_frames_count > 0) {
        actual_cursor = working_cursor_data + working_frames_offsets[current_working_frame];
    }
    uint8_t* dib = actual_cursor + 6 + 16; struct bmp_info_header* bih = (struct bmp_info_header*)dib; uint8_t* pixels = dib + bih->biSize;
    int32_t w = bih->biWidth, h = bih->biHeight / 2;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t px = mx + x, py = my + y; if (px < 0 || px >= (int32_t)fb->framebuffer_width || py < 0 || py >= (int32_t)fb->framebuffer_height) continue;
            uint8_t* p = pixels + ((h - 1 - y) * w * 4) + (x * 4);
            uint8_t alpha = p[3];
            if (alpha > 0) {
                uint32_t color = (p[2] << 16) | (p[1] << 8) | p[0];
                if (alpha == 255) {
                    draw_pixel(px, py, color, fb);
                } else {
                    uint8_t* screen = (uint8_t*)fb->framebuffer_addr;
                    uint32_t offset = py * fb->framebuffer_pitch + px * (fb->framebuffer_bpp / 8);
                    uint32_t bg;
                    if (fb->framebuffer_bpp == 32) bg = *(uint32_t*)(screen + offset);
                    else bg = (screen[offset+2] << 16) | (screen[offset+1] << 8) | screen[offset];
                    draw_pixel(px, py, blend_colors(bg, color, alpha), fb);
                }
            }
        }
    }
}

/* --- Rendering Engine --- */

void compose_frame(struct multiboot_tag_framebuffer* real_fb, uint64_t multiboot_addr) {
    if (!screen_backbuffer) return;
    struct multiboot_tag_framebuffer back_fb = *real_fb; back_fb.framebuffer_addr = (uint64_t)screen_backbuffer; back_fb.framebuffer_pitch = real_fb->framebuffer_width * 4; back_fb.framebuffer_bpp = 32;
    int any_window_open = preferences_window_open || file_explorer_window_open;
    int is_loading = (file_explorer_window_open && file_explorer_needs_init) || (preferences_window_open && preferences_needs_init);
    
    if (!is_loading) {
        draw_background(&back_fb);
        int show_desktop = !any_window_open || !kernel_api.window_maximized || kernel_api.window_minimized;
        if (show_desktop) {
            draw_desktop_icons(&back_fb);
            draw_dock(&back_fb); 
            draw_status_bar(&back_fb);
        }
    }
    
    if (any_window_open && active_app >= 0) {
        if (!kernel_api.window_minimized) {
            if (active_app == 0) draw_file_explorer_window(&back_fb, file_explorer_needs_init ? APP_EVENT_INIT : APP_EVENT_TICK);
            else draw_preferences_window(&back_fb, preferences_needs_init ? APP_EVENT_INIT : APP_EVENT_TICK);
        } else {
            struct multiboot_tag_framebuffer dummy_fb = back_fb;
            dummy_fb.framebuffer_addr = (uint64_t)preferences_window_buffer;
            if (active_app == 0) run_app("Sysroot:/AnimOS/apps/file_explorer.bin", &dummy_fb, file_explorer_needs_init ? APP_EVENT_INIT : APP_EVENT_TICK);
            else run_app("Sysroot:/AnimOS/apps/preferences.bin", &dummy_fb, preferences_needs_init ? APP_EVENT_INIT : APP_EVENT_TICK);
        }
        if (active_app == 0) { file_explorer_needs_init = 0; app_maximized[0] = kernel_api.window_maximized; app_minimized[0] = kernel_api.window_minimized; }
        else if (active_app == 1) { preferences_needs_init = 0; app_maximized[1] = kernel_api.window_maximized; app_minimized[1] = kernel_api.window_minimized; }
    }
    
    if (power_menu_progress > 0) draw_power_menu(&back_fb, power_menu_progress);
    if (dialog_state == 1) draw_dialog(&back_fb, "Restart", "Are you sure you want to restart the system?");
    else if (dialog_state == 2) draw_dialog(&back_fb, "Shutdown", "Are you sure you want to shutdown the system?");
    
    // Save area under cursor from screen_backbuffer
    int32_t cursor_saved_x = frame_mx;
    int32_t cursor_saved_y = frame_my;
    int32_t cursor_saved_w = 0;
    int32_t cursor_saved_h = 0;
    
    if (cursor_data) {
        uint8_t* actual_cursor = cursor_data;
        if (is_system_busy && working_cursor_data && working_frames_count > 0) {
            actual_cursor = working_cursor_data + working_frames_offsets[current_working_frame];
        }
        uint8_t* dib = actual_cursor + 6 + 16;
        struct bmp_info_header* bih = (struct bmp_info_header*)dib;
        cursor_saved_w = bih->biWidth;
        cursor_saved_h = bih->biHeight / 2;
        if (cursor_saved_w > 64) cursor_saved_w = 64;
        if (cursor_saved_h > 64) cursor_saved_h = 64;
        
        for (int32_t y = 0; y < cursor_saved_h; y++) {
            for (int32_t x = 0; x < cursor_saved_w; x++) {
                int32_t px = cursor_saved_x + x;
                int32_t py = cursor_saved_y + y;
                if (px >= 0 && px < (int32_t)screen_w && py >= 0 && py < (int32_t)screen_h) {
                    cursor_buffer[y * cursor_saved_w + x] = screen_backbuffer[py * screen_w + px];
                }
            }
        }
    }

    // Draw cursor onto the backbuffer so the blit is atomic and doesn't flicker/blink!
    draw_cursor_simple(frame_mx, frame_my, &back_fb);
    
    // Blit the double-buffered frame to the physical screen
    blit_buffer(screen_backbuffer, real_fb);
    
    // Restore the clean backbuffer background area under the cursor
    if (cursor_saved_w > 0 && cursor_saved_h > 0) {
        for (int32_t y = 0; y < cursor_saved_h; y++) {
            for (int32_t x = 0; x < cursor_saved_w; x++) {
                int32_t px = cursor_saved_x + x;
                int32_t py = cursor_saved_y + y;
                if (px >= 0 && px < (int32_t)screen_w && py >= 0 && py < (int32_t)screen_h) {
                    screen_backbuffer[py * screen_w + px] = cursor_buffer[y * cursor_saved_w + x];
                }
            }
        }
    }
}

void redraw_desktop(struct multiboot_tag_framebuffer* fb) {
    if (!screen_backbuffer) { draw_background(fb); draw_desktop_icons(fb); draw_dock(fb); draw_status_bar(fb); return; }
    struct multiboot_tag_framebuffer temp_fb = *fb; temp_fb.framebuffer_addr = (uint64_t)screen_backbuffer; temp_fb.framebuffer_pitch = fb->framebuffer_width * 4; temp_fb.framebuffer_bpp = 32;
    draw_background(&temp_fb); draw_desktop_icons(&temp_fb); draw_dock(&temp_fb); draw_status_bar(&temp_fb);
    for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
        uint8_t* dest = (uint8_t*)fb->framebuffer_addr + y * fb->framebuffer_pitch; uint32_t* src = screen_backbuffer + y * fb->framebuffer_width;
        if (fb->framebuffer_bpp == 32) { uint32_t* dest32 = (uint32_t*)dest; for (uint32_t x = 0; x < fb->framebuffer_width; x++) dest32[x] = src[x]; }
        else if (fb->framebuffer_bpp == 24) { for (uint32_t x = 0; x < fb->framebuffer_width; x++) { uint32_t color = src[x]; dest[x*3] = color & 0xFF; dest[x*3+1] = (color >> 8) & 0xFF; dest[x*3+2] = (color >> 16) & 0xFF; } }
    }
}

/* --- Interrupt Handlers and Hardware Init --- */

void pic_remap(void) {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xF9); outb(0xA1, 0xEF);
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF); idt[num].base_mid = (base >> 16) & 0xFFFF; idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel; idt[num].ist = 0; idt[num].flags = flags; idt[num].reserved = 0;
}

extern void idt_load(struct idt_ptr* ptr); extern void isr_mouse_stub(void); extern void isr_keyboard_stub(void);
extern void isr_common_stub(void);

void keyboard_handler_main() {
    uint8_t status = inb(0x64);
    while (status & 1) {
        if (!(status & 0x20)) {
            uint8_t scancode = inb(0x60);
            if (scancode == 0x5B || scancode == 0x5C) {
                if (!preferences_window_open || !kernel_api.window_maximized) {
                    power_menu_open = !power_menu_open;
                }
            }
        } else {
            break; // Mouse data, let the other handler take it
        }
        status = inb(0x64);
    }
    outb(0x20, 0x20);
}

uint8_t mouse_cycle = 0, mouse_byte[4], mouse_mode = 0;
void mouse_wait(uint8_t a_type) { uint32_t timeout = 100000; if (a_type == 0) { while (timeout--) { if (inb(0x64) & 1) return; } } else { while (timeout--) { if (!(inb(0x64) & 2)) return; } } }
void ps2_flush() { uint32_t timeout = 1000; while (timeout-- && (inb(0x64) & 1)) inb(0x60); }
void mouse_write(uint8_t data) { mouse_wait(1); outb(0x64, 0xD4); mouse_wait(1); outb(0x60, data); }
uint8_t mouse_read() { uint32_t timeout = 100000; while (timeout--) { if (inb(0x64) & 1) return inb(0x60); } return 0; }

void mouse_init() {
    __asm__ volatile("cli"); ps2_flush();
    mouse_wait(1); outb(0x64, 0xA8); mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); uint8_t conf = (inb(0x60) | 2) & ~0x20;
    mouse_wait(1); outb(0x64, 0x60); mouse_wait(1); outb(0x60, conf);
    
    mouse_write(0xF6); mouse_read(); // Set defaults (resets to 3-byte mode)

    // IntelliMouse detection sequence to enable scroll wheel
    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(80);  mouse_read();
    mouse_write(0xF2); mouse_read();
    uint8_t res = mouse_read(); // Should be 3 if wheel supported
    if (res == 3) mouse_mode = 1; else mouse_mode = 0;
    
    mouse_write(0xF4); mouse_read(); // Enable data reporting
    ps2_flush(); __asm__ volatile("sti");
}

void mouse_handler_main() {
    uint8_t status = inb(0x64);
    while (status & 1) {
        if (status & 0x20) {
            uint8_t data = inb(0x60);
            switch (mouse_cycle) {
                case 0: if (data & 0x08) { mouse_byte[0] = data; mouse_cycle = 1; } break;
                case 1: mouse_byte[1] = data; mouse_cycle = 2; break;
                case 2: 
                    mouse_byte[2] = data;
                    if (mouse_mode == 0) {
                        mouse_cycle = 0;
                        goto process_packet;
                    } else {
                        mouse_cycle = 3;
                    }
                    break;
                case 3:
                    mouse_byte[3] = data; mouse_cycle = 0; 
                process_packet: {
                    int32_t dx = (int32_t)mouse_byte[1], dy = (int32_t)mouse_byte[2];
                    if (mouse_byte[0] & 0x10) dx -= 256; if (mouse_byte[0] & 0x20) dy -= 256;
                    mouse_x += (dx * mouse_speed) / 10; mouse_y -= (dy * mouse_speed) / 10;
                    if (mouse_x < 0) mouse_x = 0; if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x > (int32_t)screen_w - 5) mouse_x = screen_w - 5; if (mouse_y > (int32_t)screen_h - 5) mouse_y = screen_h - 5;
                    
                    uint8_t hardware_left = mouse_byte[0] & 1;
                    uint8_t hardware_right = (mouse_byte[0] >> 1) & 1;
                    uint8_t logical_left = (mouse_primary_button == 0) ? hardware_left : hardware_right;
                    
                    if (logical_left && !mouse_left_button) mouse_click_count++; 
                    mouse_left_button = logical_left;
                    
                    if (mouse_mode == 1) {
                        // Wheel delta (Z-axis) - lower 4 bits are the delta (signed 4-bit)
                        int8_t wheel_delta = (int8_t)(mouse_byte[3] & 0x0F);
                        if (wheel_delta & 0x08) wheel_delta |= 0xF0; // Sign extend
                        mouse_wheel -= (wheel_delta * mouse_scroll_speed); // Natural scroll scaled by speed
                    }
                    break;
                }
            }
        } else {
            break; // Keyboard data
        }
        status = inb(0x64);
    }
    outb(0x20, 0x20); outb(0xA0, 0x20);
}

void reboot() {
    uint8_t good = 0x02; while (good & 0x02) good = inb(0x64); outb(0x64, 0xFE);
    static struct idt_ptr zero_idtp = {0, 0}; __asm__ volatile("lidt %0" : : "m"(zero_idtp)); __asm__ volatile("int $3");
    while(1) __asm__ volatile("hlt");
}

void shutdown(uint64_t multiboot_addr) {
    outw(0xB004, 0x2000); outw(0x604, 0x2000); outw(0x4004, 0x3400);
    struct multiboot_tag* tag; uint8_t* rsdp = 0;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8); tag->type != 0; tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 14 || tag->type == 15) { rsdp = (uint8_t*)tag + 8; break; }
    }
    if (rsdp) {
        uint8_t* rsdt = (uint8_t*)(uint64_t)(*(uint32_t*)(rsdp + 16));
        if (memcmp_custom(rsdt, "RSDT", 4) == 0) {
            uint32_t entries = (*(uint32_t*)(rsdt + 4) - 36) / 4, *table_ptr = (uint32_t*)(rsdt + 36);
            for (uint32_t i = 0; i < entries; i++) {
                uint8_t* table = (uint8_t*)(uint64_t)table_ptr[i];
                if (memcmp_custom(table, "FACP", 4) == 0) {
                    uint32_t pm1a_cnt = *(uint32_t*)(table + 64);
                    outw((uint16_t)pm1a_cnt, 0x2000 | (5 << 10)); msleep(100); outw((uint16_t)pm1a_cnt, 0x3400);
                }
            }
        }
    }
    while(1) __asm__ volatile("hlt");
}

/* --- kernel_main --- */

void kernel_main(uint64_t multiboot_addr) {
    struct multiboot_tag_framebuffer* fb = 0; struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8); tag->type != 0; tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 8) fb = (struct multiboot_tag_framebuffer*)tag;
        if (tag->type == 4) {
            struct multiboot_tag_basic_meminfo* meminfo = (struct multiboot_tag_basic_meminfo*)tag;
            if (global_ram_mb == 0) global_ram_mb = (meminfo->mem_upper + 1024) / 1024;
        }
        if (tag->type == 6) {
            struct multiboot_tag_mmap* mmap = (struct multiboot_tag_mmap*)tag;
            uint64_t total_mem = 0;
            for (struct multiboot_mmap_entry* entry = mmap->entries;
                 (uint8_t*)entry < (uint8_t*)tag + tag->size;
                 entry = (struct multiboot_mmap_entry*)((uintptr_t)entry + mmap->entry_size)) {
                if (entry->type == 1) {
                    total_mem += entry->len;
                }
            }
            global_ram_mb = (uint32_t)(total_mem / (1024 * 1024));
        }
    }
    if (fb && fb->framebuffer_addr != 0) {
        global_fb = fb; screen_w = fb->framebuffer_width; screen_h = fb->framebuffer_height; mouse_x = screen_w / 2; mouse_y = screen_h / 2;
        pic_remap();
        
        // IDT feltöltése: 0-31 kivételek egyedi stubokkal
        extern void exception_stub_0(void);  extern void exception_stub_1(void);  extern void exception_stub_2(void);  extern void exception_stub_3(void);
        extern void exception_stub_4(void);  extern void exception_stub_5(void);  extern void exception_stub_6(void);  extern void exception_stub_7(void);
        extern void exception_stub_8(void);  extern void exception_stub_9(void);  extern void exception_stub_10(void); extern void exception_stub_11(void);
        extern void exception_stub_12(void); extern void exception_stub_13(void); extern void exception_stub_14(void); extern void exception_stub_15(void);
        extern void exception_stub_16(void); extern void exception_stub_17(void); extern void exception_stub_18(void); extern void exception_stub_19(void);
        extern void exception_stub_20(void); extern void exception_stub_21(void); extern void exception_stub_22(void); extern void exception_stub_23(void);
        extern void exception_stub_24(void); extern void exception_stub_25(void); extern void exception_stub_26(void); extern void exception_stub_27(void);
        extern void exception_stub_28(void); extern void exception_stub_29(void); extern void exception_stub_30(void); extern void exception_stub_31(void);

        idt_set_gate(0, (uint64_t)exception_stub_0, 0x08, 0x8E);   idt_set_gate(1, (uint64_t)exception_stub_1, 0x08, 0x8E);
        idt_set_gate(2, (uint64_t)exception_stub_2, 0x08, 0x8E);   idt_set_gate(3, (uint64_t)exception_stub_3, 0x08, 0x8E);
        idt_set_gate(4, (uint64_t)exception_stub_4, 0x08, 0x8E);   idt_set_gate(5, (uint64_t)exception_stub_5, 0x08, 0x8E);
        idt_set_gate(6, (uint64_t)exception_stub_6, 0x08, 0x8E);   idt_set_gate(7, (uint64_t)exception_stub_7, 0x08, 0x8E);
        idt_set_gate(8, (uint64_t)exception_stub_8, 0x08, 0x8E);   idt_set_gate(9, (uint64_t)exception_stub_9, 0x08, 0x8E);
        idt_set_gate(10, (uint64_t)exception_stub_10, 0x08, 0x8E); idt_set_gate(11, (uint64_t)exception_stub_11, 0x08, 0x8E);
        idt_set_gate(12, (uint64_t)exception_stub_12, 0x08, 0x8E); idt_set_gate(13, (uint64_t)exception_stub_13, 0x08, 0x8E);
        idt_set_gate(14, (uint64_t)exception_stub_14, 0x08, 0x8E); idt_set_gate(15, (uint64_t)exception_stub_15, 0x08, 0x8E);
        idt_set_gate(16, (uint64_t)exception_stub_16, 0x08, 0x8E); idt_set_gate(17, (uint64_t)exception_stub_17, 0x08, 0x8E);
        idt_set_gate(18, (uint64_t)exception_stub_18, 0x08, 0x8E); idt_set_gate(19, (uint64_t)exception_stub_19, 0x08, 0x8E);
        idt_set_gate(20, (uint64_t)exception_stub_20, 0x08, 0x8E); idt_set_gate(21, (uint64_t)exception_stub_21, 0x08, 0x8E);
        idt_set_gate(22, (uint64_t)exception_stub_22, 0x08, 0x8E); idt_set_gate(23, (uint64_t)exception_stub_23, 0x08, 0x8E);
        idt_set_gate(24, (uint64_t)exception_stub_24, 0x08, 0x8E); idt_set_gate(25, (uint64_t)exception_stub_25, 0x08, 0x8E);
        idt_set_gate(26, (uint64_t)exception_stub_26, 0x08, 0x8E); idt_set_gate(27, (uint64_t)exception_stub_27, 0x08, 0x8E);
        idt_set_gate(28, (uint64_t)exception_stub_28, 0x08, 0x8E); idt_set_gate(29, (uint64_t)exception_stub_29, 0x08, 0x8E);
        idt_set_gate(30, (uint64_t)exception_stub_30, 0x08, 0x8E); idt_set_gate(31, (uint64_t)exception_stub_31, 0x08, 0x8E);

        extern void irq_common_stub(void);
        for (int i = 32; i < 256; i++) idt_set_gate(i, (uint64_t)irq_common_stub, 0x08, 0x8E);
        
        idt_set_gate(33, (uint64_t)isr_keyboard_stub, 0x08, 0x8E);
        idt_set_gate(44, (uint64_t)isr_mouse_stub, 0x08, 0x8E);
        
        idtp.limit = sizeof(idt) - 1; idtp.base = (uint64_t)&idt; idt_load(&idtp); mouse_init();
        for (uint32_t y = 0; y < fb->framebuffer_height; y++) for (uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
        if (vfs_init() != 0) { draw_string_scaled(100, 100, "FATAL: VFS INIT FAILED!", 0xFFFFFF, 100, fb); while(1) __asm__ volatile("hlt"); }
        boot_logo_data = load_asset("Sysroot:/AnimOS/boot/assets/boot_logo.bmp");
        arial_font_data = load_asset("Sysroot:/AnimOS/assets/fonts/arial_black/arial_black.bmp");
        arial_font_xml_data = load_asset("Sysroot:/AnimOS/assets/fonts/arial_black/arial_black.xml");
        if (boot_logo_data) {
            struct bmp_info_header* bih = (struct bmp_info_header*)(boot_logo_data + sizeof(struct bmp_file_header));
            draw_icon((fb->framebuffer_width - bih->biWidth) / 2, (fb->framebuffer_height - (bih->biHeight < 0 ? -bih->biHeight : bih->biHeight)) / 2 - 80, boot_logo_data, fb);
        }

        uint32_t bar_w = 400, bar_h = 16, bar_x = (fb->framebuffer_width - bar_w) / 2, bar_y = fb->framebuffer_height / 2 + 120;
        
        if (arial_font_data && arial_font_xml_data) {
            const char* copyright = "AnimOS (C) 2026. All rights reserved.";
            const char* loading = "AnimOS is booting up...";
            draw_string_scaled((fb->framebuffer_width - get_string_width_scaled(copyright, 60)) / 2, fb->framebuffer_height - 60, copyright, 0x888888, 60, fb);
            draw_string_scaled((fb->framebuffer_width - get_string_width_scaled(loading, 70)) / 2, bar_y + 40, loading, 0x555555, 70, fb);
        }

        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 10, fb);
        wallpaper_data = load_asset("Sysroot:/AnimOS/assets/wallpapers/bubble.bmp"); init_wallpaper_info(); precompute_scaled_wallpaper(fb);
        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 25, fb);
        cursor_data = load_asset("Sysroot:/AnimOS/assets/cursor/Default/Normal Select.cur");
        normal_cursor_data = cursor_data;
        working_cursor_data = load_asset("Sysroot:/AnimOS/assets/cursor/Default/Working In Background.ani");
        if (working_cursor_data) {
            find_ani_frames(working_cursor_data, vfs_get_file_size("Sysroot:/AnimOS/assets/cursor/Default/Working In Background.ani"));
        }
        power_icon_data = load_asset("Sysroot:/AnimOS/assets/taskbar/power.bmp");
        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 40, fb);
        offline_icon_data = load_asset("Sysroot:/AnimOS/assets/ui/offline.bmp"); online_icon_data = load_asset("Sysroot:/AnimOS/assets/ui/online.bmp");
        file_explorer_icon_data = load_asset("Sysroot:/AnimOS/assets/icons/file_explorer.bmp"); preferences_icon_data = load_asset("Sysroot:/AnimOS/assets/icons/preferences.bmp");
        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 60, fb);
        close_icon_data = load_asset("Sysroot:/AnimOS/assets/ui/close.bmp"); minimize_icon_data = load_asset("Sysroot:/AnimOS/assets/ui/minimize.bmp"); maximize_icon_data = load_asset("Sysroot:/AnimOS/assets/ui/maximize.bmp");
        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 75, fb);
        init_kernel_api();
        struct pci_device net_dev;
        if (pci_find_device(0xFFFF, 0xFFFF, &net_dev)) {
            if (net_dev.vendor_id == 0x8086 && (net_dev.device_id == 0x100E || net_dev.device_id == 0x100F || net_dev.device_id == 0x10D3)) {
                if (e1000_init(&net_dev) == 0) { net_status = 1; net_init((10) | (0 << 8) | (2 << 16) | (15 << 24)); msleep(500); }
            }
        }
        draw_boot_progress_bar(bar_x, bar_y, bar_w, bar_h, 100, fb); msleep(200);
        
        desktop_ready = 1;
        struct bmp_info_header* pbih = (struct bmp_info_header*)(power_icon_data + sizeof(struct bmp_file_header));
        int32_t picon_h = pbih->biHeight < 0 ? -pbih->biHeight : pbih->biHeight, picon_w = pbih->biWidth, picon_x = 20 + 15, picon_y = (fb->framebuffer_height - 55 - 20) + (55 - picon_h) / 2;
        uint32_t ticks = 0;
        while(1) {
            ticks++;
            // 0. Atomic snapshot of mouse state for this frame
            frame_mx = mouse_x; frame_my = mouse_y;
            frame_wheel = mouse_wheel; mouse_wheel = 0;
            frame_clicked = (mouse_click_count > last_kernel_click_count);
            last_kernel_click_count = mouse_click_count;
            
            int32_t mx = frame_mx, my = frame_my;
            
            // 1. Calculate Hit Areas
            int any_window_open = preferences_window_open || file_explorer_window_open;
            if (any_window_open && active_app < 0) {
                if (file_explorer_window_open) active_app = 0;
                else if (preferences_window_open) active_app = 1;
            }
            int window_covers_desktop = any_window_open && kernel_api.window_maximized && !kernel_api.window_minimized;
            int over_window = 0;
            if (any_window_open && !kernel_api.window_minimized) {
                if (kernel_api.window_maximized) over_window = 1;
                else {
                    uint32_t win_w = 800, win_h = 600;
                    int32_t win_x = (screen_w - win_w) / 2, win_y = (screen_h - win_h) / 2;
                    if (mx >= win_x && mx <= win_x + (int32_t)win_w && my >= win_y && my <= win_y + (int32_t)win_h) over_window = 1;
                }
            }
            int over_dock = (my >= (int32_t)(fb->framebuffer_height - 55 - 20));

            // 2. Hover Logic
            hover_icon = -1;
            if (!dialog_state && !window_covers_desktop && !over_window && !over_dock) {
                if (mx >= 30 && mx <= 130 && my >= 30 && my <= 110) hover_icon = 0;
                else if (mx >= 30 && mx <= 130 && my >= 130 && my <= 210) hover_icon = 1;
            }

            if (frame_clicked) {
                int click_handled = 0;

                // 3. Power Menu hit test (if open)
                if (power_menu_open) {
                    uint32_t menu_x = 20, menu_y = (fb->framebuffer_height - 55 - 20) - 120 - 10;
                    if (mx >= (int32_t)menu_x && mx <= (int32_t)(menu_x + 200) && my >= (int32_t)menu_y && my <= (int32_t)(menu_y + 120)) {
                        if (my >= (int32_t)(menu_y + 10) && my <= (int32_t)(menu_y + 60)) { dialog_state = 1; power_menu_open = 0; power_menu_progress = 0; }
                        else if (my >= (int32_t)(menu_y + 70) && my <= (int32_t)(menu_y + 120)) { dialog_state = 2; power_menu_open = 0; power_menu_progress = 0; }
                        click_handled = 1;
                    } else {
                        power_menu_open = 0; // Clicked outside, close menu
                        if (mx >= picon_x && mx <= picon_x + picon_w && my >= picon_y && my <= picon_y + picon_h) {
                            click_handled = 1;
                        }
                    }
                }

                // 4. Dialog hit test (if active)
                if (!click_handled && dialog_state != 0) {
                    const char* dmsg = (dialog_state == 1) ? "Are you sure you want to restart the system?" : "Are you sure you want to shutdown the system?";
                    uint32_t dw = get_string_width(dmsg) + 80; if (dw < 350) dw = 350;
                    uint32_t dh = 220, dx = (fb->framebuffer_width - dw) / 2, dy = (fb->framebuffer_height - dh) / 2;
                    uint32_t b_w = 120, b_h = 45, sp = 20, s_x = dx + (dw - (2 * b_w + sp)) / 2, b_y = dy + 145;

                    if (mx >= (int32_t)s_x && mx <= (int32_t)(s_x + b_w) && my >= (int32_t)b_y && my <= (int32_t)(b_y + b_h)) {
                        for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                        if (dialog_state == 1) {
                            const char* m = "Restarting..."; draw_string((screen_w - get_string_width(m)) / 2, screen_h / 2, m, 0xFFFFFF, fb);
                            msleep(500); reboot();
                        } else {
                            const char* m1 = "Logging off..."; draw_string((screen_w - get_string_width(m1)) / 2, screen_h / 2, m1, 0xFFFFFF, fb);
                            msleep(800); for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                            const char* m2 = "Saving your settings..."; draw_string((screen_w - get_string_width(m2)) / 2, screen_h / 2, m2, 0xFFFFFF, fb);
                            msleep(800); for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                            const char* m3 = "AnimOS is shutting down..."; draw_string((screen_w - get_string_width(m3)) / 2, screen_h / 2, m3, 0xFFFFFF, fb);
                            msleep(800); shutdown(multiboot_addr);
                            for(uint32_t y = 0; y < fb->framebuffer_height; y++) for(uint32_t x = 0; x < fb->framebuffer_width; x++) draw_pixel(x, y, 0, fb);
                            const char* m4 = "It is now safe to turn off your computer."; draw_string((screen_w - get_string_width(m4)) / 2, screen_h / 2, m4, 0xFFFFFF, fb);
                        }
                        while(1) __asm__ volatile("hlt");
                    } else if (mx >= (int32_t)(s_x + b_w + sp) && mx <= (int32_t)(s_x + 2 * b_w + sp) && my >= (int32_t)b_y && my <= (int32_t)(b_y + b_h)) dialog_state = 0;
                    click_handled = 1;
                }

                // 5. Active Window hit test (if open and visible)
                if (!click_handled && over_window && active_app >= 0) {
                    uint32_t close_x = screen_w - 22 - 12, close_y = (40 - 22) / 2;
                    if (!kernel_api.window_maximized) {
                        uint32_t win_w = 800, win_h = 600;
                        close_x = (screen_w + win_w) / 2 - 22 - 12;
                        close_y = (screen_h - win_h) / 2 + (40 - 22) / 2;
                    }
                    if (mx >= (int32_t)close_x && mx <= (int32_t)(close_x + 22) && my >= (int32_t)close_y && my <= (int32_t)(close_y + 22)) {
                        if (active_app >= 0) {
                            app_state_saved[active_app] = 0;
                        }
                        if (active_app == 0) file_explorer_window_open = 0;
                        else if (active_app == 1) preferences_window_open = 0;
                        if (file_explorer_window_open) active_app = 0;
                        else if (preferences_window_open) active_app = 1;
                        else active_app = -1;
                        if (active_app >= 0) {
                            kernel_api.window_maximized = app_maximized[active_app];
                            kernel_api.window_minimized = app_minimized[active_app];
                        }
                    } else {
                        if (active_app == 0) draw_file_explorer_window(fb, APP_EVENT_CLICK);
                        else if (active_app == 1) draw_preferences_window(fb, APP_EVENT_CLICK);
                    }
                    click_handled = 1;
                }

                // 6. Dock hit test (Always checked if dock is hit)
                if (!click_handled && over_dock) {
                    if (mx >= picon_x && mx <= picon_x + picon_w && my >= picon_y && my <= picon_y + picon_h) {
                        power_menu_open = !power_menu_open;
                        click_handled = 1;
                    } else {
                        // Use exact same calculations as draw_dock for consistency
                        uint32_t margin = 20, dock_h = 55, dock_x = margin, dock_w = fb->framebuffer_width - 2 * margin;
                        uint32_t dock_y = fb->framebuffer_height - dock_h - margin;
                        
                        int dock_open[2] = { file_explorer_window_open, preferences_window_open };
                        int icon_index = 0;
                        uint32_t icon_size = 32;
                        uint32_t gap = 12;
                        int icon_count = 0;
                        for (int i = 0; i < 2; i++) if (dock_open[i]) icon_count++;
                        if (icon_count > 0) {
                            uint32_t total_w = icon_count * icon_size + (icon_count - 1) * gap;
                            uint32_t start_x = dock_x + (dock_w - total_w) / 2;
                            for (int i = 0; i < 2; i++) {
                                if (!dock_open[i]) continue;
                                uint32_t icon_x = start_x + icon_index * (icon_size + gap);
                                uint32_t icon_y = dock_y + (dock_h - icon_size) / 2;
                                if (mx >= (int32_t)icon_x && mx <= (int32_t)(icon_x + icon_size) && my >= (int32_t)icon_y && my <= (int32_t)(icon_y + icon_size)) {
                                    if (active_app == i) {
                                        // Clicking the active app: toggle minimize
                                        app_minimized[i] = !app_minimized[i];
                                        kernel_api.window_minimized = app_minimized[i];
                                    } else {
                                        // Save the current active app state before switching
                                        if (active_app >= 0) {
                                            app_maximized[active_app] = kernel_api.window_maximized;
                                            app_minimized[active_app] = kernel_api.window_minimized;
                                        }
                                        // Switch to a different app
                                        active_app = i;
                                        app_minimized[i] = 0;
                                        kernel_api.window_minimized = 0;
                                        kernel_api.window_maximized = app_maximized[i];
                                        // Don't re-initialize, just restore state
                                    }
                                    click_handled = 1;
                                    break;
                                }
                                icon_index++;
                            }
                        }
                    }
                }

                // 7. Desktop Icons hit test
                if (!click_handled) {
                    if (hover_icon != -1) {
                        if (hover_icon == last_clicked_icon && (ticks - last_click_time) < 50) {
                            int app_id = hover_icon;
                            if (app_id == 0) {
                                if (file_explorer_window_open) {
                                    if (active_app == 0) {
                                        app_minimized[0] = 0;
                                        kernel_api.window_minimized = 0;
                                    } else {
                                        // Save the current active app state before switching
                                        if (active_app >= 0) {
                                            app_maximized[active_app] = kernel_api.window_maximized;
                                            app_minimized[active_app] = kernel_api.window_minimized;
                                        }
                                        // Switch to file explorer
                                        active_app = 0;
                                        kernel_api.window_minimized = app_minimized[0];
                                        kernel_api.window_maximized = app_maximized[0];
                                    }
                                } else {
                                    // Save the current active app state before opening new app
                                    if (active_app >= 0) {
                                        app_maximized[active_app] = kernel_api.window_maximized;
                                        app_minimized[active_app] = kernel_api.window_minimized;
                                    }
                                    file_explorer_window_open = 1;
                                    file_explorer_needs_init = 1;
                                    active_app = 0;
                                    app_minimized[0] = 0;
                                    kernel_api.window_minimized = 0;
                                    kernel_api.window_maximized = app_maximized[0];
                                }
                            } else {
                                if (preferences_window_open) {
                                    if (active_app == 1) {
                                        app_minimized[1] = 0;
                                        kernel_api.window_minimized = 0;
                                    } else {
                                        // Save the current active app state before switching
                                        if (active_app >= 0) {
                                            app_maximized[active_app] = kernel_api.window_maximized;
                                            app_minimized[active_app] = kernel_api.window_minimized;
                                        }
                                        // Switch to preferences
                                        active_app = 1;
                                        kernel_api.window_minimized = app_minimized[1];
                                        kernel_api.window_maximized = app_maximized[1];
                                    }
                                } else {
                                    // Save the current active app state before opening new app
                                    if (active_app >= 0) {
                                        app_maximized[active_app] = kernel_api.window_maximized;
                                        app_minimized[active_app] = kernel_api.window_minimized;
                                    }
                                    preferences_window_open = 1;
                                    preferences_needs_init = 1;
                                    active_app = 1;
                                    app_minimized[1] = 0;
                                    kernel_api.window_minimized = 0;
                                    kernel_api.window_maximized = app_maximized[1];
                                }
                            }
                        }
                        selected_icon = hover_icon; last_clicked_icon = hover_icon; last_click_time = ticks;
                    } else if (my < (int32_t)(fb->framebuffer_height - 55 - 20)) selected_icon = -1;
                }
            }
            if (power_menu_open && power_menu_progress < 100) power_menu_progress += 20; else if (!power_menu_open && power_menu_progress > 0) power_menu_progress -= 20;

            // Ensure app state is correctly synced after any event handling
            if (any_window_open && active_app >= 0) {
                if (active_app == 0) { app_maximized[0] = kernel_api.window_maximized; app_minimized[0] = kernel_api.window_minimized; }
                else if (active_app == 1) { app_maximized[1] = kernel_api.window_maximized; app_minimized[1] = kernel_api.window_minimized; }
            }

            compose_frame(fb, multiboot_addr); kernel_yield(); msleep(10);
            
            // Heartbeat (villogó pixel a bal felső sarokban)
            draw_pixel(0, 0, (ticks % 20 < 10) ? 0x00FF00 : 0x000000, fb);
        }
    }
    while(1) __asm__ volatile("hlt");
}
