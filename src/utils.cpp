#include "utils.h"

char itohc(unsigned int val) {
    char c;
    val = val & 0x0F;
    if(val < 10) { // NUMERAL
        c = '0' + val;
    }
    else {
        c = 'A' + (val-10);
    }
    return c;
}

char* my_itoa(char* buff, const unsigned int val, const unsigned int size) {
    unsigned int i;
    int rev_i;
    for(i = 0,rev_i = size-1; i < size; i++, rev_i--) {
        *(buff+i) = itohc(val >> (rev_i*4));
    }
    *(buff+i) = 0;
    return buff;
}

int emu_is_digit(char c) {
    if(c >='0' && c <= '9') return c - '0';
    return -1;
}
int emu_is_hex(char c) {
    if(c >='0' && c <= '9') return c - '0';
    if(c >='A' && c <= 'F') return c - 'A' + 10;
    if(c >='a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
 
int range_helper(char *args, uint32_t& start_addr, uint32_t& end_addr) {
    char * cur = args;

    start_addr = 0;

    int offset = my_get_uint(cur, start_addr);
    if(offset == 0) return 0;
    cur += offset;

    end_addr = start_addr;
    if(*cur == '-') { // Range
        cur++;

        int offset = my_get_uint(cur, end_addr);
        if(offset == 0) return 0; // Error
        cur += offset;
    }
    else if(*cur == '+') { // Relative
        cur++;

        int offset = my_get_uint(cur, end_addr);
        if(offset == 0) return 0; // Error
        cur += offset;
        end_addr = end_addr + start_addr;
    }
    return cur-args;
}

// return bytes consumed
int my_get_uint(char *numbers,  uint32_t& dest) {
    char *cur = numbers;

    int8_t type = 0;  // Format of current token  0=UNKNOWN  1=DEC   2=HEX
    int digit;
    uint32_t num;
    
    // Determin number base and start.  ignore non numeral (and hex) digits

    while(*cur) {
        
        if(*cur == '$')  {
            type = 2;
            cur++;
            break;
        }
        else if(*cur == '0' && *(cur+1) == 'x')  {
            type = 2;
            cur++; cur++;
            break;
        }
        else if(emu_is_digit(*cur) >= 0) {
            type = 1;
            break;
        }
        cur++;
    }
    if(type == 0) return 0;

    num = 0;
    // cur points to start of number
    if(type == 1) { // DEC
        while( *cur && (digit = emu_is_digit(*cur) ) >=0) {
            num = 10 * num + digit;            
            cur++;
        }
    }
    else if (type == 2) { // HEX
        while( *cur && (digit = emu_is_hex(*cur) ) >=0) {
            num = 16 * num + digit;       
            cur++;     
        }
    }
    // num should contain full address
    dest = num;
    return(cur-numbers);
}

int htoi(char * cur) {
    int num = 0, digit =0;
    while( *cur && (digit = emu_is_hex(*cur) ) >=0) {
        num = 16 * num + digit;       
        cur++;     
    }
    return num;
}