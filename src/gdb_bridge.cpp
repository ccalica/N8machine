#include "gdb_bridge.h"
#include "gdb_stub.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"
#include "emu_tty.h"
#include "emu_video.h"
#include "emu_kbd.h"

#include <cstring>

// Externs from emulator.cpp needed by GDB callbacks
extern m6502_t cpu;
extern uint64_t pins;

// ---- GDB stub callbacks ----

static uint8_t gdb_read_reg8(int reg_id) {
    switch (reg_id) {
        case 0: return emulator_read_a();
        case 1: return emulator_read_x();
        case 2: return emulator_read_y();
        case 3: return emulator_read_s();
        case 4: return emulator_read_p();
        default: return 0;
    }
}

static uint16_t gdb_read_reg16(int reg_id) {
    if (reg_id == 5) return emulator_getpc();
    return 0;
}

static void gdb_write_reg8(int reg_id, uint8_t val) {
    switch (reg_id) {
        case 0: emulator_write_a(val); break;
        case 1: emulator_write_x(val); break;
        case 2: emulator_write_y(val); break;
        case 3: emulator_write_s(val); break;
        case 4: emulator_write_p(val); break;
    }
}

static void gdb_write_reg16(int reg_id, uint16_t val) {
    if (reg_id == 5) emulator_write_pc(val);
}

static uint8_t gdb_read_mem(uint16_t addr) {
    if (addr >= N8_FB_BASE && addr <= N8_FB_END)
        return frame_buffer[addr - N8_FB_BASE];
    return mem[addr];
}

static void gdb_write_mem(uint16_t addr, uint8_t val) {
    if (addr >= N8_FB_BASE && addr <= N8_FB_END) {
        frame_buffer[addr - N8_FB_BASE] = val;
        fb_dirty = true;
    } else {
        mem[addr] = val;
    }
}

static int gdb_step_instruction(void) {
    int guard = gdb_stub_get_step_guard();
    int ticks = 0;
    do {
        emulator_step();
        ticks++;
        if (ticks >= guard) return 4; // SIGILL — likely jammed
    } while (!(pins & M6502_SYNC));
    return 5; // SIGTRAP
}

static void gdb_set_breakpoint(uint16_t addr) {
    bp_mask[addr] = true;
    emulator_enablebp(true);
}

static void gdb_clear_breakpoint(uint16_t addr) {
    bp_mask[addr] = false;
    bool any = false;
    for (int i = 0; i < 65536 && !any; i++) any = bp_mask[i];
    if (!any) emulator_enablebp(false);
}

static void gdb_set_watchpoint(uint16_t addr, int type) {
    if (type == 2) {
        wp_write_mask[addr] = true;
    } else if (type == 3) {
        wp_read_mask[addr] = true;
    } else if (type == 4) {
        wp_write_mask[addr] = true;
        wp_read_mask[addr] = true;
    }
    emulator_enablewp(true);
}

static void gdb_clear_watchpoint(uint16_t addr, int type) {
    if (type == 2) {
        wp_write_mask[addr] = false;
    } else if (type == 3) {
        wp_read_mask[addr] = false;
    } else if (type == 4) {
        wp_write_mask[addr] = false;
        wp_read_mask[addr] = false;
    }
    bool any = false;
    for (int i = 0; i < 65536 && !any; i++) any = wp_write_mask[i] || wp_read_mask[i];
    if (!any) emulator_enablewp(false);
}

static void gdb_continue_exec(void) {
    emulator_set_running(true);
}

static void gdb_halt(void) {
    emulator_set_running(false);
}

static uint16_t gdb_get_pc(void) {
    return emulator_getpc();
}

static int gdb_get_stop_reason(void) {
    return 5; // SIGTRAP default
}

static void gdb_reset(void) {
    // D47: Use M6502_RES pin, NOT emulator_reset() (avoids ROM reload)
    pins |= M6502_RES;
    tty_reset();
    video_reset();
    kbd_reset();
    memset(frame_buffer, 0, N8_FB_SIZE);
    fb_dirty = true;
}

// ---- Public API ----

void gdb_bridge_init() {
    static gdb_stub_callbacks_t gdb_cb = {
        gdb_read_reg8, gdb_read_reg16,
        gdb_write_reg8, gdb_write_reg16,
        gdb_read_mem, gdb_write_mem,
        gdb_step_instruction,
        gdb_set_breakpoint, gdb_clear_breakpoint,
        gdb_set_watchpoint, gdb_clear_watchpoint,
        gdb_get_pc, gdb_get_stop_reason,
        gdb_reset, gdb_continue_exec, gdb_halt
    };
    static gdb_stub_config_t gdb_cfg = { 3333, true, 16 };
    gdb_stub_init(&gdb_cb, &gdb_cfg);
}

void gdb_bridge_poll() {
    // Process GDB commands — call once per frame
    switch (gdb_stub_poll()) {
        case GDB_POLL_HALTED:
            emulator_set_running(false);
            emulator_set_gdb_halted(true);
            emulator_enablebp(true);
            break;
        case GDB_POLL_RESUMED:
            emulator_set_gdb_halted(false);
            emulator_set_running(true);
            break;
        case GDB_POLL_STEPPED:
            emulator_set_gdb_halted(true);
            emulator_set_running(false);
            break;
        case GDB_POLL_DETACHED:
            emulator_set_gdb_halted(false);
            memset(bp_mask, 0, sizeof(bool) * 65536);
            memset(wp_write_mask, 0, sizeof(bool) * 65536);
            memset(wp_read_mask, 0, sizeof(bool) * 65536);
            emulator_enablebp(false);
            emulator_enablewp(false);
            break;
        case GDB_POLL_KILL:
            emulator_set_gdb_halted(false);
            emulator_set_running(true);
            break;
        case GDB_POLL_NONE:
            break;
    }
}

bool gdb_bridge_check_stop() {
    // Check for bp/wp hits — call after each emulator_step() in the time-slice
    if (emulator_bp_hit()) {
        emulator_set_running(false);
        emulator_clear_bp_hit();
        if (gdb_stub_is_connected()) {
            emulator_set_gdb_halted(true);
            gdb_stub_notify_stop(5);
        }
        return true;
    }

    if (emulator_wp_hit()) {
        emulator_set_running(false);
        uint16_t wa = emulator_wp_hit_addr();
        int wt = emulator_wp_hit_type();
        emulator_clear_wp_hit();
        if (gdb_stub_is_connected()) {
            emulator_set_gdb_halted(true);
            gdb_stub_notify_watchpoint(wa, wt);
        }
        return true;
    }

    return false;
}

void gdb_bridge_shutdown() {
    gdb_stub_shutdown();
}
