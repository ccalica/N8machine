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
#define N8_VID_CTRL        0x09
#define N8_VID_DATA        0x0A
#define N8_VID_STATUS      0x0B
#define N8_VID_REG_COUNT   12       // 0x00-0x0B
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
#define N8_VIDOP_CLEAR          0x05
#define N8_VIDOP_CURSOR_UP      0x06
#define N8_VIDOP_CURSOR_DOWN    0x07
#define N8_VIDOP_CURSOR_LEFT    0x08
#define N8_VIDOP_CURSOR_RIGHT   0x09
#define N8_VIDOP_CURSOR_HOME    0x0A

// VID_CTRL bit fields
#define N8_VIDCTRL_ADVANCE   0x01
#define N8_VIDCTRL_WRAP      0x02
#define N8_VIDCTRL_SCROLL    0x04
#define N8_VIDCTRL_MASK      0x07    // valid bits mask
#define N8_VIDCTRL_DEFAULT   0x07    // all enabled on reset

// VID_STATUS bit fields
#define N8_VIDSTAT_OVERFLOW  0x01

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

// --- Storage Device (slot 4) ---
#define N8_STORAGE_BASE       0xD880
#define N8_DISK_CHAN          0x00    // Register offsets within slot
#define N8_DISK_DATA          0x01
#define N8_DISK_ERROR         0x02
#define N8_DISK_CTRL          0x03
#define N8_DISK_STATUS        0x04
#define N8_DISK_REG_COUNT     5
#define N8_STORAGE_SLOT       4

// DISK_CHAN read bits
#define N8_DISK_CHAN_MASK     0x0F
#define N8_DISK_CHAN_ACTIVE   0x20    // bit 5

// DISK_ERROR bits
#define N8_DISK_ERR_CMD       0x80   // bit 7: command error flag

// DISK_CTRL bits
#define N8_DISK_CTRL_PARSER   0x01   // bit 0: reset command parser
#define N8_DISK_CTRL_CHANNEL  0x02   // bit 1: reset current channel
#define N8_DISK_CTRL_DEVICE   0x80   // bit 7: full device reset

// DISK_STATUS bits
#define N8_DISK_STAT_AVAIL    0x0F   // bits 0-3 mask
#define N8_DISK_STAT_EOF      0x20   // bit 5
#define N8_DISK_STAT_CTS      0x40   // bit 6
#define N8_DISK_STAT_BUSY     0x80   // bit 7

// I/O error codes (DISK_ERROR bits 0-6)
#define N8_DISK_IOE_NONE          0x00
#define N8_DISK_IOE_NOT_OPEN      0x01
#define N8_DISK_IOE_DISK_FULL     0x02
#define N8_DISK_IOE_PAST_EOF      0x03
#define N8_DISK_IOE_PERMISSION    0x04
#define N8_DISK_IOE_NOT_READY     0x05
#define N8_DISK_IOE_CMD_OVERFLOW  0x06

// Command error codes (read from DISK_DATA when bit 7 set)
#define N8_DISK_CE_FILE_NOT_FOUND  0x01
#define N8_DISK_CE_FILE_EXISTS     0x02
#define N8_DISK_CE_DIR_NOT_FOUND   0x03
#define N8_DISK_CE_CHAN_NOT_OPEN   0x04
#define N8_DISK_CE_NO_FREE_CHAN    0x05
#define N8_DISK_CE_DISK_FULL       0x06
#define N8_DISK_CE_PERMISSION      0x08
#define N8_DISK_CE_BAD_SYNTAX      0x09
#define N8_DISK_CE_BAD_ARG         0x0A
#define N8_DISK_CE_NAME_TOO_LONG   0x0B
#define N8_DISK_CE_NOT_A_DIR       0x0C
#define N8_DISK_CE_DIR_NOT_EMPTY   0x0D
#define N8_DISK_CE_NOT_READY       0x0E

// Control channel ID
#define N8_DISK_CONTROL_CHAN  0x0F

// Key codes — control & nav ($00-$1F)
#define N8_KEY_NONE        0x00
#define N8_KEY_UP          0x01
#define N8_KEY_DOWN        0x02
#define N8_KEY_LEFT        0x03
#define N8_KEY_RIGHT       0x04
#define N8_KEY_HOME        0x05
#define N8_KEY_END         0x06
#define N8_KEY_BACKSPACE   0x08
#define N8_KEY_TAB         0x09
#define N8_KEY_PAGEUP      0x0A
#define N8_KEY_PAGEDOWN    0x0B
#define N8_KEY_ENTER       0x0D
#define N8_KEY_INSERT      0x0E
#define N8_KEY_DELETE      0x0F
#define N8_KEY_PRINTSCR    0x10
#define N8_KEY_PAUSE       0x11
#define N8_KEY_ESCAPE      0x1B
// Function keys ($80-$8B)
#define N8_KEY_F1          0x80
#define N8_KEY_F12         0x8B

// --- Dev Bank ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_SIZE    0x0800
#define N8_DEVBANK_END     0xD7FF

// --- ROM ---
#define N8_ROM_BASE        0xE000
#define N8_ROM_SIZE        0x2000   // 8 KB
#define N8_ROM_END         0xFFFF

// --- Shell ($E000-$EFFF, 4KB) ---
#define N8_SHELL_BASE      0xE000
#define N8_SHELL_SIZE      0x1000

// --- Kernel ($F000-$FFFF, 4KB) ---
#define N8_KERNEL_BASE     0xF000
#define N8_KERNEL_SIZE     0x1000

// --- Kernel Entry Jump Table ---
#define N8_KENTRY_BASE     0xFE00

// --- Vectors (within ROM) ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE

// --- Font ---
#define N8_FONT_CHARS      256
#define N8_FONT_WIDTH      8
#define N8_FONT_HEIGHT     16
