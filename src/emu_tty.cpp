
#include "emu_tty.h"
#include "emulator.h"
#include "m6502.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <termios.h>

#include <queue>
using namespace std;

struct termios orig_termios;
queue<uint8_t> tty_buff;

void tty_reset_term() {
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio() {
    struct termios new_termios;

    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    atexit(tty_reset_term);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

int tty_kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

int getch() {
    int r;
    unsigned char c;
    if ((r=read(0, &c, sizeof(c))) < 0) {
        return r;
    }
    return c;
}

void tty_tick(uint64_t &pins) {
    if(tty_buff.size() > 0) { 
        emu_set_irq(1);
    }
    if(!tty_kbhit()) {
        return;
    }
    int c;
    if((c = getch()) < 0) { // error condition
        exit(-1);
    }
    tty_buff.push((uint8_t (c & 0xff)));
    emu_set_irq(1);
}

void tty_decode(uint64_t &pins, uint8_t dev_reg) {
    if((pins & M6502_RW)) { // Read
        uint8_t data_bus;
        switch(dev_reg) {
            case 0x00: // Out Status
                data_bus = 0x00;  // Always ready to receive
                break;
            case 0x01: // Out Data  
                data_bus = 0xFF; // this shouldn't happen.
                break;
            case 0x02: // In Status
                data_bus = 0x00; // set this to something if we have data
                if(tty_buff.size() > 0) {
                    data_bus = 0x01;
                }
                // printf("read tty In ctrl\r\n");
                // fflush(stdout);
                break;
            case 0x03: // In Data
                // printf("Read TTY Reg\n\r");
                // fflush(stdout);
                data_bus = tty_buff.front();
                tty_buff.pop();
                if(tty_buff.size()== 0) {
                    emu_clr_irq(1);
                }
                break;
            default:
                data_bus = 0x00;
        }
        M6502_SET_DATA(pins, data_bus);
    }
    else {  // Write
        char c;
        switch(dev_reg) {
            case 0x01: // Main path
                c = M6502_GET_DATA(pins);
                putchar(c);
                fflush(stdout);
                break;
            case 0x00:
            case 0x02:
            case 0x03:
                ;;;
            default:
                ;;; // nothing
        }
        // printf("Um got char %c (%d)\n", c, c);
        // BUS_LOG(tick_count, "TTY", BUS_READ, addr, frame_buffer[dev_addr] );
    }

}

void tty_inject_char(uint8_t c) {
    tty_buff.push(c);
}
int tty_buff_count() {
    return (int)tty_buff.size();
}

void tty_reset() {
    while( !tty_buff.empty()) {
        tty_buff.pop();
    }
    emu_clr_irq(1);
    printf("tty_reset():\r\n");
    fflush(stdout);
}

void tty_init() {
    set_conio();
}
