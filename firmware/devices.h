/*
 * n8_devices.h -- Device register addresses for cc65 C programs
 * Auto-derived from n8_memory_map.inc / src/n8_memory_map.h
 */

#ifndef __N8_DEVICES_H__
#define __N8_DEVICES_H__

/* --- System / IRQ (slot 0) --- */
#define N8_IRQ_FLAGS        0xD800
#define N8_IRQ_BIT_TTY      1

/* --- TTY (slot 1) --- */
#define N8_TTY_OUT_STATUS   0xD820
#define N8_TTY_OUT_DATA     0xD821
#define N8_TTY_IN_STATUS    0xD822
#define N8_TTY_IN_DATA      0xD823

/* --- Video Control (slot 2) --- */
#define N8_VID_BASE         0xD840
#define N8_VID_MODE         0xD840
#define N8_VID_WIDTH        0xD841
#define N8_VID_HEIGHT       0xD842
#define N8_VID_STRIDE       0xD843
#define N8_VID_OPER         0xD844
#define N8_VID_CURSOR       0xD845
#define N8_VID_CURCOL       0xD846
#define N8_VID_CURROW       0xD847
#define N8_VID_VSYNC        0xD848

/* VID_MODE values */
#define N8_VIDMODE_TEXT_DEFAULT  0x00
#define N8_VIDMODE_TEXT_CUSTOM   0x01

/* VID_OPER values (write-once trigger, does not latch) */
#define N8_VIDOP_NOP            0x00
#define N8_VIDOP_SCROLL_UP      0x01
#define N8_VIDOP_SCROLL_DOWN    0x02
#define N8_VIDOP_SCROLL_LEFT    0x03
#define N8_VIDOP_SCROLL_RIGHT   0x04

/* VID_CURSOR bit fields */
#define N8_VID_CURSOR_MODE_MASK   0x03
#define N8_VID_CURSOR_SHAPE_MASK  0x0C
#define N8_VID_CURSOR_RATE_MASK   0xF0
#define N8_VID_CURSOR_OFF         0x00
#define N8_VID_CURSOR_STEADY      0x01
#define N8_VID_CURSOR_FLASH       0x02
#define N8_VID_CURSOR_UNDERLINE   0x00
#define N8_VID_CURSOR_BLOCK       0x04

/* Default text mode dimensions */
#define N8_VID_DEFAULT_WIDTH    80
#define N8_VID_DEFAULT_HEIGHT   25

/* --- Keyboard (slot 3) --- */
#define N8_KBD_DATA         0xD860
#define N8_KBD_STATUS       0xD861
#define N8_KBD_ACK          0xD861
#define N8_KBD_CTRL         0xD862

/* KBD_STATUS bits */
#define N8_KBD_STAT_AVAIL     0x01
#define N8_KBD_STAT_OVERFLOW  0x02
#define N8_KBD_STAT_SHIFT     0x04
#define N8_KBD_STAT_CTRL      0x08
#define N8_KBD_STAT_ALT       0x10
#define N8_KBD_STAT_CAPS      0x20

/* --- Frame Buffer --- */
#define N8_FB_BASE          0xC000
#define N8_FB_SIZE          0x1000
#define N8_FB_END           0xCFFF

/* --- Dev Bank --- */
#define N8_DEVBANK_BASE     0xD000
#define N8_DEVBANK_SIZE     0x0800
#define N8_DEVBANK_END      0xD7FF

/* --- ROM --- */
#define N8_ROM_BASE         0xE000
#define N8_ROM_SIZE         0x2000
#define N8_ROM_END          0xFFFF

/* --- Shell ($E000-$EFFF, 4KB) --- */
#define N8_SHELL_BASE       0xE000
#define N8_SHELL_SIZE       0x1000

/* --- Kernel ($F000-$FFFF, 4KB) --- */
#define N8_KERNEL_BASE      0xF000
#define N8_KERNEL_SIZE      0x1000

/* --- Kernel Entry Jump Table --- */
#define N8_KENTRY_BASE      0xFE00

/* --- Vectors --- */
#define N8_VEC_NMI          0xFFFA
#define N8_VEC_RESET        0xFFFC
#define N8_VEC_IRQ          0xFFFE

#endif /* __N8_DEVICES_H__ */
