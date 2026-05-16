#include "../../kernel/api.h"

// Constants
static uint32_t title_bar_h = 40;
static uint32_t sidebar_w = 200;
static uint32_t btn_size = 22;

// Internal State
static int selected_menu = 0;

void app_itoa(uint32_t n, char* s) {
    int i = 0;
    if (n == 0) { s[i++] = '0'; s[i] = 0; return; }
    while (n > 0) { s[i++] = (n % 10) + '0'; n /= 10; }
    s[i] = 0;
    for (int j = 0; j < i / 2; j++) { char c = s[j]; s[j] = s[i-1-j]; s[i-1-j] = c; }
}

void render_to_buffer(kernel_api_t* api, struct multiboot_tag_framebuffer* fb, struct multiboot_tag_framebuffer* target_fb) {
    uint32_t w = fb->framebuffer_width;
    uint32_t h = fb->framebuffer_height;

    // 1. Draw the window background and layout to the target buffer
    api->draw_rect(0, 0, w, title_bar_h, 0xF0F0F0, target_fb); // Title bar
    api->draw_rect(0, title_bar_h, sidebar_w, h - title_bar_h, 0xF9F9F9, target_fb); // Sidebar
    api->draw_rect(sidebar_w, title_bar_h, w - sidebar_w, h - title_bar_h, 0xFFFFFF, target_fb); // Main area

    // 2. Draw separators
    api->draw_rect(0, title_bar_h, w, 1, 0xDDDDDD, target_fb); // Horizontal line
    api->draw_rect(sidebar_w, title_bar_h, 1, h - title_bar_h, 0xDDDDDD, target_fb); // Vertical line

    // 3. Draw Title
    api->draw_string_scaled(20, (title_bar_h - (18 * 80 / 100)) / 2, "Preferences", 0x333333, 80, target_fb);

    // 4. Window Buttons
    uint32_t close_size = 22;
    uint32_t max_size = 24;

    uint32_t close_y = (title_bar_h - close_size) / 2;
    uint32_t close_x = w - close_size - 12;
    if (api->close_icon) api->draw_icon_scaled(close_x, close_y, close_size, close_size, api->close_icon, target_fb);

    uint32_t max_y = (title_bar_h - max_size) / 2;
    uint32_t max_x = close_x - max_size - 8;
    if (api->maximize_icon) api->draw_icon_scaled(max_x, max_y, max_size, max_size, api->maximize_icon, target_fb);

    // 5. Sidebar Menu Items
    const char* menu_items[] = {"Display", "About"};
    for (int i = 0; i < 2; i++) {
        uint32_t item_y = title_bar_h + 20 + (i * 40);
        if (i == selected_menu) {
            api->draw_rect(10, item_y - 10, sidebar_w - 20, 35, 0xE0E0E0, target_fb);
        }
        api->draw_string_scaled(30, item_y, menu_items[i], 0x333333, 70, target_fb);
    }

    // 6. Content Area based on selection
    uint32_t cx = sidebar_w + 40;
    uint32_t cy = title_bar_h + 40;

    if (selected_menu == 0) { // Display
        api->draw_string_scaled(cx, cy, "Display Settings", 0x222222, 90, target_fb);
        api->draw_string_scaled(cx, cy + 50, "Resolution: 1024x768 (VBE)", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx, cy + 80, "Wallpaper: bubble.bmp", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx, cy + 110, "Scaling: Nearest Neighbor", 0x555555, 70, target_fb);
    } 
    else if (selected_menu == 1) { // About
        api->draw_string_scaled(cx, cy, "About AnimOS", 0x222222, 90, target_fb);
        api->draw_string_scaled(cx, cy + 50, "Version: 1.0 (Alpha)", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx, cy + 80, "Kernel: AnimKernel v1.0 x86_64 64bit", 0x555555, 70, target_fb);
        
        // Processor Brand
        api->draw_string_scaled(cx, cy + 115, "Processor: ", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx + 80, cy + 115, api->cpu_brand, 0x555555, 70, target_fb);

        // RAM Size
        char ram_val[16]; app_itoa(api->ram_size_mb, ram_val);
        api->draw_string_scaled(cx, cy + 145, "Memory:", 0x555555, 70, target_fb);
        uint32_t ram_val_x = cx + api->get_string_width_scaled("Memory: ", 70);
        api->draw_string_scaled(ram_val_x, cy + 145, ram_val, 0x333333, 70, target_fb);
        api->draw_string_scaled(ram_val_x + api->get_string_width_scaled(ram_val, 70) + 5, cy + 145, "MB RAM", 0x555555, 70, target_fb);

        // Disk Size
        char disk_val[16];
        api->draw_string_scaled(cx, cy + 175, "Storage:", 0x555555, 70, target_fb);
        uint32_t disk_val_x = cx + api->get_string_width_scaled("Storage: ", 70);
        if (api->disk_size_gb > 0) {
            app_itoa(api->disk_size_gb, disk_val);
            api->draw_string_scaled(disk_val_x, cy + 175, disk_val, 0x333333, 70, target_fb);
            api->draw_string_scaled(disk_val_x + api->get_string_width_scaled(disk_val, 70) + 5, cy + 175, "GB Total", 0x555555, 70, target_fb);
        } else {
            app_itoa(api->disk_size_mb, disk_val);
            api->draw_string_scaled(disk_val_x, cy + 175, disk_val, 0x333333, 70, target_fb);
            api->draw_string_scaled(disk_val_x + api->get_string_width_scaled(disk_val, 70) + 5, cy + 175, "MB Total", 0x555555, 70, target_fb);
        }

        api->draw_string_scaled(cx, cy + 220, "Copyright (C) 2026 Sipocz Adam - All Rights Reserved.", 0x888888, 60, target_fb);
    }
}

// Force main to the very beginning of the binary
__attribute__((section(".text.main")))
void main(kernel_api_t* api, struct multiboot_tag_framebuffer* fb, app_event_t event) {
    if (event == APP_EVENT_INIT) {
        selected_menu = 0;
    }

    if (event == APP_EVENT_CLICK) {
        int32_t mx, my; uint8_t clicked;
        api->get_mouse_pos(&mx, &my, &clicked);
        
        // Window Buttons Hit Test
        uint32_t close_size = 22;
        uint32_t max_size = 24;
        
        uint32_t close_x = fb->framebuffer_width - close_size - 12;
        uint32_t max_x = close_x - max_size - 8;
        uint32_t max_y = (title_bar_h - max_size) / 2;
        
        if (mx >= (int32_t)max_x && mx <= (int32_t)(max_x + max_size) && my >= (int32_t)max_y && my <= (int32_t)(max_y + max_size)) {
            api->window_maximized = !api->window_maximized;
            return;
        }

        // Sidebar area hit test
        if (mx >= 0 && mx <= (int32_t)sidebar_w && my >= (int32_t)title_bar_h) {
            for (int i = 0; i < 4; i++) {
                uint32_t item_y = title_bar_h + 20 + (i * 40);
                if (my >= (int32_t)(item_y - 10) && my <= (int32_t)(item_y + 25)) {
                    selected_menu = i;
                }
            }
        }
    }

    if (event == APP_EVENT_INIT || event == APP_EVENT_TICK || event == APP_EVENT_CLICK) {
        // Use double buffering for the app's internal composition
        struct multiboot_tag_framebuffer buffer_fb = *fb;
        buffer_fb.framebuffer_addr = (uint64_t)api->window_buffer;
        buffer_fb.framebuffer_pitch = fb->framebuffer_width * 4;
        buffer_fb.framebuffer_bpp = 32;

        render_to_buffer(api, fb, &buffer_fb);

        // Blit to the target framebuffer provided by kernel
        api->blit_buffer(api->window_buffer, fb);

        if (event == APP_EVENT_TICK) {
            api->yield();
        }
    }
}
