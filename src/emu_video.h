#pragma once
#include <cstdint>

void video_init();
void video_reset();
void video_decode(uint64_t& pins, uint8_t dev_reg);

// State accessors (for future rendering pipeline)
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
