#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("bus") {

    // -------------------------------------------------------------------------
    // T62: RAM write
    // -------------------------------------------------------------------------

    TEST_CASE("T62: RAM write -- LDA #$55; STA $0200 writes to mem[0x0200]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x55, 0x8D, 0x00, 0x02});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(mem[0x0200] == 0x55);
    }

    // -------------------------------------------------------------------------
    // T63: RAM read
    // -------------------------------------------------------------------------

    TEST_CASE("T63: RAM read -- LDA $0200 loads value preset in mem[0x0200]") {
        EmulatorFixture f;
        mem[0x0200] = 0xAA;
        f.load_at(0xD000, {0xAD, 0x00, 0x02});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(m6502_a(&cpu) == 0xAA);
    }

    // -------------------------------------------------------------------------
    // T64: Frame buffer write
    // -------------------------------------------------------------------------

    TEST_CASE("T64: Frame buffer write -- LDA #$41; STA $C000 writes to frame_buffer[0]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x41, 0x8D, 0x00, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(frame_buffer[0] == 0x41);
    }

    // -------------------------------------------------------------------------
    // T65: Frame buffer read
    // -------------------------------------------------------------------------

    TEST_CASE("T65: Frame buffer read -- LDA $C000 loads value preset in frame_buffer[0]") {
        EmulatorFixture f;
        frame_buffer[0] = 0x42;
        f.load_at(0xD000, {0xAD, 0x00, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(m6502_a(&cpu) == 0x42);
    }

    // -------------------------------------------------------------------------
    // T66: Frame buffer end boundary
    // -------------------------------------------------------------------------

    TEST_CASE("T66: Frame buffer end -- LDA #$7E; STA $C0FF writes to frame_buffer[0xFF]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x7E, 0x8D, 0xFF, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(frame_buffer[0xFF] == 0x7E);
    }

    // -------------------------------------------------------------------------
    // T67: $C100 not mapped to frame buffer
    // -------------------------------------------------------------------------

    TEST_CASE("T67: $C100 not frame buffer -- STA $C100 does not touch frame_buffer[]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x99, 0x8D, 0x00, 0xC1});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        bool all_zero = true;
        for (int i = 0; i < 256; i++) {
            if (frame_buffer[i] != 0) {
                all_zero = false;
                break;
            }
        }
        CHECK(all_zero);
    }

    // -------------------------------------------------------------------------
    // T68: Write hits both mem[] and frame_buffer[]
    // -------------------------------------------------------------------------

    TEST_CASE("T68: Write hits both mem and device -- STA $C005 writes mem[0xC005] and frame_buffer[5]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x33, 0x8D, 0x05, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(mem[0xC005] == 0x33);
        CHECK(frame_buffer[5] == 0x33);
    }

} // TEST_SUITE("bus")
