#define CHIPS_IMPL
#include "m6502.h"

#include "imgui.h"

#include "emulator.h"
#include "n8_memory_map.h"
#include "emu_tty.h"
#include "emu_video.h"
#include "emu_kbd.h"
#include "emu_storage.h"
#include "emu_labels.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>



#define BUS_DECODE(bus, base, mask) if((bus & mask) == base)
// #define BUS_LOG(tc,sys,rw,a,d) printf("%lu: %s %s %04X: %02X\r\n",tc,sys,rw ? "R" : "W",a,d);
#define BUS_LOG(tc,sys,rw,a,d) ;;

#define IRQ_CLR() mem[N8_IRQ_FLAGS] = 0x00;
#define IRQ_SET(bit) mem[N8_IRQ_FLAGS] = (mem[N8_IRQ_FLAGS] | (0x01 << bit))

const char *kernel_file = "n8_kernel";
const char *shell_file = "n8_shell";
uint64_t tick_count = 0;

// 64 KB zero-initialized memory
uint8_t mem[(1<<16)] = { };

// 4 KB frame buffer (separate backing store, not backed by mem[])
uint8_t frame_buffer[N8_FB_SIZE] = { };
bool fb_dirty = true;

m6502_t cpu;
m6502_desc_t desc;
uint64_t pins;
bool bp_enable;
bool bp_hit = false;
bool bp_mask[65536] {false};
bool wp_write_mask[65536] {false};
bool wp_read_mask[65536] {false};
static bool wp_enable = false;
static bool wp_hit_flag = false;
static uint16_t wp_addr = 0;
static int wp_type = 0;  // 2=write, 3=read, 4=access
static bool run_emulator = false;
static bool step_emulator = false;
static bool gdb_halted = false;

bool pc_mask[65536] {false};
bool label_mask[65536] {false};  // TODO: init via .sym file
uint16_t cur_instruction = 0x00;

// these are the same.
#define BUS_READ (pins & M6502_RW)
bool emu_bus_read() {
    return BUS_READ;
}
void emu_set_irq(int bit) {
    IRQ_SET(bit);
}
void emu_clr_irq(int bit) {
    mem[N8_IRQ_FLAGS] = (mem[N8_IRQ_FLAGS] & ~(0x01 << bit) );
}

static void load_rom_file(const char *path, uint16_t base, uint16_t max_size) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("ERROR: Cannot open ROM file '%s'\r\n", path);
        fflush(stdout);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size > max_size) {
        printf("WARNING: ROM '%s' is %ld bytes, truncating to %d\r\n",
               path, size, max_size);
        fflush(stdout);
        size = max_size;
    }

    printf("Loading ROM '%s' at $%04X (%ld bytes)\r\n", path, base, size);
    fflush(stdout);

    fread(&mem[base], 1, size, fp);
    fclose(fp);
}

void emulator_loadrom() {
    load_rom_file(kernel_file, N8_KERNEL_BASE, N8_KERNEL_SIZE);
    load_rom_file(shell_file, N8_SHELL_BASE, N8_SHELL_SIZE);
}
void emulator_init() {
    emulator_loadrom();
    emu_labels_init();
    
    pins = m6502_init(&cpu, &desc);
    tty_init();
    video_init();
    kbd_init();
    storage_init();

}

void emulator_step() {
        char debug_msg[256];
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);

        if(addr == m6502_pc(&cpu)) {
            // printf("ci_next\r\n");fflush(stdout);
            cur_instruction = m6502_pc(&cpu);
        }

        if(bp_enable && bp_mask[addr] && (pins & M6502_SYNC)) {
            bp_hit = true;
            snprintf(debug_msg, 256, "BP Hit: %4.4x (%d)\r\n", addr, addr);
            gui_con_printmsg(debug_msg);

        }
        if (wp_enable) {
            bool is_write = !(pins & M6502_RW);
            if ((wp_write_mask[addr] && is_write) ||
                (wp_read_mask[addr] && (pins & M6502_RW) && !(pins & M6502_SYNC))) {
                if (!wp_hit_flag) {
                    wp_hit_flag = true;
                    wp_addr = addr;
                    wp_type = is_write ? 2 : 3;
                    // If both masks set at this addr, this is an access watchpoint (Z4)
                    if (wp_write_mask[addr] && wp_read_mask[addr]) wp_type = 4;
                }
            }
        }
        // IRQ tick: clear all flags, let devices reassert, then update pin
        IRQ_CLR();
        tty_tick(pins);
        storage_tick();
        if (mem[N8_IRQ_FLAGS] != 0)
            pins |= M6502_IRQ;
        else
            pins &= ~M6502_IRQ;

        // Frame buffer: $C000-$CFFF (separate backing store)
        bool fb_access = (addr >= N8_FB_BASE && addr <= N8_FB_END);
        if (fb_access) {
            uint16_t fb_offset = addr - N8_FB_BASE;
            if (pins & M6502_RW) {
                M6502_SET_DATA(pins, frame_buffer[fb_offset]);
            } else {
                uint8_t val = M6502_GET_DATA(pins);
                if (frame_buffer[fb_offset] != val) {
                    frame_buffer[fb_offset] = val;
                    fb_dirty = true;
                }
            }
        }

        // Device register space: $D800-$DFFF
        bool dev_access = !fb_access && (addr >= N8_DEV_BASE && addr <= N8_DEV_END);
        if (dev_access) {
            uint16_t dev_offset = addr - N8_DEV_BASE;
            uint8_t  slot = dev_offset >> 5;    // 0-63
            uint8_t  reg  = dev_offset & 0x1F;  // 0-31

            switch (slot) {
                case N8_IRQ_SLOT:  // $D800: System / IRQ (read-only to CPU)
                    if (pins & M6502_RW) {
                        M6502_SET_DATA(pins, mem[N8_IRQ_FLAGS]);
                    }
                    // CPU writes ignored — IRQ flags managed by emu_set_irq/emu_clr_irq
                    break;
                case N8_TTY_SLOT:  // $D820: TTY
                    tty_decode(pins, reg);
                    break;
                case N8_VID_SLOT:  // $D840: Video Control
                    video_decode(pins, reg);
                    break;
                case N8_KBD_SLOT:  // $D860: Keyboard
                    kbd_decode(pins, reg);
                    break;
                case N8_STORAGE_SLOT:  // $D880: Storage
                    storage_decode(pins, reg);
                    break;
                default:
                    // Reserved slots: read returns $00, write ignored
                    if (pins & M6502_RW) {
                        M6502_SET_DATA(pins, 0x00);
                    }
                    break;
            }
        }

        // Generic RAM/ROM access (skip if FB or device handled it)
        if (!fb_access && !dev_access) {
            if (BUS_READ) {
                M6502_SET_DATA(pins, mem[addr]);
            }
            else {
                if (addr < N8_ROM_BASE) {
                    mem[addr] = M6502_GET_DATA(pins);
                }
                // else: silently ignore CPU writes to ROM ($E000-$FFFF)
            }
        }

        tick_count++;

}
uint16_t emulator_getci() {
    return cur_instruction;
}
uint16_t emulator_getpc() {
    return m6502_pc(&cpu);
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
    video_reset();
    kbd_reset();
    storage_reset();
    memset(frame_buffer, 0, N8_FB_SIZE);
    fb_dirty = true;
    emulator_loadrom();
    emu_labels_init();
}

// ---- GDB stub accessor functions ----

uint8_t emulator_read_a()  { return m6502_a(&cpu); }
uint8_t emulator_read_x()  { return m6502_x(&cpu); }
uint8_t emulator_read_y()  { return m6502_y(&cpu); }
uint8_t emulator_read_s()  { return m6502_s(&cpu); }
uint8_t emulator_read_p()  { return m6502_p(&cpu); }

void emulator_write_a(uint8_t v) { m6502_set_a(&cpu, v); }
void emulator_write_x(uint8_t v) { m6502_set_x(&cpu, v); }
void emulator_write_y(uint8_t v) { m6502_set_y(&cpu, v); }
void emulator_write_s(uint8_t v) { m6502_set_s(&cpu, v); }
void emulator_write_p(uint8_t v) { m6502_set_p(&cpu, v); }

void emulator_write_pc(uint16_t addr) {
    // TODO: prefetch ignores frame_buffer[] — if PC is set into $C000-$CFFF
    // via GDB, the CPU will see mem[addr] (zeros) instead of frame_buffer[].
    // Not a practical issue since FB is not executable, but fix if GDB PC
    // write to FB ever becomes a real use case.
    pins = (pins & (M6502_IRQ | M6502_NMI | M6502_RES | M6502_RDY)) | M6502_SYNC | M6502_RW;
    M6502_SET_ADDR(pins, addr);
    M6502_SET_DATA(pins, mem[addr]);
    m6502_set_pc(&cpu, addr);
}

bool emulator_bp_hit()      { return bp_enable && bp_hit; }
void emulator_clear_bp_hit() { bp_hit = false; }
bool emulator_bp_enabled()   { return bp_enable; }

void emulator_enablewp(bool en) { wp_enable = en; }
bool emulator_wp_enabled()      { return wp_enable; }
bool emulator_wp_hit()          { return wp_enable && wp_hit_flag; }
void emulator_clear_wp_hit()    { wp_hit_flag = false; }
uint16_t emulator_wp_hit_addr() { return wp_addr; }
int emulator_wp_hit_type()      { return wp_type; }

bool emulator_is_running()           { return run_emulator; }
void emulator_set_running(bool run)  { run_emulator = run; }
bool emulator_is_stepping()          { return step_emulator; }
void emulator_set_stepping(bool s)   { step_emulator = s; }
bool emulator_is_gdb_halted()        { return gdb_halted; }
void emulator_set_gdb_halted(bool h) { gdb_halted = h; }

void emulator_show_memdump_window(bool &show_memmap_window) {
    static bool update_mem_dump = false;
    int line_len = 0x10;
    const int row_size = ((3 * line_len) + 9);
    // const int row_count= (total_memory / line_len) + 1 + 2; // Need 1 but 2 extra for space
    // const int mdb_len = row_count*row_size;
    char* memory_dump_buffer= new char[row_size];
    static char mem_range[1024] = "$022d+$25,$0+$10";


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

    delete[] memory_dump_buffer;

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

    ImGui::Text("Data: %2.2x     Bu Addr: %4.4x", M6502_GET_DATA(pins), M6502_GET_ADDR(pins));
    ImGui::Text("  SP: %2.2x        PC: %4.4x",m6502_s(&cpu),m6502_pc(&cpu));
    // ImGui::Text(" IRQ: %2d %2d ", (pins & M6502_IRQ) == M6502_IRQ, (int) (mem[0x00FF] != 0));
    ImGui::Text(" IRQ: %2d %2d Last PC: %4.4x", (pins & M6502_IRQ) == M6502_IRQ, (int) (mem[N8_IRQ_FLAGS] != 0), cur_instruction);
    ImGui::Text("App avg %.3f ms/frame (%.1f FPS)", frame_time, fps);
    ImGui::Text("Ticks: %lu", tick_count);
    // if (ImGui::Button("Close Me"))
    //     show_status_window = false;
    ImGui::End();

}

void emulator_show_console_window(bool &show_console_window) {
    gui_show_console_window(show_console_window);
}


