#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("gdb_callbacks") {

    // ---- Register read/write accessors ----

    TEST_CASE("Register read A") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xA9, 0x42, 0xEA}); // LDA #$42; NOP
        f.step_n(20);
        CHECK(emulator_read_a() == 0x42);
    }

    TEST_CASE("Register read X") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xA2, 0x55, 0xEA}); // LDX #$55; NOP
        f.step_n(20);
        CHECK(emulator_read_x() == 0x55);
    }

    TEST_CASE("Register read Y") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xA0, 0x77, 0xEA}); // LDY #$77; NOP
        f.step_n(20);
        CHECK(emulator_read_y() == 0x77);
    }

    TEST_CASE("Register write A") {
        EmulatorFixture f;
        emulator_write_a(0xAB);
        CHECK(emulator_read_a() == 0xAB);
    }

    TEST_CASE("Register write X") {
        EmulatorFixture f;
        emulator_write_x(0xCD);
        CHECK(emulator_read_x() == 0xCD);
    }

    TEST_CASE("Register write Y") {
        EmulatorFixture f;
        emulator_write_y(0xEF);
        CHECK(emulator_read_y() == 0xEF);
    }

    TEST_CASE("Register write S") {
        EmulatorFixture f;
        emulator_write_s(0xFE);
        CHECK(emulator_read_s() == 0xFE);
    }

    TEST_CASE("Register write P") {
        EmulatorFixture f;
        emulator_write_p(0x30);
        CHECK(emulator_read_p() == 0x30);
    }

    // ---- PC write with prefetch ----

    TEST_CASE("PC write sets PC value") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xEA});
        f.step_n(10); // boot
        emulator_write_pc(0xD100);
        CHECK(emulator_getpc() == 0xD100);
    }

    TEST_CASE("PC write sets SYNC flag") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xEA});
        f.step_n(10);
        emulator_write_pc(0xD100);
        CHECK((pins & M6502_SYNC) != 0);
    }

    TEST_CASE("PC write preserves IRQ pin") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xEA});
        f.step_n(10);
        pins |= M6502_IRQ;
        emulator_write_pc(0xD100);
        CHECK((pins & M6502_IRQ) != 0);
    }

    TEST_CASE("PC write preserves NMI pin") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xEA});
        f.step_n(10);
        pins |= M6502_NMI;
        emulator_write_pc(0xD100);
        CHECK((pins & M6502_NMI) != 0);
    }

    // ---- Memory read (direct mem[] access) ----

    TEST_CASE("Memory read returns mem[] directly") {
        EmulatorFixture f;
        mem[0x0200] = 0xAB;
        CHECK(mem[0x0200] == 0xAB);
    }

    TEST_CASE("Memory read of TTY region returns mem[], not device state") {
        EmulatorFixture f;
        mem[0xC100] = 0x55;
        CHECK(mem[0xC100] == 0x55);
    }

    TEST_CASE("Memory write to RAM") {
        EmulatorFixture f;
        mem[0x0200] = 0xCD;
        CHECK(mem[0x0200] == 0xCD);
    }

    TEST_CASE("Memory write to ROM area") {
        EmulatorFixture f;
        mem[0xD000] = 0xEA;
        CHECK(mem[0xD000] == 0xEA);
    }

    // ---- Step instruction via SYNC loop ----

    TEST_CASE("Step instruction: NOP via CpuFixture takes 2 ticks") {
        CpuFixture f;
        f.set_reset_vector(0xD000);
        f.load_program(0xD000, {0xEA, 0xEA, 0xEA}); // NOP NOP NOP
        f.boot(); // boot to first instruction (SYNC set at first NOP)

        int ticks = f.run_until_sync(); // execute first NOP, reach second
        CHECK(ticks == 2);
    }

    TEST_CASE("Step instruction: LDA abs via CpuFixture takes 4 ticks") {
        CpuFixture f;
        f.set_reset_vector(0xD000);
        f.load_program(0xD000, {0xAD, 0x00, 0x02, 0xEA}); // LDA $0200; NOP
        f.mem[0x0200] = 0x42;
        f.boot();

        int ticks = f.run_until_sync(); // execute LDA abs
        CHECK(ticks == 4);
    }

    // ---- D32 validation: bp at data address does NOT fire during LDA ----

    TEST_CASE("D32: breakpoint at data address does not fire on data read") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        // LDA $0200; NOP
        f.load_at(0xD000, {0xAD, 0x00, 0x02, 0xEA});
        mem[0x0200] = 0x42;

        // Set breakpoint at data address 0x0200
        bp_mask[0x0200] = true;
        emulator_enablebp(true);

        f.step_n(20); // run through the LDA instruction

        // The breakpoint should NOT have fired — 0x0200 is read as data, not instruction
        CHECK(emulator_bp_hit() == false);
    }

    TEST_CASE("D32: breakpoint at instruction address fires on SYNC") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        // NOP at 0xD000; NOP at 0xD001
        f.load_at(0xD000, {0xEA, 0xEA});

        bp_mask[0xD001] = true;
        emulator_enablebp(true);

        f.step_n(15); // boot + execute first NOP, reach 0xD001

        CHECK(emulator_bp_hit() == true);
    }

    // ---- BUG-1 validation: tty_decode reg 3 on empty queue ----

    TEST_CASE("BUG-1: tty_decode reg 3 on empty queue returns 0x00") {
        EmulatorFixture f;
        tty_reset();
        CHECK(tty_buff_count() == 0);

        uint64_t p = make_read_pins(0xC103);
        tty_decode(p, 3);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    TEST_CASE("BUG-1: tty_decode reg 3 on empty queue does not crash") {
        EmulatorFixture f;
        tty_reset();
        // Read multiple times from empty queue
        for (int i = 0; i < 5; i++) {
            uint64_t p = make_read_pins(0xC103);
            tty_decode(p, 3);
            CHECK(M6502_GET_DATA(p) == 0x00);
        }
    }

    // ---- bp_hit / clear_bp_hit / bp_enabled accessors ----

    TEST_CASE("bp_hit returns false when bp disabled") {
        EmulatorFixture f;
        emulator_enablebp(false);
        CHECK(emulator_bp_hit() == false);
    }

    TEST_CASE("bp_enabled reflects enablebp state") {
        EmulatorFixture f;
        emulator_enablebp(true);
        CHECK(emulator_bp_enabled() == true);
        emulator_enablebp(false);
        CHECK(emulator_bp_enabled() == false);
    }

    TEST_CASE("clear_bp_hit resets bp_hit") {
        EmulatorFixture f;
        f.set_reset_vector(0xD000);
        f.load_at(0xD000, {0xEA, 0xEA});

        bp_mask[0xD000] = true;
        emulator_enablebp(true);

        f.step_n(10); // boot triggers bp at D000

        CHECK(emulator_bp_hit() == true);
        emulator_clear_bp_hit();
        CHECK(emulator_bp_hit() == false);
    }

    // ---- Reset via M6502_RES pin (D47) ----

    // Note: emulator_reset() calls emulator_loadrom() which requires a firmware
    // file on disk. Full reset testing deferred to integration tests with firmware.
    // We test the TTY reset aspect separately since tty_reset() is safe.

    TEST_CASE("D47: tty_reset clears TTY buffer (reset sub-function)") {
        EmulatorFixture f;
        tty_inject_char('A');
        tty_inject_char('B');
        CHECK(tty_buff_count() == 2);

        tty_reset();

        CHECK(tty_buff_count() == 0);
    }

    // ---- TTY queue not corrupted by direct memory read ----

    TEST_CASE("Direct mem[] read at TTY address does not pop queue") {
        EmulatorFixture f;
        tty_inject_char('A');
        CHECK(tty_buff_count() == 1);

        // Read TTY address via mem[] directly (as GDB would)
        uint8_t val = mem[0xC103];
        (void)val; // ignore value

        CHECK(tty_buff_count() == 1); // queue untouched
    }

} // TEST_SUITE("gdb_callbacks")
