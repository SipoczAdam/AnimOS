#include "../../kernel/api.h"

static uint32_t title_bar_h = 40;
static uint32_t sidebar_w = 200;
static uint8_t* disk_icon = 0;
static uint8_t* back_icon = 0;
static uint8_t* reload_icon = 0;
static uint8_t* folder_icon = 0;
static uint8_t* file_icon = 0;
static uint8_t* exe_icon = 0;
static uint8_t* bmp_icon = 0;
static uint8_t* xml_icon = 0;
static uint8_t* cursor_icon = 0;

typedef struct {
    char name[256];
    char date[32];
    int is_directory;
} file_entry_t;

static file_entry_t current_files[128];
static int file_count = 0;
static int scroll_offset = 0;
static char current_path[512] = "Local Disk (Sysroot:)/";
static int hover_file_index = -1;
static int selected_file_index = -1;

static int is_hovered = 0;
static int is_selected = 0;
static int is_drive_opened = 0;

static int hover_back = 0;
static int hover_reload = 0;

static uint32_t current_ticks = 0;
static uint32_t last_click_tick = 0;

static int is_inside(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return (mx >= (int32_t)x && mx <= (int32_t)(x + w) && my >= (int32_t)y && my <= (int32_t)(y + h));
}

static void itoa_custom(uint32_t n, char* s) {
    uint32_t i = 0, j;
    if (n == 0) { s[i++] = '0'; s[i] = 0; return; }
    while (n > 0) { s[i++] = (n % 10) + '0'; n /= 10; }
    s[i] = 0;
    for (j = 0; j < i / 2; j++) { char c = s[j]; s[j] = s[i - 1 - j]; s[i - 1 - j] = c; }
}

static void strcat_custom(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static void strcpy_custom(char* dest, const char* src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int strcmp_custom(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int ends_with_ignore_case(const char* str, const char* suffix) {
    int str_len = 0; while (str[str_len]) str_len++;
    int suf_len = 0; while (suffix[suf_len]) suf_len++;
    if (suf_len > str_len) return 0;
    
    for (int i = 0; i < suf_len; i++) {
        char c1 = str[str_len - suf_len + i];
        char c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

static void refresh_file_list(kernel_api_t* api) {
    file_count = 0;
    static char buffer[8192];
    int res = api->list_dir(current_path, buffer, sizeof(buffer));
    if (res <= 0) return;

    char* ptr = buffer;
    char* end = buffer + res;
    while (ptr < end && file_count < 128) {
        char type = *ptr++;
        
        int k;
        for (k = 0; k < 16 && ptr < end; k++) {
            current_files[file_count].date[k] = *ptr++;
        }
        current_files[file_count].date[k] = 0;

        int i = 0;
        while (ptr < end && *ptr != '\n' && i < 255) {
            current_files[file_count].name[i++] = *ptr++;
        }
        current_files[file_count].name[i] = 0;
        if (ptr < end && *ptr == '\n') ptr++;
        
        current_files[file_count].is_directory = (type == 'D');
        file_count++;
    }
    selected_file_index = -1;
    hover_file_index = -1;
}

static void draw_window(kernel_api_t* api, struct multiboot_tag_framebuffer* fb) {
    uint32_t w = fb->framebuffer_width;
    uint32_t h = fb->framebuffer_height;

    uint32_t nav_bar_h = 36;
    api->draw_rect(0, 0, w, title_bar_h, 0xF0F0F0, fb); // Title bar
    
    // Navigation bar (Breadcrumb) - Always visible
    api->draw_rect(0, title_bar_h, w, nav_bar_h, 0xFAFAFA, fb);
    api->draw_rect(0, title_bar_h + nav_bar_h, w, 1, 0xEEEEEE, fb);

    // Navigation buttons
    uint32_t btn_y = title_bar_h + (nav_bar_h - 22) / 2;
    if (hover_back) api->draw_rounded_rect(10, btn_y - 2, 26, 26, 4, 0xEEEEEE, fb);
    if (back_icon) api->draw_icon_scaled(12, btn_y, 22, 22, back_icon, fb);
    
    if (hover_reload) api->draw_rounded_rect(44, btn_y - 2, 26, 26, 4, 0xEEEEEE, fb);
    if (reload_icon) api->draw_icon_scaled(46, btn_y, 22, 22, reload_icon, fb);

    api->draw_rect(80, title_bar_h + 8, 1, nav_bar_h - 16, 0xDDDDDD, fb); // Separator

    uint32_t bar_text_y = title_bar_h + (nav_bar_h - (18 * 70 / 100)) / 2;
    uint32_t cur_x = 100;

    // Breadcrumb: File Explorer
    api->draw_string_scaled(cur_x, bar_text_y, "File Explorer", 0x555555, 70, fb);
    cur_x += api->get_string_width_scaled("File Explorer", 70) + 8;
    api->draw_string_scaled(cur_x, bar_text_y + 1, ">", 0x888888, 70, fb);
    cur_x += api->get_string_width_scaled(">", 70) + 8;

    if (is_drive_opened) {
        // Breadcrumb: Path
        char temp_path[512];
        strcpy_custom(temp_path, current_path);
        
        char* p = temp_path;
        if (strcmp_custom(p, "Sysroot:/") == 0) {
            api->draw_string_scaled(cur_x, bar_text_y, "Local Disk (Sysroot:)", 0x555555, 70, fb);
            cur_x += api->get_string_width_scaled("Local Disk (Sysroot:)", 70) + 8;
            api->draw_string_scaled(cur_x, bar_text_y + 1, ">", 0x888888, 70, fb);
        } else {
            // Very basic breadcrumb for nested paths: just show the full path string
            api->draw_string_scaled(cur_x, bar_text_y, current_path, 0x555555, 70, fb);
            cur_x += api->get_string_width_scaled(current_path, 70) + 8;
            api->draw_string_scaled(cur_x, bar_text_y + 1, ">", 0x888888, 70, fb);
        }
    }

    uint32_t content_y = title_bar_h + nav_bar_h + 1;
    api->draw_rect(0, content_y, sidebar_w, h - content_y, 0xF9F9F9, fb); // Sidebar
    api->draw_rect(sidebar_w, content_y, w - sidebar_w, h - content_y, 0xFFFFFF, fb); // Main area

    api->draw_rect(0, title_bar_h, w, 1, 0xDDDDDD, fb); // Horizontal separator under title
    api->draw_rect(sidebar_w, content_y, 1, h - content_y, 0xDDDDDD, fb); // Vertical separator

    api->draw_string_scaled(20, (title_bar_h - (18 * 80 / 100)) / 2, "File Explorer", 0x333333, 80, fb);

    uint32_t cx = sidebar_w + 40;
    uint32_t cy = content_y + 40;

    if (is_drive_opened) {
        // Column Headers
        uint32_t header_y = content_y + 12;
        api->draw_string_scaled(sidebar_w + 60, header_y, "Name", 0x888888, 65, fb);
        api->draw_string_scaled(w - 180, header_y, "Modification date", 0x888888, 65, fb);
        api->draw_rect(sidebar_w + 20, header_y + 18, w - sidebar_w - 40, 1, 0xEEEEEE, fb);

        uint32_t visible_h = h - cy;
        int total_items_h = file_count * 30;
        int max_scroll = (total_items_h > (int)visible_h - 20) ? (total_items_h - ((int)visible_h - 20)) : 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
        if (scroll_offset < 0) scroll_offset = 0;

        if (file_count == 0) {
            api->draw_string_scaled(cx, cy, "This folder is empty.", 0x888888, 75, fb);
        } else {
            for (int i = 0; i < file_count; i++) {
                int item_y_final = (i * 30) - scroll_offset;
                if (item_y_final + 30 < 0) continue;
                if (item_y_final > (int)visible_h - 20) continue;

                uint32_t item_y = cy + item_y_final;
                uint32_t item_w = w - sidebar_w - 80;
                uint32_t item_h = 28;

                if (selected_file_index == i) {
                    api->draw_rounded_rect(sidebar_w + 20, item_y - 4, item_w, item_h, 4, 0xD7E8FA, fb);
                } else if (hover_file_index == i) {
                    api->draw_rounded_rect(sidebar_w + 20, item_y - 4, item_w, item_h, 4, 0xEDF4FC, fb);
                }

                uint8_t* icon = folder_icon;
                if (!current_files[i].is_directory) {
                    icon = file_icon;
                    const char* name = current_files[i].name;
                    if (ends_with_ignore_case(name, ".bin")) {
                        icon = exe_icon;
                    } else if (ends_with_ignore_case(name, ".bmp")) {
                        icon = bmp_icon;
                    } else if (ends_with_ignore_case(name, ".xml")) {
                        icon = xml_icon;
                    } else if (ends_with_ignore_case(name, ".cur") || ends_with_ignore_case(name, ".ani")) {
                        icon = cursor_icon;
                    }
                }
                
                if (icon) api->draw_icon_scaled(sidebar_w + 30, item_y, 20, 20, icon, fb);
                api->draw_string_scaled(sidebar_w + 60, item_y + 2, current_files[i].name, 0x333333, 70, fb);
                
                uint32_t date_x = w - 180;
                api->draw_string_scaled(date_x, item_y + 2, current_files[i].date, 0x888888, 65, fb);
            }

            if (max_scroll > 0) {
                uint32_t sb_w = 8;
                uint32_t sb_x = w - sb_w - 4;
                uint32_t sb_y = content_y + 40;
                uint32_t sb_h = h - content_y - 50;
                api->draw_rect(sb_x, sb_y, sb_w, sb_h, 0xF4F4F4, fb);
                uint32_t thumb_h = (sb_h * sb_h) / (sb_h + max_scroll);
                if (thumb_h < 30) thumb_h = 30;
                uint32_t thumb_y = sb_y + (scroll_offset * (sb_h - thumb_h)) / max_scroll;
                api->draw_rounded_rect(sb_x, thumb_y, sb_w, thumb_h, 4, 0xCDCDCD, fb);
            }
        }
    } else {
        api->draw_string_scaled(cx, cy, "Disks and Drives", 0x222222, 90, fb);
        
        uint32_t icon_cy = cy; // Reference point for disk icon
        if (disk_icon) {
            uint32_t icon_y = icon_cy + 60;

            // Selection/Hover highlight
            uint32_t item_x = cx - 10;
            uint32_t item_y = icon_y - 10;
            uint32_t item_w = 320;
            uint32_t item_h = 70;

            if (is_selected) {
                api->draw_rounded_rect(item_x, item_y, item_w, item_h, 4, 0xD7E8FA, fb);
            } else if (is_hovered) {
                api->draw_rounded_rect(item_x, item_y, item_w, item_h, 4, 0xEDF4FC, fb);
            }

            uint32_t icon_size = 48;
            api->draw_icon_scaled(cx, icon_y, icon_size, icon_size, disk_icon, fb);

            uint32_t info_h = 18 + 8 + 14 + 6; // Height of label + bar + info + small gaps
            uint32_t info_y = icon_y + (icon_size - info_h) / 2;

            api->draw_string_scaled(cx + 64, info_y, "Local Disk (Sysroot:)", 0x333333, 75, fb);

            uint32_t total_mb = api->disk_size_mb;
            uint32_t used_mb = api->disk_used_mb;
            uint32_t free_mb = (total_mb > used_mb) ? (total_mb - used_mb) : 0;

            char buf[128];
            char num_buf[32];
            int pos = 0;

            if (total_mb < 1024) {
                itoa_custom(free_mb, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                const char* mid = " MB free of ";
                for(int k=0; mid[k]; k++) buf[pos++] = mid[k];
                itoa_custom(total_mb, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                const char* end = " MB";
                for(int k=0; end[k]; k++) buf[pos++] = end[k];
            } else {
                uint32_t free_gb_int = free_mb / 1024;
                uint32_t free_gb_frac = (free_mb % 1024) * 10 / 1024;
                uint32_t total_gb_int = total_mb / 1024;
                uint32_t total_gb_frac = (total_mb % 1024) * 10 / 1024;
                itoa_custom(free_gb_int, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                buf[pos++] = '.';
                itoa_custom(free_gb_frac, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                const char* mid = " GB free of ";
                for(int k=0; mid[k]; k++) buf[pos++] = mid[k];
                itoa_custom(total_gb_int, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                buf[pos++] = '.';
                itoa_custom(total_gb_frac, num_buf);
                for(int k=0; num_buf[k]; k++) buf[pos++] = num_buf[k];
                const char* end = " GB";
                for(int k=0; end[k]; k++) buf[pos++] = end[k];
            }
            buf[pos] = 0;

            // Progress bar
            uint32_t bar_w = 200;
            uint32_t bar_y = info_y + 22;
            api->draw_rect(cx + 64, bar_y, bar_w, 8, 0xEEEEEE, fb);
            if (total_mb > 0) {
                uint32_t fill_w = (used_mb * bar_w) / total_mb;
                if (fill_w > bar_w) fill_w = bar_w;
                api->draw_rect(cx + 64, bar_y, fill_w, 8, 0x0078D7, fb);
            }

            // Space info
            api->draw_string_scaled(cx + 64, bar_y + 14, buf, 0x777777, 60, fb);
        }
    }

    uint32_t close_size = 22;
    uint32_t max_size = 24;
    uint32_t min_size = 22;
    uint32_t close_y = (title_bar_h - close_size) / 2;
    uint32_t close_x = w - close_size - 12;
    if (api->close_icon) api->draw_icon_scaled(close_x, close_y, close_size, close_size, api->close_icon, fb);
    uint32_t max_y = (title_bar_h - max_size) / 2;
    uint32_t max_x = close_x - max_size - 8;
    if (api->maximize_icon) api->draw_icon_scaled(max_x, max_y, max_size, max_size, api->maximize_icon, fb);
    uint32_t min_y = (title_bar_h - min_size) / 2;
    uint32_t min_x = max_x - min_size - 8;
    if (api->minimize_icon) api->draw_icon_scaled(min_x, min_y, min_size, min_size, api->minimize_icon, fb);
}

__attribute__((section(".text.main")))
void main(kernel_api_t* api, struct multiboot_tag_framebuffer* fb, app_event_t event) {
    if (event == APP_EVENT_CLICK || event == APP_EVENT_TICK) {
        int32_t mx, my, wheel;
        uint8_t clicked;
        api->get_mouse_pos(&mx, &my, &clicked, &wheel);

        if (wheel != 0) {
            uint32_t nav_bar_h = 36;
            uint32_t content_y = title_bar_h + nav_bar_h + 1;
            if (is_inside(mx, my, sidebar_w, content_y, fb->framebuffer_width - sidebar_w, fb->framebuffer_height - content_y)) {
                scroll_offset -= wheel * 30;
                
                int total_items_h = file_count * 30;
                int visible_h = fb->framebuffer_height - content_y;
                int max_scroll = (total_items_h > (int)visible_h - 60) ? (total_items_h - ((int)visible_h - 60)) : 0;
                if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                if (scroll_offset < 0) scroll_offset = 0;
            }
        }
    }

    if (event == APP_EVENT_CLICK) {
        int32_t mx, my, wheel;
        uint8_t clicked;
        api->get_mouse_pos(&mx, &my, &clicked, &wheel);

        uint32_t close_size = 22;
        uint32_t max_size = 24;
        uint32_t min_size = 22;
        uint32_t close_x = fb->framebuffer_width - close_size - 12;
        uint32_t close_y = (title_bar_h - close_size) / 2;
        uint32_t max_x = close_x - max_size - 8;
        uint32_t max_y = (title_bar_h - max_size) / 2;
        uint32_t min_x = max_x - min_size - 8;
        uint32_t min_y = (title_bar_h - min_size) / 2;

        if (mx >= (int32_t)max_x && mx <= (int32_t)(max_x + max_size) && my >= (int32_t)max_y && my <= (int32_t)(max_y + max_size)) {
            api->window_maximized = !api->window_maximized;
            return;
        }
        if (mx >= (int32_t)min_x && mx <= (int32_t)(min_x + min_size) && my >= (int32_t)min_y && my <= (int32_t)(min_y + min_size)) {
            api->window_minimized = 1;
            return;
        }

        uint32_t nav_bar_h = 36;
        uint32_t btn_y = title_bar_h + (nav_bar_h - 22) / 2;
        if (is_inside(mx, my, 10, btn_y - 2, 26, 26)) {
            if (is_drive_opened) {
                int len = 0; while (current_path[len]) len++;
                if (len > 9) { // More than "Sysroot:/"
                    if (current_path[len-1] == '/') current_path[--len] = 0;
                    while (len > 9 && current_path[len-1] != '/') current_path[--len] = 0;
                    if (len == 9) is_drive_opened = 0;
                    else refresh_file_list(api);
                } else {
                    is_drive_opened = 0;
                }
            }
            return;
        }

        if (is_inside(mx, my, 44, btn_y - 2, 26, 26)) {
            if (is_drive_opened) refresh_file_list(api);
            return;
        }

        if (is_drive_opened) {
            uint32_t content_y = title_bar_h + nav_bar_h + 1;
            uint32_t cy = content_y + 40;
            int visible_h = fb->framebuffer_height - content_y;
            int found_click = 0;
            for (int i = 0; i < file_count; i++) {
                int item_y_final = (i * 30) - scroll_offset;
                if (item_y_final + 30 < 0 || item_y_final > (int)visible_h - 60) continue;
                uint32_t item_y = cy + item_y_final;
                uint32_t item_w = fb->framebuffer_width - sidebar_w - 80;
                if (is_inside(mx, my, sidebar_w + 20, item_y - 4, item_w, 28)) {
                    if (selected_file_index == i && (current_ticks - last_click_tick) < 50) {
                        if (current_files[i].is_directory) {
                            strcat_custom(current_path, current_files[i].name);
                            strcat_custom(current_path, "/");
                            refresh_file_list(api);
                        } else {
                            const char* name = current_files[i].name;
                            int name_len = 0; while (name[name_len]) name_len++;
                            if (ends_with_ignore_case(name, ".bin")) {
                                if (strcmp_custom(name, "file_explorer.bin") != 0 && strcmp_custom(name, "FILE_EXPLORER.BIN") != 0) {
                                    char full_path[512];
                                    strcpy_custom(full_path, current_path);
                                    strcat_custom(full_path, name);
                                    api->launch_app(full_path);
                                }
                            }
                        }
                    }
                    selected_file_index = i;
                    found_click = 1;
                    break;
                }
            }
            if (!found_click) selected_file_index = -1;
        } else {
            uint32_t cx = sidebar_w + 40;
            uint32_t cy = title_bar_h + nav_bar_h + 1 + 40;
            uint32_t icon_y = cy + 60;
            int clicked_drive = is_inside(mx, my, cx - 10, icon_y - 10, 320, 70);
            if (clicked_drive && is_selected && (current_ticks - last_click_tick) < 50) {
                is_drive_opened = 1;
                strcpy_custom(current_path, "Sysroot:/");
                refresh_file_list(api);
            }
            is_selected = clicked_drive;
        }
        
        last_click_tick = current_ticks;
    }

    if (event == APP_EVENT_INIT || event == APP_EVENT_TICK || event == APP_EVENT_CLICK) {
        if (event == APP_EVENT_TICK) current_ticks++;
        if (event == APP_EVENT_INIT) {
            disk_icon = api->disk_icon;
            back_icon = api->back_icon;
            reload_icon = api->reload_icon;
            folder_icon = api->folder_icon;
            file_icon = api->file_icon;
            exe_icon = api->exe_icon;
            bmp_icon = api->bmp_icon;
            xml_icon = api->xml_icon;
            cursor_icon = api->cursor_icon;

            refresh_file_list(api);
        }
        if (event == APP_EVENT_TICK) {
            int32_t mx, my, wheel;
            uint8_t clicked;
            api->get_mouse_pos(&mx, &my, &clicked, &wheel);

            uint32_t nav_bar_h = 36;
            uint32_t btn_y = title_bar_h + (nav_bar_h - 22) / 2;
            hover_back = is_inside(mx, my, 10, btn_y - 2, 26, 26);
            hover_reload = is_inside(mx, my, 44, btn_y - 2, 26, 26);

            if (is_drive_opened) {
                uint32_t content_y = title_bar_h + nav_bar_h + 1;
                uint32_t cy = content_y + 40;
                hover_file_index = -1;
                for (int i = 0; i < file_count; i++) {
                    int item_y_final = (i * 30) - scroll_offset;
                    if (item_y_final + 30 < 0 || item_y_final > (int)(fb->framebuffer_height - cy - 20)) continue;
                    uint32_t item_y = cy + item_y_final;
                    uint32_t item_w = fb->framebuffer_width - sidebar_w - 80;
                    if (is_inside(mx, my, sidebar_w + 20, item_y - 4, item_w, 28)) {
                        hover_file_index = i;
                        break;
                    }
                }
            } else {
                uint32_t cx = sidebar_w + 40;
                uint32_t cy = title_bar_h + nav_bar_h + 1 + 40;
                uint32_t icon_y = cy + 60;
                is_hovered = is_inside(mx, my, cx - 10, icon_y - 10, 320, 70);
            }
        }

        struct multiboot_tag_framebuffer buffer_fb = *fb;
        buffer_fb.framebuffer_addr = (uint64_t)api->window_buffer;
        buffer_fb.framebuffer_pitch = fb->framebuffer_width * 4;
        buffer_fb.framebuffer_bpp = 32;

        draw_window(api, &buffer_fb);
        api->blit_buffer(api->window_buffer, fb);

        if (event == APP_EVENT_TICK) {
            api->yield();
        }
    }
}
