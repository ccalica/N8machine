#include "m6502.h"

#include "imgui.h"

#include "emulator.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <stdlib.h>
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
    static char cmd[1024] {0};
    char debug_msg[256] {0};

    ImGui::Begin("Console");
    ImGui::Text("Console:");
    ImGui::BeginChild("console",ImVec2(0,-25.0));
    
    for(int n = 0; n<console_buffer.size(); n++) {
        
        ImGui::Text(console_buffer[n].c_str());
    }
    ImGui::SetScrollY(ImGui::GetScrollMaxY());
    ImGui::EndChild();
    if(ImGui::InputText("CMD", cmd,IM_ARRAYSIZE(cmd),ImGuiInputTextFlags_EnterReturnsTrue)) { 
        // emulator_setbp(break_points);
        int tail = strlen(cmd) - 1;
        snprintf(debug_msg, 256, "CMD tail: %c (%d)", cmd[tail], cmd[tail]);
        gui_con_printmsg(debug_msg);
    }
    ImGui::End();    

}

void gui_con_init() {
    ;;
}