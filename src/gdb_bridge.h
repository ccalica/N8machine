#pragma once

void gdb_bridge_init();
void gdb_bridge_poll();        // process GDB commands — call once per frame
bool gdb_bridge_check_stop();  // check bp/wp hits — call after each step; returns true to stop
void gdb_bridge_shutdown();
