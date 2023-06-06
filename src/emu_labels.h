
#pragma once

#include <cstdint>
#include <string>
#include <list>

std::list<std::string> emu_labels_get(uint16_t addr);
void emu_labels_console_list();
void emu_labels_load();
void emu_labels_init();
// emu_labels_get(uint);
