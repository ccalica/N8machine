#pragma once
#include "emu_screen.h"

void display_init(const n8_screen_t* screen);
void display_render();
void display_shutdown();
bool display_has_focus();
