#pragma once
#include <cstdint>

// Maximum pixel dimensions (80*8 x 25*16)
#define N8_SCREEN_MAX_W 640
#define N8_SCREEN_MAX_H 400

typedef struct {
    uint32_t* pixels;   // RGBA8888 pixel buffer (hardware-owned)
    int       width;    // Pixel width
    int       height;   // Pixel height
    bool      dirty;    // Set by hardware, cleared by display after upload
} n8_screen_t;
