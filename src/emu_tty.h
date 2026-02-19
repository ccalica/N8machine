

#pragma once
#include <stdint.h>

void tty_reset_term();
// void tty_set_conio();
int tty_kbhit();
void tty_init();
void tty_reset();
void tty_tick(uint64_t&);
void tty_decode(uint64_t&, uint8_t);
void tty_inject_char(uint8_t);
int tty_buff_count();
