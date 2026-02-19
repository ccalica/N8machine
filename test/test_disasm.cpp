#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("disasm") {

    // -------------------------------------------------------------------------
    // T80: NOP
    // -------------------------------------------------------------------------

    TEST_CASE("T80: NOP -- 0xEA returns length 1 and contains NOP") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0xEA;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 1);
        CHECK(disasm_contains(buf, "NOP"));
    }

    // -------------------------------------------------------------------------
    // T81: LDA immediate
    // -------------------------------------------------------------------------

    TEST_CASE("T81: LDA immediate -- 0xA9 0x42 returns length 2 and contains LDA") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0xA9;
        mem[0x0401] = 0x42;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 2);
        CHECK(disasm_contains(buf, "LDA"));
    }

    // -------------------------------------------------------------------------
    // T82: JMP absolute
    // -------------------------------------------------------------------------

    TEST_CASE("T82: JMP absolute -- 0x4C 0x00 0xD0 returns length 3 and contains JMP") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0x4C;
        mem[0x0401] = 0x00;
        mem[0x0402] = 0xD0;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 3);
        CHECK(disasm_contains(buf, "JMP"));
    }

    // -------------------------------------------------------------------------
    // T83: STA zero-page,X
    // -------------------------------------------------------------------------

    TEST_CASE("T83: STA zero-page,X -- 0x95 0x10 returns length 2") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0x95;
        mem[0x0401] = 0x10;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 2);
    }

    // -------------------------------------------------------------------------
    // T84: LDA (indirect,X)
    // -------------------------------------------------------------------------

    TEST_CASE("T84: LDA (indirect,X) -- 0xA1 0x20 returns length 2") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0xA1;
        mem[0x0401] = 0x20;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 2);
    }

    // -------------------------------------------------------------------------
    // T85: LDA (indirect),Y
    // -------------------------------------------------------------------------

    TEST_CASE("T85: LDA (indirect),Y -- 0xB1 0x30 returns length 2") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0xB1;
        mem[0x0401] = 0x30;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 2);
    }

    // -------------------------------------------------------------------------
    // T86: BEQ relative forward
    // -------------------------------------------------------------------------

    TEST_CASE("T86: BEQ relative forward -- 0xF0 0x05 returns length 2") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0xF0;
        mem[0x0401] = 0x05;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 2);
    }

    // -------------------------------------------------------------------------
    // T89: ASL accumulator
    // -------------------------------------------------------------------------

    TEST_CASE("T89: ASL accumulator -- 0x0A returns length 1") {
        memset(mem, 0, 65536);
        mem[0x0400] = 0x0A;
        char buf[64];
        int len = emu_dis6502_decode(0x0400, buf, sizeof(buf));
        CHECK(len == 1);
    }

} // TEST_SUITE("disasm")
