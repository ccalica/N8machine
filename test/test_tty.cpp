#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("tty") {

    // -------------------------------------------------------------------------
    // T71: Read Out Status (reg 0) -- always ready, returns 0x00
    // -------------------------------------------------------------------------

    TEST_CASE("T71: Read Out Status (reg 0) returns 0x00") {
        tty_reset();
        mem[0x00FF] = 0;
        uint64_t p = make_read_pins(0xC100);
        tty_decode(p, 0);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T72: Read Out Data (reg 1) -- should not happen, returns 0xFF
    // -------------------------------------------------------------------------

    TEST_CASE("T72: Read Out Data (reg 1) returns 0xFF") {
        tty_reset();
        mem[0x00FF] = 0;
        uint64_t p = make_read_pins(0xC101);
        tty_decode(p, 1);
        CHECK(M6502_GET_DATA(p) == 0xFF);
    }

    // -------------------------------------------------------------------------
    // T73: Read In Status (reg 2) -- buffer empty returns 0x00
    // -------------------------------------------------------------------------

    TEST_CASE("T73: Read In Status empty (reg 2) returns 0x00") {
        tty_reset();
        mem[0x00FF] = 0;
        uint64_t p = make_read_pins(0xC102);
        tty_decode(p, 2);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T74: Read In Status (reg 2) -- buffer has data returns 0x01
    // -------------------------------------------------------------------------

    TEST_CASE("T74: Read In Status with data (reg 2) returns 0x01") {
        tty_reset();
        mem[0x00FF] = 0;
        tty_inject_char('A');
        uint64_t p = make_read_pins(0xC102);
        tty_decode(p, 2);
        CHECK(M6502_GET_DATA(p) == 0x01);
    }

    // -------------------------------------------------------------------------
    // T75: Read In Data (reg 3) -- returns injected char, drains buffer
    // -------------------------------------------------------------------------

    TEST_CASE("T75: Read In Data (reg 3) returns injected char and drains buffer") {
        tty_reset();
        mem[0x00FF] = 0;
        tty_inject_char(0x41);
        uint64_t p = make_read_pins(0xC103);
        tty_decode(p, 3);
        CHECK(M6502_GET_DATA(p) == 0x41);
        CHECK(tty_buff_count() == 0);
    }

    // -------------------------------------------------------------------------
    // T76: Read In Data clears IRQ bit 1 when buffer drains
    // -------------------------------------------------------------------------

    TEST_CASE("T76: Read In Data (reg 3) clears IRQ bit 1 after buffer drains") {
        tty_reset();
        mem[0x00FF] = 0;
        tty_inject_char('X');
        emu_set_irq(1);
        uint64_t p = make_read_pins(0xC103);
        tty_decode(p, 3);
        CHECK((mem[0x00FF] & 0x02) == 0);
    }

    // -------------------------------------------------------------------------
    // T77: Write Out Data (reg 1) -- putchar side effect, no crash
    // -------------------------------------------------------------------------

    TEST_CASE("T77: Write Out Data (reg 1) does not crash") {
        tty_reset();
        mem[0x00FF] = 0;
        uint64_t write_p = make_write_pins(0xC101, 'H');
        tty_decode(write_p, 1);
        // putchar side effect is acceptable; just verify no crash
        CHECK(true);
    }

    // -------------------------------------------------------------------------
    // T78: Write to read-only regs (0, 2, 3) -- no crash
    // -------------------------------------------------------------------------

    TEST_CASE("T78: Write to read-only regs 0, 2, 3 does not crash") {
        tty_reset();
        mem[0x00FF] = 0;
        uint64_t p0 = make_write_pins(0xC100, 0xAA);
        tty_decode(p0, 0);
        uint64_t p2 = make_write_pins(0xC102, 0xBB);
        tty_decode(p2, 2);
        uint64_t p3 = make_write_pins(0xC103, 0xCC);
        tty_decode(p3, 3);
        CHECK(true);
    }

    // -------------------------------------------------------------------------
    // T78a: Read TTY phantom addresses (regs 4-15) -- return 0x00, no crash
    // -------------------------------------------------------------------------

    TEST_CASE("T78a: Read TTY phantom addresses (regs 4-15) return 0x00") {
        tty_reset();
        mem[0x00FF] = 0;
        for (uint8_t reg = 4; reg <= 15; reg++) {
            uint64_t p = make_read_pins(0xC100 + reg);
            tty_decode(p, reg);
            CHECK(M6502_GET_DATA(p) == 0x00);
        }
    }

    // -------------------------------------------------------------------------
    // T79: tty_reset clears the buffer
    // -------------------------------------------------------------------------

    TEST_CASE("T79: tty_reset clears buffer after injecting chars") {
        tty_reset();
        mem[0x00FF] = 0;
        tty_inject_char('A');
        tty_inject_char('B');
        tty_reset();
        CHECK(tty_buff_count() == 0);
    }

} // TEST_SUITE("tty")
