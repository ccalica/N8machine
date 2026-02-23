#include "emu_video.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"
#include <cstring>

static uint8_t vid_regs[8] = { 0 };

void video_init() { video_reset(); }

void video_reset() {
    memset(vid_regs, 0, sizeof(vid_regs));
    vid_regs[N8_VID_MODE]   = N8_VIDMODE_TEXT_DEFAULT;
    vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
    vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
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
    if (reg > 7) {
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
