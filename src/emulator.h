
#pragma once

#include "m6502.h"


void emulator_init(char*);
void emulator_step();

const char *rom_file = "N8firmware.bin";
