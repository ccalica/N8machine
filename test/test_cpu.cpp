#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("cpu") {

    // T23: Reset vector fetch
    TEST_CASE("T23: reset vector fetch sets PC") {
        CpuFixture f;
        f.mem[0xFFFC] = 0x00;
        f.mem[0xFFFD] = 0xD0;
        f.boot();
        CHECK(f.pc() == 0xD000);
    }

    // T24: SP after reset
    TEST_CASE("T24: SP is 0xFD after reset") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.load_program(0x0400, {0xEA});
        f.boot();
        CHECK(f.s() == 0xFD);
    }

    // T25: LDA #$42
    TEST_CASE("T25: LDA immediate loads value, clears Z and N") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.load_program(0x0400, {0xA9, 0x42, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.a() == 0x42);
        CHECK(f.flag_z() == false);
        CHECK(f.flag_n() == false);
    }

    // T26: LDA #$00
    TEST_CASE("T26: LDA #$00 sets Z, clears N") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.load_program(0x0400, {0xA9, 0x00, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.a() == 0x00);
        CHECK(f.flag_z() == true);
        CHECK(f.flag_n() == false);
    }

    // T27: LDA #$80
    TEST_CASE("T27: LDA #$80 sets N, clears Z") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.load_program(0x0400, {0xA9, 0x80, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.a() == 0x80);
        CHECK(f.flag_z() == false);
        CHECK(f.flag_n() == true);
    }

    // T28: LDA zero-page
    TEST_CASE("T28: LDA zero-page loads from zero-page address") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x10] = 0x77;
        f.load_program(0x0400, {0xA5, 0x10, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.a() == 0x77);
    }

    // T28a: LDA zero-page,X
    TEST_CASE("T28a: LDA zero-page,X loads from (zp + X)") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x15] = 0x33;
        // LDX #$05; LDA $10,X
        f.load_program(0x0400, {0xA2, 0x05, 0xB5, 0x10, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x33);
    }

    // T29: LDA absolute
    TEST_CASE("T29: LDA absolute loads from 16-bit address") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x1234] = 0xAB;
        f.load_program(0x0400, {0xAD, 0x34, 0x12, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.a() == 0xAB);
    }

    // T29a: LDA absolute,X
    TEST_CASE("T29a: LDA absolute,X loads from (abs + X)") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x1236] = 0xCC;
        // LDX #$02; LDA $1234,X
        f.load_program(0x0400, {0xA2, 0x02, 0xBD, 0x34, 0x12, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0xCC);
    }

    // T29b: LDA absolute,Y
    TEST_CASE("T29b: LDA absolute,Y loads from (abs + Y)") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x1237] = 0xDD;
        // LDY #$03; LDA $1234,Y
        f.load_program(0x0400, {0xA0, 0x03, 0xB9, 0x34, 0x12, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0xDD);
    }

    // T30: STA zero-page
    TEST_CASE("T30: STA zero-page stores A to zero-page address") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$55; STA $20
        f.load_program(0x0400, {0xA9, 0x55, 0x85, 0x20, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.mem[0x20] == 0x55);
    }

    // T31: ADC no carry
    TEST_CASE("T31: ADC no carry produces correct sum") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // CLC; LDA #$10; ADC #$20
        f.load_program(0x0400, {0x18, 0xA9, 0x10, 0x69, 0x20, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x30);
        CHECK(f.flag_c() == false);
    }

    // T32: ADC carry out
    TEST_CASE("T32: ADC carry out sets C and Z on overflow to zero") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // CLC; LDA #$FF; ADC #$01
        f.load_program(0x0400, {0x18, 0xA9, 0xFF, 0x69, 0x01, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x00);
        CHECK(f.flag_c() == true);
        CHECK(f.flag_z() == true);
    }

    // T33: ADC overflow
    TEST_CASE("T33: ADC signed overflow sets V and N") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // CLC; LDA #$7F; ADC #$01
        f.load_program(0x0400, {0x18, 0xA9, 0x7F, 0x69, 0x01, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x80);
        CHECK(f.flag_v() == true);
        CHECK(f.flag_n() == true);
    }

    // T34: SBC immediate
    TEST_CASE("T34: SBC immediate subtracts with borrow clear") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEC; LDA #$50; SBC #$20
        f.load_program(0x0400, {0x38, 0xA9, 0x50, 0xE9, 0x20, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x30);
        CHECK(f.flag_c() == true);
    }

    // T35: INX/DEX
    TEST_CASE("T35: INX and DEX adjust X correctly") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDX #$05; INX; DEX; DEX
        f.load_program(0x0400, {0xA2, 0x05, 0xE8, 0xCA, 0xCA, 0xEA});
        f.boot();
        f.run_instructions(5);
        CHECK(f.x() == 0x04);
    }

    // T36: INY/DEY
    TEST_CASE("T36: INY and DEY adjust Y correctly") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDY #$03; INY; DEY; DEY
        f.load_program(0x0400, {0xA0, 0x03, 0xC8, 0x88, 0x88, 0xEA});
        f.boot();
        f.run_instructions(5);
        CHECK(f.y() == 0x02);
    }

    // T37: TAX/TXA
    TEST_CASE("T37: TAX transfers A to X; TXA transfers X back to A") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$AA; TAX; LDA #$00; TXA
        f.load_program(0x0400, {0xA9, 0xAA, 0xAA, 0xA9, 0x00, 0x8A, 0xEA});
        f.boot();
        f.run_instructions(5);
        CHECK(f.a() == 0xAA);
        CHECK(f.x() == 0xAA);
    }

    // T38: PHA/PLA
    TEST_CASE("T38: PHA pushes A to stack; PLA restores it") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$BB; PHA; LDA #$00; PLA
        f.load_program(0x0400, {0xA9, 0xBB, 0x48, 0xA9, 0x00, 0x68, 0xEA});
        f.boot();
        f.run_instructions(5);
        CHECK(f.a() == 0xBB);
    }

    // T39: JSR/RTS
    TEST_CASE("T39: JSR jumps to subroutine; RTS returns to caller+1") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // 0x0400: JSR $0410; NOP (at 0x0403)
        f.load_program(0x0400, {0x20, 0x10, 0x04, 0xEA});
        // 0x0410: RTS
        f.load_program(0x0410, {0x60});
        f.boot();
        // boot() reaches first instruction (JSR at 0x0400)
        // run_instructions(3): JSR, RTS, NOP at 0x0403
        f.run_instructions(3);
        CHECK(f.pc() == 0x0404);
    }

    // T40: JMP absolute
    TEST_CASE("T40: JMP absolute transfers control to target address") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // 0x0400: JMP $0500
        f.load_program(0x0400, {0x4C, 0x00, 0x05});
        // 0x0500: LDA #$99; NOP
        f.load_program(0x0500, {0xA9, 0x99, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x99);
    }

    // T41: BEQ taken
    TEST_CASE("T41: BEQ taken skips over two bytes when Z=1") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$00 (sets Z); BEQ +2 (skip LDA #$FF); LDA #$42; NOP
        f.load_program(0x0400, {0xA9, 0x00, 0xF0, 0x02, 0xA9, 0xFF, 0xA9, 0x42, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x42);
    }

    // T42: BEQ not taken
    TEST_CASE("T42: BEQ not taken falls through when Z=0") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$01 (clears Z); BEQ +2 (not taken); LDA #$55; NOP
        f.load_program(0x0400, {0xA9, 0x01, 0xF0, 0x02, 0xA9, 0x55, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x55);
    }

    // T43: BNE taken
    TEST_CASE("T43: BNE taken skips over two bytes when Z=0") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$01 (clears Z); BNE +2 (skip LDA #$FF); LDA #$42; NOP
        f.load_program(0x0400, {0xA9, 0x01, 0xD0, 0x02, 0xA9, 0xFF, 0xA9, 0x42, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x42);
    }

    // T44: CMP equal
    TEST_CASE("T44: CMP equal sets Z=1 and C=1") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$50; CMP #$50
        f.load_program(0x0400, {0xA9, 0x50, 0xC9, 0x50, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.flag_z() == true);
        CHECK(f.flag_c() == true);
    }

    // T45: CMP less than
    TEST_CASE("T45: CMP less than clears Z and C") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$10; CMP #$50
        f.load_program(0x0400, {0xA9, 0x10, 0xC9, 0x50, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.flag_z() == false);
        CHECK(f.flag_c() == false);
    }

    // T46: AND immediate
    TEST_CASE("T46: AND immediate masks A correctly") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$FF; AND #$0F
        f.load_program(0x0400, {0xA9, 0xFF, 0x29, 0x0F, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x0F);
    }

    // T47: ORA immediate
    TEST_CASE("T47: ORA immediate ORs bits into A") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$F0; ORA #$0F
        f.load_program(0x0400, {0xA9, 0xF0, 0x09, 0x0F, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0xFF);
    }

    // T48: EOR immediate
    TEST_CASE("T48: EOR immediate XORs bits in A") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$FF; EOR #$AA
        f.load_program(0x0400, {0xA9, 0xFF, 0x49, 0xAA, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x55);
    }

    // T49: ASL accumulator
    TEST_CASE("T49: ASL accumulator shifts left, old bit 7 into C") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$81; ASL A
        f.load_program(0x0400, {0xA9, 0x81, 0x0A, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x02);
        CHECK(f.flag_c() == true);
    }

    // T50: LSR accumulator
    TEST_CASE("T50: LSR accumulator shifts right, old bit 0 into C") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // LDA #$03; LSR A
        f.load_program(0x0400, {0xA9, 0x03, 0x4A, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x01);
        CHECK(f.flag_c() == true);
    }

    // T51: ROL accumulator
    TEST_CASE("T51: ROL rotates A left through carry") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEC; LDA #$80; ROL A
        f.load_program(0x0400, {0x38, 0xA9, 0x80, 0x2A, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x01);
        CHECK(f.flag_c() == true);
    }

    // T52: ROR accumulator
    TEST_CASE("T52: ROR rotates A right through carry") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEC; LDA #$01; ROR A
        f.load_program(0x0400, {0x38, 0xA9, 0x01, 0x6A, 0xEA});
        f.boot();
        f.run_instructions(4);
        CHECK(f.a() == 0x80);
        CHECK(f.flag_c() == true);
    }

    // T53: INC zero-page
    TEST_CASE("T53: INC zero-page increments memory twice wrapping to 0x00") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x30] = 0xFE;
        // INC $30; INC $30
        f.load_program(0x0400, {0xE6, 0x30, 0xE6, 0x30, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.mem[0x30] == 0x00);
    }

    // T54: DEC zero-page
    TEST_CASE("T54: DEC zero-page decrements memory to 0x00") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        f.mem[0x30] = 0x01;
        // DEC $30
        f.load_program(0x0400, {0xC6, 0x30, 0xEA});
        f.boot();
        f.run_instructions(2);
        CHECK(f.mem[0x30] == 0x00);
    }

    // T55: LDA (indirect,X)
    TEST_CASE("T55: LDA (indirect,X) fetches via zero-page pointer indexed by X") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // Pointer at 0x14 -> 0x0300
        f.mem[0x14] = 0x00;
        f.mem[0x15] = 0x03;
        f.mem[0x0300] = 0x77;
        // LDX #$04; LDA ($10,X)
        f.load_program(0x0400, {0xA2, 0x04, 0xA1, 0x10, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x77);
    }

    // T56: LDA (indirect),Y
    TEST_CASE("T56: LDA (indirect),Y fetches via zero-page pointer then adds Y") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // Pointer at 0x20 -> 0x0300
        f.mem[0x20] = 0x00;
        f.mem[0x21] = 0x03;
        f.mem[0x0302] = 0x88;
        // LDY #$02; LDA ($20),Y
        f.load_program(0x0400, {0xA0, 0x02, 0xB1, 0x20, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.a() == 0x88);
    }

    // T58: CLI/SEI
    TEST_CASE("T58: SEI sets I; CLI clears I") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEI; CLI
        f.load_program(0x0400, {0x78, 0x58, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.flag_i() == false);
    }

    // T59: CLC/SEC
    TEST_CASE("T59: SEC sets C; CLC clears C") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEC; CLC
        f.load_program(0x0400, {0x38, 0x18, 0xEA});
        f.boot();
        f.run_instructions(3);
        CHECK(f.flag_c() == false);
    }

    // T60: PHP/PLP
    TEST_CASE("T60: PHP saves P to stack; PLP restores it including C") {
        CpuFixture f;
        f.set_reset_vector(0x0400);
        // SEC; PHP; CLC; PLP
        f.load_program(0x0400, {0x38, 0x08, 0x18, 0x28, 0xEA});
        f.boot();
        f.run_instructions(5);
        CHECK(f.flag_c() == true);
    }

} // TEST_SUITE("cpu")
