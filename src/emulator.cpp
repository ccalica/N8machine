#define CHIPS_IMPL
#include "m6502.h"

#include "imgui.h"

#include "emulator.h"
#include "emu_tty.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>



#define BUS_DECODE(bus, base, mask) if((bus & mask) == base)
// #define BUS_LOG(tc,sys,rw,a,d) printf("%lu: %s %s %04X: %02X\r\n",tc,sys,rw ? "R" : "W",a,d);
#define BUS_LOG(tc,sys,rw,a,d) ;;

#define IRQ_CLR() mem[0x00FF] = 0x00;
#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)

const char *rom_file = "N8firmware";
uint64_t tick_count = 0;

// 64 KB zero-initialized memory
uint8_t mem[(1<<16)] = { };
uint8_t frame_buffer[(1<<8)] = { };

m6502_t cpu;
m6502_desc_t desc;
uint64_t pins;
bool bp_enable;
bool bp_hit = false;
bool bp_mask[65536] {false};
bool pc_mask[65536] {false};
bool label_mask[65536] {false};  // TODO: init via .sym file


// these are the same.
#define BUS_READ (pins & M6502_RW)
bool emu_bus_read() {
    return BUS_READ;
}
void emu_set_irq(int bit) {
    IRQ_SET(bit);
}
void emu_clr_irq(int bit) {
    mem[0x00FF] = (mem[0x00FF] & ~(0x01 << bit) );
}

void emulator_init() {
    uint16_t rom_ptr = 0xD000;
    printf("Loading ROM\n");
    FILE *fp = fopen(rom_file, "r");
    while(1) {
        uint8_t c = fgetc(fp);
        if(feof(fp)) break;
        mem[rom_ptr] = c;
        if(rom_ptr % 16 == 0) printf("%04X:", rom_ptr);
        printf(" %02X", mem[rom_ptr]);   
        if(rom_ptr % 16 == 15) printf("\n");
        rom_ptr++;  
    }
    fclose(fp);
    puts("\n\n");

    // initialize a 6502 instance:
    //desc.bcd_disabled=false;
    
    pins = m6502_init(&cpu, &desc);
    tty_init();
    
}

void emulator_step() {
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);

        if(bp_enable && bp_mask[addr]) {
            bp_hit = true;
            printf("BP hit: %4.4x (%d): ", addr, addr);
            fflush(stdout);
        }
        IRQ_CLR();
        // pins = pins & ~M6502_IRQ;

        tty_tick(pins);
        // pins & M6502_IRQ ==  M6502_IRQ
        if(mem[0x00FF] == 0) {
            pins = pins & ~M6502_IRQ;
            // pins = pins | M6502_IRQ; // pull up
            // printf("IRQ is set: %d %d\r\n", mem[0x00FF],pins & M6502_IRQ ==  M6502_IRQ);
            // printf("PC: %4.4x\r\n", m6502_pc(&cpu));
            // printf("P: 0x%2.2x\r\n", m6502_p(&cpu));
            // fflush(stdout);
        }
        else {
            // pins = pins & ~M6502_IRQ;
            pins = pins | M6502_IRQ; // pull up
            // printf("IRQ is clear\r\n");
            // fflush(stdout);
        }

        // Write to underlying RAM first
        if (BUS_READ) {
            M6502_SET_DATA(pins, mem[addr]);
        }
        else {
            mem[addr] = M6502_GET_DATA(pins);
            // printf("%04X: %02X\n", addr, mem[addr]);
        }

        // Monitor Zeropage
        BUS_DECODE(addr, 0x0000, 0xFF00) {
            // BUS_LOG(tick_count, "0Pg", BUS_READ, addr, mem[addr] );
        }

        // Handle some devices    
        BUS_DECODE(addr, 0xC000, 0xFF00) {
            // printf("FB: %04X\n", addr);
            uint16_t dev_addr = addr - 0xC000;
            if(BUS_READ) { // Read
                M6502_SET_DATA(pins, frame_buffer[dev_addr]);
            }
            else {
                frame_buffer[dev_addr] = M6502_GET_DATA(pins);
                BUS_LOG(tick_count, "TXT", BUS_READ, addr, frame_buffer[dev_addr] );
            }
        }
        BUS_DECODE(addr, 0xFFF0, 0xFFF0) {
            BUS_LOG(tick_count, "VEC", BUS_READ, addr, mem[addr] );
        }
        // TTY device
        BUS_DECODE(addr, 0xC100, 0xFFF0) { 
            const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
            tty_decode(pins, dev_reg);
        }
        // IRQ handling
        
        tick_count++;

}

bool emulator_check_break() {
    if(bp_enable && bp_hit) {
        bp_hit = false;
        return true;
    }
    return false;
}
void emulator_enablebp(bool en) {
    bp_enable = en;
}

void emulator_logbp() {
    char debug_msg[256] {0};

    int addr = 0;
    while(addr < 65536) {
        if(bp_mask[addr]) {
            snprintf(debug_msg, 256, "  BP: %4.4x (%d)", addr, addr);
            gui_con_printmsg(debug_msg);
        }
        addr++;
    }
}
void emulator_setbp(char * buff) {
    char *cur = buff;
    char debug_msg[256] {0};

    uint32_t bp;
    
    // // Clear the mask
    // for(int i = 0; i<65536; i++) bp_mask[i] = false;

    while(*cur) {
        bp = 0;

        int offset = my_get_uint(cur, bp);
        if(offset == 0) return;
        cur += offset;

        uint16_t addr = (uint16_t) bp;
        bp_mask[addr] = true;

        snprintf(debug_msg, 256, "Set BP: %4.4x (%d)\r\n", bp, bp);
        gui_con_printmsg(debug_msg);
    }

}
void emulator_setbp_old(char * buff) {
    char *cur = buff;
    char debug_msg[256] {0};

    int8_t type = 0;  // Format of current token  0=DEC  1=HEX
    int digit;
    int bp;
    
    // Clear the mask
    for(int i = 0; i<65536; i++) bp_mask[i] = false;

    while(*cur) {
        bp = 0;
        
        if(*cur == '$')  {
            type = 1;
            cur++;
        }
        else if(*cur == '0' && *(cur+1) == 'x')  {
            type = 1;
            cur++; cur++;
        }
        else if(emu_is_digit(*cur) >= 0) {
            type = 0;
        }
        else { // not start of number
            type = -1;
            cur++;
        }

        if(type == 0) { // DEC
            while( *cur && (digit = emu_is_digit(*cur) ) >=0) {
                bp = 10 * bp + digit;            
                cur++;
            }
        }
        else if (type == 1) { // HEX
            while( *cur && (digit = emu_is_hex(*cur) ) >=0) {
                bp = 16 * bp + digit;       
                cur++;     
            }
        }
        if(type >= 0) {
            // bp should contain full address
            uint16_t addr = (uint16_t) bp;
            bp_mask[addr] = true;
            snprintf(debug_msg, 256, "PARSED type %d BREAK POINT: %4.4x (%d)\r\n",type , bp, bp);
            gui_con_printmsg(debug_msg);
        }

    }

}

void emulator_reset() {
    pins = pins | M6502_RES;
    tty_reset();
}

void emulator_show_memdump_window(bool &show_memmap_window) {
    static bool update_mem_dump = false;
    int line_len = 0x10;
    const int row_size = ((3 * line_len) + 9);
    // const int row_count= (total_memory / line_len) + 1 + 2; // Need 1 but 2 extra for space
    // const int mdb_len = row_count*row_size;
    char* memory_dump_buffer= new char[row_size];
    static char mem_range[1024];


    char *cur;
    uint32_t start_addr=0, end_addr = 0;
    static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;

    ImGui::Begin("Memory Map", &show_memmap_window);

    // ImGui::CheckboxFlags("ImGuiInputTextFlags_ReadOnly", &flags, ImGuiInputTextFlags_ReadOnly);
    ImGui::Checkbox("Update", &update_mem_dump);   
    ImGui::SameLine(); 
    if(ImGui::InputText("Range",mem_range,1024)) {
        // process mem_range
    }
    ImGui::BeginChild("mem",ImVec2(0,-25.0));

    if(update_mem_dump) {
        cur = mem_range;

        while(*cur) {
            start_addr = 0;
            end_addr = 0;

            int offset = range_helper(cur, start_addr, end_addr);
            if(offset == 0) { break;}
            cur += offset;

            while(start_addr<=end_addr) {
                char line[64] {0};
                // char buff[8] {0};
                char* line_cur = line;

                for(int i = 0; i<line_len && start_addr+i <= end_addr; i++) {
                    my_itoa(line_cur, mem[start_addr+i],2);
                    line_cur++;line_cur++;
                    *line_cur = ' ';
                    line_cur++;
                }                
                *line_cur = 0;
                ImGui::Text("0x%4.4x: %s", start_addr, line);
                start_addr = start_addr + line_len;
            }
        }

    }
    ImGui::EndChild();
    ImGui::End();

    delete memory_dump_buffer;

}


#define sr_bit(bit) (0x01 & (m6502_p(&cpu) >> bit))

void emulator_show_status_window(bool &show_status_window, float frame_time, float fps ) {
    float val_off=30, lab_off=70;
    ImGui::Begin("CPU Registers", &show_status_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
    ImGui::Text("A:"); 
    ImGui::SameLine(val_off); ImGui::Text("%2.2x",m6502_a(&cpu));
    ImGui::SameLine(lab_off); ImGui::Text("X:");
    ImGui::SameLine(lab_off+val_off); ImGui::Text("%2.2x", m6502_x(&cpu));
    ImGui::SameLine(2.0 * lab_off); ImGui::Text("Y:");
    ImGui::SameLine(2.0 * lab_off + val_off); ImGui::Text("%2.2x",m6502_y(&cpu));

    ImGui::Text("SR:");
    ImGui::SameLine(lab_off); ImGui::Text("N%d V%d -%d B%d D%d I%d Z%d C%d",sr_bit(7),sr_bit(6),sr_bit(5),sr_bit(4), \
                                                    sr_bit(3),sr_bit(2),sr_bit(1),sr_bit(0));

    ImGui::Text("Data: %2.2x     Addr: %4.4x", M6502_GET_DATA(pins), M6502_GET_ADDR(pins));
    ImGui::Text("  SP: %2.2x       PC: %4.4x",m6502_s(&cpu),m6502_pc(&cpu));
    ImGui::Text("IRQ: %d %d", (pins & M6502_IRQ) == M6502_IRQ, mem[0x00FF] == 0);
    ImGui::Text("App avg %.3f ms/frame (%.1f FPS)", frame_time, fps);
    ImGui::Text("Ticks: %lu", tick_count);
    // if (ImGui::Button("Close Me"))
    //     show_status_window = false;
    ImGui::End();

}

void emulator_show_console_window(bool &show_console_window) {
    gui_show_console_window(show_console_window);
}


