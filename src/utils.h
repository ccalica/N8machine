
#pragma once
#include <cstdint>

char itohc(unsigned int);
char* my_itoa(char*, const unsigned int, const unsigned int);
int my_get_uint(char *, uint32_t&);

int range_helper(char *, uint32_t&, uint32_t&);
int  emu_is_digit(char);
int  emu_is_hex(char);
int htoi(char *);
