
#pragma once

#include "m6502.h"


void emulator_init();
void emulator_step();
void emulator_reset();
void emulator_show_memdump_window(bool &);
void emulator_show_status_window(bool &);
void emulator_show_console_window(bool &,float,float);


