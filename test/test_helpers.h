#pragma once

// CRITICAL: Do NOT define CHIPS_IMPL in any test file.
// The m6502 implementation is already compiled into emulator.o.
#include "m6502.h"
#include "emulator.h"
#include "emu_tty.h"
#include "emu_labels.h"
#include "emu_dis6502.h"
#include "utils.h"

#include <cstring>
#include <vector>
#include <string>
#include <deque>

// ---- Externs for emulator globals not declared in headers ----
extern m6502_t cpu;
extern m6502_desc_t desc;
extern uint64_t pins;
extern uint64_t tick_count;

// ---- Externs for emu_labels functions not declared in emu_labels.h ----
extern void emu_labels_add(uint16_t addr, char* label);
extern void emu_labels_clear();

// ---- Console stub helpers (defined in test_stubs.cpp) ----
extern std::deque<std::string>& stub_get_console_buffer();
extern void stub_clear_console_buffer();

// ---- Pin Construction Helpers ----
// tty_decode() takes uint64_t& (reference). Store return value in a local
// variable before passing:
//   uint64_t p = make_read_pins(0xC100);
//   tty_decode(p, 0);
//   uint8_t result = M6502_GET_DATA(p);

inline uint64_t make_read_pins(uint16_t addr) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    p |= M6502_RW;
    return p;
}

inline uint64_t make_write_pins(uint16_t addr, uint8_t data) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    M6502_SET_DATA(p, data);
    return p;
}

// ---- Disassembler assertion helper ----
inline bool disasm_contains(const char* buf, const char* expected) {
    return std::string(buf).find(expected) != std::string::npos;
}

// ---- CpuFixture -- Isolated CPU Testing ----
// Owns its own m6502_t, mem[], and pins. Zero dependency on emulator globals.
struct CpuFixture {
    m6502_t cpu;
    m6502_desc_t desc;
    uint8_t mem[1 << 16];
    uint64_t pins;

    CpuFixture() {
        memset(mem, 0, sizeof(mem));
        memset(&desc, 0, sizeof(desc));
        memset(&cpu, 0, sizeof(cpu));
        pins = m6502_init(&cpu, &desc);
    }

    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = addr & 0xFF;
        mem[0xFFFD] = (addr >> 8) & 0xFF;
    }

    void set_irq_vector(uint16_t addr) {
        mem[0xFFFE] = addr & 0xFF;
        mem[0xFFFF] = (addr >> 8) & 0xFF;
    }

    void set_nmi_vector(uint16_t addr) {
        mem[0xFFFA] = addr & 0xFF;
        mem[0xFFFB] = (addr >> 8) & 0xFF;
    }

    void load_program(uint16_t addr, const std::vector<uint8_t>& program) {
        for (size_t i = 0; i < program.size(); i++) {
            mem[addr + i] = program[i];
        }
    }

    void tick() {
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, mem[addr]);
        } else {
            mem[addr] = M6502_GET_DATA(pins);
        }
    }

    int run_until_sync() {
        int ticks = 0;
        do {
            tick();
            ticks++;
        } while (!(pins & M6502_SYNC) && ticks < 100);
        return ticks;
    }

    bool timed_out() {
        return !(pins & M6502_SYNC);
    }

    void boot() {
        // Must tick at least once — SYNC may be set after m6502_init
        do {
            tick();
        } while (!(pins & M6502_SYNC));
    }

    void run_instructions(int n) {
        for (int i = 0; i < n; i++) {
            run_until_sync();
        }
    }

    uint8_t a()   { return m6502_a(&cpu); }
    uint8_t x()   { return m6502_x(&cpu); }
    uint8_t y()   { return m6502_y(&cpu); }
    uint8_t s()   { return m6502_s(&cpu); }
    uint8_t p()   { return m6502_p(&cpu); }
    uint16_t pc() { return m6502_pc(&cpu); }

    bool flag_c() { return p() & 0x01; }
    bool flag_z() { return p() & 0x02; }
    bool flag_i() { return p() & 0x04; }
    bool flag_d() { return p() & 0x08; }
    bool flag_v() { return p() & 0x40; }
    bool flag_n() { return p() & 0x80; }
};

// ---- EmulatorFixture -- Bus Decode & Integration Testing ----
// Uses the actual emulator globals (mem[], cpu, pins).
struct EmulatorFixture {
    EmulatorFixture() {
        memset(mem, 0, sizeof(uint8_t) * 65536);
        memset(bp_mask, 0, sizeof(bool) * 65536);
        memset(wp_write_mask, 0, sizeof(bool) * 65536);
        memset(wp_read_mask, 0, sizeof(bool) * 65536);
        memset(&desc, 0, sizeof(desc));
        tick_count = 0;
        emulator_enablebp(false);
        emulator_enablewp(false);
        emulator_clear_wp_hit();
        emu_labels_clear();
        tty_reset();
        pins = m6502_init(&cpu, &desc);
        stub_clear_console_buffer();
    }

    void load_at(uint16_t addr, const std::vector<uint8_t>& data) {
        for (size_t i = 0; i < data.size(); i++) {
            mem[addr + i] = data[i];
        }
    }

    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = addr & 0xFF;
        mem[0xFFFD] = (addr >> 8) & 0xFF;
    }

    void set_irq_vector(uint16_t addr) {
        mem[0xFFFE] = addr & 0xFF;
        mem[0xFFFF] = (addr >> 8) & 0xFF;
    }

    void step_n(int n) {
        for (int i = 0; i < n; i++) {
            emulator_step();
        }
    }
};
