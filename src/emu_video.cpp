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

static uint8_t vid_regs[N8_VID_REG_COUNT] = { 0 };
static uint8_t vsync_counter = 0;
static bool overflow_flag = false;

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
    vid_regs[N8_VID_CTRL]   = N8_VIDCTRL_DEFAULT;
    overflow_flag = false;
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

static void video_clear_overflow() { overflow_flag = false; }

// Shared cursor advance logic for VID_DATA read/write.
// allow_scroll: true for writes (SCROLL bit honored), false for reads (never scroll).
static void video_advance_cursor(bool allow_scroll) {
    uint8_t ctrl = vid_regs[N8_VID_CTRL];
    if (!(ctrl & N8_VIDCTRL_ADVANCE)) return;

    uint8_t col = vid_regs[N8_VID_CURCOL];
    uint8_t row = vid_regs[N8_VID_CURROW];
    uint8_t width = vid_regs[N8_VID_WIDTH];
    uint8_t height = vid_regs[N8_VID_HEIGHT];

    col++;

    if (col >= width) {
        if (ctrl & N8_VIDCTRL_WRAP) {
            col = 0;
            row++;
        } else {
            col = width > 0 ? width - 1 : 0;
            overflow_flag = true;
        }
    }

    if (row >= height) {
        if (allow_scroll && (ctrl & N8_VIDCTRL_SCROLL)) {
            video_scroll_up();
            row = height > 0 ? height - 1 : 0;
        } else {
            row = height > 0 ? height - 1 : 0;
            overflow_flag = true;
        }
    }

    vid_regs[N8_VID_CURCOL] = col;
    vid_regs[N8_VID_CURROW] = row;
}

void video_decode(uint64_t& pins, uint8_t reg) {
    if (reg == N8_VID_VSYNC) {
        // VID_VSYNC: read-only frame counter
        if (pins & M6502_RW) M6502_SET_DATA(pins, vsync_counter);
        return;
    }
    if (reg >= N8_VID_REG_COUNT) {
        // Phantom registers: read 0, write no-op
        if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
        return;
    }

    if (pins & M6502_RW) {
        // ---- Read path ----
        switch (reg) {
            case N8_VID_OPER:
                M6502_SET_DATA(pins, 0x00);  // Write-only; reads return 0
                break;
            case N8_VID_STATUS:
                M6502_SET_DATA(pins, overflow_flag ? N8_VIDSTAT_OVERFLOW : 0x00);
                break;
            case N8_VID_DATA: {
                uint8_t row = vid_regs[N8_VID_CURROW];
                uint8_t col = vid_regs[N8_VID_CURCOL];
                int stride = vid_regs[N8_VID_STRIDE];
                int offset = row * stride + col;
                if (offset >= N8_FB_SIZE) {
                    M6502_SET_DATA(pins, 0x00);
                    overflow_flag = true;
                } else {
                    M6502_SET_DATA(pins, frame_buffer[offset]);
                    video_advance_cursor(false);  // never scroll on read
                }
                break;
            }
            default:
                M6502_SET_DATA(pins, vid_regs[reg]);
                break;
        }
    } else {
        // ---- Write path ----
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_VID_MODE:
                vid_regs[reg] = val;
                video_apply_mode(val);
                break;
            case N8_VID_OPER:
                // Write-once trigger, does not latch
                switch (val) {
                    case N8_VIDOP_NOP:          break;
                    case N8_VIDOP_SCROLL_UP:    video_scroll_up();    break;
                    case N8_VIDOP_SCROLL_DOWN:  video_scroll_down();  break;
                    case N8_VIDOP_SCROLL_LEFT:  video_scroll_left();  break;
                    case N8_VIDOP_SCROLL_RIGHT: video_scroll_right(); break;
                    case N8_VIDOP_CLEAR: {
                        int stride = vid_regs[N8_VID_STRIDE];
                        int height = vid_regs[N8_VID_HEIGHT];
                        int bytes = stride * height;
                        if (bytes > N8_FB_SIZE) bytes = N8_FB_SIZE;
                        memset(frame_buffer, 0x20, bytes);
                        vid_regs[N8_VID_CURCOL] = 0;
                        vid_regs[N8_VID_CURROW] = 0;
                        fb_dirty = true;
                        video_clear_overflow();
                        break;
                    }
                    case N8_VIDOP_CURSOR_UP:
                        if (vid_regs[N8_VID_CURROW] > 0) vid_regs[N8_VID_CURROW]--;
                        video_clear_overflow();
                        break;
                    case N8_VIDOP_CURSOR_DOWN: {
                        uint8_t h = vid_regs[N8_VID_HEIGHT];
                        if (h > 0 && vid_regs[N8_VID_CURROW] < h - 1)
                            vid_regs[N8_VID_CURROW]++;
                        video_clear_overflow();
                        break;
                    }
                    case N8_VIDOP_CURSOR_LEFT:
                        if (vid_regs[N8_VID_CURCOL] > 0) vid_regs[N8_VID_CURCOL]--;
                        video_clear_overflow();
                        break;
                    case N8_VIDOP_CURSOR_RIGHT: {
                        uint8_t w = vid_regs[N8_VID_WIDTH];
                        if (w > 0 && vid_regs[N8_VID_CURCOL] < w - 1)
                            vid_regs[N8_VID_CURCOL]++;
                        video_clear_overflow();
                        break;
                    }
                    case N8_VIDOP_CURSOR_HOME:
                        vid_regs[N8_VID_CURCOL] = 0;
                        vid_regs[N8_VID_CURROW] = 0;
                        video_clear_overflow();
                        break;
                }
                break;
            case N8_VID_CTRL:
                vid_regs[reg] = val & N8_VIDCTRL_MASK;
                video_clear_overflow();
                break;
            case N8_VID_DATA: {
                uint8_t row = vid_regs[N8_VID_CURROW];
                uint8_t col = vid_regs[N8_VID_CURCOL];
                int stride = vid_regs[N8_VID_STRIDE];
                int offset = row * stride + col;
                if (offset >= N8_FB_SIZE) {
                    overflow_flag = true;
                } else {
                    frame_buffer[offset] = val;
                    fb_dirty = true;
                    video_advance_cursor(true);  // allow scroll on write
                }
                break;
            }
            case N8_VID_STATUS:
                // Read-only — writes ignored
                break;
            case N8_VID_CURCOL:
                vid_regs[reg] = val;
                video_clear_overflow();
                break;
            case N8_VID_CURROW:
                vid_regs[reg] = val;
                video_clear_overflow();
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
uint8_t video_get_ctrl()         { return vid_regs[N8_VID_CTRL]; }
uint8_t video_get_status()       { return overflow_flag ? N8_VIDSTAT_OVERFLOW : 0x00; }

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
