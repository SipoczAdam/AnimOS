#include "../../kernel/api.h"

// Constants
static uint32_t title_bar_h = 40;
static uint32_t sidebar_w = 200;
static uint32_t btn_size = 22;

// Internal State
static int selected_menu = 0;
static char wallpaper_list[1024];
static int wallpaper_count = 0;
static int selected_tz = 0;
static int tz_dropdown_open = 0;
static int tz_scroll_offset = 0;

void app_itoa(uint32_t n, char* s) {
    int i = 0;
    if (n == 0) { s[i++] = '0'; s[i] = 0; return; }
    while (n > 0) { s[i++] = (n % 10) + '0'; n /= 10; }
    s[i] = 0;
    for (int j = 0; j < i / 2; j++) { char c = s[j]; s[j] = s[i-1-j]; s[i-1-j] = c; }
}

void ip_to_str(uint32_t ip, char* s) {
    if (ip == 0) {
        s[0] = '0'; s[1] = '.'; s[2] = '0'; s[3] = '.'; s[4] = '0'; s[5] = '.'; s[6] = '0'; s[7] = 0;
        return;
    }
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t part = (ip >> (i * 8)) & 0xFF;
        char part_str[4];
        app_itoa(part, part_str);
        int j = 0;
        while (part_str[j]) s[pos++] = part_str[j++];
        if (i < 3) s[pos++] = '.';
    }
    s[pos] = 0;
}

void mac_to_str(uint8_t* mac, char* s) {
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        s[i * 3] = hex[mac[i] >> 4];
        s[i * 3 + 1] = hex[mac[i] & 0x0F];
        if (i < 5) s[i * 3 + 2] = ':';
    }
    s[17] = 0;
}

void timestamp_to_str(kernel_api_t* api, uint64_t ts, char* s) {
    if (ts == 0) {
        s[0] = 'N'; s[1] = 'e'; s[2] = 'v'; s[3] = 'e'; s[4] = 'r'; s[5] = 0;
        return;
    }
    uint32_t h = (ts / 3600) % 24;
    uint32_t m = (ts / 60) % 60;
    uint32_t sc = ts % 60;
    h = (h + api->get_timezone_offset()) % 24;
    
    int pos = 0;
    s[pos++] = (h / 10) + '0'; s[pos++] = (h % 10) + '0'; s[pos++] = ':';
    s[pos++] = (m / 10) + '0'; s[pos++] = (m % 10) + '0'; s[pos++] = ':';
    s[pos++] = (sc / 10) + '0'; s[pos++] = (sc % 10) + '0'; s[pos] = 0;
}

int app_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int app_stricmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
        if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
    if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
    return (unsigned char)c1 - (unsigned char)c2;
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
    const char* menu_items[] = {"Wallpaper", "Network", "Date & Time", "About"};
    for (int i = 0; i < 4; i++) {
        uint32_t item_y = title_bar_h + 20 + (i * 40);
        if (i == selected_menu) {
            api->draw_rect(10, item_y - 10, sidebar_w - 20, 35, 0xE0E0E0, target_fb);
        }
        api->draw_string_scaled(30, item_y, menu_items[i], 0x333333, 70, target_fb);
    }

    // 6. Content Area based on selection
    uint32_t cx = sidebar_w + 40;
    uint32_t cy = title_bar_h + 40;

    if (selected_menu == 0) { // Wallpaper
        api->draw_string_scaled(cx, cy, "Wallpaper Settings", 0x222222, 90, target_fb);
        
        uint32_t grid_y = cy + 60;
        uint32_t item_w = 150;
        uint32_t item_h = 40;
        uint32_t items_per_row = (w - sidebar_w - 60) / item_w;
        if (items_per_row == 0) items_per_row = 1;

        char* p = wallpaper_list;
        int i = 0;
        while (*p && i < 100) {
            char name[256];
            int k = 0;
            while (*p && *p != '\n' && k < 255) name[k++] = *p++;
            name[k] = 0;
            if (*p == '\n') p++;

            uint32_t row = i / items_per_row;
            uint32_t col = i % items_per_row;
            uint32_t ix = cx + col * item_w;
            uint32_t iy = grid_y + row * (item_h + 10);
            uint32_t box_w = item_w - 10;

            uint32_t bg_color = 0xF0F0F0;
            if (app_stricmp(name, api->current_wallpaper) == 0) {
                bg_color = 0xD5D5D5;
            }

            api->draw_rounded_rect(ix, iy, box_w, item_h, 8, bg_color, target_fb);
            
            uint32_t text_w = api->get_string_width_scaled(name, 60);
            uint32_t tx = ix + (box_w > text_w ? (box_w - text_w) / 2 : 0);
            api->draw_string_scaled(tx, iy + (item_h - 14) / 2, name, 0x444444, 60, target_fb);
            
            i++;
        }
    }

    else if (selected_menu == 1) { // Network
        api->draw_string_scaled(cx, cy, "Network Settings", 0x222222, 90, target_fb);
        
        char buf[32];
        uint32_t ly = cy + 60;
        uint32_t label_x = cx;
        uint32_t value_x = cx + 150;
        uint32_t spacing = 35;

        // IP Address
        api->draw_string_scaled(label_x, ly, "IP Address:", 0x555555, 70, target_fb);
        ip_to_str(api->net_get_ip(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
        ly += spacing;

        // Subnet Mask
        api->draw_string_scaled(label_x, ly, "Subnet Mask:", 0x555555, 70, target_fb);
        ip_to_str(api->net_get_subnet(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
        ly += spacing;

        // Gateway
        api->draw_string_scaled(label_x, ly, "Gateway:", 0x555555, 70, target_fb);
        ip_to_str(api->net_get_gateway(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
        ly += spacing;

        // Primary DNS
        api->draw_string_scaled(label_x, ly, "Primary DNS:", 0x555555, 70, target_fb);
        ip_to_str(api->net_get_dns_primary(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
        ly += spacing;

        // Secondary DNS
        api->draw_string_scaled(label_x, ly, "Secondary DNS:", 0x555555, 70, target_fb);
        ip_to_str(api->net_get_dns_secondary(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
        ly += spacing + 15; // Extra space

        // NIC Name
        api->draw_string_scaled(label_x, ly, "Network Adapter:", 0x555555, 70, target_fb);
        api->draw_string_scaled(value_x, ly, api->net_get_nic_name(), 0x333333, 70, target_fb);
        ly += spacing;

        // MAC Address
        api->draw_string_scaled(label_x, ly, "MAC Address:", 0x555555, 70, target_fb);
        uint8_t mac[6];
        api->net_get_mac(mac);
        mac_to_str(mac, buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);
    }  
    
    else if (selected_menu == 2) { // Date & Time
        api->draw_string_scaled(cx, cy, "Date & Time", 0x222222, 90, target_fb);

        char buf[32];
        uint32_t ly = cy + 60;
        uint32_t label_x = cx;
        uint32_t value_x = cx + 180;
        uint32_t spacing = 35;

        // NTP Server
        api->draw_string_scaled(label_x, ly, "NTP Server:", 0x555555, 70, target_fb);
        api->draw_string_scaled(value_x, ly, api->net_get_ntp_server(), 0x333333, 70, target_fb);
        ly += spacing;

        // Timezone
        api->draw_string_scaled(label_x, ly, "Timezone:", 0x555555, 70, target_fb);
        api->draw_string_scaled(value_x, ly, api->get_timezone(), 0x333333, 70, target_fb);
        ly += spacing;

        // Last Sync
        api->draw_string_scaled(label_x, ly, "Last Sync:", 0x555555, 70, target_fb);
        timestamp_to_str(api, api->net_get_last_sync_time(), buf);
        api->draw_string_scaled(value_x, ly, buf, 0x333333, 70, target_fb);

        ly += spacing + 10;
        api->draw_rect(cx, ly, w - cx - 40, 1, 0xDDDDDD, target_fb); // Separator line
        ly += 25;
        api->draw_string_scaled(label_x, ly, "Set Timezone Manually", 0x333333, 80, target_fb);

        ly += 45;
        api->draw_string_scaled(label_x, ly, "Timezone:", 0x555555, 70, target_fb);
        
        uint32_t box_w = 220;
        uint32_t box_h = 32;
        api->draw_rounded_rect(value_x, ly - 8, box_w, box_h, 4, 0xFFFFFF, target_fb);
        api->draw_rect(value_x, ly - 8, box_w, 1, 0xCCCCCC, target_fb); // Top
        api->draw_rect(value_x, ly + box_h - 9, box_w, 1, 0xCCCCCC, target_fb); // Bottom
        api->draw_rect(value_x, ly - 8, 1, box_h, 0xCCCCCC, target_fb); // Left
        api->draw_rect(value_x + box_w - 1, ly - 8, 1, box_h, 0xCCCCCC, target_fb); // Right

        const char* tz_names[] = {
            "NTP (Automatic)",
            "UTC (Universal Time)",
            "GMT+0 (London, Lisbon)",
            "GMT+1 (Budapest, Paris)",
            "GMT+2 (Athens, Cairo)",
            "GMT+3 (Moscow, Riyadh)",
            "GMT+8 (Beijing, HK)",
            "GMT+9 (Tokyo, Seoul)",
            "GMT-5 (New York, Miami)",
            "GMT-8 (LA, Seattle)"
        };
        int tz_count = 10;
        api->draw_string_scaled(value_x + 10, ly, tz_names[selected_tz], 0x333333, 70, target_fb);

        // Arrow icon (small triangle)
        uint32_t arrow_x = value_x + box_w - 20;
        uint32_t arrow_y = ly + 4;
        for (int i = 0; i < 4; i++) {
            api->draw_rect(arrow_x + i, arrow_y + i, (4 - i) * 2, 1, 0x666666, target_fb);
        }

        if (tz_dropdown_open) {
            uint32_t drop_y = ly - 8 + box_h;
            uint32_t max_visible = 6;
            if (drop_y + (tz_count * box_h) > h - 20) {
                 max_visible = (h - 20 - drop_y) / box_h;
            }
            if (max_visible < 2) max_visible = 2;
            if (max_visible > (uint32_t)tz_count) max_visible = (uint32_t)tz_count;

            uint32_t drop_h = max_visible * box_h; 
            api->draw_rect(value_x, drop_y, box_w, drop_h, 0xFFFFFF, target_fb);
            api->draw_rect(value_x, drop_y, box_w, 1, 0xCCCCCC, target_fb); // Top border
            api->draw_rect(value_x, drop_y + drop_h - 1, box_w, 1, 0xCCCCCC, target_fb); // Bottom
            api->draw_rect(value_x, drop_y, 1, drop_h, 0xCCCCCC, target_fb); // Left
            api->draw_rect(value_x + box_w - 1, drop_y, 1, drop_h, 0xCCCCCC, target_fb); // Right
            
            // Scrollbar background
            if ((uint32_t)tz_count > max_visible) {
                api->draw_rect(value_x + box_w - 12, drop_y + 1, 11, drop_h - 2, 0xF0F0F0, target_fb);
                uint32_t thumb_h = (max_visible * (drop_h - 2)) / tz_count;
                uint32_t thumb_y = drop_y + 1 + (tz_scroll_offset * (drop_h - 2 - thumb_h)) / (tz_count - max_visible);
                api->draw_rect(value_x + box_w - 11, thumb_y, 9, thumb_h, 0xBBBBBB, target_fb);
            }

            for (uint32_t i = 0; i < max_visible; i++) {
                uint32_t idx = i + tz_scroll_offset;
                if (idx >= (uint32_t)tz_count) break;
                
                uint32_t item_y = drop_y + (i * box_h);
                if (idx == (uint32_t)selected_tz) {
                    api->draw_rect(value_x + 1, item_y + 1, box_w - ((uint32_t)tz_count > max_visible ? 13 : 2), box_h - 2, 0xE0E0E0, target_fb);
                }
                api->draw_string_scaled(value_x + 10, item_y + 8, tz_names[idx], 0x333333, 70, target_fb);
            }
        }
    }  

    else if (selected_menu == 3) { // About
        api->draw_string_scaled(cx, cy, "About AnimOS", 0x222222, 90, target_fb);
        
        // Boot Logo - Centered between the actual end of text and the right window edge
        if (api->boot_logo) {
            uint32_t logo_size = (w > 900) ? 180 : 120; // Slightly smaller in windowed mode
            
            // Calculate the actual right edge of the info text (widest line is usually Processor)
            uint32_t label_w = api->get_string_width_scaled("Processor: ", 70);
            uint32_t cpu_w = api->get_string_width_scaled(api->cpu_brand, 70);
            uint32_t actual_text_end = cx + label_w + cpu_w; 
            
            // Center the logo in the remaining space to the right
            uint32_t logo_x = actual_text_end + (w - actual_text_end) / 2 - (logo_size / 2);
            
            // Safety: Ensure it doesn't overlap text if window is very small
            if (logo_x < actual_text_end + 20) logo_x = actual_text_end + 20;
            if (logo_x + logo_size > w - 20) logo_x = w - logo_size - 20;

            // Vertically center relative to the info lines (from Version to Storage)
            uint32_t logo_y = cy + 50 + (125 / 2) - (logo_size / 2);
            
            api->draw_icon_scaled(logo_x, logo_y, logo_size, logo_size, api->boot_logo, target_fb);
        }

        api->draw_string_scaled(cx, cy + 50, "Version: AnimOS Experience 1.0 (Alpha)", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx, cy + 80, "Kernel: AnimKernel v1.0 x86_64 64bit", 0x555555, 70, target_fb);
        
        // Processor Brand
        api->draw_string_scaled(cx, cy + 115, "Processor: ", 0x555555, 70, target_fb);
        api->draw_string_scaled(cx + 80, cy + 115, api->cpu_brand, 0x555555, 70, target_fb);

        // RAM Size
        char ram_val[16];
        api->draw_string_scaled(cx, cy + 145, "Memory:", 0x555555, 70, target_fb);
        uint32_t ram_val_x = cx + api->get_string_width_scaled("Memory: ", 70);
        if (api->ram_size_mb >= 1024) {
            app_itoa((api->ram_size_mb + 512) / 1024, ram_val);
            api->draw_string_scaled(ram_val_x, cy + 145, ram_val, 0x333333, 70, target_fb);
            api->draw_string_scaled(ram_val_x + api->get_string_width_scaled(ram_val, 70) + 5, cy + 145, "GB Total", 0x555555, 70, target_fb);
        } else {
            app_itoa(api->ram_size_mb, ram_val);
            api->draw_string_scaled(ram_val_x, cy + 145, ram_val, 0x333333, 70, target_fb);
            api->draw_string_scaled(ram_val_x + api->get_string_width_scaled(ram_val, 70) + 5, cy + 145, "MB Total", 0x555555, 70, target_fb);
        }

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
        for (int i = 0; i < 1024; i++) wallpaper_list[i] = 0;
        api->list_dir("Sysroot:/AnimOS/assets/wallpapers", wallpaper_list, 1024);
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
                    tz_dropdown_open = 0;
                }
            }
        }

        // Content area hit test
        if (selected_menu == 0) { // Wallpaper
            uint32_t cx = sidebar_w + 40;
            uint32_t cy = title_bar_h + 40;
            uint32_t grid_y = cy + 60;
            uint32_t item_w = 150;
            uint32_t item_h = 40;
            uint32_t w = fb->framebuffer_width;
            uint32_t items_per_row = (w - sidebar_w - 60) / item_w;
            if (items_per_row == 0) items_per_row = 1;

            char* p = wallpaper_list;
            int i = 0;
            while (*p && i < 100) {
                char name[256];
                int k = 0;
                while (*p && *p != '\n' && k < 255) name[k++] = *p++;
                name[k] = 0;
                if (*p == '\n') p++;

                uint32_t row = i / items_per_row;
                uint32_t col = i % items_per_row;
                uint32_t ix = cx + col * item_w;
                uint32_t iy = grid_y + row * (item_h + 10);
                uint32_t box_w = item_w - 10;

                if (mx >= (int32_t)ix && mx <= (int32_t)(ix + box_w) && my >= (int32_t)iy && my <= (int32_t)(iy + item_h)) {
                    api->set_wallpaper(name);
                    return;
                }
                i++;
            }
        }

        if (selected_menu == 2) { // Date & Time
            uint32_t cx = sidebar_w + 40;
            uint32_t cy = title_bar_h + 40;
            uint32_t ly = cy + 60 + (35 * 3) + 10 + 25 + 45; 
            uint32_t value_x = cx + 180;
            uint32_t box_w = 220;
            uint32_t box_h = 32;

            if (tz_dropdown_open) {
                uint32_t drop_y = ly - 8 + box_h;
                uint32_t h = fb->framebuffer_height;
                uint32_t max_visible = 6;
                if (drop_y + (10 * box_h) > h - 20) {
                     max_visible = (h - 20 - drop_y) / box_h;
                }
                if (max_visible < 2) max_visible = 2;
                if (max_visible > 10) max_visible = 10;
                uint32_t drop_h = max_visible * box_h;

                // Scrollbar click
                if (10 > max_visible && mx >= (int32_t)(value_x + box_w - 12) && mx <= (int32_t)(value_x + box_w)) {
                    if (my < (int32_t)(drop_y + drop_h / 2)) {
                        if (tz_scroll_offset > 0) tz_scroll_offset--;
                    } else {
                        if (tz_scroll_offset < (int)(10 - max_visible)) tz_scroll_offset++;
                    }
                    return;
                }

                if (mx >= (int32_t)value_x && mx <= (int32_t)(value_x + box_w)) {
                    for (uint32_t i = 0; i < max_visible; i++) {
                        uint32_t item_y = drop_y + (i * box_h);
                        if (my >= (int32_t)item_y && my <= (int32_t)(item_y + box_h)) {
                            selected_tz = i + tz_scroll_offset;
                            tz_dropdown_open = 0;
                            return;
                        }
                    }
                }
                tz_dropdown_open = 0;
                if (mx >= (int32_t)value_x && mx <= (int32_t)(value_x + box_w) && my >= (int32_t)(ly - 8) && my <= (int32_t)(ly - 8 + box_h)) {
                   return; 
                }
            } else {
                if (mx >= (int32_t)value_x && mx <= (int32_t)(value_x + box_w) && my >= (int32_t)(ly - 8) && my <= (int32_t)(ly - 8 + box_h)) {
                    tz_dropdown_open = 1;
                    return;
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

