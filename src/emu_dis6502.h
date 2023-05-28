
#pragma once


void emu_dis6502_init();
// void emu_dis6502_decode(int);
int emu_dis6502_decode(int, char *, int);
void emu_dis6502_log(char * args);

