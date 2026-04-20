typedef unsigned char      uint8_t;
typedef char               int8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;

extern uint8_t wallpaper_data[];
extern uint8_t cursor_data[];

uint32_t screen_w = 1024;
uint32_t screen_h = 768;
int32_t cursor_w = 32;
int32_t cursor_h = 32;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void io_wait(void) { outb(0x80, 0); }

void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFB); outb(0xA1, 0xEF);
}

struct idt_entry {
    uint16_t base_low; uint16_t selector; uint8_t ist; uint8_t flags;
    uint16_t base_mid; uint32_t base_high; uint32_t reserved;
} __attribute__((packed));
struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
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

volatile int32_t mouse_x = 512, mouse_y = 384;
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
                    // Csak akkor kezdünk csomagot, ha a 3. bit 1. 
                    // Az overflow biteket (6,7) NEM dobjuk el, mert VirtualBoxban gyakoriak!
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
                    
                    // Előjel kiterjesztés a 4. és 5. bit alapján
                    if (mouse_byte[0] & 0x10) dx -= 256;
                    if (mouse_byte[0] & 0x20) dy -= 256;
                    
                    mouse_x += dx;
                    mouse_y -= dy;
                    
                    // Clamping a képernyőre
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x > (int32_t)screen_w - 5) mouse_x = screen_w - 5;
                    if (mouse_y > (int32_t)screen_h - 5) mouse_y = screen_h - 5;
                    break;
            }
        }
        status = inb(0x64);
    }
    outb(0x20, 0x20); outb(0xA0, 0x20);
}

struct multiboot_tag { uint32_t type; uint32_t size; };
struct multiboot_tag_framebuffer {
    uint32_t type; uint32_t size; uint64_t framebuffer_addr; uint32_t framebuffer_pitch;
    uint32_t framebuffer_width; uint32_t framebuffer_height; uint8_t framebuffer_bpp;
    uint8_t framebuffer_type; uint16_t reserved;
};

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

uint32_t get_wallpaper_pixel(uint32_t x, uint32_t y, struct multiboot_tag_framebuffer* fb) {
    uint8_t* bmp_data = wallpaper_data;
    struct bmp_file_header* bfh = (struct bmp_file_header*)bmp_data;
    struct bmp_info_header* bih = (struct bmp_info_header*)(bmp_data + sizeof(struct bmp_file_header));
    uint8_t* pixels = bmp_data + bfh->bfOffBits;
    int32_t bmp_w = bih->biWidth, bmp_h = bih->biHeight, abs_bmp_h = bmp_h < 0 ? -bmp_h : bmp_h;
    uint32_t row_size = (bmp_w * (bih->biBitCount / 8) + 3) & ~3;
    int32_t src_y = (bmp_h > 0) ? (abs_bmp_h - 1 - (int32_t)(y * abs_bmp_h / fb->framebuffer_height)) : (int32_t)(y * abs_bmp_h / fb->framebuffer_height);
    uint8_t* p = pixels + (src_y * row_size) + ((int32_t)(x * bmp_w / fb->framebuffer_width) * (bih->biBitCount / 8));
    return (p[2] << 16) | (p[1] << 8) | p[0];
}

uint32_t cursor_buffer[64 * 64];
int32_t last_mouse_x = -1, last_mouse_y = -1;

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
        idt_set_gate(44, (uint64_t)isr_mouse_stub, 0x08, 0x8E);
        idtp.limit = sizeof(idt) - 1; idtp.base = (uint64_t)&idt;
        idt_load(&idtp);
        
        mouse_init();

        for (uint32_t y = 0; y < fb->framebuffer_height; y++) {
            for (uint32_t x = 0; x < fb->framebuffer_width; x++) { draw_pixel(x, y, get_wallpaper_pixel(x, y, fb), fb); }
        }
        uint32_t dock_h = 55, dock_w = (fb->framebuffer_width * 85) / 100, dock_x = (fb->framebuffer_width - dock_w) / 2, dock_y = fb->framebuffer_height - dock_h - 20, radius = 25, dock_color = 0xFFFFFF;
        for (uint32_t y = dock_y; y < dock_y + dock_h; y++) {
            for (uint32_t x = dock_x; x < dock_x + dock_w; x++) {
                uint8_t alpha = 150; uint32_t dx = 0, dy = 0; int is_corner = 0;
                if (x < dock_x + radius && y < dock_y + radius) { dx = (dock_x + radius) - x; dy = (dock_y + radius) - y; is_corner = 1; }
                else if (x > dock_x + dock_w - radius && y < dock_y + radius) { dx = x - (dock_x + dock_w - radius); dy = (dock_y + radius) - y; is_corner = 1; }
                else if (x < dock_x + radius && y > dock_y + dock_h - radius) { dx = (dock_x + radius) - x; dy = y - (dock_y + dock_h - radius); is_corner = 1; }
                else if (x > dock_x + dock_w - radius && y > dock_y + dock_h - radius) { dx = x - (dock_x + dock_w - radius); dy = y - (dock_y + dock_h - radius); is_corner = 1; }
                if (is_corner) {
                    uint32_t dist_sq = dx*dx + dy*dy;
                    if (dist_sq > radius*radius) alpha = 0;
                    else { uint32_t dist = sqrt_int(dist_sq); if (dist > radius - 2) alpha = (150 * (radius - dist)) / 2; }
                }
                if (alpha > 0) draw_pixel(x, y, blend_colors(get_wallpaper_pixel(x, y, fb), dock_color, alpha), fb);
            }
        }
        while(1) { 
            int32_t mx = mouse_x, my = mouse_y;
            draw_cursor(mx, my, fb); 
            for(int i = 0; i < 500; i++) __asm__ volatile("pause"); 
        }
    }
    while(1) { __asm__ volatile("hlt"); }
}
