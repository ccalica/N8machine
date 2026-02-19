# N8Machine Test Harness Specification v2

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| v1 | 2026-02-18 | Initial spec from 3-agent synthesis |
| v2 | 2026-02-19 | Incorporated implementer (Critic A) and methodology (Critic B) feedback. See Section 12 for full disposition log. |

---

## 1. Overview

### Purpose

Automated test harness for the N8Machine 6502 emulator. Covers the C++ emulation core (CPU, bus decode, devices, utilities) with a deferred plan for firmware-level integration tests.

### Scope

- Unit tests for pure utility functions
- CPU instruction correctness via isolated m6502.h tick loop
- Bus decode and memory-mapped device behavior
- TTY device register logic (including empty-queue edge case)
- IRQ pipeline behavior (assertion, masking, clearing, multi-bit)
- Disassembler opcode decode
- Label database operations
- Integration tests using the full `emulator_step()` path
- Firmware tests (DEFERRED -- requires GDB RSP stub)

### Design Principles

1. **Simplicity** -- Minimal infrastructure, no over-engineering.
2. **Near-zero production code changes** for the initial harness. All decoupling via link-time stubs, with one exception: a `tty_inject_char()` / `tty_buff_count()` accessor pair added to `emu_tty.cpp` (required for TTY and IRQ testability).
3. **Self-contained tests** -- No dependency on firmware ROM files. Test programs are inline byte arrays.
4. **Single test binary** -- One `make test` command, one executable, one exit code.
5. **Incremental** -- Start with P0 tests, expand coverage over time.
6. **Sequential execution** -- Tests run sequentially (doctest default). No parallel test execution. Global state is reset between tests via fixtures.

### Bugs Found During Spec Review

The following production code issues were identified by critics during specification review:

| ID | File | Description | Severity |
|----|------|-------------|----------|
| BUG-1 | `emu_tty.cpp:87-88` | `tty_decode()` register 3 read calls `tty_buff.front()` and `tty_buff.pop()` unconditionally. If queue is empty, `front()` is **undefined behavior** (crash or garbage). No guard on `tty_buff.size()`. | High |
| BUG-2 | `firmware/tty.s:47` | `_tty_peekc` uses `SBC rb_start` without first executing `SEC`. Carry flag from prior execution contaminates the result, causing off-by-one errors. | Medium |
| BUG-3 | `emu_labels.cpp:17` | `emu_labels_add()` takes `char*` not `const char*`. String literal arguments produce `-Wwrite-strings` warnings in C++. | Low |

---

## 2. Framework Selection

**Decision: doctest (single-header, C++11)**

doctest is the test framework. Vendor `doctest.h` directly in the repository.

### Acquisition

Download doctest v2.4.11 (latest stable):
```
https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
```

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
|   +-- doctest.h              # Vendored test framework (single header, v2.4.11)
|   +-- test_main.cpp          # doctest main entry point (2 lines)
|   +-- test_helpers.h         # Shared fixtures, helpers, pin constructors, externs
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
+-- src/                       # Minimal changes (tty_inject_char only)
+-- Makefile                   # Modified: add `test` target
+-- ...
```

### Naming Conventions

- Directory: `test/` (not `tests/`).
- Test files: `test_<module>.cpp` matching the source module under test.
- Test cases: Use `TEST_SUITE("category")` with descriptive `TEST_CASE("name")` strings.
- Stubs: `test_stubs.cpp` -- single file for all GUI/system link-time replacements.
- Helpers: `test_helpers.h` -- fixtures, pin constructors, common utilities.

### test_main.cpp

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

---

## 4. Build Integration

### CHIPS_IMPL Warning

**CRITICAL: NEVER define `CHIPS_IMPL` in any test file.** The m6502 implementation (3000+ lines of function bodies inside `#ifdef CHIPS_IMPL`) is already compiled into `emulator.o` (which defines `CHIPS_IMPL` at line 1). Test files include `m6502.h` for declarations only. Defining `CHIPS_IMPL` in any test file will cause duplicate symbol errors for every m6502 function.

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
	./$(TEST_EXE) < /dev/null

$(TEST_EXE): $(TEST_SRC_OBJS) $(TEST_OBJS)
	$(CXX) -o $@ $^

clean-test:
	rm -f $(TEST_EXE) $(TEST_BUILD_DIR)/*.o
```

**Note**: `test` runs with `< /dev/null` to prevent `tty_kbhit()` / `select()` from consuming unexpected stdin data in CI environments.

### Build Sequence

```bash
make && make test
```

`make test` reuses the production `.o` files (which are compiled with ImGui/SDL headers). The test binary links against these objects but does NOT link SDL2, ImGui, or OpenGL libraries. Unresolved ImGui symbols are satisfied by `test_stubs.cpp`.

**Note on `make test` without `make` first**: If production `.o` files don't exist, Make will attempt to build them using the production pattern rule with `$(CXXFLAGS)`. This requires SDL2-dev and ImGui headers. Running `make` first is recommended.

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

### ImGui Stub Derivation Procedure

Do NOT guess ImGui function signatures. Derive them from the actual production objects:

```bash
# Step 1: Build production objects
make

# Step 2: Extract unresolved ImGui symbols from the production .o files
nm -u build/emulator.o build/emu_dis6502.o | grep -i imgui | c++filt | sort -u

# Step 3: Write stub functions matching the exact demangled signatures
# Only function symbols need stubs. Enum constants (e.g., ImGuiInputTextFlags_AllowTabInput)
# are resolved at compile time and do NOT need stubs.
```

This is a one-time exercise. The resulting stubs go in `test_stubs.cpp`.

---

## 5. Decoupling Strategy

**Approach: Link-time stubs with one minimal production code addition.**

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
// IMPORTANT: These stubs satisfy unresolved symbols from emulator_show_* and
// emu_dis6502_window. They are NEVER called in tests.
//
// Stub signatures MUST match the exact mangled symbols in the production .o files.
// Derive them using: nm -u build/emulator.o build/emu_dis6502.o | grep imgui | c++filt
//
// Only function symbols need stubs. Compile-time constants (enum values like
// ImGuiInputTextFlags_AllowTabInput) are already resolved in the production .o
// files and do NOT need stubs.
//
// The stubs below are TEMPLATES based on source analysis. Before first compile,
// verify against actual nm output and adjust signatures as needed.

struct ImVec2 {
    float x, y;
    ImVec2() : x(0), y(0) {}
    ImVec2(float a, float b) : x(a), y(b) {}
};
struct ImVec4 {
    float x, y, z, w;
    ImVec4() : x(0), y(0), z(0), w(0) {}
    ImVec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

namespace ImGui {
    bool Begin(const char*, bool*, int) { return true; }
    void End() {}
    void Text(const char*, ...) {}
    void TextColored(const ImVec4&, const char*, ...) {}
    void SameLine(float, float) {}
    bool Checkbox(const char*, bool*) { return false; }
    // NOTE: InputText signature MUST match real ImGui. The callback parameter
    // is a function pointer, not void*. Verify with nm output.
    // bool InputText(const char*, char*, size_t, ImGuiInputTextFlags, ImGuiInputTextCallback, void*);
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

**After first build attempt**: if any ImGui symbol fails to link, run the `nm` procedure from Section 4 and fix the stub signature to match exactly. Pay special attention to `InputText` -- the callback parameter type affects C++ name mangling.

### 5.3 emu_tty.cpp -- Linked Directly (With Test Accessor Addition)

`emu_tty.cpp` has no GUI dependencies. `tty_kbhit()` returns 0 immediately when no keyboard input is pending (uses `select()` with zero timeout). `tty_tick()` calls `tty_kbhit()` and returns early -- safe in test environments when stdin is redirected from `/dev/null`.

**Production code addition** (the sole production change in this spec):

Add to `emu_tty.cpp`:

```cpp
void tty_inject_char(uint8_t c) {
    tty_buff.push(c);
}
int tty_buff_count() {
    return (int)tty_buff.size();
}
```

Add to `emu_tty.h`:

```cpp
void tty_inject_char(uint8_t);
int tty_buff_count();
```

**Rationale**: `tty_buff` is file-static (`std::queue<uint8_t>` in `emu_tty.cpp`). Without these accessors, 5 P0 tests (T74, T75, T76, T79, and IRQ integration T102) plus additional P1 tests are impossible to implement. The alternative (testing only through `tty_tick()` which reads from stdin) is not viable in automated tests. These 4 lines are the minimal bridge between test isolation and testability.

**stdin safety**: Every call to `emulator_step()` invokes `tty_tick()`, which calls `tty_kbhit()` via `select()` on stdin. In CI/CD or if stdin has unexpected data, this could consume input into `tty_buff`, corrupting test state. Running tests with `< /dev/null` (see Makefile `test` target) mitigates this. If `read()` inside `getch()` fails, `exit(-1)` is called -- hence the `/dev/null` redirect is important.

### 5.4 emulator_loadrom() / emulator_reset() -- Bypassed

`emulator_loadrom()` opens `"N8firmware"` from CWD. `emulator_reset()` calls both `emulator_loadrom()` and `emu_labels_init()`, both of which open files and crash if files are missing. Tests bypass `emulator_init()` and `emulator_reset()` entirely. Instead:
- Manually `memset(mem, 0, sizeof)` and write test programs as byte arrays
- Call `m6502_init(&cpu, &desc)` directly
- Set reset vectors manually

**WARNING**: Never call `emulator_init()`, `emulator_reset()`, `emulator_loadrom()`, `emu_labels_init()`, or `emu_labels_load()` from tests. They require files on disk (`N8firmware`, `N8firmware.sym`) and will crash the test process with `fopen` failure / `exit(-1)`.

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
| `tty_buff` | `emu_tty.cpp` | No | Via `tty_inject_char()` / `tty_buff_count()` |
| `desc` (m6502_desc_t) | `emulator.cpp` | No | Needs extern in `test_helpers.h` |

`test_helpers.h` will declare the necessary externs for test access without modifying production headers.

---

## 6. Test Fixtures & Helpers

### 6.1 Consolidated test_helpers.h

```cpp
#pragma once

// CRITICAL: Do NOT define CHIPS_IMPL in any test file.
// The m6502 implementation is already compiled into emulator.o.
#include "m6502.h"
#include "emulator.h"
#include "emu_tty.h"
#include "emu_labels.h"
#include "utils.h"

#include <cstring>
#include <vector>
#include <string>
#include <deque>

// ---- Externs for emulator globals not declared in headers ----
extern m6502_t cpu;
extern m6502_desc_t desc;
extern uint64_t pins;
extern uint8_t frame_buffer[];
extern uint64_t tick_count;

// ---- Externs for emu_labels functions not declared in emu_labels.h ----
// Note: emu_labels_add takes char* (not const char*). Use cast for string literals.
extern void emu_labels_add(uint16_t addr, char* label);
extern void emu_labels_clear();

// ---- Console stub helpers (defined in test_stubs.cpp) ----
extern std::deque<std::string>& stub_get_console_buffer();
extern void stub_clear_console_buffer();

// ---- Pin Construction Helpers ----
// Used for direct tty_decode() testing.
// tty_decode() takes uint64_t& (reference) and modifies the data bus.
// These helpers return values -- store in a local variable before passing
// to tty_decode():
//
//   uint64_t p = make_read_pins(0xC100);
//   tty_decode(p, 0);
//   uint8_t result = M6502_GET_DATA(p);
//
// Note: tty_decode() does not read the address from pins -- it receives
// the register offset as a separate parameter. The address in these helpers
// is included for bus-level test reuse but is ignored by tty_decode().

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

// ---- Disassembler assertion helper ----
// Use: CHECK(disasm_contains(buf, "LDA #$42"));
inline bool disasm_contains(const char* buf, const char* expected) {
    return std::string(buf).find(expected) != std::string::npos;
}

// ---- CpuFixture -- Isolated CPU Testing ----
// Owns its own m6502_t, mem[], and pins. Zero dependency on emulator globals.
// Used for CPU instruction correctness tests.

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

    // Returns number of ticks consumed. If 100 is returned, check timed_out().
    int run_until_sync() {
        int ticks = 0;
        do {
            tick();
            ticks++;
        } while (!(pins & M6502_SYNC) && ticks < 100);
        return ticks;
    }

    // Returns true if last run_until_sync hit the 100-tick safety limit.
    bool timed_out() {
        return !(pins & M6502_SYNC);
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

// ---- EmulatorFixture -- Bus Decode & Integration Testing ----
// Uses the actual emulator globals (mem[], frame_buffer[], cpu, pins).
// For bus decode and integration tests.

struct EmulatorFixture {
    EmulatorFixture() {
        memset(mem, 0, sizeof(uint8_t) * 65536);
        memset(frame_buffer, 0, 256);
        memset(bp_mask, 0, sizeof(bool) * 65536);
        memset(&desc, 0, sizeof(desc));
        tick_count = 0;
        emulator_enablebp(false);
        emu_labels_clear();
        tty_reset();
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

    void set_irq_vector(uint16_t addr) {
        mem[0xFFFE] = addr & 0xFF;
        mem[0xFFFF] = (addr >> 8) & 0xFF;
    }

    void step_n(int n) {
        for (int i = 0; i < n; i++) {
            emulator_step();
        }
    }
};
```

### 6.2 Design Notes

- **CpuFixture** owns all its state. Completely isolated from emulator globals. Used for CPU instruction correctness.
- **EmulatorFixture** resets ALL global state in its constructor, including `desc`, `emu_labels_clear()`, and `tty_reset()`. This prevents state leakage between tests (e.g., residual labels affecting disassembler output).
- **Pin helpers** return values that MUST be stored in a local `uint64_t` variable before passing to `tty_decode()`. `tty_decode()` takes `uint64_t&` (lvalue reference) and modifies the data bus bits. Passing an rvalue directly will not compile.
- **`timed_out()` helper** on CpuFixture lets tests assert they did not hit the 100-tick safety limit.

---

## 7. Test Categories & Cases

Test IDs are sequential (T01, T02, ...). Priority tiers:
- **P0** -- Core functionality. Must pass for the emulator to be considered operational.
- **P1** -- Important coverage. Should be implemented after P0.
- **P2** -- Edge cases and nice-to-haves.

### Implementation Phasing

**Phase 0 (Smoke Test)**: Before implementing all P0 tests, implement one test per module to validate the build pipeline and fixture setup end-to-end: T01, T25, T62, T71, T80, T90, T96. If these 7 tests pass, the harness infrastructure is validated.

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
| T21 | htoi hex string | `htoi((char*)"D000")` | Returns 0xD000 | P0 |
| T22 | htoi invalid | `htoi((char*)"xyz")` | Returns 0 | P1 |

**Note**: `htoi()`, `my_get_uint()`, and `range_helper()` take `char*` parameters. Use `(char*)` cast for string literal arguments to avoid `-Wwrite-strings` warnings. (These functions do not modify their input.)

#### IRQ Register Utility Tests

These test `emu_set_irq()` / `emu_clr_irq()` directly (no bus decode involved). Moved here from bus suite for clarity.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T69 | IRQ register set bit 1 | `emu_set_irq(1)` | `mem[0x00FF] & 0x02` (bit 1 set) | P0 |
| T69a | IRQ register set bit 0 | `emu_set_irq(0)` | `mem[0x00FF] & 0x01` (bit 0 set) | P0 |
| T69b | IRQ multi-bit set | `emu_set_irq(0)` then `emu_set_irq(1)` | `mem[0x00FF] == 0x03` | P1 |
| T70 | IRQ register clear | `emu_set_irq(1)` then `emu_clr_irq(1)` | `mem[0x00FF] == 0x00` | P0 |
| T70a | IRQ clear preserves other bits | `emu_set_irq(0)` + `emu_set_irq(1)`, then `emu_clr_irq(1)` | `mem[0x00FF] == 0x01` (bit 0 remains) | P1 |

**Important**: These tests must NOT call `emulator_step()` between set and check, because `emulator_step()` calls `IRQ_CLR()` (line 92) which zeroes `mem[0x00FF]` every tick.

### 7.2 CPU Instruction Tests (`test_cpu.cpp`)

Suite: `"cpu"`

Uses `CpuFixture`. Programs loaded as byte arrays. Reset vector at 0x0400 unless noted. All test programs should include a trailing `EA` (NOP) after the last instruction to prevent BRK from firing during sync-detection.

| ID | Name | Program (at 0x0400) | Expected | Pri |
|----|------|---------------------|----------|-----|
| T23 | Reset vector fetch | mem[0xFFFC]=0x00, mem[0xFFFD]=0xD0. Boot. | pc() == 0xD000 | P0 |
| T24 | SP after reset | Boot. | s() == 0xFD | P0 |
| T25 | LDA immediate | A9 42 EA | a()==0x42, Z=0, N=0 | P0 |
| T26 | LDA imm zero | A9 00 EA | a()==0, Z=1, N=0 | P0 |
| T27 | LDA imm negative | A9 80 EA | a()==0x80, Z=0, N=1 | P0 |
| T28 | LDA zero-page | mem[0x10]=0x77. A5 10 EA | a()==0x77 | P0 |
| T28a | LDA zero-page,X | mem[0x15]=0x33. A2 05 B5 10 EA (LDX #5; LDA $10,X) | a()==0x33 | P1 |
| T29 | LDA absolute | mem[0x1234]=0xAB. AD 34 12 EA | a()==0xAB | P0 |
| T29a | LDA absolute,X | mem[0x1236]=0xCC. A2 02 BD 34 12 EA (LDX #2; LDA $1234,X) | a()==0xCC | P1 |
| T29b | LDA absolute,Y | mem[0x1237]=0xDD. A0 03 B9 34 12 EA (LDY #3; LDA $1234,Y) | a()==0xDD | P1 |
| T30 | STA zero-page | A9 55 85 20 EA | mem[0x20]==0x55 | P0 |
| T31 | ADC no carry | A9 10 69 20 EA | a()==0x30, C=0, Z=0 | P0 |
| T32 | ADC carry out | A9 FF 69 01 EA | a()==0x00, C=1, Z=1 | P0 |
| T33 | ADC overflow | A9 7F 69 01 EA | a()==0x80, V=1, N=1 | P0 |
| T33a | ADC BCD mode | F8 18 A9 15 69 27 EA (SED; CLC; LDA #$15; ADC #$27) | a()==0x42 (BCD result) | P2 |
| T34 | SBC immediate | 38 A9 50 E9 20 EA (SEC; LDA #$50; SBC #$20) | a()==0x30, C=1 | P1 |
| T34a | SBC BCD mode | F8 38 A9 50 E9 18 EA (SED; SEC; LDA #$50; SBC #$18) | a()==0x32 (BCD result) | P2 |
| T35 | INX / DEX | A2 05 E8 CA CA EA | x()==0x04 | P0 |
| T36 | INY / DEY | A0 03 C8 88 88 EA | y()==0x02 | P0 |
| T37 | TAX / TXA | A9 AA AA A9 00 8A EA | a()==0xAA, x()==0xAA | P1 |
| T38 | PHA / PLA | A9 BB 48 A9 00 68 EA | a()==0xBB | P0 |
| T39 | JSR / RTS | 0x0400: 20 10 04 EA. 0x0410: 60. | pc()==0x0404 after 3 instrs (JSR, RTS, NOP) | P0 |
| T40 | JMP absolute | 4C 00 05 at 0x0400. A9 99 EA at 0x0500. | a()==0x99 | P0 |
| T41 | BEQ taken | A9 00 F0 02 A9 FF A9 42 EA | a()==0x42 | P0 |
| T42 | BEQ not taken | A9 01 F0 02 A9 55 EA | a()==0x55 | P0 |
| T43 | BNE taken | A9 01 D0 02 A9 FF A9 42 EA | a()==0x42 | P1 |
| T44 | CMP equal | A9 50 C9 50 EA | Z=1, C=1 | P0 |
| T45 | CMP less than | A9 10 C9 50 EA | Z=0, C=0, N=1 | P1 |
| T46 | AND immediate | A9 FF 29 0F EA | a()==0x0F | P1 |
| T47 | ORA immediate | A9 F0 09 0F EA | a()==0xFF | P1 |
| T48 | EOR immediate | A9 FF 49 AA EA | a()==0x55 | P1 |
| T49 | ASL accumulator | A9 81 0A EA | a()==0x02, C=1 | P1 |
| T50 | LSR accumulator | A9 03 4A EA | a()==0x01, C=1 | P1 |
| T51 | ROL accumulator | 38 A9 80 2A EA (SEC; LDA #$80; ROL A) | a()==0x01, C=1 | P1 |
| T52 | ROR accumulator | 38 A9 01 6A EA (SEC; LDA #$01; ROR A) | a()==0x80, C=1 | P1 |
| T53 | INC zero-page | mem[0x30]=0xFE. E6 30 E6 30 EA | mem[0x30]==0x00, Z=1 | P1 |
| T54 | DEC zero-page | mem[0x30]=0x01. C6 30 EA | mem[0x30]==0x00, Z=1 | P1 |
| T55 | LDA (indirect,X) | LDX #$04; LDA ($10,X). Pointer at 0x14->0x0300, data at 0x0300=0x77. | a()==0x77 | P1 |
| T56 | LDA (indirect),Y | LDY #$02; LDA ($20),Y. Pointer at 0x20->0x0300, data at 0x0302=0x88. | a()==0x88 | P1 |
| T57 | BIT test | mem[0x40]=0xC0. A9 C0 24 40 EA | Z=0, N=1, V=1 | P2 |
| T58 | CLI / SEI | 78 58 EA (SEI; CLI) | I=0 after CLI | P1 |
| T59 | CLC / SEC | 38 18 EA (SEC; CLC) | C=0 after CLC | P1 |
| T60 | PHP / PLP | 38 08 18 28 EA (SEC; PHP; CLC; PLP) | C=1 restored | P1 |
| T61 | NMI vector | Set NMI vector, assert NMI on pins | PC at NMI handler | P2 |
| T29c | LDA absolute,X page cross | mem[0x1100]=0xEE. A2 01 BD FF 10 EA (LDX #1; LDA $10FF,X) | a()==0xEE | P2 |

### 7.3 Bus Decode Tests (`test_bus.cpp`)

Suite: `"bus"`

Uses `EmulatorFixture`. Tests exercise `emulator_step()` with the real bus decode logic.

Verified from source: bus decode order is RAM first (lines 113-119), then device overlays. Reads: devices call `M6502_SET_DATA()` to override RAM. Writes: data goes to both `mem[]` and device.

**Note on write-to-device path**: Writes hit BOTH `mem[]` AND the device. This means `mem[0xC000]` through `mem[0xC0FF]` contain stale copies of frame buffer data, and `mem[0xC100]` through `mem[0xC10F]` contain stale TTY register writes. Tests should verify the device-side state (e.g., `frame_buffer[]`), not just `mem[]`.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T62 | RAM write plain | LDA #$55; STA $0200 | mem[0x0200]==0x55 | P0 |
| T63 | RAM read plain | mem[0x0200]=0xAA. LDA $0200 | a()==0xAA | P0 |
| T64 | Frame buffer write | LDA #$41; STA $C000 | frame_buffer[0]==0x41 | P0 |
| T64a | Frame buffer at $C000 is frame_buffer[0] | Write 0x99 to $C000, read back | frame_buffer[0]==0x99 (not a separate ctrl register) | P1 |
| T65 | Frame buffer read | frame_buffer[0]=0x42. LDA $C000 | a()==0x42 | P0 |
| T66 | Frame buffer end boundary | LDA #$7E; STA $C0FF | frame_buffer[0xFF]==0x7E | P0 |
| T67 | $C100 does not hit frame buffer | LDA #$99; STA $C100 | All 256 bytes of frame_buffer[] remain 0 (pre-zeroed by fixture). Hits TTY register 0 (write is no-op). | P1 |
| T67a | $C110 does not hit TTY decode | LDA #$AA; STA $C110 | mem[0xC110]==0xAA. Does NOT enter TTY decode path (0xC110 & 0xFFF0 = 0xC110 != 0xC100). | P1 |
| T68 | Write hits both mem and device | LDA #$33; STA $C005 | mem[0xC005]==0x33 AND frame_buffer[5]==0x33 | P1 |

**Source verification note**: `IRQ_SET(bit)` uses `mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)`. C operator precedence makes `0x01 << bit` evaluate before `|`, which is correct. `emu_clr_irq(bit)` uses `~(0x01 << bit)`, also correct. IRQ register tests are in Section 7.1 (utility tests) since they test functions directly without bus decode.

### 7.4 TTY Device Tests (`test_tty.cpp`)

Suite: `"tty"`

Tests call `tty_decode()` directly with constructed pin masks. Do NOT call `tty_init()` or `tty_tick()`.

**Important**: `tty_decode()` takes `uint64_t& pins` by reference. Pin helpers return rvalues -- store in a local variable first:
```cpp
uint64_t p = make_read_pins(0xC100);
tty_decode(p, 0);
uint8_t result = M6502_GET_DATA(p);
```

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T71 | Read Out Status (reg 0) | `tty_decode(read_pins, 0)` | Data bus == 0x00 | P0 |
| T72 | Read Out Data (reg 1) | `tty_decode(read_pins, 1)` | Data bus == 0xFF | P1 |
| T73 | Read In Status empty (reg 2) | Buffer empty. `tty_decode(read_pins, 2)` | Data bus == 0x00 | P0 |
| T74 | Read In Status with data (reg 2) | `tty_inject_char('A')`. `tty_decode(read_pins, 2)` | Data bus == 0x01 | P0 |
| T75 | Read In Data (reg 3) | `tty_inject_char(0x41)`. `tty_decode(read_pins, 3)` | Data bus == 0x41, `tty_buff_count()==0` | P0 |
| T75a | Read In Data empty buffer (UB) | Buffer empty. `tty_decode(read_pins, 3)` | **BUG-1**: This is undefined behavior in production code (`front()` on empty queue). Test documents the bug. Expected: crash or garbage data. (See bug note in Section 1.) | P0 |
| T76 | Read In Data clears IRQ | `tty_inject_char('X')`, `emu_set_irq(1)`. Read reg 3 to drain. | `mem[0x00FF]` bit 1 clear, `tty_buff_count()==0` | P0 |
| T77 | Write Out Data (reg 1) | `tty_decode(write_pins_with_'H', 1)` | putchar('H') called (capture stdout or accept side effect) | P1 |
| T78 | Write to read-only regs | Write to regs 0, 2, 3 | No crash, no side effects | P2 |
| T78a | Read TTY phantom addresses | `tty_decode(read_pins, 4)` through `tty_decode(read_pins, 15)` | Data bus == 0x00 for all (default case in switch). No crash. | P1 |
| T79 | tty_reset clears buffer | `tty_inject_char('A')`, `tty_inject_char('B')`. `tty_reset()` | `tty_buff_count()==0`, `mem[0x00FF]` bit 1 clear | P0 |

**Note on T75a**: This test documents BUG-1. If the production code is fixed (add `if (tty_buff.size() == 0) { data_bus = 0x00; break; }` guard before `front()`), update the expected result to data_bus == 0x00.

### 7.5 Disassembler Tests (`test_disasm.cpp`)

Suite: `"disasm"`

Uses emulator global `mem[]` (required by `emu_dis6502_decode()`). Each test should `memset(mem, 0, 65536)` at the top or use a fixture/SUBCASE that resets memory.

`emu_dis6502_decode()` signature: `int emu_dis6502_decode(int addr, char *menomic, int m_len)`. Returns instruction length. Use the `disasm_contains()` helper for assertions:
```cpp
char buf[256] = {0};
int len = emu_dis6502_decode(0x0400, buf, 256);
CHECK(len == 2);
CHECK(disasm_contains(buf, "LDA #$42"));
```

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

**Note**: `emu_labels_add()` and `emu_labels_clear()` are defined in `emu_labels.cpp` but NOT declared in `emu_labels.h`. The extern declarations for these are in `test_helpers.h`.

**WARNING**: Never call `emu_labels_init()` or `emu_labels_load()` from tests. They require `N8firmware.sym` on disk and call `exit(-1)` if the file is missing.

**Note**: `emu_labels_add()` takes `char*` not `const char*`. Cast string literals: `emu_labels_add(0xD000, (char*)"main")`.

| ID | Name | Action | Expected | Pri |
|----|------|--------|----------|-----|
| T90 | Add and get | `emu_labels_add(0xD000, (char*)"main")`, get | List contains "main" | P0 |
| T91 | Get empty | `emu_labels_get(0x1234)` | Empty list | P0 |
| T92 | Multiple labels same addr | Add "foo" and "bar" at 0xD000 | Both in list | P1 |
| T93 | Clear | Add labels, `emu_labels_clear()`, get | Empty | P0 |
| T94 | No duplicates | Add "main" at 0xD000 twice | List has exactly one "main" | P1 |
| T94a | Console list output | Add labels, call `emu_labels_console_list()` | Console buffer contains label text | P1 |

### 7.7 Integration Tests (`test_integration.cpp`)

Suite: `"integration"`

Uses `EmulatorFixture`. Tests the full `emulator_step()` path including bus decode, TTY tick, and IRQ handling.

| ID | Name | Setup | Expected | Pri |
|----|------|-------|----------|-----|
| T95 | Boot to reset vector | Program at 0xD000, reset vector set | PC near 0xD000 after ~10 steps | P0 |
| T96 | Simple program exec | LDA #$42; STA $0200 at 0xD000 | mem[0x0200]==0x42 | P0 |
| T96a | tick_count increments | Call `emulator_step()` N times | tick_count == N | P2 |
| T96b | cur_instruction tracks PC | Run 2-instruction program | `emulator_getci()` returns address of second instruction | P1 |
| T97 | Breakpoint hit | Set bp_mask[target], enable bp, run | emulator_check_break() returns true | P0 |
| T98 | Breakpoint disabled | Same as T97, bp disabled | emulator_check_break() returns false | P1 |
| T99 | Frame buffer via program | LDA #$48; STA $C000; LDA #$69; STA $C001 | frame_buffer[0]==0x48, [1]==0x69 | P0 |
| T100 | Breakpoint set parsing | `emulator_setbp((char*)"$D000 $D005 $D00A")` | bp_mask at all 3 addresses true | P1 |
| T100a | Breakpoints accumulate | `emulator_setbp((char*)"$D000")`, then `emulator_setbp((char*)"$D005")` | Both bp_mask[0xD000] and bp_mask[0xD005] true | P1 |
| T100b | Empty breakpoint string | `emulator_setbp((char*)"")` | No crash, no breakpoints changed | P1 |
| T100c | Invalid breakpoint string | `emulator_setbp((char*)"garbage")` | No crash | P2 |
| T100d | Log breakpoints | Set BPs at 0xD000 and 0xD005, call `emulator_logbp()` | Console buffer contains both addresses | P1 |

#### IRQ Integration Tests

These test the full IRQ pipeline through `emulator_step()`. They require `tty_inject_char()` to populate the TTY buffer, which causes `tty_tick()` to assert IRQ.

| ID | Name | Setup | Expected | Pri |
|----|------|-------|----------|-----|
| T101 | IRQ triggers vector | Set IRQ vector at 0xD100, program at 0xD000 with CLI + NOP loop. `tty_inject_char('A')`. Step enough ticks. | CPU reaches IRQ handler (PC near 0xD100) | P0 |
| T101a | IRQ masked by I flag | Program with SEI (no CLI). `tty_inject_char('A')`. Step. | CPU does NOT vector to IRQ handler. Stays in main program. | P0 |
| T101b | IRQ timing -- completes current instruction | Program: CLI, LDA #$42. Inject char before stepping. | After IRQ, a()==0x42 (LDA completed before vectoring) | P1 |
| T101c | IRQ after buffer drain | Inject char, step until IRQ fires. Read reg 3 to drain buffer (via `tty_decode`). Step again. | IRQ pin de-asserts on subsequent step (tty_buff empty, tty_tick won't re-set) | P1 |
| T101d | IRQ multi-bit persistence | Call `emu_set_irq(0)` before step. Inject char (sets bit 1 via tty_tick). After step, check. | `mem[0x00FF]` has both bits after tty_tick, before next IRQ_CLR(). **Note**: Due to IRQ_CLR() at line 92, bits set outside tty_tick are lost each step. This test documents that architectural behavior. | P2 |

**Implementation guidance for T101**: The program should be:
```
0xD000: 58       CLI
0xD001: EA       NOP  (infinite NOP loop or JMP $D001)
0xD002: 4C 01 D0 JMP $D001
```
IRQ handler at 0xD100: any instruction (e.g., `A9 FF` LDA #$FF) followed by `40` (RTI).
Set `mem[0xFFFE]=0x00, mem[0xFFFF]=0xD1` (IRQ vector = 0xD100).
Call `tty_inject_char('A')` to populate the buffer. Then `step_n(50)` or similar.
Check `emulator_getpc()` is near 0xD100 (in the IRQ handler) or has returned from it.

### 7.8 Disassembler Integration Tests

| ID | Name | Setup | Expected | Pri |
|----|------|-------|----------|-----|
| T89a | emu_dis6502_log output | Load known bytes at 0x0400. Call `emu_dis6502_log((char*)"$0400+$05")`. | Console buffer contains expected disassembly lines. | P1 |

### Priority Summary

| Priority | Count | Description |
|----------|-------|-------------|
| P0 | ~45 | Core CPU, basic bus decode, reset, breakpoints, utilities, key disasm, labels, TTY with inject, IRQ pipeline basics |
| P1 | ~38 | Extended CPU, TTY phantom addresses, integration, addressing modes, flag edge cases, IRQ clearing, breakpoint accumulation, disasm log |
| P2 | ~14 | BCD, NMI, undefined opcodes, page crossing, infinite loop timeout, tick_count, backward branches |
| **Total** | **~97** | |

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
| FW-3a | tty_recv | Boundary at 31 chars | 31st char accepted, 32nd triggers `CMP #$E1` discard |
| FW-3b | tty_recv | Magic constant `#$E1` validation | Fill to 32, next char is discarded. Validates the `#$E1`/`#$FF` boundary logic. |
| FW-4 | _tty_putc | Ready output | $C101 == char |
| FW-5 | _tty_putc | Wait loop | PC stuck at @wait |
| FW-6 | _tty_puts | Short string | Characters appear at $C101 |
| FW-7 | _tty_puts | Empty string | No write to $C101 |
| FW-8 | _tty_getc | With data | A == char, rb_start increments |
| FW-8a | _tty_getc | Wrap-around | Fill and drain buffer until rb_start wraps past rb_len. Verify correct char after wrap. |
| FW-9 | _tty_getc | Empty buffer | A == 0 |
| FW-10 | _tty_peekc | Count available | A == expected count |
| FW-10a | _tty_peekc | Carry flag bug | Call `_tty_peekc` with carry flag clear. **BUG-2**: SBC without SEC means result is off by one if carry not set. Document actual behavior. |
| FW-11 | _tty_peekc | Wrap count | A == correct wrapped count |
| FW-12 | IRQ handler | TTY char receive | Char in ring buffer, regs restored |
| FW-13 | IRQ handler | Multiple chars | All chars in ring buffer |
| FW-14 | BRK | Trap to brken | PC stuck at brken loop |
| FW-14a | BRK vs IRQ distinction | Trigger IRQ and BRK in sequence. | Handler correctly identifies each via B flag on stacked status register. |
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
|   +-- test_utils.cpp       -- pure function tests + IRQ register tests
|   +-- test_cpu.cpp         -- isolated CPU instruction tests
|   +-- test_bus.cpp         -- bus decode via emulator_step()
|   +-- test_tty.cpp         -- TTY device register tests
|   +-- test_disasm.cpp      -- disassembler decode tests
|   +-- test_labels.cpp      -- label management tests
|   +-- test_integration.cpp -- multi-component scenarios + IRQ pipeline
|
+-- Firmware integration tests (DEFERRED, requires GDB stub)
    +-- test_firmware.cpp    -- GDB RSP client, loads ROMs, verifies behavior
```

### 9.2 How They Coexist

- All tests share the same `n8_test` binary.
- C++ tests use doctest's `TEST_SUITE` for filtering: `-ts="cpu"`, `-ts="utils"`, etc.
- Firmware tests (when implemented) use a separate test suite: `TEST_SUITE("firmware")`.
- Firmware tests detect if the emulator is not running and skip with a message rather than failing.
- Tests run sequentially (doctest default). No parallel execution. Global state reset by fixtures prevents cross-test contamination.

### 9.3 Running

```bash
# All C++ tests (stdin redirected to /dev/null for safety)
make && make test

# Specific suite
./n8_test -ts="cpu" < /dev/null

# Specific test case
./n8_test -tc="LDA immediate" < /dev/null

# DEFERRED: firmware tests
make -C test/firmware
./n8 --headless --gdb &
./n8_test -ts="firmware"
```

---

## 10. Design Decisions

### D1: Test framework is doctest (single-header)

**Disposition**: Adopted from Agent A (with Agent B partial support).

Use doctest as the sole test framework. Vendor `doctest.h` v2.4.11 in `test/`. C++11, single header, expression decomposition, subcases, auto-registration. See Section 2 for full rationale.

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

### D6: Near-zero production code changes (link-time stubs + tty_inject_char)

**Disposition**: Modified from Agent C's zero-change position.

The test binary reuses production `.o` files unchanged for ImGui and gui_console symbols (satisfied by `test_stubs.cpp`). No `#ifdef`, no file splits, no refactoring. The sole production change is adding `tty_inject_char()` and `tty_buff_count()` to `emu_tty.cpp` (4 lines). This is necessary because `tty_buff` is file-static and 5+ P0 tests are otherwise impossible. Agent B's `emulator_core.cpp` / `emulator_gui.cpp` split is cleaner long-term but adds complexity. Simplicity wins for v1.

### D7: Reuse production `.o` files

**Disposition**: Adopted from Agent C.

Tests link against the exact same object code that runs in production. No recompilation with different flags. ImGui headers must be available at compile time (already satisfied by the existing build), but no ImGui libraries are linked.

### D8: ImGui stubs derived from `nm` output

**Disposition**: Modified from v1 (which guessed signatures).

The spec provides template stubs based on source analysis, but the implementer MUST verify against `nm -u` output before committing. Only function symbols need stubs; compile-time constants (enums) are already resolved in production objects.

### D9: CPU tests use CpuFixture (isolated, no emulator globals)

**Disposition**: Adopted -- Agents A, B agree.

CPU instruction correctness tested against clean m6502.h API. Own `m6502_t`, own `mem[]`, minimal tick loop. No bus decode, no TTY, no IRQ handling side effects.

### D10: Bus decode / integration tests use EmulatorFixture (real globals)

**Disposition**: Modified from v1.

EmulatorFixture now resets ALL global state: `mem`, `frame_buffer`, `bp_mask`, `desc`, `tick_count`, labels (via `emu_labels_clear()`), and TTY buffer (via `tty_reset()`). This prevents state leakage between tests.

### D11: Global state acceptable (reset between tests)

**Disposition**: Adopted from Agent A (Agents B, C agree pragmatically).

Emulator uses C-style globals by design. Each test resets via memset + m6502_init. No premature encapsulation.

### D12: Test suites map to categories

**Disposition**: Adopted from Agent A.

`TEST_SUITE("utils")`, `"cpu"`, `"bus"`, `"tty"`, `"disasm"`, `"labels"`, `"integration"`, (future: `"firmware"`). Enables selective test execution via doctest's `-ts` flag.

### D13: Test data -- no firmware dependency

**Disposition**: Adopted -- Agents A, B agree.

All test programs are inline byte arrays with trailing NOPs. Tests bypass `emulator_init()` / `emulator_loadrom()`. No dependency on `N8firmware` or `N8firmware.sym`.

### D14: TTY tests call `tty_decode()` directly

**Disposition**: Adopted from Agent B.

Test `tty_decode()` with constructed pin masks. Do not test `tty_tick()` (terminal I/O dependency). Do not call `tty_init()` (raw mode side effects).

### D15: Pin construction helpers for TTY tests

**Disposition**: Modified from v1.

`make_read_pins()` / `make_write_pins()` helpers in `test_helpers.h`. Documentation added that these return rvalues which MUST be stored in local variables before passing to `tty_decode()` (which takes `uint64_t&`). Address parameter retained for bus-level reuse even though `tty_decode()` ignores it.

### D16: Console stub captures output (inspectable buffer)

**Disposition**: Adopted from Agent C (Agent B has same idea).

`gui_con_printmsg()` stub pushes to `deque<string>`. Tests can verify debug message generation (breakpoint hit, label list, etc.).

### D17: Firmware tests use in-process GDB RSP client (DEFERRED)

**Disposition**: Adopted from Agent C.

When implemented, firmware tests use a C++ GDB RSP client compiled into `n8_test`. Keeps everything in one test runner with consistent output.

### D18: Firmware tests use isolated test ROMs (DEFERRED)

**Disposition**: Adopted from Agent C.

Individual test ROMs per component rather than testing against full production firmware.

### D19: Priority tiers (P0/P1/P2) with Phase 0 smoke test

**Disposition**: Modified from v1.

P0 = core operations. P1 = important coverage. P2 = edge cases. Added Phase 0 milestone: 7 smoke tests (one per module) to validate harness infrastructure before implementing all P0 tests.

### D20: `make test` with stdin redirect

**Disposition**: Modified from v1.

`make test` runs with `< /dev/null` to prevent `tty_kbhit()` / `select()` / `getch()` from consuming unexpected stdin data or crashing with `exit(-1)` on `read()` failure.

### D21: CHIPS_IMPL guard documented

**Disposition**: New decision from Critic A Issue #1.

Explicit warning in spec: never define `CHIPS_IMPL` in test files. The implementation lives in `emulator.o`. Test files include `m6502.h` for declarations only.

### D22: IRQ register tests moved to utility suite

**Disposition**: New decision from Critic A Issue #24 and Critic B Issue #21.

T69/T70 (and new variants) test `emu_set_irq()` / `emu_clr_irq()` directly without bus decode. Moved from `"bus"` to `"utils"` suite for clarity.

### D23: Production bugs documented in spec

**Disposition**: New decision from Critic B Issues #2 and #4.

Bugs found during specification review (BUG-1: empty queue UB, BUG-2: carry flag in `_tty_peekc`, BUG-3: `char*` vs `const char*`) are documented in Section 1. Tests T75a and FW-10a explicitly exercise these bugs.

### D24: `emulator_reset()` never called in tests

**Disposition**: New decision from Critic B Issue #3.

`emulator_reset()` calls `emulator_loadrom()` and `emu_labels_init()`, both of which open files and crash if missing. Tests never call it. CPU reset is achieved by setting `M6502_RES` on pins and ticking, or by calling `m6502_init()` directly.

---

## 11. Decision Reconciliation Log (v1)

*Retained from v1 for historical context. These document the original 3-agent reconciliation.*

### Conflict 1: Framework Selection

| Agent | Position |
|-------|----------|
| A | doctest (single-header, C++11, expression decomposition) |
| B | Leans GoogleTest but acknowledges doctest/Catch2 work |
| C | No framework (custom T_ASSERT macros, ~50 lines) |

**Resolution**: doctest. 2-of-3 support a real framework (A, B). doctest wins on all criteria.

### Conflict 2: Production Code Changes (Source Split vs Link-Time Stubs)

| Agent | Position |
|-------|----------|
| A | Minimal refactoring needed (split or ifdef) |
| B | Split emulator.cpp into emulator_core.cpp + emulator_gui.cpp |
| C | Zero production changes; link-time stubs for ImGui |

**Resolution**: Link-time stubs with `tty_inject_char()` exception (see D6).

### Conflict 3: Test Directory Name

**Resolution**: `test/` (see D4).

### Conflict 4: Fixture Design

**Resolution**: Merged best elements from A and B (see D9, D10).

### Conflict 5: ROM / Firmware Dependency

**Resolution**: No ROM dependency. Tests bypass `emulator_init()` (see D13).

### Conflict 6: emu_dis6502.cpp Handling

**Resolution**: Link-time ImGui stubs (see D6, D8).

---

## 12. Critique Disposition Log

Every issue raised by Critic A (implementer) and Critic B (methodology) is dispositioned below.

### Critic A (Implementer) Issues

| ID | Summary | Disposition | Rationale | Spec Change |
|----|---------|-------------|-----------|-------------|
| A-1 | CHIPS_IMPL duplicate symbol risk | **Accept** | Verified: `emulator.cpp` line 1 defines `CHIPS_IMPL`. m6502 implementation (lines 398-3097 of m6502.h) compiles into `emulator.o`. Test files must NOT define it. Critic's analysis is exactly correct. | Added CHIPS_IMPL warning in Section 4, D21, and `test_helpers.h` comment. |
| A-2 | ImGuiInputTextFlags enum -- not a linker issue | **Accept** | Verified: enum constants are compile-time, resolved in production `.o`. Not a blocker. Spec should clarify stubs only cover function symbols. | Added clarification in Section 5.2 that only function symbols need stubs. |
| A-3 | InputText stub signature mismatch (void* vs function pointer) | **Accept** | Verified: C++ name mangling depends on exact parameter types. `ImGuiInputTextCallback` is a function pointer type, not `void*`. Mangled names will differ. | Changed Section 5.2 to prescribe `nm -u` derivation procedure rather than guessed signatures. Template stubs kept as starting point with explicit "verify" warning. |
| A-4 | ImVec2/ImVec4 struct redefinitions may not match | **Accept** | Verified: real ImVec4 needs constructor `ImVec4(float,float,float,float)` (used at emu_dis6502.cpp:219). Spec's original `ImVec4` had no constructors. | Added constructors to both `ImVec2` and `ImVec4` stubs. Added `nm` derivation procedure. |
| A-5 | Missing SameLine overloads | **Reject** | Critic self-resolves: default parameters don't change the mangled symbol. Only one symbol generated. `main.o` (which has the int-literal call) is not linked. | No change. |
| A-6 | `make test` dependency chain | **Accept (partial)** | Verified: running `make test` without `make` first will attempt to build production `.o` with production CXXFLAGS (which requires SDL2-dev). This is correct behavior but should be documented. Not worth adding `test: all` dependency since it would force full rebuild. | Added note in Section 4 about `make test` requiring production objects. |
| A-7 | emu_labels_init() calls exit(-1) | **Accept** | Verified: `emu_labels.cpp` line 47: `if(!fp) { exit(-1); }`. Tests must never call `emu_labels_init()` or `emu_labels_load()`. | Added explicit WARNING in Sections 5.4 and 7.6. |
| A-8 | Exhaustive ImGui symbol audit missing | **Accept** | Verified: ImVec4 constructor missing from v1 stubs. The `nm` procedure is the correct approach. | Added `nm -u` derivation procedure in Section 4. Added missing ImVec4 constructors. |
| A-9 | gui_console stubs are complete | **Accept** | Verified: stubs match `gui_console.h` exactly. | No change needed. |
| A-10 | emu_dis6502.o refs gui_con_printmsg -- covered | **Accept** | Verification note. | No change. |
| A-11 | emu_labels.o refs gui_con_printmsg -- covered | **Accept** | Verification note. | No change. |
| A-12 | emu_tty.o refs emu_set_irq/emu_clr_irq -- covered | **Accept** | Verification note. | No change. |
| A-13 | tty_decode() takes uint64_t& reference, not value | **Accept** | Verified: `emu_tty.h` line 12: `void tty_decode(uint64_t&, uint8_t)`. Pin helpers return rvalues that can't bind to non-const lvalue reference. | Added documentation in `test_helpers.h` (Section 6.1) and Section 7.4 with usage example showing required local variable storage. |
| A-14 | tty_decode() called with register offset, not address | **Accept** | Verified: `emulator.cpp` line 143: `dev_reg = (addr - 0xC100) & 0x00FF`. The address in pins is irrelevant to tty_decode(). Spec's test cases were already correct (passing register 0-3). | Added clarifying comment to pin helpers in `test_helpers.h`. |
| A-15 | EmulatorFixture doesn't memset desc | **Accept** | Verified: `desc` is a global zero-initialized once, but tests should defensively reset it. | Added `memset(&desc, 0, sizeof(desc))` to EmulatorFixture constructor. |
| A-16 | tty_tick/tty_kbhit select() on stdin risk | **Accept** | Verified: `emu_tty.cpp` lines 34-39 call `select()` on fd 0. Lines 45-48: if `read()` fails, `exit(-1)`. | Added `< /dev/null` to Makefile `test` target. Added documentation in Section 5.3. |
| A-17 | IRQ_CLR() zeroes mem[0x00FF] every tick | **Accept** | Verified: `emulator.cpp` line 92. IRQ register tests must not call `emulator_step()` between set/check. IRQ integration tests work because `tty_tick()` re-asserts IRQ when `tty_buff` is non-empty. | Added detailed implementation guidance for T101. Moved IRQ register tests to utility suite. |
| A-18 | T74-T76, T79 impossible without tty_buff access | **Accept** | Verified: `tty_buff` is file-static `queue<uint8_t>` in `emu_tty.cpp` line 17. No public accessor. 5 P0 tests blocked. | Added `tty_inject_char()` and `tty_buff_count()` as the sole production code change. Updated D6. |
| A-19 | T67 frame buffer boundary test logic ambiguous | **Accept** | Verified: 0xC100 & 0xFF00 = 0xC100 != 0xC000 (no frame buffer match). 0xC100 & 0xFFF0 = 0xC100 (TTY match, register 0 write = no-op). | Clarified T67 expected result: "All 256 bytes of frame_buffer[] remain 0." |
| A-20 | T23 reset vector test needs boot sequence | **Reject** | Critic self-resolves: `CpuFixture::boot()` ticks until SYNC, which is correct. No ambiguity. | No change. |
| A-21 | LDA immediate tests need precise tick counting | **Accept (partial)** | The concern about BRK firing is valid though not strictly a bug. Adding trailing NOPs is good defensive practice. | Added trailing `EA` (NOP) to all test programs. |
| A-22 | T39 JSR/RTS test description ambiguous | **Accept** | Verified: after JSR, RTS, NOP the PC is 0x0404. | Changed T39 expected to `pc()==0x0404`. |
| A-23 | T42 BEQ not taken test has "..." | **Accept** | Incomplete byte sequence. | Completed byte sequence: `A9 01 F0 02 A9 55 EA`. |
| A-24 | T69/T70 not really bus decode tests | **Accept** | Verified: these call `emu_set_irq()` / `emu_clr_irq()` directly. No bus decode involved. | Moved to utility suite (Section 7.1). |
| A-25 | Disassembler tests use global mem[] | **Accept** | Each disasm test should reset memory. | Added note: "Each test should `memset(mem, 0, 65536)` at the top." |
| A-26 | emu_labels_add/clear not in header; char* issue | **Accept** | Verified: `emu_labels.h` does not declare `emu_labels_add` or `emu_labels_clear`. `emu_labels_add` takes `char*` not `const char*`. | Added extern declarations to `test_helpers.h`. Added `(char*)` cast guidance in Sections 7.1 and 7.6. |
| A-27 | T101 blocked by tty_buff access | **Accept** | Same root cause as A-18. Resolved by `tty_inject_char()`. | T101 now uses `tty_inject_char()`. Added implementation guidance. |
| A-28 | No consolidated test_helpers.h | **Accept** | Spec scattered fixture/helper definitions across 5 sections. | Provided complete `test_helpers.h` in Section 6.1. |
| A-29 | No test_main.cpp content shown | **Accept** | Trivial but helpful. | Added `test_main.cpp` listing in Section 3. |
| A-30 | doctest.h version/URL not specified | **Accept** | | Added version (2.4.11) and URL in Section 2. |
| A-31 | Disassembler output assertion pattern | **Accept** | | Added `disasm_contains()` helper to `test_helpers.h` with usage example in Section 7.5. |
| A-32 | emu_dis6502_decode uses int addr | **Defer** | Minor type note. Implicit conversion from uint16_t to int is harmless. | No change. |
| A-33 | No guidance on test execution order | **Accept** | Tests must run sequentially due to shared global state. | Added note in Section 1 (Design Principles #6) and Section 9.2. |
| A-34 | Pin helpers overengineered | **Reject** | Address parameter retained for bus-level test reuse. Comment added per critic suggestion. | Added comment to pin helpers explaining address inclusion. |
| A-35 | Console buffer may not be needed for P0 | **Reject** | Infrastructure is 5 lines and used by P1 tests. Keep it. | No change. |
| A-36 | 81 test cases overscoped | **Accept (partial)** | Added Phase 0 smoke test milestone (7 tests, one per module) to validate harness before full P0 implementation. | Added Phase 0 in Section 7 intro. |

### Critic B (Methodology) Issues

| ID | Summary | Disposition | Rationale | Spec Change |
|----|---------|-------------|-----------|-------------|
| B-1 | IRQ pipeline undertested | **Accept** | Verified: `emulator_step()` lines 92-110 implement a 3-phase pipeline (clear, re-assert via tty_tick, check). Only one test (T101, P1) in v1. | Added T101a (P0, IRQ masked), T101b (P1, timing), T101c (P1, drain), T101d (P2, multi-bit). Promoted T101 to P0. |
| B-2 | tty_decode() empty queue UB | **Accept** | Verified: `emu_tty.cpp` lines 87-88: `tty_buff.front()` then `tty_buff.pop()` with no size check. `front()` on empty `std::queue` is UB per C++ standard. | Added T75a (P0) to test this. Documented as BUG-1 in Section 1. |
| B-3 | emulator_reset() untested / unsafe | **Accept** | Verified: `emulator.cpp` lines 259-264: calls `emulator_loadrom()` and `emu_labels_init()`, both open files. Will crash in test environment. | Added WARNING in Section 5.4. Added D24 documenting that tests never call `emulator_reset()`. CPU reset tested via `M6502_RES` pin. |
| B-4 | TTY bus mask 0xFFF0 selects 16 addresses, not 4 | **Accept** | Verified: `emulator.cpp` line 142: `BUS_DECODE(addr, 0xC100, 0xFFF0)` matches 0xC100-0xC10F (16 addresses). `dev_reg` = `(addr-0xC100) & 0xFF` yields 0-15. Registers 4-15 hit `default` case in `tty_decode()`. | Added T78a (P1, phantom addresses 4-15). Added T67a (P1, verify 0xC110 does NOT hit TTY). |
| B-5 | IRQ_SET operator precedence / multi-bit untested | **Accept** | Verified: `0x01 << bit` evaluates correctly due to precedence. But only bit=1 was tested in v1. | Added T69a (P0, bit 0), T69b (P1, multi-bit), T70a (P1, selective clear). |
| B-6 | tty_tick select() overhead and CI risk | **Accept** | Same as A-16. Thousands of syscalls per integration test, plus CI data corruption risk. | Added `< /dev/null` to test target. Documented risk in Section 5.3. |
| B-7 | Breakpoint clearing behavior untested | **Accept** | Verified: `emulator_setbp()` (line 181-203) only sets bits, never clears. Old version cleared all first but is commented out. | Added T100a (accumulate), T100b (empty string), T100c (garbage input). |
| B-8 | emulator_logbp() and emu_labels_console_list() untested | **Accept** | Both output through `gui_con_printmsg()` which is stubbed with inspectable buffer. Testable and useful. | Added T100d (logbp) and T94a (labels console list). |
| B-9 | Frame buffer $C000 is also TXT_CTRL | **Accept** | Verified: emulator treats 0xC000-0xC0FF as flat 256-byte frame_buffer. `devices.inc` defines TXT_CTRL at $C000 but emulator has no separate ctrl register. Worth documenting. | Added T64a (P1) to document that $C000 is frame_buffer[0], not a separate register. |
| B-10 | run_until_sync 100-tick timeout | **Accept** | | Added `timed_out()` helper to CpuFixture. Documented 100-tick safety limit. |
| B-11 | emu_dis6502_log() untested | **Accept** | Entry point for console `d` command. Uses `range_helper()` + `emu_dis6502_decode()` + `gui_con_printmsg()`. | Added T89a (P1). |
| B-12 | No BCD mode tests | **Accept** | Verified: `m6502_desc_t` has `bcd_disabled` field. CpuFixture zeroes `desc`, so `bcd_enabled = !false = true`. BCD mode is active by default. | Added T33a (P2, ADC BCD) and T34a (P2, SBC BCD). |
| B-13 | ROL/ROR should be P1 not P2 | **Accept** | ROL/ROR are common in 6502 firmware (address calculation, bit manipulation). More important than some current P1 tests. | Promoted T51, T52 to P1. |
| B-14 | Missing addressing mode coverage | **Accept** | Zero-page,X; absolute,X; absolute,Y not tested for CPU execution. | Added T28a (P1), T29a (P1), T29b (P1), T29c (P2, page crossing). |
| B-15 | EmulatorFixture tick_count reset | **Accept** | Fixture correctly resets tick_count=0. No test uses it. | Added T96a (P2) to verify tick_count increments. |
| B-16 | cur_instruction tracking | **Accept** | `emulator_step()` lines 81-84 set `cur_instruction` when bus address matches PC. Not tested. | Added T96b (P1) to verify `emulator_getci()`. |
| B-17 | Labels array not reset in EmulatorFixture | **Accept** | Residual labels could affect disassembler output between tests. | Added `emu_labels_clear()` to EmulatorFixture constructor. |
| B-18 | tty_buff access strategy contradicts itself | **Accept** | v1 said "prefer option (b) no production changes" but T74-T76 say "push byte to buffer." Contradiction. | Resolved: adopted option (a) -- `tty_inject_char()` added. D6 updated. |
| B-19 | emu_bus_read() untested | **Defer** | Minor utility function that returns `pins & M6502_RW`. Low value for P0/P1. | Noted for future P2 test. |
| B-20 | htoi returns int, potential sign issues | **Defer** | Works correctly for all 16-bit values on 32-bit int platforms (which this project targets). Low risk. | No change. |
| B-21 | T69/T70 naming -- bus vs utility | **Accept** | Same as A-24. | Moved to utility suite. |
| B-22 | my_itoa size=0 edge case | **Defer** | Never called with size=0 in production code. Low priority edge case. | No change. |
| B-23 | range_helper comma separator untested | **Defer** | Multi-range parsing is tested end-to-end through `emulator_show_memdump_window` in production. Low priority for unit test harness. | No change. |
| B-24 | _tty_peekc carry flag bug (firmware) | **Accept** | Verified: `tty.s` line 47: `SBC rb_start` without preceding `SEC`. SBC with carry=0 subtracts an extra 1. | Added FW-10a to firmware test plan. Documented as BUG-2 in Section 1. |
| B-25 | tty_recv magic constant #$E1 boundary | **Accept** | Verified: `tty.s` line 78: `CMP #$E1` is `-31` in two's complement (buffer size - 1). If rb_len changes from 32, this constant breaks. | Added FW-3a and FW-3b to firmware test plan. |
| B-26 | _tty_getc wrap-around | **Accept** | Verified: `tty.s` lines 58-60 wrap logic. Not tested at boundary. | Added FW-8a to firmware test plan. |
| B-27 | BRK vs IRQ distinction in handler | **Accept** | 6502 BRK pushes PC+2 and sets B flag. Shared $FFFE vector must distinguish. | Added FW-14a to firmware test plan. |
| B-28 | emulator_setbp_old() is dead code | **Defer** | Dead code with no callers. Not worth testing. If deleted, no tests needed. | No change. Excluded from test scope. |
| B-29 | pc_mask[] and label_mask[] unused globals | **Defer** | Technical debt noted in source (line 39-40, TODO comment). Not testable since nothing reads/writes them. | No change. |
| B-30 | Write-to-device hits both mem[] and device | **Accept** | Already noted in v1 T68. Expanded documentation for future test authors. | Added note at top of Section 7.3 warning about dual-write behavior. |
