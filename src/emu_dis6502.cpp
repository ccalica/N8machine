
#include "emu_dis6502.h"
#include "emulator.h"
#include "emu_tty.h"
#include "emu_labels.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <string>
#include <list>

#include "../imgui/imgui.h"

using namespace std;

// Padding for 1,2 & 3 byte instructions
const char *padding[3] = {"        ","    ",""};

// 57 Instructions + Undefined ("???")
const char *instruction[58] = {
//       0     1     2     3     4     5     6     7     8     9
    "ADC","AND","ASL","BCC","BCS","BEQ","BIT","BMI","BNE","BPL", // 0
    "BRK","BVC","BVS","CLC","CLD","CLI","CLV","CMP","CPX","CPY", // 1
    "DEC","DEX","DEY","EOR","INC","INX","INY","JMP","JSR","LDA", // 2
    "LDX","LDY","LSR","NOP","ORA","PHA","PHP","PLA","PLP","ROL", // 3
    "ROR","ROT","RTI","RTS","SBC","SEC","SED","SEI","STA","STX", // 4
    "STY","TAX","TAY","TSX","TXA","TXS","TYA","???"};            // 5

// This is a lookup of the text formating required for mode output, plus one entry to distinguish relative mode
const char *modes[9][2]={{"",""},{"#",""},{"",",X"},{"",",Y"},{"(",",X)"},{"(","),Y"},{"(",")"},{"A",""},{"",""}};

// Opcode Properties for 256 opcodes {length_in_bytes, mnemonic_lookup, mode_chars_lookup}
const int opcode_props[256][3] = {
//         0        1        2        3        4        5        6        7        8        9        A        B        C        D        E        F
//     ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** --------
    {1,10,0},{2,34,4},{1,57,0},{1,57,0},{1,57,0},{2,34,0},{2,2,0}, {1,57,0},{1,36,0},{2,34,1},{1,2,7}, {1,57,0},{1,57,0},{3,34,0},{3,2,0}, {1,57,0}, // 0
    {2,9,8}, {2,34,5},{1,57,0},{1,57,0},{1,57,0},{2,34,2},{2,2,2}, {1,57,0},{1,13,0},{3,34,3},{1,57,0},{1,57,0},{1,57,0},{3,34,2},{3,2,2}, {1,57,0}, // 1
    {3,28,0},{2,1,4}, {1,57,0},{1,57,0},{2,6,0}, {2,1,0}, {2,39,0},{1,57,0},{1,38,0},{2,1,1}, {1,39,7},{1,57,0},{3,6,0}, {3,1,0}, {3,39,0},{1,57,0}, // 2
    {2,7,8}, {2,1,5}, {1,57,0},{1,57,0},{1,57,0},{2,1,2}, {2,39,2},{1,57,0},{1,45,0},{3,1,3}, {1,57,0},{1,57,0},{1,57,0},{3,1,2}, {3,39,2},{1,57,0}, // 3
    {1,42,0},{2,23,4},{1,57,0},{1,57,0},{1,57,0},{2,23,0},{2,32,0},{1,57,0},{1,35,0},{2,23,1},{1,32,7},{1,57,0},{3,27,0},{3,23,0},{3,32,0},{1,57,0}, // 4
    {2,11,8},{2,23,5},{1,57,0},{1,57,0},{1,57,0},{2,23,2},{2,32,2},{1,57,0},{1,15,0},{3,23,3},{1,57,0},{1,57,0},{1,57,0},{3,23,2},{3,32,2},{1,57,0}, // 5
    {1,43,0},{2,0,4}, {1,57,0},{1,57,0},{1,57,0},{2,0,0}, {2,40,0},{1,57,0},{1,37,0},{2,0,1}, {1,40,7},{1,57,0},{3,27,6},{3,0,0}, {3,40,0},{1,57,0}, // 6
    {2,12,8},{2,0,5}, {1,57,0},{1,57,0},{1,57,0},{2,0,2}, {2,40,2},{1,57,0},{1,47,0},{3,0,3}, {1,57,0},{1,57,0},{1,57,0},{3,0,2}, {3,40,2},{1,57,0}, // 7
    {1,57,0},{2,48,4},{1,57,0},{1,57,0},{2,50,0},{2,48,0},{2,49,0},{1,57,0},{1,22,0},{1,57,0},{1,54,0},{1,57,0},{3,50,0},{3,48,0},{3,49,0},{1,57,0}, // 8
    {2,3,8}, {2,48,5},{1,57,0},{1,57,0},{2,50,2},{2,48,2},{2,49,3},{1,57,0},{1,56,0},{3,48,3},{1,55,0},{1,57,0},{1,57,0},{3,48,2},{1,57,0},{1,57,0}, // 9
    {2,31,1},{2,29,4},{2,30,1},{1,57,0},{2,31,0},{2,29,0},{2,30,0},{1,57,0},{1,52,0},{2,29,1},{1,51,0},{1,57,0},{3,31,0},{3,29,0},{3,30,0},{1,57,0}, // A
    {2,4,8}, {2,29,5},{1,57,0},{1,57,0},{2,31,2},{2,29,2},{2,30,3},{1,57,0},{1,16,0},{3,29,3},{1,53,0},{1,57,0},{3,31,2},{3,29,2},{3,30,3},{1,57,0}, // B
    {2,19,1},{2,17,4},{1,57,0},{1,57,0},{2,19,0},{2,17,0},{2,20,0},{1,57,0},{1,26,0},{2,17,1},{1,21,0},{1,57,0},{3,19,0},{3,17,0},{3,20,0},{1,57,0}, // C
    {2,8,8}, {2,17,5},{1,57,0},{1,57,0},{1,57,0},{2,17,2},{2,20,2},{1,57,0},{1,14,0},{3,17,3},{1,57,0},{1,57,0},{1,57,0},{3,17,2},{3,20,2},{1,57,0}, // D
    {2,18,1},{2,44,4},{1,57,0},{1,57,0},{2,18,0},{2,44,0},{2,24,0},{1,57,0},{1,25,0},{2,44,1},{1,33,0},{1,57,0},{3,18,0},{3,44,0},{3,24,0},{1,57,0}, // E
    {2,5,8}, {2,44,5},{1,57,0},{1,57,0},{1,57,0},{2,44,2},{2,24,2},{1,57,0},{1,46,0},{3,44,3},{1,57,0},{1,57,0},{1,57,0},{3,44,2},{3,24,2},{1,57,0}  // F
};

// 
int emu_dis6502_decode(int addr,char *menomic, int m_len) { 
    int inst_len, addrmode;
    const char *opcode, *pre, *post;// *pad;
    // char output[512] {0};
    char address[16] {0};

    inst_len = opcode_props[mem[addr]][0];              //Get instruction length
    opcode = instruction[opcode_props[mem[addr]][1]];     //Get opcode name
    addrmode = opcode_props[mem[addr]][2];                //Get info required to display addressing mode
    pre = modes[addrmode][0];                               //Look up pre-operand formatting text
    post = modes[addrmode][1];                              //Look up post-operand formatting text

    if(inst_len == 2) { // Single byte operand
        if(addrmode != 8) {
            snprintf(address,8, "$%02X", mem[addr+1]);
        }
        else {   // relative addressing
            int8_t rel_jmp = (int8_t) mem[addr+1];
            list<string> labels = emu_labels_get( (uint16_t) addr+2+rel_jmp);
            if(labels.size() > 0) {
                snprintf(address,16, "%s $%04X", labels.front().c_str(), addr+2+rel_jmp);
            }
            else {
                snprintf(address,8, "$%04X", (addr + 2 + rel_jmp));
            }
        }
    } 
    if(inst_len == 3) {
        uint16_t label_addr = (mem[addr+2] << 8) | mem[addr+1];
        list<string> labels = emu_labels_get( label_addr );
        if(labels.size() > 0) {
            snprintf(address,16, "%s $%04X", labels.front().c_str(), label_addr);
        }
        else {
            snprintf(address,8,"$%04X", label_addr);
            // snprintf(address,8,"$%02X%02X", mem[addr+2], mem[addr+1]);
        }
        
    }
    snprintf(menomic, m_len,"%s %s%s%s", opcode, pre, address, post);

    return inst_len;
}

void emu_dis6502_log(char * args) {
    char console_msg[1256] {0};
    char decode[256] {0};
    char mem_dump[16] {0};  // should only need 9

    char *cur = args;
    uint32_t address1, address2;

    printf("In emu_dis6502_log\r\n"); fflush(stdout);
    while(*cur) {
        address1 = 0;
        address2 = 0;

        int offset = range_helper(cur, address1, address2);
        if(offset == 0) { return;}
        cur += offset;

        while(address1 <= address2) {
            int len = emu_dis6502_decode(address1, decode, 256);
            switch(len) {
                case 1:
                    snprintf(mem_dump, 16, "%2.2x ", mem[address1]);
                    break;
                case 2:
                    snprintf(mem_dump, 16, "%2.2x %2.2x", mem[address1], mem[address1+1]);
                    break;
                case 3:
                    snprintf(mem_dump, 16, "%2.2x %2.2x %2.2x ", mem[address1], mem[address1+1], mem[address1+2]);
                    break;
                default:
                    break;
            }
            for(int i = 0; i < len; i++) {
                list<string> labels = emu_labels_get( (uint16_t) address1+i);
                if(labels.size() > 0) {
                    for(auto label : labels) {
                        snprintf(console_msg, 256, "%s:", label.c_str());
                        gui_con_printmsg(console_msg);
                    }
                }
            }
            snprintf(console_msg, 1256, "%4.4x: %-12s  %s", address1, mem_dump, decode);
            gui_con_printmsg(console_msg);
            address1 += len;
        }
    }

}

void emu_dis6502_init() {
    ;;;
}

void emu_dis6502_window(bool show_window) {
    static char mem_range[1024] = "$d075+$180";
    static bool follow_ci = false;
    static uint16_t last_ci = 0;

    uint32_t start_addr, end_addr;
    char decode[256] {0};
    char mem_dump[16] {0};  // should only need 9
    char *cur;

    uint16_t ci = emulator_getci();
    if(last_ci != ci) last_ci = ci;

    int ci_line=0, cur_line = 0;

    ImGui::Begin("Disassembly", &show_window);

    // ImGui::Checkbox("Update", &update_mem_dump);   
    // ImGui::SameLine(); 
    if(ImGui::InputText("Range",mem_range,1024)) {
        // process mem_range
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow CI",&follow_ci);

    ImGui::BeginChild("dis",ImVec2(0,-25.0));

    cur = mem_range;

    while(*cur) {
        start_addr = 0;
        end_addr = 0;

        int offset = range_helper(cur, start_addr, end_addr);
        if(offset == 0) { break;}
        cur += offset;

        while(start_addr<=end_addr) {
            int len = emu_dis6502_decode(start_addr, decode, 256);
            switch(len) {
                case 1:
                    snprintf(mem_dump, 16, "%2.2x ", mem[start_addr]);
                    break;
                case 2:
                    snprintf(mem_dump, 16, "%2.2x %2.2x", mem[start_addr], mem[start_addr+1]);
                    break;
                case 3:
                    snprintf(mem_dump, 16, "%2.2x %2.2x %2.2x ", mem[start_addr], mem[start_addr+1], mem[start_addr+2]);
                    break;
                default:
                    break;
            }
            for(int i = 0; i < len; i++) {
                list<string> labels = emu_labels_get( (uint16_t) start_addr+i);
                if(labels.size() > 0) {
                    for(auto label : labels) {
                        ImGui::Text("%s:", label.c_str());
                        cur_line++;
                    }
                }
            }
            char buff[256] {0};
            snprintf(buff,256, "%4.4x:",start_addr);
            if(ImGui::Checkbox(buff, &bp_mask[start_addr])){;;}
            ImGui::SameLine();
            if( last_ci >= start_addr && last_ci < (start_addr+len)) { // current instruction
                ImGui::TextColored(ImVec4(0.0f,1.0f,0.0f,1.0f),"  %-12s  %s", mem_dump, decode);
                ci_line=cur_line;
            }
            else {
                ImGui::Text("  %-12s  %s", mem_dump, decode);
            }
            start_addr += len;
            cur_line++;
       }
    }
    if(follow_ci && ci_line) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY()* ci_line / (1.0f * cur_line));
    }
    ImGui::EndChild();
    ImGui::End();

}