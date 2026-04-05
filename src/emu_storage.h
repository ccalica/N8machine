#pragma once
#include <cstdint>

void storage_init();
void storage_reset();
void storage_tick();
void storage_decode(uint64_t& pins, uint8_t reg);

// Configuration
void storage_set_root_path(const char* path);

// GDB bridge accessors (no side effects)
uint8_t storage_get_chan();
uint8_t storage_get_error();
uint8_t storage_get_status();
