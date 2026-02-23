#pragma once
#include <cstdint>

void kbd_init();
void kbd_reset();
void kbd_decode(uint64_t& pins, uint8_t dev_reg);
void kbd_tick();

// Host-side injection (from SDL event loop)
void kbd_inject_key(uint8_t keycode, uint8_t modifiers);

// Test helpers
uint8_t kbd_get_data();
uint8_t kbd_get_status();
bool kbd_data_available();
