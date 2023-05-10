#define CHIPS_IMPL
#include "m6502.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

const char *rom_file = "N8firmware.bin";

#define BUS_READ (pins & M6502_RW)
#define BUS_DECODE(bus, base, mask) if((bus & mask) == base)
#define BUS_LOG(tc,sys,rw,a,d) printf("%u: %s %s %04X: %02X\n",tc,sys,rw ? "R" : "W",a,d);

#define IRQ_CLR() mem[0x00FF] = 0x00;
#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)


int main() {
        uint64_t tick_count = 0;
    // 64 KB zero-initialized memory
    uint8_t mem[(1<<16)] = { };
    uint8_t frame_buffer[(1<<8)] = { };

    uint16_t rom_ptr = 0xC000;
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

    // Setup RESET Vector
    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0xC0;

    // // put an LDA #$33 instruction at address 0
    // mem[0x0200] = 0xA9;
    // mem[0x0201] = 0x33;

    // initialize a 6502 instance:
    m6502_t cpu;
    m6502_desc_t desc;
    //desc.bcd_disabled=false;
    
    uint64_t pins = m6502_init(&cpu, &desc);
    
    // run for 9 ticks (7 ticks reset sequence, plus 2 ticks for LDA #$33)
    while( 1 ) {
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);
        IRQ_CLR();

        // Write to underlying RAM first
        if (pins & M6502_RW) {
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
        // Monitor ROM
        BUS_DECODE(addr, 0xC000, 0xC000) {
            BUS_LOG(tick_count, "ROM", BUS_READ, addr, mem[addr] );
        }

        // Handle some devices    
        BUS_DECODE(addr, 0x0200, 0xFF00) {
            // printf("FB: %04X\n", addr);
            uint16_t dev_addr = addr - 0x0200;
            if(BUS_READ) { // Read
                M6502_SET_DATA(pins, frame_buffer[dev_addr]);
            }
            else {
                frame_buffer[dev_addr] = M6502_GET_DATA(pins);
                BUS_LOG(tick_count, "TXT", BUS_READ, addr, frame_buffer[addr] );
            }
        }

        // IRQ handling
        
        if(tick_count == 100) break;
        tick_count++;
    }

    return 0;
}
