#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("utils") {

    // -------------------------------------------------------------------------
    // itohc
    // -------------------------------------------------------------------------

    TEST_CASE("T01: itohc 0-9 returns '0'-'9'") {
        CHECK(itohc(0)  == '0');
        CHECK(itohc(1)  == '1');
        CHECK(itohc(5)  == '5');
        CHECK(itohc(9)  == '9');
    }

    TEST_CASE("T02: itohc 10-15 returns 'A'-'F'") {
        CHECK(itohc(10) == 'A');
        CHECK(itohc(11) == 'B');
        CHECK(itohc(12) == 'C');
        CHECK(itohc(13) == 'D');
        CHECK(itohc(14) == 'E');
        CHECK(itohc(15) == 'F');
    }

    TEST_CASE("T03: itohc(0x1A) returns 'A' (lower nibble mask)") {
        CHECK(itohc(0x1A) == 'A');
    }

    // -------------------------------------------------------------------------
    // my_itoa
    // -------------------------------------------------------------------------

    TEST_CASE("T04: my_itoa 0xFF with size 2 produces \"FF\"") {
        char buf[16];
        my_itoa(buf, 0xFF, 2);
        CHECK(std::string(buf) == "FF");
    }

    TEST_CASE("T05: my_itoa 0xD000 with size 4 produces \"D000\"") {
        char buf[16];
        my_itoa(buf, 0xD000, 4);
        CHECK(std::string(buf) == "D000");
    }

    TEST_CASE("T06: my_itoa 0x05 with size 4 produces \"0005\"") {
        char buf[16];
        my_itoa(buf, 0x05, 4);
        CHECK(std::string(buf) == "0005");
    }

    // -------------------------------------------------------------------------
    // emu_is_digit
    // -------------------------------------------------------------------------

    TEST_CASE("T07: emu_is_digit('5') returns 5") {
        CHECK(emu_is_digit('5') == 5);
    }

    TEST_CASE("T08: emu_is_digit('A') returns -1") {
        CHECK(emu_is_digit('A') == -1);
    }

    // -------------------------------------------------------------------------
    // emu_is_hex
    // -------------------------------------------------------------------------

    TEST_CASE("T09: emu_is_hex('a') returns 10") {
        CHECK(emu_is_hex('a') == 10);
    }

    TEST_CASE("T10: emu_is_hex('F') returns 15") {
        CHECK(emu_is_hex('F') == 15);
    }

    TEST_CASE("T11: emu_is_hex('9') returns 9") {
        CHECK(emu_is_hex('9') == 9);
    }

    TEST_CASE("T12: emu_is_hex('G') returns -1") {
        CHECK(emu_is_hex('G') == -1);
    }

    // -------------------------------------------------------------------------
    // my_get_uint
    // -------------------------------------------------------------------------

    TEST_CASE("T13: my_get_uint decimal \"1234\" -> dest==1234, returns 4") {
        uint32_t dest = 0;
        int consumed = my_get_uint((char*)"1234", dest);
        CHECK(dest == 1234);
        CHECK(consumed == 4);
    }

    TEST_CASE("T14: my_get_uint \"$D000\" -> dest==0xD000, returns 5") {
        uint32_t dest = 0;
        int consumed = my_get_uint((char*)"$D000", dest);
        CHECK(dest == 0xD000);
        CHECK(consumed == 5);
    }

    TEST_CASE("T15: my_get_uint \"0xFF\" -> dest==0xFF, returns 4") {
        uint32_t dest = 0;
        int consumed = my_get_uint((char*)"0xFF", dest);
        CHECK(dest == 0xFF);
        CHECK(consumed == 4);
    }

    TEST_CASE("T16: my_get_uint \" $0A\" (leading space) -> dest==0x0A") {
        uint32_t dest = 0;
        my_get_uint((char*)" $0A", dest);
        CHECK(dest == 0x0A);
    }

    TEST_CASE("T17: my_get_uint \"xyz\" (no number) -> returns 0") {
        uint32_t dest = 0;
        int consumed = my_get_uint((char*)"xyz", dest);
        CHECK(consumed == 0);
    }

    // -------------------------------------------------------------------------
    // range_helper
    // -------------------------------------------------------------------------

    TEST_CASE("T18: range_helper \"$100-$1FF\" -> s=0x100, e=0x1FF") {
        uint32_t s = 0, e = 0;
        range_helper((char*)"$100-$1FF", s, e);
        CHECK(s == 0x100);
        CHECK(e == 0x1FF);
    }

    TEST_CASE("T19: range_helper \"$100+$10\" -> s=0x100, e=0x110") {
        uint32_t s = 0, e = 0;
        range_helper((char*)"$100+$10", s, e);
        CHECK(s == 0x100);
        CHECK(e == 0x110);
    }

    TEST_CASE("T20: range_helper \"$D000\" (single addr) -> s==e==0xD000") {
        uint32_t s = 0, e = 0;
        range_helper((char*)"$D000", s, e);
        CHECK(s == 0xD000);
        CHECK(e == 0xD000);
    }

    // -------------------------------------------------------------------------
    // htoi
    // -------------------------------------------------------------------------

    TEST_CASE("T21: htoi(\"D000\") -> 0xD000") {
        CHECK(htoi((char*)"D000") == 0xD000);
    }

    TEST_CASE("T22: htoi(\"xyz\") -> 0") {
        CHECK(htoi((char*)"xyz") == 0);
    }

    // -------------------------------------------------------------------------
    // IRQ register tests (direct set/clear, no emulator_step)
    // -------------------------------------------------------------------------

    TEST_CASE("T69: emu_set_irq(1) sets bit 1 in mem[0x00FF]") {
        mem[0x00FF] = 0;
        emu_set_irq(1);
        CHECK((mem[0x00FF] & 0x02) != 0);
    }

    TEST_CASE("T69a: emu_set_irq(0) sets bit 0 in mem[0x00FF]") {
        mem[0x00FF] = 0;
        emu_set_irq(0);
        CHECK((mem[0x00FF] & 0x01) != 0);
    }

    TEST_CASE("T69b: emu_set_irq(0) then emu_set_irq(1) -> mem[0x00FF]==0x03") {
        mem[0x00FF] = 0;
        emu_set_irq(0);
        emu_set_irq(1);
        CHECK(mem[0x00FF] == 0x03);
    }

    TEST_CASE("T70: emu_set_irq(1) then emu_clr_irq(1) -> mem[0x00FF]==0x00") {
        mem[0x00FF] = 0;
        emu_set_irq(1);
        emu_clr_irq(1);
        CHECK(mem[0x00FF] == 0x00);
    }

    TEST_CASE("T70a: set bits 0+1, clr bit 1 -> mem[0x00FF]==0x01") {
        mem[0x00FF] = 0;
        emu_set_irq(0);
        emu_set_irq(1);
        emu_clr_irq(1);
        CHECK(mem[0x00FF] == 0x01);
    }

} // TEST_SUITE("utils")
