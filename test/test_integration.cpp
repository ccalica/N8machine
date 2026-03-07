#include "doctest.h"
#include "test_helpers.h"

extern const char *kernel_file;
extern const char *shell_file;

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

    TEST_CASE("T99: Frame buffer via program -- LDA/STA to $C000/$C001 writes frame_buffer[]") {
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

    // -------------------------------------------------------------------------
    // T170: emulator_loadrom places 8KB binary at $E000
    // -------------------------------------------------------------------------

    TEST_CASE("T170: emulator_loadrom places kernel at $F000 and monitor at $E000") {
        EmulatorFixture f;
        const char *old_kernel = kernel_file;
        const char *old_monitor = shell_file;
        kernel_file = "/tmp/n8_test_kernel_170.bin";
        shell_file = "/tmp/n8_test_monitor_170.bin";

        // Write a small kernel ROM: 16 bytes at $F000
        FILE *fp = fopen(kernel_file, "w");
        REQUIRE(fp != nullptr);
        uint8_t kern_data[] = {0xA9, 0x42, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
                               0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA};
        fwrite(kern_data, 1, sizeof(kern_data), fp);
        fclose(fp);

        // Write a small monitor ROM: 16 bytes at $E000
        fp = fopen(shell_file, "w");
        REQUIRE(fp != nullptr);
        uint8_t mon_data[] = {0xA2, 0x55, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
                              0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA};
        fwrite(mon_data, 1, sizeof(mon_data), fp);
        fclose(fp);

        emulator_loadrom();
        CHECK(mem[0xF000] == 0xA9);
        CHECK(mem[0xF001] == 0x42);
        CHECK(mem[0xE000] == 0xA2);
        CHECK(mem[0xE001] == 0x55);

        kernel_file = old_kernel;
        shell_file = old_monitor;
    }

    // -------------------------------------------------------------------------
    // T171: Code at $E000 is executable
    // -------------------------------------------------------------------------

    TEST_CASE("T171: Code at $E000 is executable") {
        EmulatorFixture f;
        // LDA #$42; STA $0200; NOP sled
        f.load_at(0xE000, {0xA9, 0x42, 0x8D, 0x00, 0x02, 0xEA, 0xEA, 0xEA});
        f.set_reset_vector(0xE000);
        f.step_n(30);
        CHECK(mem[0x0200] == 0x42);
    }

    // -------------------------------------------------------------------------
    // T172: Reset vector at $FFFC-$FFFD works from ROM region
    // -------------------------------------------------------------------------

    TEST_CASE("T172: Reset vector in ROM region works") {
        EmulatorFixture f;
        f.load_at(0xE000, {0xEA, 0xEA, 0xEA, 0xEA, 0xEA});
        f.set_reset_vector(0xE000);
        f.step_n(20);
        uint16_t pc = m6502_pc(&cpu);
        CHECK(pc >= 0xE000);
        CHECK(pc < 0xE010);
    }

    // -------------------------------------------------------------------------
    // T173: RAM at $0400 is writable
    // -------------------------------------------------------------------------

    TEST_CASE("T173: RAM at $0400 is writable") {
        EmulatorFixture f;
        // LDA #$55; STA $0400; NOP
        f.load_at(0xE000, {0xA9, 0x55, 0x8D, 0x00, 0x04, 0xEA});
        f.set_reset_vector(0xE000);
        f.step_n(30);
        CHECK(mem[0x0400] == 0x55);
    }

    // -------------------------------------------------------------------------
    // T174: Oversized ROM is truncated to 8KB at $E000
    // -------------------------------------------------------------------------

    TEST_CASE("T174: Oversized kernel is truncated to 4KB at $F000") {
        EmulatorFixture f;
        const char *old_kernel = kernel_file;
        const char *old_monitor = shell_file;
        kernel_file = "/tmp/n8_test_kernel_174.bin";
        shell_file = "/tmp/n8_test_monitor_174_empty.bin";

        // Write an 8KB kernel (oversized — should truncate to 4KB)
        FILE *fp = fopen(kernel_file, "w");
        REQUIRE(fp != nullptr);
        uint8_t byte = 0xEA;
        for (int i = 0; i < 8192; i++) fwrite(&byte, 1, 1, fp);
        // Put marker at start
        fseek(fp, 0, SEEK_SET);
        uint8_t marker[] = {0xA9, 0xFF};
        fwrite(marker, 1, 2, fp);
        fclose(fp);

        // Write an empty monitor so load doesn't error
        fp = fopen(shell_file, "w");
        REQUIRE(fp != nullptr);
        fclose(fp);

        emulator_loadrom();
        // Kernel loads at $F000, truncated to 4KB
        CHECK(mem[0xF000] == 0xA9);
        CHECK(mem[0xF001] == 0xFF);
        // $E000 should NOT have kernel overflow
        CHECK(mem[0xE000] == 0x00);

        kernel_file = old_kernel;
        shell_file = old_monitor;
    }

    // -------------------------------------------------------------------------
    // T175: CPU write to $E000 is silently ignored (ROM protection)
    // -------------------------------------------------------------------------

    TEST_CASE("T175: CPU write to $E000 silently ignored") {
        EmulatorFixture f;
        mem[0xE000] = 0xEA; // preset to NOP
        // Program at $D000: LDA #$FF; STA $E000; NOP
        f.load_at(0xD000, {0xA9, 0xFF, 0x8D, 0x00, 0xE0, 0xEA});
        f.set_reset_vector(0xD000);
        f.step_n(30);
        // CPU write should be ignored — mem[0xE000] should still be NOP
        CHECK(mem[0xE000] == 0xEA);
    }

    // -------------------------------------------------------------------------
    // T176: Dev bank $D000-$D7FF is writable RAM
    // -------------------------------------------------------------------------

    TEST_CASE("T176: Dev bank $D000-$D7FF is writable RAM") {
        EmulatorFixture f;
        // Program at $E000: LDA #$AA; STA $D000; LDA #$BB; STA $D7FF; NOP
        f.load_at(0xE000, {0xA9, 0xAA, 0x8D, 0x00, 0xD0,
                            0xA9, 0xBB, 0x8D, 0xFF, 0xD7,
                            0xEA});
        f.set_reset_vector(0xE000);
        f.step_n(40);
        CHECK(mem[0xD000] == 0xAA);
        CHECK(mem[0xD7FF] == 0xBB);
    }

} // TEST_SUITE("integration")
