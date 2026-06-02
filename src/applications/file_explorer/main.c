#include "../../kernel/api.h"

static uint32_t title_bar_h = 40;

static void draw_window(kernel_api_t* api, struct multiboot_tag_framebuffer* fb) {
    uint32_t w = fb->framebuffer_width;
    uint32_t h = fb->framebuffer_height;

    api->draw_rect(0, 0, w, title_bar_h, 0xF0F0F0, fb);
    api->draw_rect(0, title_bar_h, w, h - title_bar_h, 0xFFFFFF, fb);
    api->draw_rect(0, title_bar_h, w, 1, 0xDDDDDD, fb);

    api->draw_string_scaled(20, (title_bar_h - (18 * 80 / 100)) / 2, "File Explorer", 0x333333, 80, fb);

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
    }

    if (event == APP_EVENT_INIT || event == APP_EVENT_TICK || event == APP_EVENT_CLICK) {
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
