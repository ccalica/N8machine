# N8Machine Test Harness Specification v1

## 1. Overview

### Purpose

Automated test harness for the N8Machine 6502 emulator. Covers the C++ emulation core (CPU, bus decode, devices, utilities) with a deferred plan for firmware-level integration tests.

### Scope

- Unit tests for pure utility functions
- CPU instruction correctness via isolated m6502.h tick loop
- Bus decode and memory-mapped device behavior
- TTY device register logic
- Disassembler opcode decode
- Label database operations
- Integration tests using the full `emulator_step()` path
- Firmware tests (DEFERRED -- requires GDB RSP stub)

### Design Principles

1. **Simplicity** -- Minimal infrastructure, no over-engineering.
2. **Zero production code changes** for the initial harness. All decoupling via link-time stubs.
3. **Self-contained tests** -- No dependency on firmware ROM files. Test programs are inline byte arrays.
4. **Single test binary** -- One `make test` command, one executable, one exit code.
5. **Incremental** -- Start with P0 tests, expand coverage over time.

---

## 2. Framework Selection

**Decision: doctest (single-header, C++11)**

doctest is the test framework. Vendor `doctest.h` directly in the repository.

### Rationale

Agents A and B both support a real framework over hand-rolled macros. Agent A conducted a thorough survey and recommends doctest. Agent B leans toward GoogleTest but acknowledges doctest/Catch2 work. Agent C recommends no framework (custom T_ASSERT macros).

By the 2-of-3 rule, a real framework wins. Between frameworks, doctest is the clear choice:

- **Single header, C++11** -- Drop one file into the repo. Matches the project's `-std=c++11` and Makefile-based build.
- **Expression decomposition** -- `CHECK(m6502_a(&cpu) == 0x42)` prints `0x00 == 0x42` on failure. Critical for debugging CPU register mismatches. Agent C's `T_ASSERT_EQ(a, b)` only prints the macro text, not actual values.
- **Subcases** -- Shared CPU setup with per-opcode assertions, reducing boilerplate for instruction tests.
- **Test suites and filtering** -- `./n8_test -ts="cpu"` runs only CPU tests. Agent C's substring filter is replicated natively.
- **~280KB, zero dependencies** -- Comparable weight to Agent C's ~50 lines of infrastructure, vastly more capable.
- **Auto-registration** -- No manual test array to maintain (unlike Agent C's static `tests[]`).

### Why Not Others

| Framework | Rejection Reason |
|-----------|-----------------|
| GoogleTest | Requires building a library, pthread dependency, overkill for this project |
| Catch2 v3 | Requires C++14, no longer single-header |
| Catch2 v2 | EOL / maintenance-only |
| utest.h | No expression decomposition |
| Boost.Test | Drags in Boost, disqualified |
| No framework | Loses expression decomposition, auto-registration, subcases; more maintenance burden for marginal simplicity gain |

See Agent A's spec (Section 2) for the full survey.

---

## 3. File Structure

```
N8machine/
+-- test/
|   +-- doctest.h              # Vendored test framework (single header)
|   +-- test_main.cpp          # #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
|   +-- test_helpers.h         # Shared fixtures, helpers, pin constructors
|   +-- test_stubs.cpp         # Link-time stubs (gui_console, ImGui)
|   +-- test_utils.cpp         # Tests for utils.cpp
|   +-- test_cpu.cpp           # CPU instruction tests (isolated m6502 tick loop)
|   +-- test_bus.cpp           # Bus decode tests (via emulator_step)
|   +-- test_tty.cpp           # TTY device register tests
|   +-- test_disasm.cpp        # Disassembler decode tests
|   +-- test_labels.cpp        # Label database tests
|   +-- test_integration.cpp   # Multi-component integration tests
|   +-- firmware/              # DEFERRED -- test-specific firmware ROMs
|       +-- test_tty_rb.s
|       +-- test_tty_putc.s
|       +-- Makefile
+-- src/                       # Unchanged
+-- Makefile                   # Modified: add `test` target
+-- ...
```

### Naming Conventions

- Directory: `test/` (not `tests/`).
- Test files: `test_<module>.cpp` matching the source module under test.
- Test cases: Use `TEST_SUITE("category")` with descriptive `TEST_CASE("name")` strings.
- Stubs: `test_stubs.cpp` -- single file for all GUI/system link-time replacements.
- Helpers: `test_helpers.h` -- fixtures, pin constructors, common utilities.

---

## 4. Build Integration

### Makefile Additions

Append to the existing `Makefile`:

```makefile
##---------------------------------------------------------------------
## TEST BUILD
##---------------------------------------------------------------------

TEST_DIR = test
TEST_BUILD_DIR = build/test
TEST_EXE = n8_test

# Production source objects reused by test binary
TEST_SRC_OBJS = $(BUILD_DIR)/emulator.o $(BUILD_DIR)/emu_tty.o \
                $(BUILD_DIR)/emu_dis6502.o $(BUILD_DIR)/emu_labels.o \
                $(BUILD_DIR)/utils.o

# Test source files
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.cpp)
_TEST_OBJS = $(addsuffix .o, $(basename $(notdir $(TEST_SOURCES))))
TEST_OBJS = $(patsubst %, $(TEST_BUILD_DIR)/%, $(_TEST_OBJS))

# Test compiler flags: same C++ standard, add test/ and src/ to include path
# No SDL/ImGui flags needed for test sources (they don't include ImGui)
TEST_CXXFLAGS = -std=c++11 -g -Wall -Wformat -I$(SRC_DIR) -I$(TEST_DIR)

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

.PHONY: test clean-test

test: $(TEST_EXE)
	./$(TEST_EXE)

$(TEST_EXE): $(TEST_SRC_OBJS) $(TEST_OBJS)
	$(CXX) -o $@ $^

clean-test:
	rm -f $(TEST_EXE) $(TEST_BUILD_DIR)/*.o
```

### Build Sequence

```bash
make && make test
```

`make test` reuses the production `.o` files (which are compiled with ImGui/SDL headers). The test binary links against these objects but does NOT link SDL2, ImGui, or OpenGL libraries. Unresolved ImGui symbols are satisfied by `test_stubs.cpp`.

### Compiler Flags Summary

| Build | CXXFLAGS | LIBS |
|-------|----------|------|
| Production | `-std=c++11 -g -Wall -Wformat -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends` + `sdl2-config --cflags` | `-lGL -ldl` + `sdl2-config --libs` |
| Test sources | `-std=c++11 -g -Wall -Wformat -I$(SRC_DIR) -I$(TEST_DIR)` | (none) |

### Test Binary Link Composition

```
n8_test binary
  |
  +-- test/*.cpp               (test cases + stubs)
  +-- build/emulator.o         (core emulation: step, init, bp, bus decode + show_* dead code)
  +-- build/emu_tty.o          (TTY device logic)
  +-- build/emu_labels.o       (label database)
  +-- build/emu_dis6502.o      (disassembler decode + window dead code)
  +-- build/utils.o            (pure utility functions)
  |
  NOT included:
  +-- build/main.o
  +-- build/gui_console.o
  +-- build/imgui*.o
  +-- SDL2, OpenGL libs
```

---

## 5. Decoupling Strategy

**Approach: Link-time stubs. Zero production code changes.**

Agents B and C disagree on this. Agent B wants to split `emulator.cpp` into `emulator_core.cpp` + `emulator_gui.cpp`. Agent C says zero production changes, use link-time stubs for ImGui. Agent A's D6 leans toward minimal refactoring (split or ifdef).

Resolution: Agent C's approach (stubs, no split) is simpler and requires zero production changes. Agent B's split is cleaner long-term but is an optimization that can be done later if the stub approach becomes painful. Simplicity wins for v1.

### 5.1 gui_console Stub

Exclude `gui_console.o` from the test link. Provide replacement symbols in `test_stubs.cpp`:

```cpp
// test_stubs.cpp
#include <string>
#include <deque>

// ---- gui_console stubs ----
static std::deque<std::string> stub_console_buffer;

void gui_con_printmsg(char* msg) {
    stub_console_buffer.push_back(std::string(msg));
}
void gui_con_printmsg(std::string msg) {
    stub_console_buffer.push_back(msg);
}
void gui_con_init() {}
void gui_show_console_window(bool&) {}

// Test helpers to inspect captured output
std::deque<std::string>& stub_get_console_buffer() {
    return stub_console_buffer;
}
void stub_clear_console_buffer() {
    stub_console_buffer.clear();
}
```

The stub captures `gui_con_printmsg()` output into an inspectable buffer, enabling tests to verify debug messages (breakpoint hit, label list, etc.).

### 5.2 ImGui Linker Stubs

`emulator.o` and `emu_dis6502.o` contain calls to ImGui functions in their `_show_*` / `_window` functions. These functions are never called in tests, but the linker needs symbols. Add to `test_stubs.cpp`:

```cpp
// ---- ImGui linker stubs ----
// Satisfies unresolved symbols from emulator_show_* and emu_dis6502_window.
// These functions are never called in tests.
struct ImVec2 { float x, y; ImVec2() : x(0), y(0) {} ImVec2(float a, float b) : x(a), y(b) {} };
struct ImVec4 { float x, y, z, w; };

namespace ImGui {
    bool Begin(const char*, bool*, int) { return true; }
    void End() {}
    void Text(const char*, ...) {}
    void TextColored(const ImVec4&, const char*, ...) {}
    void SameLine(float, float) {}
    bool Checkbox(const char*, bool*) { return false; }
    bool InputText(const char*, char*, unsigned long, int, void*, void*) { return false; }
    void BeginChild(const char*, const ImVec2&, bool, int) {}
    void EndChild() {}
    void SetScrollY(float) {}
    float GetScrollMaxY() { return 0.0f; }
    void BeginDisabled(bool) {}
    void EndDisabled() {}
    bool Button(const char*, const ImVec2&) { return false; }
}
```

Exact signatures depend on the vendored ImGui version. Compile, check for unresolved symbols, add stubs as needed. One-time exercise.

### 5.3 emu_tty.cpp -- Linked Directly

`emu_tty.cpp` has no GUI dependencies. `tty_kbhit()` returns 0 immediately when no keyboard input is pending (uses `select()` with zero timeout). `tty_tick()` calls `tty_kbhit()` and returns early -- safe in test environments.

**Concern**: `emulator_init()` calls `tty_init()` which puts the terminal in raw mode. Tests that use `emulator_step()` should bypass `emulator_init()` entirely and manually init the CPU + load memory. Tests that need TTY behavior test `tty_decode()` directly, never calling `tty_init()`.

### 5.4 emulator_loadrom() -- Bypassed

`emulator_loadrom()` opens `"N8firmware"` from CWD. Tests bypass `emulator_init()` entirely. Instead:
- Manually `memset(mem, 0, sizeof)` and write test programs as byte arrays
- Call `m6502_init(&cpu, &desc)` directly
- Set reset vectors manually

This keeps tests self-contained with no firmware file dependency.

### 5.5 Globals Requiring Test Access

Source verified -- these globals exist in `emulator.cpp`:

| Global | Declared In | Already Extern? | Test Access |
|--------|-------------|-----------------|-------------|
| `mem[65536]` | `emulator.cpp` | Yes (`emulator.h`) | Direct |
| `bp_mask[65536]` | `emulator.cpp` | Yes (`emulator.h`) | Direct |
| `cpu` (m6502_t) | `emulator.cpp` | No | Needs extern in `test_helpers.h` |
| `pins` (uint64_t) | `emulator.cpp` | No | Needs extern in `test_helpers.h` |
| `frame_buffer[256]` | `emulator.cpp` | No | Needs extern in `test_helpers.h` |
| `tick_count` | `emulator.cpp` | No | Needs extern in `test_helpers.h` |
| `bp_enable` | `emulator.cpp` | No | Via `emulator_enablebp()` |
| `bp_hit` | `emulator.cpp` | No | Via `emulator_check_break()` |
| `cur_instruction` | `emulator.cpp` | No | Via `emulator_getci()` |
| `tty_buff` | `emu_tty.cpp` | No | Needs accessor (see Fixtures) |
| `desc` (m6502_desc_t) | `emulator.cpp` | No | Needs extern in `test_helpers.h` |

`test_helpers.h` will declare the necessary externs for test access without modifying production headers.

---

## 6. Test Fixtures & Helpers

### 6.1 CpuFixture -- Isolated CPU Testing

For CPU instruction tests. Owns its own `m6502_t`, `mem[]`, and `pins`. Zero dependency on emulator globals.

```cpp
struct CpuFixture {
    m6502_t cpu;
    m6502_desc_t desc;
    uint8_t mem[1 << 16];
    uint64_t pins;

    CpuFixture() {
        memset(mem, 0, sizeof(mem));
        memset(&desc, 0, sizeof(desc));
        memset(&cpu, 0, sizeof(cpu));
        pins = m6502_init(&cpu, &desc);
    }

    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = addr & 0xFF;
        mem[0xFFFD] = (addr >> 8) & 0xFF;
    }

    void set_irq_vector(uint16_t addr) {
        mem[0xFFFE] = addr & 0xFF;
        mem[0xFFFF] = (addr >> 8) & 0xFF;
    }

    void set_nmi_vector(uint16_t addr) {
        mem[0xFFFA] = addr & 0xFF;
        mem[0xFFFB] = (addr >> 8) & 0xFF;
    }

    void load_program(uint16_t addr, const std::vector<uint8_t>& program) {
        for (size_t i = 0; i < program.size(); i++) {
            mem[addr + i] = program[i];
        }
    }

    void tick() {
        pins = m6502_tick(&cpu, pins);
        const uint16_t addr = M6502_GET_ADDR(pins);
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, mem[addr]);
        } else {
            mem[addr] = M6502_GET_DATA(pins);
        }
    }

    int run_until_sync() {
        int ticks = 0;
        do {
            tick();
            ticks++;
        } while (!(pins & M6502_SYNC) && ticks < 100);
        return ticks;
    }

    void boot() {
        while (!(pins & M6502_SYNC)) {
            tick();
        }
    }

    void run_instructions(int n) {
        for (int i = 0; i < n; i++) {
            run_until_sync();
        }
    }

    // Register accessors
    uint8_t a()  { return m6502_a(&cpu); }
    uint8_t x()  { return m6502_x(&cpu); }
    uint8_t y()  { return m6502_y(&cpu); }
    uint8_t s()  { return m6502_s(&cpu); }
    uint8_t p()  { return m6502_p(&cpu); }
    uint16_t pc(){ return m6502_pc(&cpu); }

    // Flag checks
    bool flag_c() { return p() & 0x01; } // M6502_CF
    bool flag_z() { return p() & 0x02; } // M6502_ZF
    bool flag_i() { return p() & 0x04; } // M6502_IF
    bool flag_d() { return p() & 0x08; } // M6502_DF
    bool flag_v() { return p() & 0x40; } // M6502_VF
    bool flag_n() { return p() & 0x80; } // M6502_NF
};
```

### 6.2 EmulatorFixture -- Bus Decode & Integration Testing

Uses the actual emulator globals (`mem[]`, `frame_buffer[]`, `cpu`, `pins`). For bus decode and integration tests.

```cpp
// Externs for emulator globals (declared in test_helpers.h)
extern m6502_t cpu;
extern m6502_desc_t desc;
extern uint64_t pins;
extern uint8_t frame_buffer[];
extern uint64_t tick_count;

struct EmulatorFixture {
    EmulatorFixture() {
        memset(mem, 0, sizeof(uint8_t) * 65536);
        memset(frame_buffer, 0, 256);
        memset(bp_mask, 0, sizeof(bool) * 65536);
        tick_count = 0;
        emulator_enablebp(false);
        pins = m6502_init(&cpu, &desc);
        stub_clear_console_buffer();
    }

    void load_at(uint16_t addr, const std::vector<uint8_t>& data) {
        for (size_t i = 0; i < data.size(); i++) {
            mem[addr + i] = data[i];
        }
    }

    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = addr & 0xFF;
        mem[0xFFFD] = (addr >> 8) & 0xFF;
    }

    void step_n(int n) {
        for (int i = 0; i < n; i++) {
            emulator_step();
        }
    }
};
```

### 6.3 Pin Construction Helpers

For direct `tty_decode()` testing:

```cpp
inline uint64_t make_read_pins(uint16_t addr) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    p |= M6502_RW;  // read
    return p;
}

inline uint64_t make_write_pins(uint16_t addr, uint8_t data) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    M6502_SET_DATA(p, data);
    // RW bit clear = write
    return p;
}
```

### 6.4 TTY Buffer Access

`tty_buff` is file-static in `emu_tty.cpp`. Two options:

1. **Add `tty_inject_char()` / `tty_buff_size()` helpers** to `emu_tty.cpp` (minimal production change -- adding test accessors).
2. **Test only through bus-level operations** where the emulator writes to the TTY input buffer via `tty_tick()`.

For v1, prefer option 2 (no production changes). If direct buffer manipulation is needed for TTY tests, add the accessors as a follow-up.

### 6.5 Console Stub Helpers

Declared in `test_helpers.h`, defined in `test_stubs.cpp`:

```cpp
extern std::deque<std::string>& stub_get_console_buffer();
extern void stub_clear_console_buffer();
```

---

## 7. Test Categories & Cases

Test IDs are sequential (T01, T02, ...). Priority tiers:
- **P0** -- Core functionality. Must pass for the emulator to be considered operational.
- **P1** -- Important coverage. Should be implemented after P0.
- **P2** -- Edge cases and nice-to-haves.

### 7.1 Utility Tests (`test_utils.cpp`)

Suite: `"utils"`

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T01 | itohc digits 0-9 | `itohc(0)` through `itohc(9)` | Returns '0' through '9' | P0 |
| T02 | itohc hex A-F | `itohc(10)` through `itohc(15)` | Returns 'A' through 'F' | P0 |
| T03 | itohc masks to 4 bits | `itohc(0x1A)` | Returns 'A' (lower nibble only) | P1 |
| T04 | my_itoa 2-digit hex | `my_itoa(buf, 0xFF, 2)` | buf == "FF" | P0 |
| T05 | my_itoa 4-digit hex | `my_itoa(buf, 0xD000, 4)` | buf == "D000" | P0 |
| T06 | my_itoa zero padding | `my_itoa(buf, 0x05, 4)` | buf == "0005" | P1 |
| T07 | emu_is_digit valid | `emu_is_digit('5')` | Returns 5 | P0 |
| T08 | emu_is_digit invalid | `emu_is_digit('A')` | Returns -1 | P0 |
| T09 | emu_is_hex lowercase | `emu_is_hex('a')` | Returns 10 | P0 |
| T10 | emu_is_hex uppercase | `emu_is_hex('F')` | Returns 15 | P0 |
| T11 | emu_is_hex digit | `emu_is_hex('9')` | Returns 9 | P0 |
| T12 | emu_is_hex invalid | `emu_is_hex('G')` | Returns -1 | P0 |
| T13 | my_get_uint decimal | `my_get_uint("1234", dest)` | dest == 1234, returns 4 | P0 |
| T14 | my_get_uint hex $ | `my_get_uint("$D000", dest)` | dest == 0xD000, returns 5 | P0 |
| T15 | my_get_uint hex 0x | `my_get_uint("0xFF", dest)` | dest == 0xFF, returns 4 | P0 |
| T16 | my_get_uint leading space | `my_get_uint(" $0A", dest)` | dest == 0x0A, returns 4 | P1 |
| T17 | my_get_uint no number | `my_get_uint("xyz", dest)` | Returns 0 | P1 |
| T18 | range_helper absolute range | `range_helper("$100-$1FF", s, e)` | s=0x100, e=0x1FF | P0 |
| T19 | range_helper relative | `range_helper("$100+$10", s, e)` | s=0x100, e=0x110 | P0 |
| T20 | range_helper single addr | `range_helper("$D000", s, e)` | s=e=0xD000 | P0 |
| T21 | htoi hex string | `htoi("D000")` | Returns 0xD000 | P0 |
| T22 | htoi invalid | `htoi("xyz")` | Returns 0 | P1 |

### 7.2 CPU Instruction Tests (`test_cpu.cpp`)

Suite: `"cpu"`

Uses `CpuFixture`. Programs loaded as byte arrays. Reset vector at 0x0400 unless noted.

| ID | Name | Program (at 0x0400) | Expected | Pri |
|----|------|---------------------|----------|-----|
| T23 | Reset vector fetch | mem[0xFFFC]=0x00, mem[0xFFFD]=0xD0. Boot. | pc() == 0xD000 | P0 |
| T24 | SP after reset | Boot. | s() == 0xFD | P0 |
| T25 | LDA immediate | A9 42 (LDA #$42) | a()==0x42, Z=0, N=0 | P0 |
| T26 | LDA imm zero | A9 00 | a()==0, Z=1, N=0 | P0 |
| T27 | LDA imm negative | A9 80 | a()==0x80, Z=0, N=1 | P0 |
| T28 | LDA zero-page | mem[0x10]=0x77. A5 10 | a()==0x77 | P0 |
| T29 | LDA absolute | mem[0x1234]=0xAB. AD 34 12 | a()==0xAB | P0 |
| T30 | STA zero-page | A9 55 85 20 | mem[0x20]==0x55 | P0 |
| T31 | ADC no carry | A9 10 69 20 | a()==0x30, C=0, Z=0 | P0 |
| T32 | ADC carry out | A9 FF 69 01 | a()==0x00, C=1, Z=1 | P0 |
| T33 | ADC overflow | A9 7F 69 01 | a()==0x80, V=1, N=1 | P0 |
| T34 | SBC immediate | 38 A9 50 E9 20 (SEC; LDA #$50; SBC #$20) | a()==0x30, C=1 | P1 |
| T35 | INX / DEX | A2 05 E8 CA CA | x()==0x04 | P0 |
| T36 | INY / DEY | A0 03 C8 88 88 | y()==0x02 | P0 |
| T37 | TAX / TXA | A9 AA AA A9 00 8A | a()==0xAA, x()==0xAA | P1 |
| T38 | PHA / PLA | A9 BB 48 A9 00 68 | a()==0xBB | P0 |
| T39 | JSR / RTS | JSR $0410 at 0x0400. RTS at 0x0410. NOP at 0x0403. | PC past 0x0403 after 3 instrs | P0 |
| T40 | JMP absolute | 4C 00 05 at 0x0400. A9 99 at 0x0500. | a()==0x99 | P0 |
| T41 | BEQ taken | A9 00 F0 02 A9 FF A9 42 | a()==0x42 | P0 |
| T42 | BEQ not taken | A9 01 F0 02 A9 55 ... | a()==0x55 | P0 |
| T43 | BNE taken | A9 01 D0 02 A9 FF A9 42 | a()==0x42 | P1 |
| T44 | CMP equal | A9 50 C9 50 | Z=1, C=1 | P0 |
| T45 | CMP less than | A9 10 C9 50 | Z=0, C=0, N=1 | P1 |
| T46 | AND immediate | A9 FF 29 0F | a()==0x0F | P1 |
| T47 | ORA immediate | A9 F0 09 0F | a()==0xFF | P1 |
| T48 | EOR immediate | A9 FF 49 AA | a()==0x55 | P1 |
| T49 | ASL accumulator | A9 81 0A | a()==0x02, C=1 | P1 |
| T50 | LSR accumulator | A9 03 4A | a()==0x01, C=1 | P1 |
| T51 | ROL accumulator | 38 A9 80 2A (SEC; LDA #$80; ROL A) | a()==0x01, C=1 | P2 |
| T52 | ROR accumulator | 38 A9 01 6A (SEC; LDA #$01; ROR A) | a()==0x80, C=1 | P2 |
| T53 | INC zero-page | mem[0x30]=0xFE. E6 30 E6 30 | mem[0x30]==0x00, Z=1 | P1 |
| T54 | DEC zero-page | mem[0x30]=0x01. C6 30 | mem[0x30]==0x00, Z=1 | P1 |
| T55 | LDA (indirect,X) | LDX #$04; LDA ($10,X). Pointer at 0x14->0x0300, data at 0x0300=0x77. | a()==0x77 | P1 |
| T56 | LDA (indirect),Y | LDY #$02; LDA ($20),Y. Pointer at 0x20->0x0300, data at 0x0302=0x88. | a()==0x88 | P1 |
| T57 | BIT test | mem[0x40]=0xC0. A9 C0 24 40 | Z=0, N=1, V=1 | P2 |
| T58 | CLI / SEI | 78 58 (SEI; CLI) | I=0 after CLI | P1 |
| T59 | CLC / SEC | 38 18 (SEC; CLC) | C=0 after CLC | P1 |
| T60 | PHP / PLP | 38 08 18 28 (SEC; PHP; CLC; PLP) | C=1 restored | P1 |
| T61 | NMI vector | Set NMI vector, assert NMI on pins | PC at NMI handler | P2 |

### 7.3 Bus Decode Tests (`test_bus.cpp`)

Suite: `"bus"`

Uses `EmulatorFixture`. Tests exercise `emulator_step()` with the real bus decode logic.

Verified from source: bus decode order is RAM first (lines 113-119), then device overlays. Reads: devices call `M6502_SET_DATA()` to override RAM. Writes: data goes to both `mem[]` and device.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T62 | RAM write plain | LDA #$55; STA $0200 | mem[0x0200]==0x55 | P0 |
| T63 | RAM read plain | mem[0x0200]=0xAA. LDA $0200 | a()==0xAA | P0 |
| T64 | Frame buffer write | LDA #$41; STA $C000 | frame_buffer[0]==0x41 | P0 |
| T65 | Frame buffer read | frame_buffer[0]=0x42. LDA $C000 | a()==0x42 | P0 |
| T66 | Frame buffer end boundary | LDA #$7E; STA $C0FF | frame_buffer[0xFF]==0x7E | P0 |
| T67 | Frame buffer does not leak to TTY | LDA #$99; STA $C100 | frame_buffer untouched; hits TTY region | P1 |
| T68 | Write hits both mem and device | LDA #$33; STA $C005 | mem[0xC005]==0x33 AND frame_buffer[5]==0x33 | P1 |
| T69 | IRQ register set | `emu_set_irq(1)` | mem[0x00FF] & 0x02 | P0 |
| T70 | IRQ register clear | `emu_set_irq(1)` then `emu_clr_irq(1)` | mem[0x00FF]==0x00 | P0 |

**Source verification note**: `IRQ_SET(bit)` uses `mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)`. For bit=1 this sets bit 1 (value 0x02). `emu_clr_irq(bit)` masks off the bit. However, `emulator_step()` calls `IRQ_CLR()` which does `mem[0x00FF] = 0x00` at the start of each tick (line 92). This means the IRQ register is cleared every tick and only re-set by `tty_tick()`. Tests for `emu_set_irq` / `emu_clr_irq` should call these functions directly without `emulator_step()` in between.

### 7.4 TTY Device Tests (`test_tty.cpp`)

Suite: `"tty"`

Tests call `tty_decode()` directly with constructed pin masks. Do NOT call `tty_init()` or `tty_tick()`.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T71 | Read Out Status (reg 0) | `tty_decode(read_pins, 0)` | Data bus == 0x00 | P0 |
| T72 | Read Out Data (reg 1) | `tty_decode(read_pins, 1)` | Data bus == 0xFF | P1 |
| T73 | Read In Status empty (reg 2) | Buffer empty. `tty_decode(read_pins, 2)` | Data bus == 0x00 | P0 |
| T74 | Read In Status with data (reg 2) | Push byte to buffer. `tty_decode(read_pins, 2)` | Data bus == 0x01 | P0 |
| T75 | Read In Data (reg 3) | Push 0x41 to buffer. `tty_decode(read_pins, 3)` | Data bus == 0x41, buffer drained | P0 |
| T76 | Read In Data clears IRQ | Push byte, set IRQ bit 1. Read reg 3 to drain. | mem[0x00FF] bit 1 clear | P0 |
| T77 | Write Out Data (reg 1) | `tty_decode(write_pins_with_'H', 1)` | putchar('H') called (capture stdout) | P1 |
| T78 | Write to read-only regs | Write to regs 0, 2, 3 | No crash, no side effects | P2 |
| T79 | tty_reset clears buffer | Push multiple bytes. `tty_reset()` | Buffer empty, IRQ bit 1 clear | P0 |

**Note**: T74, T75, T76 require pushing bytes into `tty_buff`. Since `tty_buff` is file-static, this needs either: (a) a `tty_inject_char()` accessor added to `emu_tty.cpp`, or (b) testing input flow through the full emulator path. Option (a) is a minimal 2-line addition to production code and is recommended as a follow-up if these tests cannot be structured via the bus path.

### 7.5 Disassembler Tests (`test_disasm.cpp`)

Suite: `"disasm"`

Uses emulator global `mem[]` (required by `emu_dis6502_decode()`).

| ID | Name | Setup | Expected | Pri |
|----|------|-------|----------|-----|
| T80 | NOP (1-byte implied) | mem[0x0400]=0xEA | Returns 1, contains "NOP" | P0 |
| T81 | LDA immediate (2-byte) | mem[0x0400]=0xA9, 0x42 | Returns 2, contains "LDA #$42" | P0 |
| T82 | JMP absolute (3-byte) | mem[0x0400]=0x4C, 0x00, 0xD0 | Returns 3, contains "JMP $D000" | P0 |
| T83 | STA zero-page,X | mem[0x0400]=0x95, 0x10 | Returns 2, contains "STA $10,X" | P1 |
| T84 | LDA (indirect,X) | mem[0x0400]=0xA1, 0x20 | Returns 2, contains "LDA ($20,X)" | P1 |
| T85 | LDA (indirect),Y | mem[0x0400]=0xB1, 0x30 | Returns 2, contains "LDA ($30),Y" | P1 |
| T86 | BEQ relative forward | mem[0x0400]=0xF0, 0x05 | Returns 2, contains "$0407" | P1 |
| T87 | BEQ relative backward | mem[0x0400]=0xF0, 0xFB | Returns 2, contains "$03FD" | P2 |
| T88 | Undefined opcode | mem[0x0400]=0x02 | Returns 1, contains "???" | P2 |
| T89 | ASL accumulator | mem[0x0400]=0x0A | Returns 1, contains "ASL A" | P1 |

### 7.6 Label Tests (`test_labels.cpp`)

Suite: `"labels"`

**Note**: `emu_labels_add()` and `emu_labels_clear()` are defined in `emu_labels.cpp` but NOT declared in `emu_labels.h`. Test file needs `extern` declarations for these functions.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T90 | Add and get | `emu_labels_add(0xD000, "main")`, get | List contains "main" | P0 |
| T91 | Get empty | `emu_labels_get(0x1234)` | Empty list | P0 |
| T92 | Multiple labels same addr | Add "foo" and "bar" at 0xD000 | Both in list | P1 |
| T93 | Clear | Add labels, `emu_labels_clear()`, get | Empty | P0 |
| T94 | No duplicates | Add "main" at 0xD000 twice | List has exactly one "main" | P1 |

### 7.7 Integration Tests (`test_integration.cpp`)

Suite: `"integration"`

Uses `EmulatorFixture`. Tests the full `emulator_step()` path including bus decode, TTY tick, and IRQ handling.

| ID | Name | Setup | Expected | Pri |
|----|------|-------|----------|-----|
| T95 | Boot to reset vector | Program at 0xD000, reset vector set | PC near 0xD000 after ~10 steps | P0 |
| T96 | Simple program exec | LDA #$42; STA $0200 at 0xD000 | mem[0x0200]==0x42 | P0 |
| T97 | Breakpoint hit | Set bp_mask[target], enable bp, run | emulator_check_break() returns true | P0 |
| T98 | Breakpoint disabled | Same as T97, bp disabled | emulator_check_break() returns false | P1 |
| T99 | Frame buffer via program | LDA #$48; STA $C000; LDA #$69; STA $C001 | frame_buffer[0]==0x48, [1]==0x69 | P0 |
| T100 | Breakpoint set parsing | `emulator_setbp("$D000 $D005 $D00A")` | bp_mask at all 3 addresses true | P1 |
| T101 | IRQ triggers vector | Set IRQ vector, enable IRQ, CLI in program | CPU reaches IRQ handler | P1 |

### Priority Summary

| Priority | Count | Description |
|----------|-------|-------------|
| P0 | ~42 | Core CPU, basic bus decode, reset, breakpoints, utilities, key disasm, labels |
| P1 | ~30 | Extended CPU, TTY registers, integration, addressing modes, flag edge cases |
| P2 | ~9 | Rotate ops, NMI, undefined opcodes, backward branches, edge cases |
| **Total** | **~81** | |

---

## 8. 6502 Firmware Test Plan (DEFERRED)

> **STATUS: DEFERRED.** Depends on GDB RSP stub implementation (see `docs/gdb-stub-spec-v2.md`). No implementation until the GDB stub is complete.

### 8.1 Approach

Firmware tests use a GDB RSP client to control the emulator programmatically:
1. Connect to emulator's GDB port (localhost:3333)
2. Load test ROM into memory via `M` packets
3. Set breakpoints at known addresses (from .sym file)
4. Continue execution, wait for breakpoint
5. Read registers/memory, verify expected state

### 8.2 Test Firmware ROMs

Individual test ROMs for isolated testing. Each ROM contains minimal startup, code under test, and a `BRK`/spin at a known address to signal completion.

Build: `cl65 -t none --cpu 6502 -C n8.cfg -o test.bin -Ln test.sym test.s`

### 8.3 Firmware Test Cases (DEFERRED)

| ID | Component | Test | Verify |
|----|-----------|------|--------|
| FW-1 | tty_recv | Single char receive | rb_base[0]==char, rb_end==1 |
| FW-2 | tty_recv | Wrap-around | rb_end resets to 0 |
| FW-3 | tty_recv | Full buffer discard | Extra char discarded |
| FW-4 | _tty_putc | Ready output | $C101 == char |
| FW-5 | _tty_putc | Wait loop | PC stuck at @wait |
| FW-6 | _tty_puts | Short string | Characters appear at $C101 |
| FW-7 | _tty_puts | Empty string | No write to $C101 |
| FW-8 | _tty_getc | With data | A == char, rb_start increments |
| FW-9 | _tty_getc | Empty buffer | A == 0 |
| FW-10 | _tty_peekc | Count available | A == expected count |
| FW-11 | _tty_peekc | Wrap count | A == correct wrapped count |
| FW-12 | IRQ handler | TTY char receive | Char in ring buffer, regs restored |
| FW-13 | IRQ handler | Multiple chars | All chars in ring buffer |
| FW-14 | BRK | Trap to brken | PC stuck at brken loop |
| FW-15 | init | Reset vector | PC reaches _init |
| FW-16 | init | Stack init | SP == $FF |
| FW-17 | init | Main reached | PC == _main address |
| FW-18 | main | Welcome banner | Characters output via TTY |
| FW-19 | main | Echo loop | Injected char echoed back |

---

## 9. Unified Harness Concept

### 9.1 Architecture

```
n8_test (single binary)
|
+-- C++ unit tests (immediate, in-process)
|   +-- test_utils.cpp       -- pure function tests
|   +-- test_cpu.cpp         -- isolated CPU instruction tests
|   +-- test_bus.cpp         -- bus decode via emulator_step()
|   +-- test_tty.cpp         -- TTY device register tests
|   +-- test_disasm.cpp      -- disassembler decode tests
|   +-- test_labels.cpp      -- label management tests
|   +-- test_integration.cpp -- multi-component scenarios
|
+-- Firmware integration tests (DEFERRED, requires GDB stub)
    +-- test_firmware.cpp    -- GDB RSP client, loads ROMs, verifies behavior
```

### 9.2 How They Coexist

- All tests share the same `n8_test` binary.
- C++ tests use doctest's `TEST_SUITE` for filtering: `-ts="cpu"`, `-ts="utils"`, etc.
- Firmware tests (when implemented) use a separate test suite: `TEST_SUITE("firmware")`.
- Firmware tests detect if the emulator is not running and skip with a message rather than failing.

### 9.3 Running

```bash
# All C++ tests
make && make test

# Specific suite
./n8_test -ts="cpu"

# Specific test case
./n8_test -tc="LDA immediate"

# DEFERRED: firmware tests
make -C test/firmware
./n8 --headless --gdb &
./n8_test -ts="firmware"
```

---

## 10. Design Decisions

### D1: Test framework is doctest (single-header)

**Disposition**: Adopted from Agent A (with Agent B partial support).

Use doctest as the sole test framework. Vendor `doctest.h` in `test/`. C++11, single header, expression decomposition, subcases, auto-registration. See Section 2 for full rationale.

### D2: Tests build as a separate binary

**Disposition**: Adopted -- unanimous (Agents A, B, C all agree).

Test sources compile into `n8_test`. No dependency on ImGui, SDL2, or OpenGL at link time. The main `n8` binary is unchanged.

### D3: Single Makefile with `test` target

**Disposition**: Adopted from Agent A (Agent C agrees).

Add a `test` target to the existing `Makefile`. No separate `Makefile.test` or CMake.

### D4: Test directory is `test/`

**Disposition**: Modified (compromise).

Agent A and B use `test/`, Agent C uses `tests/`. Adopting `test/` -- shorter, equally conventional.

### D5: doctest.h lives in `test/`

**Disposition**: Adopted from Agent A.

Test framework header stays with test code. Makefile test target adds `-Itest` to include flags.

### D6: Zero production code changes (link-time stubs)

**Disposition**: Adopted from Agent C (over Agent B's source split).

The test binary reuses production `.o` files unchanged. ImGui and gui_console symbols are provided by `test_stubs.cpp`. No `#ifdef`, no file splits, no refactoring. Agent B's `emulator_core.cpp` / `emulator_gui.cpp` split is cleaner long-term but adds complexity and modifies production code. For v1, stubs are simpler. The split can be done later if stubs become unwieldy.

### D7: Reuse production `.o` files

**Disposition**: Adopted from Agent C.

Tests link against the exact same object code that runs in production. No recompilation with different flags. ImGui headers must be available at compile time (already satisfied by the existing build), but no ImGui libraries are linked.

### D8: ImGui stub satisfies linker (~20 lines)

**Disposition**: Adopted from Agent C.

No-op ImGui namespace stubs for `Begin`, `End`, `Text`, etc. Never called, only satisfies the linker. One-time signature matching exercise.

### D9: CPU tests use CpuFixture (isolated, no emulator globals)

**Disposition**: Adopted -- Agents A, B agree. Agent C does not address this separately.

CPU instruction correctness tested against clean m6502.h API. Own `m6502_t`, own `mem[]`, minimal tick loop. No bus decode, no TTY, no IRQ handling side effects.

### D10: Bus decode / integration tests use EmulatorFixture (real globals)

**Disposition**: Adopted from Agent B.

Bus decode is embedded in `emulator_step()` with file-scope globals. Testing it requires the real function and globals. Each test resets all globals in fixture constructor.

### D11: Global state acceptable (reset between tests)

**Disposition**: Adopted from Agent A (Agents B, C agree pragmatically).

Emulator uses C-style globals by design. Each test resets via memset + m6502_init. No premature encapsulation.

### D12: Test suites map to categories

**Disposition**: Adopted from Agent A.

`TEST_SUITE("utils")`, `"cpu"`, `"bus"`, `"tty"`, `"disasm"`, `"labels"`, `"integration"`, (future: `"firmware"`). Enables selective test execution via doctest's `-ts` flag.

### D13: Test data -- no firmware dependency

**Disposition**: Adopted -- Agents A, B agree. Agent C partially agrees (allows ROM for integration tests but agrees unit tests should be self-contained).

All test programs are inline byte arrays. Tests bypass `emulator_init()` / `emulator_loadrom()`. No dependency on `N8firmware` or `N8firmware.sym`.

### D14: TTY tests call `tty_decode()` directly

**Disposition**: Adopted from Agent B.

Test `tty_decode()` with constructed pin masks. Do not test `tty_tick()` (terminal I/O dependency). Do not call `tty_init()` (raw mode side effects).

### D15: Pin construction helpers for TTY tests

**Disposition**: Adopted from Agent B.

`make_read_pins()` / `make_write_pins()` helpers in `test_helpers.h`.

### D16: Console stub captures output (inspectable buffer)

**Disposition**: Adopted from Agent C (Agent B has same idea).

`gui_con_printmsg()` stub pushes to `deque<string>`. Tests can verify debug message generation (breakpoint hit, label list, etc.).

### D17: Firmware tests use in-process GDB RSP client (DEFERRED)

**Disposition**: Adopted from Agent C.

When implemented, firmware tests use a C++ GDB RSP client compiled into `n8_test`. Keeps everything in one test runner with consistent output.

### D18: Firmware tests use isolated test ROMs (DEFERRED)

**Disposition**: Adopted from Agent C.

Individual test ROMs per component rather than testing against full production firmware.

### D19: Priority tiers (P0/P1/P2)

**Disposition**: Adopted from Agent B.

P0 = core operations (must pass for emulator to be operational). P1 = important coverage. P2 = edge cases. Guides incremental implementation.

### D20: `make && make test` build sequence

**Disposition**: Adopted from Agent C.

`make test` depends on production `.o` files. Run `make` first. Simple, no duplicated compilation rules.

---

## 11. Decision Reconciliation Log

### Conflict 1: Framework Selection

| Agent | Position |
|-------|----------|
| A | doctest (single-header, C++11, expression decomposition) |
| B | Leans GoogleTest but acknowledges doctest/Catch2 work |
| C | No framework (custom T_ASSERT macros, ~50 lines) |

**Resolution**: doctest.

**Rationale**: 2-of-3 support a real framework (A, B). Between frameworks, doctest wins on all criteria: single-header (matches C's simplicity goal), C++11 (matches project), Makefile-friendly, and expression decomposition (critical for CPU register debugging). Agent C's T_ASSERT macros print `"a != b"` on failure without showing actual values -- a significant debugging handicap for an emulator test suite. doctest provides everything Agent C's macros do plus expression decomposition, subcases, auto-registration, and test filtering for ~280KB of vendored code.

### Conflict 2: Production Code Changes (Source Split vs Link-Time Stubs)

| Agent | Position |
|-------|----------|
| A | Minimal refactoring needed (split or ifdef) |
| B | Split emulator.cpp into emulator_core.cpp + emulator_gui.cpp |
| C | Zero production changes; link-time stubs for ImGui |

**Resolution**: Zero production changes (link-time stubs).

**Rationale**: Agent C's approach is simpler for v1. The ImGui stub is ~20 lines of no-op functions that are never called -- they only satisfy the linker. Agent B's split is architecturally cleaner and should be considered for v2 if the stub list grows unwieldy or if production code refactoring is independently motivated. But for getting tests running: stubs win on simplicity. Agent A's position is compatible with either approach.

### Conflict 3: Test Directory Name (`test/` vs `tests/`)

| Agent | Position |
|-------|----------|
| A | `test/` |
| B | `tests/` |
| C | `tests/` |

**Resolution**: `test/` (overriding 2-of-3).

**Rationale**: Both are common conventions. `test/` is shorter and equally standard. The Makefile variable is `TEST_DIR = test`. This is a trivial naming choice with no functional impact; `test/` was chosen for brevity. (If Carlo prefers `tests/`, the change is a one-line Makefile edit.)

### Conflict 4: Fixture Design

| Agent | Position |
|-------|----------|
| A | General approach: reset globals per test, minimal bus loop for CPU tests |
| B | Detailed CpuFixture (own state) + EmulatorFixture (real globals) + pin helpers + assertion macros |
| C | No fixtures per se; test functions manage their own setup |

**Resolution**: Merged best elements from A and B.

**Rationale**: Agent B's CpuFixture and EmulatorFixture are well-designed and directly usable. CpuFixture isolates CPU tests from emulator globals (Agent A's D8 agrees). EmulatorFixture handles the global state reset that Agent A's D7 calls for. Agent B's pin construction helpers enable Agent C's TTY decode testing approach. Agent C's simpler style (no fixtures, manual setup) works for utility tests but doesn't scale for CPU/bus tests with repetitive setup. The unified spec adopts B's fixtures with A's design principles.

### Conflict 5: ROM / Firmware Dependency for Tests

| Agent | Position |
|-------|----------|
| A | No ROM dependency; inline byte arrays |
| B | No ROM dependency; bypass emulator_init() |
| C | Tests run from project root; emulator_init() loads ROM; `make && make firmware && make test` |

**Resolution**: No ROM dependency. Tests bypass `emulator_init()`.

**Rationale**: 2-of-3 (A, B) say no ROM dependency. Self-contained tests with inline byte arrays are more reliable, faster to run, and don't require the cc65 toolchain. Agent C's approach ties unit tests to firmware availability, which is fragile and adds build dependencies.

### Conflict 6: emu_dis6502.cpp Handling

| Agent | Position |
|-------|----------|
| A | Not specifically addressed |
| B | ifdef `emu_dis6502_window()` with `#ifndef N8_TEST_BUILD`, or split file |
| C | Reuse production .o, stub ImGui at link time |

**Resolution**: Link-time ImGui stubs (consistent with D6).

**Rationale**: Follows the zero-production-changes principle. The ImGui stub already covers `emu_dis6502_window()`'s unresolved symbols. No need for `#ifdef` or file splitting.
