#include "emu_kbd.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"

static uint8_t kbd_data   = 0x00;
static uint8_t kbd_status = 0x00;
static uint8_t kbd_ctrl   = 0x00;

void kbd_init()  { kbd_reset(); }

void kbd_reset() {
    kbd_data   = 0x00;
    kbd_status = 0x00;
    kbd_ctrl   = 0x00;
    emu_clr_irq(N8_IRQ_BIT_KBD);
}

void kbd_inject_key(uint8_t keycode, uint8_t modifiers) {
    if (kbd_status & N8_KBD_STAT_AVAIL) {
        kbd_status |= N8_KBD_STAT_OVERFLOW;
    }
    kbd_data = keycode;
    kbd_status = (kbd_status & ~0x3C) | (modifiers & 0x3C);
    kbd_status |= N8_KBD_STAT_AVAIL;

    if (kbd_ctrl & N8_KBD_CTRL_IRQ_EN) {
        emu_set_irq(N8_IRQ_BIT_KBD);
    }
}

void kbd_tick() {
    // Reassert IRQ if data available and IRQ enabled.
    // Required because IRQ_CLR() zeros all flags every tick.
    if ((kbd_status & N8_KBD_STAT_AVAIL) &&
        (kbd_ctrl & N8_KBD_CTRL_IRQ_EN)) {
        emu_set_irq(N8_IRQ_BIT_KBD);
    }
}

void kbd_decode(uint64_t& pins, uint8_t reg) {
    if (pins & M6502_RW) {
        // Read
        uint8_t val = 0x00;
        switch (reg) {
            case N8_KBD_DATA:   val = kbd_data;   break;
            case N8_KBD_STATUS: val = kbd_status;  break;
            case N8_KBD_CTRL:   val = kbd_ctrl;    break;
            default:            val = 0x00;        break;
        }
        M6502_SET_DATA(pins, val);
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_KBD_ACK:  // offset 1 write = acknowledge
                kbd_status &= ~(N8_KBD_STAT_AVAIL | N8_KBD_STAT_OVERFLOW);
                emu_clr_irq(N8_IRQ_BIT_KBD);
                break;
            case N8_KBD_CTRL:
                kbd_ctrl = val & N8_KBD_CTRL_IRQ_EN;
                break;
            default:
                break;  // KBD_DATA is read-only
        }
    }
}

uint8_t kbd_get_data()    { return kbd_data; }
uint8_t kbd_get_status()  { return kbd_status; }
bool kbd_data_available() { return (kbd_status & N8_KBD_STAT_AVAIL) != 0; }
