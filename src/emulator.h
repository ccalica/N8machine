
#pragma once

#include <cstdint>
#include <string>

// #include "m6502.h"

extern uint8_t mem[];
extern bool bp_mask[];
extern bool wp_write_mask[];
extern bool wp_read_mask[];

void emulator_init();
void emulator_step();
void emulator_reset();
void emulator_enablebp(bool);
void emulator_logbp();
void emulator_setbp(char*);
bool emulator_check_break();
uint16_t emulator_getpc();
uint16_t emulator_getci();

void emulator_show_memdump_window(bool &);
void emulator_show_status_window(bool &,float,float);
void emulator_show_console_window(bool &);
bool emu_bus_read();
void emu_set_irq(int);
void emu_clr_irq(int);

// GDB stub accessor functions
uint8_t emulator_read_a();
uint8_t emulator_read_x();
uint8_t emulator_read_y();
uint8_t emulator_read_s();
uint8_t emulator_read_p();
void emulator_write_a(uint8_t v);
void emulator_write_x(uint8_t v);
void emulator_write_y(uint8_t v);
void emulator_write_s(uint8_t v);
void emulator_write_p(uint8_t v);
void emulator_write_pc(uint16_t addr);
bool emulator_bp_hit();
void emulator_clear_bp_hit();
bool emulator_bp_enabled();

// Watchpoint accessors
void emulator_enablewp(bool en);
bool emulator_wp_enabled();
bool emulator_wp_hit();
void emulator_clear_wp_hit();
uint16_t emulator_wp_hit_addr();
int emulator_wp_hit_type();   // returns 2=write, 3=read

