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

    TEST_CASE("T64: Frame buffer write -- LDA #$41; STA $C000 writes to mem[0xC000]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x41, 0x8D, 0x00, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(mem[0xC000] == 0x41);
    }

    // -------------------------------------------------------------------------
    // T65: Frame buffer read
    // -------------------------------------------------------------------------

    TEST_CASE("T65: Frame buffer read -- LDA $C000 loads value preset in mem[0xC000]") {
        EmulatorFixture f;
        mem[0xC000] = 0x42;
        f.load_at(0xD000, {0xAD, 0x00, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(m6502_a(&cpu) == 0x42);
    }

    // -------------------------------------------------------------------------
    // T66: Frame buffer end boundary
    // -------------------------------------------------------------------------

    TEST_CASE("T66: Frame buffer end -- LDA #$7E; STA $C0FF writes to mem[0xC0FF]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x7E, 0x8D, 0xFF, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(mem[0xC0FF] == 0x7E);
    }

    // -------------------------------------------------------------------------
    // T67: Frame buffer write to $C005
    // -------------------------------------------------------------------------

    TEST_CASE("T67: Frame buffer write -- STA $C005 writes to mem[0xC005]") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xA9, 0x33, 0x8D, 0x05, 0xC0});
        f.set_reset_vector(0xD000);
        f.step_n(20);
        CHECK(mem[0xC005] == 0x33);
    }

    // -------------------------------------------------------------------------
    // T110: Write/read $D800 (IRQ_FLAGS device space)
    // -------------------------------------------------------------------------

    TEST_CASE("T110: Device router routes $D800 reads to IRQ_FLAGS") {
        EmulatorFixture f;
        // Inject TTY char so tty_tick sets IRQ bit 1
        tty_inject_char('A');
        // Program: LDA $D800; STA $0200; NOP
        f.load_at(0xD000, {0xAD, 0x00, 0xD8, 0x8D, 0x00, 0x02, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(40);
        // IRQ bit 1 (TTY) should be set in the value read from $D800
        CHECK((mem[0x0200] & 0x02) != 0);
    }

    // -------------------------------------------------------------------------
    // T111: IRQ_CLR() clears all flags, then devices reassert
    // -------------------------------------------------------------------------

    TEST_CASE("T111: IRQ_CLR clears custom bits, tty_tick reasserts TTY bit") {
        EmulatorFixture f;
        // Set all bits in IRQ flags
        mem[N8_IRQ_FLAGS] = 0xFF;
        f.load_at(0xD000, {0xEA});
        f.set_reset_vector(0xD000);
        // After step: IRQ_CLR zeros all flags, then tty_tick reasserts TTY bit only
        emulator_step();
        // Custom bits (not TTY) should be cleared
        CHECK((mem[N8_IRQ_FLAGS] & 0xFC) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T112: IRQ line asserted when flags non-zero after tick
    // -------------------------------------------------------------------------

    TEST_CASE("T112: IRQ line asserted when N8_IRQ_FLAGS non-zero after tick") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xEA, 0xEA, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(10); // boot
        tty_inject_char('A'); // will cause tty_tick to set IRQ bit
        emulator_step();
        CHECK((pins & M6502_IRQ) != 0);
    }

    // -------------------------------------------------------------------------
    // T113: Old $00FF no longer acts as IRQ register
    // -------------------------------------------------------------------------

    TEST_CASE("T113: Old $00FF is plain RAM, not IRQ register") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xEA, 0xEA, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(10); // boot
        // Write to old IRQ address
        mem[0x00FF] = 0xFF;
        emulator_step();
        // IRQ_CLR zeros N8_IRQ_FLAGS ($D800), not $00FF
        // The old address should still have 0xFF (plain RAM, untouched by IRQ_CLR)
        CHECK(mem[0x00FF] == 0xFF);
    }

    // -------------------------------------------------------------------------
    // T114: TTY read at old $C100 does NOT trigger tty_decode
    // -------------------------------------------------------------------------

    TEST_CASE("T114: TTY read at old $C100 does NOT trigger tty_decode") {
        EmulatorFixture f;
        tty_inject_char('X');
        // Program: LDA $C100; STA $0200; NOP
        f.load_at(0xD000, {0xAD, 0x00, 0xC1, 0x8D, 0x00, 0x02, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(40);
        // $C100 is now plain RAM (generic mem[] read), not TTY out_status
        // mem[0xC100] was zeroed by EmulatorFixture, so should be 0
        CHECK(mem[0x0200] == 0x00);
    }

    // -------------------------------------------------------------------------
    // T115: TTY read at $D820 returns OUT_STATUS ($00)
    // -------------------------------------------------------------------------

    TEST_CASE("T115: TTY read at $D820 returns OUT_STATUS") {
        EmulatorFixture f;
        // Program: LDA $D820; STA $0200; NOP
        f.load_at(0xD000, {0xAD, 0x20, 0xD8, 0x8D, 0x00, 0x02, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(40);
        // OUT_STATUS always returns 0x00 (ready)
        CHECK(mem[0x0200] == 0x00);
    }

    // -------------------------------------------------------------------------
    // T116: TTY write at $D821 sends character
    // -------------------------------------------------------------------------

    TEST_CASE("T116: TTY write at $D821 sends character (no crash)") {
        EmulatorFixture f;
        // Program: LDA #$48; STA $D821; NOP  ('H' to TTY out)
        f.load_at(0xD000, {0xA9, 0x48, 0x8D, 0x21, 0xD8, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(40);
        // putchar side effect — just verify no crash
        CHECK(true);
    }

    // -------------------------------------------------------------------------
    // T116a: tty_tick reasserts IRQ bit 1 at $D800 after IRQ_CLR
    // -------------------------------------------------------------------------

    TEST_CASE("T116a: tty_tick reasserts IRQ bit 1 at $D800 after IRQ_CLR") {
        EmulatorFixture f;
        f.load_at(0xD000, {0xEA, 0xEA, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(10); // boot
        tty_inject_char('A');
        emulator_step();
        // After IRQ_CLR + tty_tick, TTY bit should be reasserted
        CHECK((mem[N8_IRQ_FLAGS] & 0x02) != 0);
    }

} // TEST_SUITE("bus")
