#pragma once
#include <stdint.h>

// ============================================================
// N8 Machine Memory Map Constants
// ============================================================

// --- Zero Page & Stack ---
#define N8_ZP_START        0x0000
#define N8_ZP_SIZE         0x0100
#define N8_STACK_START     0x0100
#define N8_STACK_SIZE      0x0100

// --- RAM ---
#define N8_RAM_START       0x0400
#define N8_RAM_END         0xBFFF

// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4 KB
#define N8_FB_END          0xCFFF

// --- Device Register Space ---
#define N8_DEV_BASE        0xD800
#define N8_DEV_SIZE        0x0800   // 2 KB total
#define N8_DEV_END         0xDFFF
#define N8_DEV_SLOT_SIZE   0x0020   // 32 bytes per device slot

// --- System / IRQ (slot 0) ---
#define N8_IRQ_BASE        0xD800
#define N8_IRQ_FLAGS       0xD800
#define N8_IRQ_SLOT        0

// --- IRQ Bits ---
#define N8_IRQ_BIT_TTY     1
#define N8_IRQ_BIT_KBD     2

// --- TTY Device (slot 1) ---
#define N8_TTY_BASE        0xD820
#define N8_TTY_OUT_STATUS  0xD820
#define N8_TTY_OUT_DATA    0xD821
#define N8_TTY_IN_STATUS   0xD822
#define N8_TTY_IN_DATA     0xD823
#define N8_TTY_SLOT        1

// --- Video Control (slot 2) ---
#define N8_VID_BASE        0xD840
#define N8_VID_MODE        0x00     // Register offsets within slot
#define N8_VID_WIDTH       0x01
#define N8_VID_HEIGHT      0x02
#define N8_VID_STRIDE      0x03
#define N8_VID_OPER        0x04
#define N8_VID_CURSOR      0x05
#define N8_VID_CURCOL      0x06
#define N8_VID_CURROW      0x07
#define N8_VID_VSYNC       0x08
#define N8_VID_SLOT        2

// VID_MODE values
#define N8_VIDMODE_TEXT_DEFAULT  0x00
#define N8_VIDMODE_TEXT_CUSTOM   0x01

// VID_OPER values (write-once trigger, does not latch)
#define N8_VIDOP_NOP            0x00  // No operation (default register value)
#define N8_VIDOP_SCROLL_UP      0x01
#define N8_VIDOP_SCROLL_DOWN    0x02
#define N8_VIDOP_SCROLL_LEFT    0x03
#define N8_VIDOP_SCROLL_RIGHT   0x04

// VID_CURSOR bit fields
//   bits 0-1: mode (0=off, 1=steady, 2=flash)
//   bits 2-3: shape (0=underline, 1=block)
//   bits 4-7: flash rate (frames per toggle; 0=cursor not displayed)
#define N8_VID_CURSOR_MODE_MASK   0x03
#define N8_VID_CURSOR_SHAPE_MASK  0x0C
#define N8_VID_CURSOR_RATE_MASK   0xF0
#define N8_VID_CURSOR_OFF         0x00
#define N8_VID_CURSOR_STEADY      0x01
#define N8_VID_CURSOR_FLASH       0x02
#define N8_VID_CURSOR_UNDERLINE   0x00
#define N8_VID_CURSOR_BLOCK       0x04

// Default text mode dimensions
#define N8_VID_DEFAULT_WIDTH    80
#define N8_VID_DEFAULT_HEIGHT   25

// --- Keyboard (slot 3) ---
#define N8_KBD_BASE        0xD860
#define N8_KBD_DATA        0x00     // Register offsets within slot
#define N8_KBD_STATUS      0x01     // Read
#define N8_KBD_ACK         0x01     // Write (same offset)
#define N8_KBD_CTRL        0x02
#define N8_KBD_SLOT        3

// KBD_STATUS bits
#define N8_KBD_STAT_AVAIL    0x01
#define N8_KBD_STAT_OVERFLOW 0x02
#define N8_KBD_STAT_SHIFT    0x04
#define N8_KBD_STAT_CTRL     0x08
#define N8_KBD_STAT_ALT      0x10
#define N8_KBD_STAT_CAPS     0x20

// KBD modifier mask (SHIFT|CTRL|ALT|CAPS)
#define N8_KBD_MODIFIER_MASK (N8_KBD_STAT_SHIFT | N8_KBD_STAT_CTRL | N8_KBD_STAT_ALT | N8_KBD_STAT_CAPS)

// KBD_CTRL bits
#define N8_KBD_CTRL_IRQ_EN   0x01

// --- Dev Bank ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_SIZE    0x0800
#define N8_DEVBANK_END     0xD7FF

// --- ROM ---
#define N8_ROM_BASE        0xE000
#define N8_ROM_SIZE        0x2000   // 8 KB
#define N8_ROM_END         0xFFFF

// --- Vectors (within ROM) ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE

// --- Font ---
#define N8_FONT_CHARS      256
#define N8_FONT_WIDTH      8
#define N8_FONT_HEIGHT     16
