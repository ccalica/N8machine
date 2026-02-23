#pragma once
#include <cstdint>
#include "emu_screen.h"

void video_init();
void video_reset();
void video_decode(uint64_t& pins, uint8_t dev_reg);

// Rendering pipeline
const n8_screen_t* video_get_screen();
void video_rasterize(uint32_t frame_count);

// State accessors
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
