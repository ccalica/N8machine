# N8Machine Test Spec -- Agent B: C++ Emulator Test Strategy

## 1. Testability Analysis

### 1.1 Source File Dependency Map

| Source File | ImGui/SDL Dependency | Testable in Isolation | Notes |
|---|---|---|---|
| `utils.cpp` | None | YES | Pure functions. No global state. Ideal unit test target. |
| `emu_labels.cpp` | None (but calls `gui_con_printmsg`) | PARTIAL | `emu_labels_load()` reads file, `emu_labels_add/get/clear` are pure data ops. `emu_labels_console_list()` calls `gui_con_printmsg` -- needs stub. |
| `emu_tty.cpp` | None | PARTIAL | `tty_decode()` is bus-level logic, testable with mock pins. `tty_tick()` calls `tty_kbhit()`/`getch()` (terminal I/O) and `emu_set_irq` -- needs decoupling. `tty_init()` calls `set_conio()` (terminal raw mode) -- skip in tests. |
| `emulator.cpp` | `#include "imgui.h"` at top; `emulator_show_*` functions use ImGui | PARTIAL | Core logic (`emulator_step`, `emulator_reset`, `emulator_setbp`, breakpoint logic, bus decode) has zero ImGui calls. The three `emulator_show_*` functions are pure GUI. The `#define CHIPS_IMPL` here instantiates the m6502 implementation. |
| `emu_dis6502.cpp` | `#include "../imgui/imgui.h"`; `emu_dis6502_window()` uses ImGui | PARTIAL | `emu_dis6502_decode()` is pure computation on `mem[]` -- fully testable. `emu_dis6502_window()` is GUI-only. |
| `gui_console.cpp` | `#include "../imgui/imgui.h"` | MINIMAL | `gui_con_printmsg()` is just a deque push -- trivially testable/stubbable. `gui_show_console_window()` is all ImGui. |
| `machine.h` | None | YES | Single constant (`total_memory = 65536`). |
| `m6502.h` | None | YES | Header-only CPU library. Instantiate with `#define CHIPS_IMPL`. Needs only a tick loop and memory array. |
| `main.cpp` | Full SDL/ImGui/OpenGL | NO | Application entry point. 100% GUI. Not a test target. |

### 1.2 Global State Inventory

The emulator uses file-scope globals in `emulator.cpp`. Tests must manage these directly:

| Global | Type | Declared In | Test Impact |
|---|---|---|---|
| `mem[65536]` | `uint8_t[]` | `emulator.cpp` (extern in `emulator.h`) | Main memory. Tests write programs here, read results. |
| `frame_buffer[256]` | `uint8_t[]` | `emulator.cpp` | Device buffer at 0xC000-0xC0FF. Not externed -- needs extern or accessor. |
| `cpu` | `m6502_t` | `emulator.cpp` | CPU state. Not externed -- tests need access for register assertions. |
| `pins` | `uint64_t` | `emulator.cpp` | Bus pin state. Not externed. |
| `bp_mask[65536]` | `bool[]` | `emulator.cpp` (extern in `emulator.h`) | Breakpoint mask. Already accessible. |
| `bp_enable` | `bool` | `emulator.cpp` | Breakpoint enable flag. Accessible via `emulator_enablebp()`. |
| `bp_hit` | `bool` | `emulator.cpp` | Accessible via `emulator_check_break()`. |
| `tick_count` | `uint64_t` | `emulator.cpp` | Not externed. |
| `cur_instruction` | `uint16_t` | `emulator.cpp` | Accessible via `emulator_getci()`. |
| `labels[65536]` | `list<string>[]` | `emu_labels.cpp` | Accessible via `emu_labels_get()`. |
| `tty_buff` | `queue<uint8_t>` | `emu_tty.cpp` | Internal TTY buffer. Not accessible -- needs accessor or extern. |

### 1.3 Coupling Summary

**Hard couplings to break for test binary:**
1. `emulator.cpp` line 2: `#include "imgui.h"` -- needed only by `emulator_show_*` functions.
2. `gui_console.cpp` line 3: `#include "../imgui/imgui.h"` -- needed only by `gui_show_console_window()`.
3. `emu_dis6502.cpp` line 13: `#include "../imgui/imgui.h"` -- needed only by `emu_dis6502_window()`.
4. `emulator_loadrom()` reads `"N8firmware"` from CWD -- tests must either provide the file or bypass.
5. `tty_init()` calls `set_conio()` which modifies terminal settings -- must not run in test.
6. `emu_labels_init()` calls `emu_labels_load()` which reads `"N8firmware.sym"` from CWD.

---

## 2. Test Categories

### 2.1 CPU Instruction Tests

Test the m6502.h CPU library directly, independent of the N8Machine emulator layer. Create a minimal test harness: `m6502_t cpu`, `uint8_t mem[65536]`, a simple tick loop that services reads/writes. No bus decode, no devices.

**What to test:**
- Correct execution of opcodes (result in A/X/Y, correct flags)
- All addressing modes (immediate, zero-page, zero-page-X/Y, absolute, absolute-X/Y, indirect-X, indirect-Y, relative)
- Flag behavior (C, Z, N, V for arithmetic; Z, N for loads/logic)
- Stack operations (PHA/PLA/PHP/PLP, JSR/RTS push/pop)
- Branch instructions (taken vs not-taken, forward and backward)
- Reset vector fetch (7-tick reset sequence loads PC from 0xFFFC/0xFFFD)

**Approach:** Write a small program into `mem[]`, set the reset vector to point at it, init CPU, tick until instruction completes (check M6502_SYNC pin for new instruction boundary), assert registers/flags/memory.

### 2.2 Bus Decode Tests

Test the address decode logic in `emulator_step()`. The key construct is:

```c
#define BUS_DECODE(bus, base, mask) if((bus & mask) == base)
```

Decode regions:
- `0x0000-0x00FF`: Zero page monitoring (no side effects in current code)
- `0xC000-0xC0FF`: Frame buffer (`frame_buffer[]` array, separate from `mem[]`)
- `0xC100-0xC10F`: TTY device (calls `tty_decode()`)
- `0xFFF0-0xFFFF`: Vector region logging
- Everything else: plain RAM read/write in `mem[]`

**What to test:**
- CPU write to 0xC000-0xC0FF lands in `frame_buffer[]`, not just `mem[]`
- CPU read from 0xC000-0xC0FF returns `frame_buffer[]` content (overrides RAM)
- CPU access to 0xC100-0xC10F triggers TTY decode
- Normal RAM access at non-device addresses works as expected
- Bus decode mask correctness (0xFF00 for frame buffer means 0xC000-0xC0FF; 0xFFF0 for TTY means 0xC100-0xC10F)

**NOTE on bus decode order:** In `emulator_step()`, RAM read/write happens FIRST (lines 113-119), THEN device decode overlays. For reads, devices call `M6502_SET_DATA()` to override the RAM value already placed on the data bus. For writes, data goes to both `mem[]` and the device. This is the actual hardware behavior to verify.

### 2.3 Device Tests

**Frame Buffer:**
- Write byte to address in 0xC000-0xC0FF range, verify `frame_buffer[addr - 0xC000]` updated
- Read from frame buffer address returns `frame_buffer[]` content, not `mem[]` content
- Boundary: 0xC0FF is last valid frame buffer address; 0xC100 should hit TTY

**TTY Device (`tty_decode`):**
- Register 0x00 (Out Status): Read always returns 0x00
- Register 0x01 (Out Data): Write calls `putchar()`; read returns 0xFF
- Register 0x02 (In Status): Read returns 0x01 if `tty_buff` has data, 0x00 otherwise
- Register 0x03 (In Data): Read pops from `tty_buff`, clears IRQ bit 1 when empty
- IRQ integration: `tty_tick()` sets IRQ bit 1 when `tty_buff` non-empty

### 2.4 Integration Tests

Run actual 6502 programs through the full emulator (minus GUI):
- Load a small program at ROM address (0xD000+), set reset vector, call `emulator_init()` (or equivalent), run N steps with `emulator_step()`, check final state
- Breakpoint test: set breakpoint, run until hit, verify `emulator_check_break()` returns true
- IRQ flow: trigger IRQ, verify CPU vectors through 0xFFFE/0xFFFF

### 2.5 Utility Tests

Pure function tests for `utils.cpp`:
- `itohc()`: integer to hex character
- `my_itoa()`: integer to hex string
- `emu_is_digit()`: ASCII digit recognition
- `emu_is_hex()`: ASCII hex digit recognition
- `my_get_uint()`: parse decimal or hex number from string
- `range_helper()`: parse address range expressions (absolute, relative with `+`, range with `-`)
- `htoi()`: hex string to integer

### 2.6 Disassembler Tests

Test `emu_dis6502_decode()` (pure function, no ImGui):
- Decode known opcodes and verify mnemonic string
- 1-byte, 2-byte, 3-byte instructions return correct length
- Addressing mode formatting (immediate `#$xx`, zero-page `$xx`, absolute `$xxxx`, indexed, indirect)

### 2.7 Label Tests

Test label database operations:
- `emu_labels_add()` / `emu_labels_get()` round-trip
- `emu_labels_clear()` empties all entries
- `emu_labels_load()` parses a `.sym` file (need test fixture file)

---

## 3. Test Fixtures & Helpers

### 3.1 CPU Test Fixture

```cpp
// Minimal CPU + memory environment, no emulator globals needed
struct CpuFixture {
    m6502_t cpu;
    m6502_desc_t desc;
    uint8_t mem[1 << 16];
    uint64_t pins;

    void setup() {
        memset(mem, 0, sizeof(mem));
        memset(&desc, 0, sizeof(desc));
        memset(&cpu, 0, sizeof(cpu));
        pins = m6502_init(&cpu, &desc);
    }

    // Set reset vector to given address
    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = addr & 0xFF;
        mem[0xFFFD] = (addr >> 8) & 0xFF;
    }

    // Set IRQ vector
    void set_irq_vector(uint16_t addr) {
        mem[0xFFFE] = addr & 0xFF;
        mem[0xFFFF] = (addr >> 8) & 0xFF;
    }

    // Load bytes into memory at given address
    void load_program(uint16_t addr, const std::vector<uint8_t>& program) {
        for (size_t i = 0; i < program.size(); i++) {
            mem[addr + i] = program[i];
        }
    }

    // Run one tick with basic memory bus service
    void tick() {
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, mem[addr]);
        } else {
            mem[addr] = M6502_GET_DATA(pins);
        }
    }

    // Run ticks until next SYNC (new instruction fetch), return tick count
    int run_until_sync() {
        int ticks = 0;
        do {
            tick();
            ticks++;
        } while (!(pins & M6502_SYNC) && ticks < 100);
        return ticks;
    }

    // Run through reset sequence (7 ticks) then until first SYNC
    void boot() {
        while (!(pins & M6502_SYNC)) {
            tick();
        }
    }

    // Execute N complete instructions (SYNC to SYNC)
    void run_instructions(int n) {
        for (int i = 0; i < n; i++) {
            run_until_sync();
        }
    }

    // Register accessors
    uint8_t a() { return m6502_a(&cpu); }
    uint8_t x() { return m6502_x(&cpu); }
    uint8_t y() { return m6502_y(&cpu); }
    uint8_t s() { return m6502_s(&cpu); }
    uint8_t p() { return m6502_p(&cpu); }
    uint16_t pc() { return m6502_pc(&cpu); }

    // Flag checks
    bool flag_c() { return p() & M6502_CF; }
    bool flag_z() { return p() & M6502_ZF; }
    bool flag_n() { return p() & M6502_NF; }
    bool flag_v() { return p() & M6502_VF; }
    bool flag_i() { return p() & M6502_IF; }
    bool flag_d() { return p() & M6502_DF; }
};
```

### 3.2 Emulator Test Fixture

For integration tests that use the full `emulator_step()` bus decode. Requires access to the globals in `emulator.cpp`.

```cpp
// Uses the actual emulator globals: mem[], frame_buffer[], cpu, pins, etc.
// Requires extern declarations or a test header exposing them.
struct EmulatorFixture {
    void setup() {
        memset(mem, 0, sizeof(uint8_t) * 65536);
        // Reset frame_buffer, bp_mask, etc. via extern or helper
        // Do NOT call emulator_init() -- it loads ROM from disk
        // Instead, manually init CPU and set vectors
    }

    void load_at(uint16_t addr, const std::vector<uint8_t>& data) {
        for (size_t i = 0; i < data.size(); i++) {
            mem[addr + i] = data[i];
        }
    }

    void step_n(int n) {
        for (int i = 0; i < n; i++) {
            emulator_step();
        }
    }
};
```

### 3.3 gui_con_printmsg Stub

For the test binary, provide a stub that captures output instead of requiring ImGui:

```cpp
// test_stubs.cpp
#include <deque>
#include <string>

std::deque<std::string> test_console_buffer;

void gui_con_printmsg(char* msg) {
    test_console_buffer.push_back(msg);
}
void gui_con_printmsg(std::string msg) {
    test_console_buffer.push_back(msg);
}
```

### 3.4 Common Assertion Helpers

```cpp
// Assert register value with descriptive failure message
#define ASSERT_REG(fixture, reg, expected) \
    ASSERT_EQ(fixture.reg(), expected) \
        << #reg " expected 0x" << std::hex << (int)(expected) \
        << " got 0x" << std::hex << (int)fixture.reg()

// Assert memory value
#define ASSERT_MEM(mem, addr, expected) \
    ASSERT_EQ(mem[addr], expected) \
        << "mem[0x" << std::hex << addr << "] expected 0x" \
        << (int)(expected) << " got 0x" << (int)mem[addr]

// Assert flag set/clear
#define ASSERT_FLAG_SET(fixture, flag) ASSERT_TRUE(fixture.flag_##flag())
#define ASSERT_FLAG_CLR(fixture, flag) ASSERT_FALSE(fixture.flag_##flag())
```

---

## 4. Test Cases

### 4.1 Utility Tests (utils.cpp)

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-01 | Utility | `itohc` -- digits 0-9 | None | `itohc(0)` through `itohc(9)` | Returns '0' through '9' | P0 |
| TC-02 | Utility | `itohc` -- hex A-F | None | `itohc(10)` through `itohc(15)` | Returns 'A' through 'F' | P0 |
| TC-03 | Utility | `itohc` -- masks to 4 bits | None | `itohc(0x1A)` | Returns 'A' (uses only lower nibble) | P1 |
| TC-04 | Utility | `my_itoa` -- 2-digit hex | `char buf[8]` | `my_itoa(buf, 0xFF, 2)` | buf == "FF", null terminated | P0 |
| TC-05 | Utility | `my_itoa` -- 4-digit hex | `char buf[8]` | `my_itoa(buf, 0xD000, 4)` | buf == "D000" | P0 |
| TC-06 | Utility | `my_itoa` -- zero padding | `char buf[8]` | `my_itoa(buf, 0x05, 4)` | buf == "0005" | P1 |
| TC-07 | Utility | `emu_is_digit` -- valid | None | `emu_is_digit('5')` | Returns 5 | P0 |
| TC-08 | Utility | `emu_is_digit` -- invalid | None | `emu_is_digit('A')` | Returns -1 | P0 |
| TC-09 | Utility | `emu_is_hex` -- lowercase | None | `emu_is_hex('a')` | Returns 10 | P0 |
| TC-10 | Utility | `emu_is_hex` -- uppercase | None | `emu_is_hex('F')` | Returns 15 | P0 |
| TC-11 | Utility | `emu_is_hex` -- digit | None | `emu_is_hex('9')` | Returns 9 | P0 |
| TC-12 | Utility | `emu_is_hex` -- invalid | None | `emu_is_hex('G')` | Returns -1 | P0 |
| TC-13 | Utility | `my_get_uint` -- decimal | None | `my_get_uint("1234", dest)` | dest == 1234, returns 4 (chars consumed) | P0 |
| TC-14 | Utility | `my_get_uint` -- hex with $ | None | `my_get_uint("$D000", dest)` | dest == 0xD000, returns 5 | P0 |
| TC-15 | Utility | `my_get_uint` -- hex with 0x | None | `my_get_uint("0xFF", dest)` | dest == 0xFF, returns 4 | P0 |
| TC-16 | Utility | `my_get_uint` -- leading non-digit | None | `my_get_uint(" $0A", dest)` | dest == 0x0A, returns 4 (skips space) | P1 |
| TC-17 | Utility | `my_get_uint` -- no valid number | None | `my_get_uint("xyz", dest)` | Returns 0 | P1 |
| TC-18 | Utility | `range_helper` -- absolute range | None | `range_helper("$100-$1FF", start, end)` | start == 0x100, end == 0x1FF | P0 |
| TC-19 | Utility | `range_helper` -- relative range | None | `range_helper("$100+$10", start, end)` | start == 0x100, end == 0x110 | P0 |
| TC-20 | Utility | `range_helper` -- single address | None | `range_helper("$D000", start, end)` | start == end == 0xD000 | P0 |
| TC-21 | Utility | `htoi` -- hex string | None | `htoi("D000")` | Returns 0xD000 | P0 |
| TC-22 | Utility | `htoi` -- empty/invalid | None | `htoi("xyz")` | Returns 0 | P1 |

### 4.2 CPU Instruction Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-30 | CPU | Reset vector fetch | Set `mem[0xFFFC]=0x00, mem[0xFFFD]=0xD0`, init CPU | Boot (run until SYNC) | `pc() == 0xD000` | P0 |
| TC-31 | CPU | LDA immediate | Reset vector -> 0x0400. `mem[0x0400]=0xA9, mem[0x0401]=0x42` (LDA #$42) | Boot, run 1 instruction | `a() == 0x42`, Z=0, N=0 | P0 |
| TC-32 | CPU | LDA immediate zero | Reset -> 0x0400. LDA #$00 at 0x0400 | Boot, run 1 instr | `a() == 0`, Z=1, N=0 | P0 |
| TC-33 | CPU | LDA immediate negative | Reset -> 0x0400. LDA #$80 at 0x0400 | Boot, run 1 instr | `a() == 0x80`, Z=0, N=1 | P0 |
| TC-34 | CPU | LDA zero-page | Reset -> 0x0400. `mem[0x10]=0x77`. LDA $10 (0xA5, 0x10) at 0x0400 | Boot, run 1 instr | `a() == 0x77` | P0 |
| TC-35 | CPU | LDA absolute | Reset -> 0x0400. `mem[0x1234]=0xAB`. LDA $1234 (0xAD, 0x34, 0x12) at 0x0400 | Boot, run 1 instr | `a() == 0xAB` | P0 |
| TC-36 | CPU | STA zero-page | LDA #$55; STA $20 (A9 55 85 20) | Boot, run 2 instrs | `mem[0x20] == 0x55` | P0 |
| TC-37 | CPU | ADC immediate -- no carry | LDA #$10; ADC #$20 (A9 10 69 20) | Boot, run 2 instrs | `a() == 0x30`, C=0, Z=0 | P0 |
| TC-38 | CPU | ADC immediate -- carry out | LDA #$FF; ADC #$01 (A9 FF 69 01) | Boot, run 2 instrs | `a() == 0x00`, C=1, Z=1 | P0 |
| TC-39 | CPU | ADC immediate -- overflow | LDA #$7F; ADC #$01 (A9 7F 69 01) | Boot, run 2 instrs | `a() == 0x80`, V=1, N=1 | P0 |
| TC-40 | CPU | SBC immediate | SEC; LDA #$50; SBC #$20 (38 A9 50 E9 20) | Boot, run 3 instrs | `a() == 0x30`, C=1 (no borrow) | P1 |
| TC-41 | CPU | INX / DEX | LDX #$05; INX; DEX; DEX (A2 05 E8 CA CA) | Boot, run 4 instrs | `x() == 0x04` | P0 |
| TC-42 | CPU | INY / DEY | LDY #$03; INY; DEY; DEY (A0 03 C8 88 88) | Boot, run 4 instrs | `y() == 0x02` | P0 |
| TC-43 | CPU | TAX / TXA | LDA #$AA; TAX; LDA #$00; TXA (A9 AA AA A9 00 8A) | Boot, run 4 instrs | `a() == 0xAA`, `x() == 0xAA` | P1 |
| TC-44 | CPU | PHA / PLA | LDA #$BB; PHA; LDA #$00; PLA (A9 BB 48 A9 00 68) | Boot, run 4 instrs | `a() == 0xBB` | P0 |
| TC-45 | CPU | JSR / RTS | JSR $0410 at 0x0400. RTS at 0x0410. NOP at 0x0403. | Boot, run 3 instrs (JSR, RTS, NOP) | PC after NOP == 0x0404 | P0 |
| TC-46 | CPU | JMP absolute | JMP $0500 at 0x0400 (4C 00 05). LDA #$99 at 0x0500. | Boot, run 2 instrs | `a() == 0x99`, `pc()` past 0x0500 | P0 |
| TC-47 | CPU | BEQ taken | LDA #$00; BEQ +2; LDA #$FF; (target:) LDA #$42 | Boot, run 3 instrs | `a() == 0x42` (skipped LDA #$FF) | P0 |
| TC-48 | CPU | BEQ not taken | LDA #$01; BEQ +2; LDA #$55 | Boot, run 3 instrs | `a() == 0x55` (fell through) | P0 |
| TC-49 | CPU | BNE taken | LDA #$01; BNE +2; LDA #$FF; LDA #$42 | Boot, run 3 instrs | `a() == 0x42` | P1 |
| TC-50 | CPU | CMP -- equal | LDA #$50; CMP #$50 (A9 50 C9 50) | Boot, run 2 instrs | Z=1, C=1 | P0 |
| TC-51 | CPU | CMP -- less than | LDA #$10; CMP #$50 | Boot, run 2 instrs | Z=0, C=0, N=1 | P1 |
| TC-52 | CPU | AND immediate | LDA #$FF; AND #$0F | Boot, run 2 instrs | `a() == 0x0F` | P1 |
| TC-53 | CPU | ORA immediate | LDA #$F0; ORA #$0F | Boot, run 2 instrs | `a() == 0xFF` | P1 |
| TC-54 | CPU | EOR immediate | LDA #$FF; EOR #$AA | Boot, run 2 instrs | `a() == 0x55` | P1 |
| TC-55 | CPU | ASL accumulator | LDA #$81; ASL A (A9 81 0A) | Boot, run 2 instrs | `a() == 0x02`, C=1 | P1 |
| TC-56 | CPU | LSR accumulator | LDA #$03; LSR A (A9 03 4A) | Boot, run 2 instrs | `a() == 0x01`, C=1 | P1 |
| TC-57 | CPU | ROL accumulator | SEC; LDA #$80; ROL A (38 A9 80 2A) | Boot, run 3 instrs | `a() == 0x01`, C=1 | P2 |
| TC-58 | CPU | ROR accumulator | SEC; LDA #$01; ROR A (38 A9 01 6A) | Boot, run 3 instrs | `a() == 0x80`, C=1 | P2 |
| TC-59 | CPU | INC zero-page | `mem[0x30]=0xFE`. LDA #$00; INC $30; INC $30 (A9 00 E6 30 E6 30) | Boot, run 3 instrs | `mem[0x30] == 0x00`, Z=1 | P1 |
| TC-60 | CPU | DEC zero-page | `mem[0x30]=0x01`. DEC $30 (C6 30) | Boot, run 1 instr | `mem[0x30] == 0x00`, Z=1 | P1 |
| TC-61 | CPU | LDA (indirect,X) | LDX #$04; LDA ($10,X): pointer at 0x14/0x15 -> 0x0300, data at 0x0300 = 0x77 | Boot, run 2 instrs | `a() == 0x77` | P1 |
| TC-62 | CPU | LDA (indirect),Y | LDY #$02; LDA ($20),Y: pointer at 0x20/0x21 -> 0x0300, data at 0x0302 = 0x88 | Boot, run 2 instrs | `a() == 0x88` | P1 |
| TC-63 | CPU | BIT test | `mem[0x40]=0xC0`. LDA #$C0; BIT $40 (A9 C0 24 40) | Boot, run 2 instrs | Z=0, N=1, V=1 | P2 |
| TC-64 | CPU | Stack pointer after reset | Init CPU, boot | Check SP | `s() == 0xFD` (reset decrements SP by 3) | P0 |
| TC-65 | CPU | CLI / SEI | SEI; CLI (78 58) | Boot, run 2 instrs | I=0 after CLI | P1 |
| TC-66 | CPU | CLC / SEC | SEC; CLC (38 18) | Boot, run 2 instrs | C=0 after CLC | P1 |
| TC-67 | CPU | PHP / PLP round-trip | SEC; PHP; CLC; PLP (38 08 18 28) | Boot, run 4 instrs | C=1 (restored from stack) | P1 |
| TC-68 | CPU | NMI vector | Set NMI vector at 0xFFFA/0xFFFB -> handler. Assert NMI on pins. | Run until handler reached | PC == NMI handler address | P2 |

### 4.3 Bus Decode Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-70 | BusDecode | RAM write plain | Program: LDA #$55; STA $0200 | Run | `mem[0x0200] == 0x55` | P0 |
| TC-71 | BusDecode | RAM read plain | `mem[0x0200]=0xAA`. Program: LDA $0200 | Run | `a() == 0xAA` | P0 |
| TC-72 | BusDecode | Frame buffer write | Program: LDA #$41; STA $C000 | Run via `emulator_step()` | `frame_buffer[0] == 0x41` | P0 |
| TC-73 | BusDecode | Frame buffer read | Set `frame_buffer[0]=0x42` (not `mem[0xC000]`). Program: LDA $C000 | Run via `emulator_step()` | `a() == 0x42` | P0 |
| TC-74 | BusDecode | Frame buffer end boundary | Program: LDA #$7E; STA $C0FF | Run via `emulator_step()` | `frame_buffer[0xFF] == 0x7E` | P0 |
| TC-75 | BusDecode | Frame buffer does not leak | Program: LDA #$99; STA $C100 | Run via `emulator_step()` | `frame_buffer` untouched; hits TTY decode region | P1 |
| TC-76 | BusDecode | Write hits both mem and device | Program: LDA #$33; STA $C005 | Run | `mem[0xC005] == 0x33` AND `frame_buffer[5] == 0x33` | P1 |
| TC-77 | BusDecode | IRQ register at 0x00FF | Call `emu_set_irq(1)` | Check `mem[0x00FF]` | Bit 1 set: `mem[0x00FF] & 0x02` | P0 |
| TC-78 | BusDecode | IRQ clear | `emu_set_irq(1)` then `emu_clr_irq(1)` | Check `mem[0x00FF]` | `mem[0x00FF] == 0x00` | P0 |

### 4.4 TTY Device Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-80 | TTY | Read Out Status (reg 0) | Setup pins for read, dev_reg=0 | `tty_decode(pins, 0)` | Data bus == 0x00 | P0 |
| TC-81 | TTY | Read Out Data (reg 1) | Setup pins for read, dev_reg=1 | `tty_decode(pins, 1)` | Data bus == 0xFF | P1 |
| TC-82 | TTY | Read In Status empty (reg 2) | `tty_buff` empty, pins for read | `tty_decode(pins, 2)` | Data bus == 0x00 | P0 |
| TC-83 | TTY | Read In Status with data (reg 2) | Push byte into `tty_buff` | `tty_decode(pins, 2)` | Data bus == 0x01 | P0 |
| TC-84 | TTY | Read In Data (reg 3) | Push 0x41 into `tty_buff` | `tty_decode(pins, 3)` | Data bus == 0x41, `tty_buff` empty | P0 |
| TC-85 | TTY | Read In Data clears IRQ | Push byte, `emu_set_irq(1)` | `tty_decode(pins, 3)` (drain buffer) | `mem[0x00FF]` bit 1 clear | P0 |
| TC-86 | TTY | Write Out Data (reg 1) | Set pins for write with data='H' | `tty_decode(pins, 1)` | `putchar('H')` called (capture stdout) | P1 |
| TC-87 | TTY | Write to read-only registers | Set pins for write, dev_reg=0,2,3 | `tty_decode(pins, dev_reg)` | No crash, no side effects | P2 |
| TC-88 | TTY | tty_reset clears buffer | Push multiple bytes into `tty_buff` | `tty_reset()` | `tty_buff` empty, IRQ bit 1 clear | P0 |

### 4.5 Integration Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-90 | Integration | Boot to reset vector | Load program at 0xD000, set vectors at 0xFFFC/0xFFFD | `emulator_init()` (or manual), step ~10 ticks | `emulator_getpc()` near 0xD000 | P0 |
| TC-91 | Integration | Simple program execution | Program at 0xD000: `LDA #$42; STA $0200; JMP $D005; (D005:) NOP loop` | Step ~50 ticks | `mem[0x0200] == 0x42` | P0 |
| TC-92 | Integration | Breakpoint hit | Load program (loop), set `bp_mask[target]=true`, `emulator_enablebp(true)` | Step until `emulator_check_break()` | Returns true at correct address | P0 |
| TC-93 | Integration | Breakpoint disabled | Same as TC-92 but `emulator_enablebp(false)` | Step past target | `emulator_check_break()` returns false | P1 |
| TC-94 | Integration | Frame buffer via program | Program: LDA #$48; STA $C000; LDA #$69; STA $C001 | Step ~30 ticks | `frame_buffer[0]==0x48`, `frame_buffer[1]==0x69` | P0 |
| TC-95 | Integration | emulator_reset reloads | Run some ticks, corrupt memory | `emulator_reset()`, step 10 ticks | CPU executing from reset vector, ROM reloaded | P1 |
| TC-96 | Integration | IRQ triggers vector | Set IRQ vector at 0xFFFE/0xFFFF -> 0xE000. Place RTI at 0xE000. Enable IRQ via `emu_set_irq(0)`. Program: CLI then loop. | Step until PC reaches 0xE000 | CPU executes IRQ handler | P1 |
| TC-97 | Integration | Multi-step breakpoint list | Set 3 breakpoints via `emulator_setbp("$D000 $D005 $D00A")` | Check `bp_mask[]` | bp_mask[0xD000], bp_mask[0xD005], bp_mask[0xD00A] all true | P1 |

### 4.6 Disassembler Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-100 | Disasm | NOP (1-byte, implied) | `mem[0x0400]=0xEA` | `emu_dis6502_decode(0x0400, buf, 256)` | Returns 1, buf contains "NOP" | P0 |
| TC-101 | Disasm | LDA immediate (2-byte) | `mem[0x0400]=0xA9, mem[0x0401]=0x42` | decode | Returns 2, buf contains "LDA #$42" | P0 |
| TC-102 | Disasm | JMP absolute (3-byte) | `mem[0x0400]=0x4C, mem[0x0401]=0x00, mem[0x0402]=0xD0` | decode | Returns 3, buf contains "JMP $D000" | P0 |
| TC-103 | Disasm | STA zero-page,X | `mem[0x0400]=0x95, mem[0x0401]=0x10` | decode | Returns 2, buf contains "STA $10,X" | P1 |
| TC-104 | Disasm | LDA (indirect,X) | `mem[0x0400]=0xA1, mem[0x0401]=0x20` | decode | Returns 2, buf contains "LDA ($20,X)" | P1 |
| TC-105 | Disasm | LDA (indirect),Y | `mem[0x0400]=0xB1, mem[0x0401]=0x30` | decode | Returns 2, buf contains "LDA ($30),Y" | P1 |
| TC-106 | Disasm | BEQ relative (forward) | `mem[0x0400]=0xF0, mem[0x0401]=0x05` | decode | Returns 2, buf contains "BEQ $0407" | P1 |
| TC-107 | Disasm | BEQ relative (backward) | `mem[0x0400]=0xF0, mem[0x0401]=0xFB` (signed -5) | decode | Returns 2, buf contains "BEQ $03FD" | P2 |
| TC-108 | Disasm | Undefined opcode | `mem[0x0400]=0x02` (undefined) | decode | Returns 1, buf contains "???" | P2 |
| TC-109 | Disasm | ASL accumulator | `mem[0x0400]=0x0A` | decode | Returns 1, buf contains "ASL A" | P1 |

### 4.7 Label Tests

| ID | Category | Name | Setup | Action | Expected | Pri |
|---|---|---|---|---|---|---|
| TC-110 | Labels | Add and get | None | `emu_labels_add(0xD000, "main")` then `emu_labels_get(0xD000)` | List contains "main" | P0 |
| TC-111 | Labels | Get empty | None | `emu_labels_get(0x1234)` | Empty list | P0 |
| TC-112 | Labels | Multiple labels same addr | None | Add "foo" and "bar" at 0xD000 | `emu_labels_get(0xD000)` has both | P1 |
| TC-113 | Labels | Clear | Add labels | `emu_labels_clear()` then get | Empty list | P0 |
| TC-114 | Labels | No duplicates | Add "main" at 0xD000 twice | `emu_labels_get(0xD000)` | List has exactly one "main" | P1 |
| TC-115 | Labels | Load from file | Create test `.sym` file with `al 00D000 .main` | `emu_labels_load()` (with overridden path) | `emu_labels_get(0xD000)` contains "main" | P2 |

---

## 5. Decoupling Requirements

### 5.1 Required Changes (Must Have)

**R1: Split emulator.cpp into logic and GUI files**

The file currently mixes core emulation logic with ImGui rendering. Split into:
- `emulator_core.cpp` -- Contains: `emulator_step()`, `emulator_init()`, `emulator_reset()`, `emulator_loadrom()`, `emulator_setbp()`, `emulator_logbp()`, `emulator_enablebp()`, `emulator_check_break()`, `emulator_getpc()`, `emulator_getci()`, `emu_bus_read()`, `emu_set_irq()`, `emu_clr_irq()`, all globals, the `#define CHIPS_IMPL` include. NO ImGui dependency.
- `emulator_gui.cpp` -- Contains: `emulator_show_memdump_window()`, `emulator_show_status_window()`, `emulator_show_console_window()`. Depends on ImGui.

**R2: Expose critical globals via test header**

Create `emulator_internal.h` (or add externs to existing header) for test access:
```cpp
// For test binary only
extern m6502_t cpu;
extern uint64_t pins;
extern uint8_t frame_buffer[];
extern uint64_t tick_count;
extern uint16_t cur_instruction;
```

**R3: Stub gui_con_printmsg for test binary**

`emulator_logbp()`, `emulator_setbp()`, and `emu_labels_console_list()` call `gui_con_printmsg()`. The test binary provides a stub (see Section 3.3) so it links without `gui_console.cpp` or ImGui.

**R4: Make ROM loading configurable**

`emulator_loadrom()` hardcodes `"N8firmware"` as filename. For tests, either:
- Option A: Make the path a parameter or global that tests can set
- Option B: Provide a `emulator_load_memory(uint16_t addr, uint8_t* data, size_t len)` function for tests to inject programs without files
- Option C (simplest): Tests that use `emulator_step()` bypass `emulator_init()` entirely and manually init the CPU + load memory. (Recommended.)

**R5: Make tty_init() safe for tests**

`tty_init()` calls `set_conio()` which modifies terminal settings and registers `atexit(tty_reset_term)`. Tests must not call `tty_init()`. Either:
- Guard it with a flag
- Or (simpler) tests that need TTY only test `tty_decode()` and `tty_reset()` directly, never calling `tty_init()`.

### 5.2 Recommended Changes (Should Have)

**R6: Extract `tty_buff` accessor**

`tty_buff` is file-static in `emu_tty.cpp`. Add helper functions:
```cpp
void tty_inject_char(uint8_t c);    // Push char into tty_buff (for testing input)
size_t tty_buff_size();             // Query buffer size
```
This avoids exposing the queue directly while enabling TTY input tests.

**R7: Make label_file configurable**

`emu_labels_load()` hardcodes `"N8firmware.sym"`. Either parameterize or allow tests to skip file loading and use `emu_labels_add()` directly.

### 5.3 Not Required

- `emu_dis6502.cpp` GUI function (`emu_dis6502_window`) -- simply don't compile it into the test binary. The `emu_dis6502_decode()` function is already decoupled from GUI.
- `main.cpp` -- entirely excluded from test binary.
- `gui_console.cpp` -- replaced by stub in test binary.

---

## 6. Design Decisions

### D11: Test Framework Selection

**Decision:** Use a lightweight, single-header or minimal-dependency C++ test framework.

**Rationale:** The project uses C++11 and Makefile-based builds. Google Test is well-supported, widely known, and provides fixtures, assertions, and test discovery out of the box. However, for maximum simplicity, a single-header alternative (like doctest or Catch2 single-header) would also work. Google Test is the recommendation because its fixture model maps cleanly to CpuFixture/EmulatorFixture.

**Alternatives considered:**
- Catch2 single-header: simpler integration, but less structured fixture support
- doctest: very lightweight, but less common
- Hand-rolled assert macros: too minimal for good test output

### D12: Separate Test Binary

**Decision:** Tests compile as a completely separate binary (`n8_test` or similar) with its own `main()` and Makefile target. It never links against ImGui, SDL2, or OpenGL.

**Rationale:** The emulator binary has deep ImGui/SDL coupling in `main.cpp` and the `_show_*` functions. Trying to conditionally compile or mock ImGui is fragile. A separate binary that includes only the core `.cpp` files plus test stubs is clean and simple.

**Alternatives considered:**
- Conditional compilation with `#ifdef TESTING`: scatters test concerns through production code
- Mocking ImGui: complex and brittle

### D13: Source File Split Strategy

**Decision:** Split `emulator.cpp` into `emulator_core.cpp` (logic) and `emulator_gui.cpp` (ImGui rendering). Keep `gui_console.cpp` as-is but provide a test stub. Do NOT split `emu_dis6502.cpp` -- just exclude the window function from test compilation via ifdef or separate file.

**Rationale:** The split follows existing function boundaries. The three `emulator_show_*` functions are the only ImGui users in `emulator.cpp`. Moving them to a separate file requires zero logic changes. For `emu_dis6502.cpp`, the decode function and window function could be separated, but an `#ifdef` or simply not linking the window function is simpler.

**Alternatives considered:**
- Move only the `#include "imgui.h"` behind `#ifdef`: leaves GUI code in core file
- Split every file: over-engineering for the current codebase size

### D14: CPU Tests Use CpuFixture, Not Emulator

**Decision:** CPU instruction tests use a standalone `CpuFixture` (own `m6502_t`, own `mem[]`) with a minimal tick loop. They do NOT use `emulator_step()`.

**Rationale:** `emulator_step()` includes bus decode, TTY ticking, IRQ handling, breakpoint checks, and `gui_con_printmsg` calls. CPU instruction correctness should be tested against the clean m6502.h API without those side effects. This also means CPU tests have zero dependencies on emulator globals.

**Alternatives considered:**
- Test via `emulator_step()`: conflates CPU correctness with bus decode correctness
- Mock the bus decode: unnecessary complexity

### D15: Bus Decode Tests Use EmulatorFixture

**Decision:** Bus decode and integration tests use the real `emulator_step()` function and its globals (`mem[]`, `frame_buffer[]`, `cpu`, `pins`).

**Rationale:** The bus decode logic is embedded in `emulator_step()` and operates on file-scope globals. Testing it requires calling the actual function. The globals must be reset between tests.

**Alternatives considered:**
- Extract bus decode into a standalone function with explicit parameters: good long-term but invasive refactor for now
- Reproduce the decode logic in tests: defeats the purpose

### D16: Test Binary File Composition

**Decision:** The test binary compiles and links these files:
- `emulator_core.cpp` (after R1 split)
- `emu_tty.cpp`
- `emu_labels.cpp`
- `emu_dis6502.cpp` (decode function only -- exclude or ifdef the window function)
- `utils.cpp`
- `test_stubs.cpp` (gui_con_printmsg stub)
- `tests/*.cpp` (test source files)

It does NOT include: `main.cpp`, `emulator_gui.cpp`, `gui_console.cpp`, any `imgui/` sources, any SDL2/OpenGL sources.

**Rationale:** Minimal link set. Every file included is either pure logic or has a stub for its GUI dependency.

**Alternatives considered:**
- Include `gui_console.cpp` and stub ImGui: requires faking ImGui types
- Compile everything and stub at link time: too complex

### D17: TTY Test Approach

**Decision:** Test `tty_decode()` directly by constructing pin masks manually. Do not test `tty_tick()` (which depends on terminal I/O via `tty_kbhit()`/`getch()`). Add `tty_inject_char()` helper for input testing.

**Rationale:** `tty_decode()` is the bus-level interface and contains all the register read/write logic. `tty_tick()` polls the host terminal -- irrelevant for emulator correctness testing. The `tty_inject_char()` function lets tests push data into the TTY input buffer without terminal I/O.

**Alternatives considered:**
- Mock `tty_kbhit()`/`getch()`: possible but tests terminal plumbing, not emulator logic
- Test only via full integration: misses register-level edge cases

### D18: Pin Mask Construction for TTY Tests

**Decision:** Provide a helper to construct `uint64_t pins` values for TTY register access:

```cpp
uint64_t make_read_pins(uint16_t addr) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    p |= M6502_RW;  // read
    return p;
}
uint64_t make_write_pins(uint16_t addr, uint8_t data) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    M6502_SET_DATA(p, data);
    // RW bit clear = write
    return p;
}
```

**Rationale:** Calling `tty_decode()` requires a correctly formed pin mask. These helpers make TTY tests readable and avoid bit-manipulation boilerplate in every test.

**Alternatives considered:**
- Inline pin construction: repetitive and error-prone
- Test only through full emulator: loses fine-grained TTY register testing

### D19: Test Data -- Avoid Firmware Dependency

**Decision:** Tests do NOT depend on the actual `N8firmware` ROM binary or `N8firmware.sym` label file. All test programs are injected as byte arrays into memory. Label tests either use `emu_labels_add()` directly or create minimal test fixture files.

**Rationale:** Tests must be self-contained and deterministic. Depending on firmware that may change or not be built breaks test isolation. Byte-array programs are explicit and reviewable.

**Alternatives considered:**
- Ship a test ROM: adds build dependency, opaque test data
- Build firmware as test prerequisite: adds cc65 toolchain dependency to test builds

### D20: Test Organization

**Decision:** Organize tests into files by category:
- `tests/test_utils.cpp` -- TC-01 through TC-22
- `tests/test_cpu.cpp` -- TC-30 through TC-68
- `tests/test_bus_decode.cpp` -- TC-70 through TC-78
- `tests/test_tty.cpp` -- TC-80 through TC-88
- `tests/test_integration.cpp` -- TC-90 through TC-97
- `tests/test_disasm.cpp` -- TC-100 through TC-109
- `tests/test_labels.cpp` -- TC-110 through TC-115

**Rationale:** Maps 1:1 to test categories. Each file has a focused scope and independent fixtures.

**Alternatives considered:**
- Single test file: unmanageable as test count grows
- Per-test-case files: too many files for the scope

### D21: Run-Until-Instruction-Complete Helper

**Decision:** The `CpuFixture::run_until_sync()` method ticks until `M6502_SYNC` pin is asserted, indicating the CPU is about to fetch a new instruction. This is used to advance one complete instruction at a time.

**Rationale:** The m6502.h CPU is cycle-stepped, not instruction-stepped. Tests that verify instruction results need to run to completion. The SYNC pin (bit 25) marks the start of a new instruction fetch, which is the natural boundary.

**Alternatives considered:**
- Count a fixed number of ticks per instruction: fragile, varies by instruction and addressing mode
- Use `m6502_pc()` change detection: unreliable for multi-cycle instructions

### D22: Emulator Global Reset Between Tests

**Decision:** Each `EmulatorFixture` test case resets ALL emulator globals in setup: `memset(mem, 0, 65536)`, clear `frame_buffer`, clear `bp_mask`, reset `bp_enable`/`bp_hit`, zero `tick_count`, reinitialize CPU via `m6502_init()`.

**Rationale:** Emulator globals persist across tests if not reset. Leaking state between tests causes flaky, order-dependent failures. The full reset is cheap (memset 64KB + struct init).

**Alternatives considered:**
- Use Google Test `SetUp()`/`TearDown()`: yes, this IS where the reset happens
- Allocate fresh globals per test: not possible with file-scope globals

### D23: Priority Tiers

**Decision:** Test priorities are:
- **P0 (Must have):** Core CPU operations (LDA, STA, ADC, branches, JSR/RTS, reset vector), bus decode basics (RAM, frame buffer), breakpoints, utility function correctness. These catch regressions in fundamental emulator behavior.
- **P1 (Should have):** Remaining ALU ops, flag edge cases, TTY registers, address mode coverage, CMP, integration scenarios. Important but less likely to regress independently.
- **P2 (Nice to have):** Rotate instructions, NMI, undefined opcodes, backward branches, obscure edge cases.

**Rationale:** P0 tests cover the critical path: can the CPU execute code, does memory work, do breakpoints work. If P0 passes, the emulator is fundamentally operational. P1 fills in coverage. P2 handles corners.

**Alternatives considered:**
- Flat priority: doesn't help with incremental implementation
- More tiers: unnecessary granularity

### D24: emu_dis6502.cpp Test Compilation

**Decision:** For the test binary, either (a) split `emu_dis6502.cpp` so that `emu_dis6502_decode()` is in one file and `emu_dis6502_window()` is in another, or (b) wrap `emu_dis6502_window()` in `#ifndef N8_TEST_BUILD` so it is excluded when building tests. Option (b) is simpler.

**Rationale:** `emu_dis6502_decode()` is valuable to test (verifies opcode table correctness) but `emu_dis6502_window()` pulls in ImGui. A single `#ifndef` is minimal-impact.

**Alternatives considered:**
- Split into two files: cleaner but more files to manage
- Skip disassembler tests entirely: loses opcode table verification

### D25: Makefile Test Target

**Decision:** Add a `test` target to the Makefile that builds `n8_test` from the test sources + core sources + stubs, linked against the test framework (e.g., Google Test). It should be invocable with `make test`.

**Rationale:** Keeps test building integrated with the existing build system. No separate CMake or build system required.

**Alternatives considered:**
- Separate Makefile for tests: harder to keep in sync with main build flags
- CMake: overkill for this project's size and existing Makefile setup

---

## Appendix A: Test Binary Link Dependency Summary

```
n8_test binary
  |
  +-- tests/test_*.cpp          (test cases)
  +-- src/emulator_core.cpp     (after R1 split: step, init, bp, bus decode)
  +-- src/emu_tty.cpp           (TTY device logic)
  +-- src/emu_labels.cpp        (label database)
  +-- src/emu_dis6502.cpp       (decode only, window #ifdef'd out)
  +-- src/utils.cpp             (pure utility functions)
  +-- tests/test_stubs.cpp      (gui_con_printmsg stub)
  +-- m6502.h                   (CHIPS_IMPL in emulator_core.cpp)
  +-- libgtest                  (or equivalent test framework)
  |
  NOT included:
  +-- src/main.cpp
  +-- src/emulator_gui.cpp
  +-- src/gui_console.cpp
  +-- imgui/*.cpp
  +-- SDL2, OpenGL libs
```

## Appendix B: Priority Summary

| Priority | Count | Categories |
|---|---|---|
| P0 | ~30 | Core CPU, basic bus decode, breakpoints, utilities, reset, disasm basics |
| P1 | ~25 | Extended CPU, TTY registers, integration, labels, addressing modes |
| P2 | ~8 | Rotate ops, NMI, undefined opcodes, edge cases |
| **Total** | **~63** | |
