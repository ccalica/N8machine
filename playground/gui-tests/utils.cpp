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
