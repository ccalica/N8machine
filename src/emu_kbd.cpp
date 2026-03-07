#include "emu_kbd.h"
#include "n8_memory_map.h"
#include "m6502.h"

// ---- 64-entry ring buffer ----

#define KBD_BUF_SIZE 64

struct kbd_entry_t {
    uint8_t keycode;
    uint8_t modifiers;  // SHIFT|CTRL|ALT|CAPS bits only
};

static kbd_entry_t kbd_buf[KBD_BUF_SIZE];
static int kbd_head  = 0;   // index of front (next to read)
static int kbd_tail  = 0;   // index of next write position
static int kbd_count = 0;   // number of entries in buffer
static uint8_t kbd_overflow = 0x00;  // N8_KBD_STAT_OVERFLOW if set

void kbd_init()  { kbd_reset(); }

void kbd_reset() {
    kbd_head = kbd_tail = kbd_count = 0;
    kbd_overflow = 0x00;
}

void kbd_inject_key(uint8_t keycode, uint8_t modifiers) {
    if (kbd_count >= KBD_BUF_SIZE) {
        kbd_overflow = N8_KBD_STAT_OVERFLOW;
        return;  // drop new key
    }
    kbd_buf[kbd_tail].keycode   = keycode;
    kbd_buf[kbd_tail].modifiers = modifiers & N8_KBD_MODIFIER_MASK;
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    kbd_count++;
}

void kbd_decode(uint64_t& pins, uint8_t reg) {
    if (pins & M6502_RW) {
        // Read
        uint8_t val = 0x00;
        switch (reg) {
            case N8_KBD_DATA:
                val = (kbd_count > 0) ? kbd_buf[kbd_head].keycode : 0x00;
                break;
            case N8_KBD_STATUS: {
                uint8_t avail = (kbd_count > 0) ? N8_KBD_STAT_AVAIL : 0x00;
                uint8_t mods  = (kbd_count > 0) ? kbd_buf[kbd_head].modifiers : 0x00;
                val = avail | kbd_overflow | mods;
                break;
            }
            default:
                val = 0x00;  // KBD_CTRL and reserved regs read 0
                break;
        }
        M6502_SET_DATA(pins, val);
    } else {
        // Write
        switch (reg) {
            case N8_KBD_ACK:  // offset 1 write = acknowledge
                if (kbd_count > 0) {
                    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
                    kbd_count--;
                }
                kbd_overflow = 0x00;  // always clear overflow on ACK
                break;
            default:
                break;  // KBD_DATA read-only, KBD_CTRL reserved
        }
    }
}

uint8_t kbd_get_data() {
    return (kbd_count > 0) ? kbd_buf[kbd_head].keycode : 0x00;
}

uint8_t kbd_get_status() {
    uint8_t avail = (kbd_count > 0) ? N8_KBD_STAT_AVAIL : 0x00;
    uint8_t mods  = (kbd_count > 0) ? kbd_buf[kbd_head].modifiers : 0x00;
    return avail | kbd_overflow | mods;
}

bool kbd_data_available() { return kbd_count > 0; }
