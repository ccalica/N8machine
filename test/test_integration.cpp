#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("integration") {

    // -------------------------------------------------------------------------
    // T95: Boot to reset vector
    // -------------------------------------------------------------------------

    TEST_CASE("T95: Boot to reset vector -- NOP sled at 0xD000, PC lands near 0xD000") {
        EmulatorFixture f;
        // NOP sled
        f.load_at(0xD000, {
            0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
            0xEA, 0xEA, 0xEA, 0xEA, 0xEA
        });
        f.set_reset_vector(0xD000);
        f.step_n(20);
        uint16_t pc = m6502_pc(&cpu);
        CHECK(pc >= 0xD000);
        CHECK(pc < 0xD010);
    }

    // -------------------------------------------------------------------------
    // T96: Simple program
    // -------------------------------------------------------------------------

    TEST_CASE("T96: Simple program -- LDA #$42; STA $0200 writes 0x42 to mem[0x0200]") {
        EmulatorFixture f;
        // LDA #$42; STA $0200; NOP loop
        f.load_at(0xD000, {0xA9, 0x42, 0x8D, 0x00, 0x02, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(30);
        CHECK(mem[0x0200] == 0x42);
    }

    // -------------------------------------------------------------------------
    // T97: Breakpoint hit
    // -------------------------------------------------------------------------

    TEST_CASE("T97: Breakpoint hit -- BP at 0xD002, emulator reports break after stepping") {
        EmulatorFixture f;
        // LDA #$42; NOP; NOP
        f.load_at(0xD000, {0xA9, 0x42, 0xEA, 0xEA});
        f.set_reset_vector(0xD000);
        bp_mask[0xD002] = true;
        emulator_enablebp(true);
        f.step_n(30);
        CHECK(emulator_check_break() == true);
    }

    // -------------------------------------------------------------------------
    // T98: Breakpoint disabled
    // -------------------------------------------------------------------------

    TEST_CASE("T98: Breakpoint disabled -- BP set but disabled, emulator does not report break") {
        EmulatorFixture f;
        // LDA #$42; NOP; NOP
        f.load_at(0xD000, {0xA9, 0x42, 0xEA, 0xEA});
        f.set_reset_vector(0xD000);
        bp_mask[0xD002] = true;
        emulator_enablebp(false);
        f.step_n(30);
        CHECK(emulator_check_break() == false);
    }

    // -------------------------------------------------------------------------
    // T99: Frame buffer via program
    // -------------------------------------------------------------------------

    TEST_CASE("T99: Frame buffer via program -- LDA/STA to $C000/$C001 fills frame_buffer") {
        EmulatorFixture f;
        // LDA #$48; STA $C000; LDA #$69; STA $C001; NOP
        f.load_at(0xD000, {0xA9, 0x48, 0x8D, 0x00, 0xC0,
                            0xA9, 0x69, 0x8D, 0x01, 0xC0,
                            0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(40);
        CHECK(frame_buffer[0] == 0x48);
        CHECK(frame_buffer[1] == 0x69);
    }

    // -------------------------------------------------------------------------
    // T100: Breakpoint set parsing
    // -------------------------------------------------------------------------

    TEST_CASE("T100: BP set parsing -- emulator_setbp parses '$D000 $D005 $D00A' into bp_mask") {
        EmulatorFixture f;
        emulator_setbp((char*)"$D000 $D005 $D00A");
        CHECK(bp_mask[0xD000]);
        CHECK(bp_mask[0xD005]);
        CHECK(bp_mask[0xD00A]);
    }

    // -------------------------------------------------------------------------
    // T100a: Breakpoints accumulate
    // -------------------------------------------------------------------------

    TEST_CASE("T100a: BPs accumulate -- two emulator_setbp calls both take effect") {
        EmulatorFixture f;
        emulator_setbp((char*)"$D000");
        emulator_setbp((char*)"$D005");
        CHECK(bp_mask[0xD000]);
        CHECK(bp_mask[0xD005]);
    }

    // -------------------------------------------------------------------------
    // T100b: Empty BP string
    // -------------------------------------------------------------------------

    TEST_CASE("T100b: Empty BP string -- emulator_setbp('') does not crash") {
        EmulatorFixture f;
        emulator_setbp((char*)"");
        // No crash is the pass condition
        CHECK(true);
    }

    // -------------------------------------------------------------------------
    // T100d: Log breakpoints
    // -------------------------------------------------------------------------

    TEST_CASE("T100d: Log BPs -- emulator_logbp writes to console buffer") {
        EmulatorFixture f;
        stub_clear_console_buffer();
        emulator_setbp((char*)"$D000 $D005");
        emulator_logbp();
        CHECK(!stub_get_console_buffer().empty());
    }

    // -------------------------------------------------------------------------
    // T101: IRQ triggers vector
    // -------------------------------------------------------------------------

    TEST_CASE("T101: IRQ triggers vector -- tty char injected, CPU jumps to IRQ handler, A==$FF") {
        EmulatorFixture f;
        // IRQ handler at 0xD100: LDA #$FF; NOP
        f.load_at(0xD100, {0xA9, 0xFF, 0xEA});
        f.set_irq_vector(0xD100);

        // Main program at 0xD000: CLI; JMP $D001 (spin loop with IRQ enabled)
        f.load_at(0xD000, {0x58, 0x4C, 0x01, 0xD0});
        f.set_reset_vector(0xD000);

        tty_inject_char('A');
        f.step_n(100);
        CHECK(m6502_a(&cpu) == 0xFF);
    }

    // -------------------------------------------------------------------------
    // T101a: IRQ masked
    // -------------------------------------------------------------------------

    TEST_CASE("T101a: IRQ masked -- SEI keeps CPU in main program, PC stays below 0xD100") {
        EmulatorFixture f;
        // IRQ handler at 0xD100
        f.load_at(0xD100, {0xA9, 0xFF, 0xEA});
        f.set_irq_vector(0xD100);

        // Main program: SEI; NOP loop
        f.load_at(0xD000, {0x78, 0xEA, 0x4C, 0x01, 0xD0});
        f.set_reset_vector(0xD000);

        tty_inject_char('A');
        f.step_n(50);
        CHECK(m6502_pc(&cpu) < 0xD100);
    }

} // TEST_SUITE("integration")
