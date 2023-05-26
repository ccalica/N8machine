#pragma once

#include <string>

void gui_con_init();
// void gui_con_step();
void gui_con_printmsg(char*);
void gui_con_printmsg(std::string);

void gui_show_console_window(bool &);
