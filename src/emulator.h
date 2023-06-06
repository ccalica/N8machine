
#pragma once

#include <cstdint>
#include <string>

// #include "m6502.h"

extern uint8_t mem[];
extern bool bp_mask[];

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

