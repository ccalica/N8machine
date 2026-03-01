#include "emu_video.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "n8_font.h"
#include "m6502.h"
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

static uint8_t vid_regs[8] = { 0 };
static uint8_t vsync_counter = 0;

// Pixel buffer for the display module
static uint32_t screen_pixels[N8_SCREEN_MAX_W * N8_SCREEN_MAX_H];
static n8_screen_t screen = { screen_pixels, 0, 0, false };

void video_init() { video_reset(); }

void video_reset() {
    memset(vid_regs, 0, sizeof(vid_regs));
    vid_regs[N8_VID_MODE]   = N8_VIDMODE_TEXT_DEFAULT;
    vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
    vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
    vsync_counter = 0;
    memset(screen_pixels, 0, sizeof(screen_pixels));
    screen.width  = N8_VID_DEFAULT_WIDTH * N8_FONT_WIDTH;
    screen.height = N8_VID_DEFAULT_HEIGHT * N8_FONT_HEIGHT;
    screen.dirty  = true;
}

static void video_apply_mode(uint8_t mode) {
    if (mode == N8_VIDMODE_TEXT_DEFAULT) {
        vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
        vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
        vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
    }
    // N8_VIDMODE_TEXT_CUSTOM: retain current values
}

// Guard: clamp stride*height to FB_SIZE to prevent overrun
static int safe_rows() {
    int s = vid_regs[N8_VID_STRIDE];
    int h = vid_regs[N8_VID_HEIGHT];
    if (s == 0) return 0;
    int max_rows = N8_FB_SIZE / s;
    return (h < max_rows) ? h : max_rows;
}

static void video_scroll_up() {
    int w = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    memmove(frame_buffer, frame_buffer + w, w * (h - 1));
    memset(frame_buffer + w * (h - 1), 0x00, w);
}

static void video_scroll_down() {
    int w = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    memmove(frame_buffer + w, frame_buffer, w * (h - 1));
    memset(frame_buffer, 0x00, w);
}

static void video_scroll_left() {
    int w = vid_regs[N8_VID_WIDTH];
    int s = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0 || w > s) return;
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x00;
    }
}

static void video_scroll_right() {
    int w = vid_regs[N8_VID_WIDTH];
    int s = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0 || w > s) return;
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line + 1, line, w - 1);
        line[0] = 0x00;
    }
}

void video_decode(uint64_t& pins, uint8_t reg) {
    if (reg == N8_VID_VSYNC) {
        // VID_VSYNC: read-only frame counter
        if (pins & M6502_RW) M6502_SET_DATA(pins, vsync_counter);
        return;
    }
    if (reg > N8_VID_VSYNC) {
        // Phantom registers: read 0, write no-op
        if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
        return;
    }

    if (pins & M6502_RW) {
        // Read
        if (reg == N8_VID_OPER) {
            M6502_SET_DATA(pins, 0x00);  // Write-only; reads return 0
        } else {
            M6502_SET_DATA(pins, vid_regs[reg]);
        }
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_VID_MODE:
                vid_regs[reg] = val;
                video_apply_mode(val);
                break;
            case N8_VID_OPER:
                // Write-once trigger, does not latch
                switch (val) {
                    case N8_VIDOP_NOP:          /* no-op */           break;
                    case N8_VIDOP_SCROLL_UP:    video_scroll_up();    break;
                    case N8_VIDOP_SCROLL_DOWN:  video_scroll_down();  break;
                    case N8_VIDOP_SCROLL_LEFT:  video_scroll_left();  break;
                    case N8_VIDOP_SCROLL_RIGHT: video_scroll_right(); break;
                }
                break;
            default:
                vid_regs[reg] = val;
                break;
        }
    }
}

uint8_t video_get_mode()         { return vid_regs[N8_VID_MODE]; }
uint8_t video_get_width()        { return vid_regs[N8_VID_WIDTH]; }
uint8_t video_get_height()       { return vid_regs[N8_VID_HEIGHT]; }
uint8_t video_get_stride()       { return vid_regs[N8_VID_STRIDE]; }
uint8_t video_get_cursor_style() { return vid_regs[N8_VID_CURSOR]; }
uint8_t video_get_cursor_col()   { return vid_regs[N8_VID_CURCOL]; }
uint8_t video_get_cursor_row()   { return vid_regs[N8_VID_CURROW]; }

const n8_screen_t* video_get_screen() { return &screen; }

void video_rasterize(uint32_t frame_count) {
    vsync_counter++;

    uint8_t cursor_reg = vid_regs[N8_VID_CURSOR];
    uint8_t cursor_mode = cursor_reg & N8_VID_CURSOR_MODE_MASK;
    bool cursor_active = (cursor_mode != N8_VID_CURSOR_OFF);

    // Skip if frame buffer hasn't changed and no cursor to animate
    if (!fb_dirty && !cursor_active) return;

    int cols = vid_regs[N8_VID_WIDTH];
    int rows = safe_rows();
    int stride = vid_regs[N8_VID_STRIDE];
    if (cols == 0 || rows == 0 || stride == 0) return;

    // Clamp to pixel buffer limits
    int px_w = cols * N8_FONT_WIDTH;
    int px_h = rows * N8_FONT_HEIGHT;
    if (px_w > N8_SCREEN_MAX_W) { cols = N8_SCREEN_MAX_W / N8_FONT_WIDTH; px_w = cols * N8_FONT_WIDTH; }
    if (px_h > N8_SCREEN_MAX_H) { rows = N8_SCREEN_MAX_H / N8_FONT_HEIGHT; px_h = rows * N8_FONT_HEIGHT; }

    // Rasterize frame_buffer characters → screen_pixels (white on black)
    const uint32_t fg = 0xFFFFFFFF;  // white
    const uint32_t bg = 0xFF000000;  // black

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            uint8_t ch = frame_buffer[r * stride + c];
            const uint8_t* glyph = n8_font[ch];
            int px_x = c * N8_FONT_WIDTH;
            int px_y = r * N8_FONT_HEIGHT;

            for (int y = 0; y < N8_FONT_HEIGHT; y++) {
                uint8_t row_bits = glyph[y];
                uint32_t* dst = &screen_pixels[(px_y + y) * px_w + px_x];
                for (int x = 0; x < N8_FONT_WIDTH; x++) {
                    dst[x] = (row_bits & (0x80 >> x)) ? fg : bg;
                }
            }
        }
    }

    // Cursor compositing
    if (cursor_active) {
        uint8_t cur_col = vid_regs[N8_VID_CURCOL];
        uint8_t cur_row = vid_regs[N8_VID_CURROW];

        // Only draw if cursor is within visible area
        if (cur_col < cols && cur_row < rows) {
            uint8_t rate = (cursor_reg & N8_VID_CURSOR_RATE_MASK) >> 4;
            bool visible = true;

            if (cursor_mode == N8_VID_CURSOR_FLASH) {
                if (rate == 0)
                    visible = false;  // rate 0 = not displayed
                else
                    visible = ((frame_count / rate) & 1) == 0;
            }

            if (visible) {
                bool is_block = (cursor_reg & N8_VID_CURSOR_SHAPE_MASK) == N8_VID_CURSOR_BLOCK;
                int cx = cur_col * N8_FONT_WIDTH;
                int cy = cur_row * N8_FONT_HEIGHT;
                int y_start = is_block ? 0 : (N8_FONT_HEIGHT - 2);
                int y_end   = N8_FONT_HEIGHT;

                for (int y = y_start; y < y_end; y++) {
                    uint32_t* dst = &screen_pixels[(cy + y) * px_w + cx];
                    for (int x = 0; x < N8_FONT_WIDTH; x++) {
                        dst[x] ^= 0x00FFFFFF;  // XOR invert RGB, preserve alpha
                    }
                }
            }
        }
    }

    screen.width  = px_w;
    screen.height = px_h;
    screen.dirty  = true;
    fb_dirty = false;
}

// ---- Screenshot (PNG encoding to memory) ----

static std::vector<uint8_t> screenshot_buf;

static void screenshot_write_func(void* context, void* data, int size) {
    auto* buf = (std::vector<uint8_t>*)context;
    const uint8_t* bytes = (const uint8_t*)data;
    buf->insert(buf->end(), bytes, bytes + size);
}

const uint8_t* video_screenshot(size_t* out_len) {
    // Force rasterize with frame_count=0 (cursor steady-visible)
    fb_dirty = true;
    video_rasterize(0);

    int w = screen.width;
    int h = screen.height;
    if (w <= 0 || h <= 0) {
        *out_len = 0;
        return nullptr;
    }

    screenshot_buf.clear();
    stbi_write_png_to_func(screenshot_write_func, &screenshot_buf,
                           w, h, 4, screen_pixels, w * 4);
    *out_len = screenshot_buf.size();
    return screenshot_buf.data();
}
