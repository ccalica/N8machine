#include "m6502.h"

#include "imgui.h"

#include "emulator.h"
#include "emu_dis6502.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <stdlib.h>
#include <string.h>
#include <deque>
#include <string>

using namespace std;

deque<string> console_buffer;

void gui_con_printmsg(char *msg) {
    string data = msg;
    console_buffer.push_back(data);
}
void gui_con_printmsg(string data) {
    console_buffer.push_back(data);
}

void gui_show_console_window(bool &show_console_window) {
    static char cmd_line[1024] {0};
    // char debug_msg[1256] {0};
    char cmd[64] {0};
    char args[1024] {0};

    ImGui::Begin("Console");
    ImGui::Text("Console:");
    ImGui::BeginChild("console",ImVec2(0,-25.0));
    
    for(int n = 0; n<console_buffer.size(); n++) {
        
        ImGui::Text(console_buffer[n].c_str());
    }
    ImGui::SetScrollY(ImGui::GetScrollMaxY());
    ImGui::EndChild();
    if(ImGui::InputText("CMD", cmd_line, IM_ARRAYSIZE(cmd_line),ImGuiInputTextFlags_EnterReturnsTrue)) { 
        // emulator_setbp(break_points);
        // int tail = strlen(cmd_line) - 1;
        sscanf(cmd_line, "%s %1024c", cmd, args);
        switch(cmd[0]) {
            case 'D':
            case 'd':
                emu_dis6502_log(args);
                break;
            case 'b':
                if(cmd[1] == 'p')
                    emulator_setbp(args);
                break;
            case 'c':
                if(cmd[1] == 'l' && cmd[2] == 'r')
                    while(console_buffer.size() > 0)
                        console_buffer.pop_back();
                break;
            case 's':
                if(strncmp(args, "bp", 2) == 0) {
                    emulator_logbp();
                }
            default:
                break;
        }
    }
    ImGui::End();    

}

void gui_con_init() {
    ;;
}