# Test Harness Spec — Agent C: Architecture, Build, and Firmware Test Plan

## 1. File Structure

```
N8machine/
├── tests/
│   ├── test_main.cpp          # Test runner entry point
│   ├── test_utils.cpp         # Tests for utils.cpp (parsing, hex conversion)
│   ├── test_emulator.cpp      # Tests for emulator core (step, breakpoints, memory, IRQ)
│   ├── test_emu_tty.cpp       # Tests for TTY device emulation (decode, ring buffer)
│   ├── test_emu_dis6502.cpp   # Tests for disassembler (opcode decode)
│   ├── test_emu_labels.cpp    # Tests for label loading and lookup
│   ├── test_stubs.cpp         # Stub implementations for gui_console, ImGui
│   ├── test_stubs.h           # Stub declarations and test-visible capture state
│   └── test_firmware.cpp      # DEFERRED — firmware integration tests via GDB RSP
├── tests/firmware/            # DEFERRED — test-specific firmware binaries
│   ├── test_tty_rb.s          # Ring buffer isolated test ROM
│   ├── test_tty_putc.s        # putc isolated test ROM
│   └── Makefile               # cc65 build for test firmware
├── src/                       # (unchanged)
├── firmware/                  # (unchanged)
├── Makefile                   # Modified: add `test` target
└── ...
```

### Naming conventions

- Test source files: `test_<module>.cpp` matching the source module under test.
- Test functions: `test_<module>_<what>()` returning `int` (0 = pass, nonzero = fail).
- Stub files: `test_stubs.cpp/.h` — single file for all GUI/system stubs.
- DEFERRED firmware test ROMs: `tests/firmware/test_<component>.s`.

---

## 2. Build Integration

### 2.1 Current Makefile analysis

The existing `Makefile` compiles all `src/*.cpp` and all `imgui/**/*.cpp` into `build/*.o`, then links into the `n8` binary. Key observations:

- Object files land in `build/` via `$(BUILD_DIR)/%.o` pattern rules.
- Source list is explicit (`SOURCES = ...`), not globbed.
- Compiler flags: `-std=c++11 -g -Wall -Wformat`, plus ImGui include paths and `sdl2-config --cflags`.
- Link flags: `-lGL -ldl` plus `sdl2-config --libs`.
- Firmware builds via `make -C firmware install`.

### 2.2 Test target additions to Makefile

Add the following to the end of the existing `Makefile`:

```makefile
##---------------------------------------------------------------------
## TEST BUILD
##---------------------------------------------------------------------

TEST_DIR = tests
TEST_BUILD_DIR = build/tests
TEST_EXE = n8_test

# Source files under test (NOT main.o, NOT gui_console.o, NOT imgui)
TEST_SRC_OBJS = $(BUILD_DIR)/emulator.o $(BUILD_DIR)/emu_tty.o \
                $(BUILD_DIR)/emu_dis6502.o $(BUILD_DIR)/emu_labels.o \
                $(BUILD_DIR)/utils.o

# Test source files
TEST_SOURCES = $(TEST_DIR)/test_main.cpp $(TEST_DIR)/test_utils.cpp \
               $(TEST_DIR)/test_emulator.cpp $(TEST_DIR)/test_emu_tty.cpp \
               $(TEST_DIR)/test_emu_dis6502.cpp $(TEST_DIR)/test_emu_labels.cpp \
               $(TEST_DIR)/test_stubs.cpp
_TEST_OBJS = $(addsuffix .o, $(basename $(notdir $(TEST_SOURCES))))
TEST_OBJS = $(patsubst %, $(TEST_BUILD_DIR)/%, $(_TEST_OBJS))

# Test compiler flags: same C++ standard, no SDL/ImGui, add src/ to include path
TEST_CXXFLAGS = -std=c++11 -g -Wall -Wformat -I$(SRC_DIR) -DN8_TEST_BUILD

# Test link flags: no SDL, no GL, no ImGui
TEST_LIBS =

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

.PHONY: test
test: $(TEST_EXE)
	./$(TEST_EXE)

$(TEST_EXE): $(TEST_SRC_OBJS) $(TEST_OBJS)
	$(CXX) -o $@ $^ $(TEST_LIBS)

clean-test:
	rm -f $(TEST_EXE) $(TEST_BUILD_DIR)/*.o
```

### 2.3 Source object compilation issue

The production `build/*.o` files are compiled with ImGui include paths and `sdl2-config --cflags`. This works because:

- `emulator.cpp` includes `imgui.h` directly (line 4).
- `gui_console.cpp` includes `imgui.h`.
- `emu_dis6502.cpp` includes `imgui.h`.

For the test build, the source `.o` files (`emulator.o`, `emu_dis6502.o`) are reused from the production build. This means SDL2/ImGui headers must be available at compile time even for the test binary. The test binary does **not** link SDL2/ImGui/GL libraries -- it only needs the headers to compile.

**This is acceptable** because:
1. The developer already has SDL2 and ImGui installed (required to build the main binary).
2. The test binary links against none of these libraries.
3. The stubs in `test_stubs.cpp` provide the symbols that `gui_console.o` would normally provide, but since we exclude `gui_console.o` entirely and provide stubs, this works.

**However**, `emulator.cpp` includes `imgui.h` (line 4) and calls `ImGui::Begin()` etc. in `emulator_show_memdump_window()` and `emulator_show_status_window()`. These ImGui calls exist in the same compilation unit as the testable functions (`emulator_step()`, `emulator_init()`, etc.).

**Resolution**: The production `emulator.o` contains unresolved ImGui symbols. The test binary does not call the `emulator_show_*` functions, but the linker still needs to resolve them. Two options:

1. **(Recommended — D26)** Add a thin ImGui stub to `test_stubs.cpp` that satisfies the linker (no-op `ImGui::Begin()`, `ImGui::End()`, `ImGui::Text()`, etc.). This is ~20 lines.
2. (Alternative) Split `emulator.cpp` into `emulator_core.cpp` (no ImGui) and `emulator_gui.cpp` (ImGui windows). Tests link only `emulator_core.o`.

Option 1 is simpler and requires zero changes to production code.

Similarly, `emu_dis6502.cpp` includes `imgui.h` and has `emu_dis6502_window()` which calls ImGui. The same stub approach covers it.

### 2.4 What the test binary links

| Object | Source | Why |
|--------|--------|-----|
| `build/emulator.o` | `src/emulator.cpp` | Core emulator: step, init, breakpoints, memory |
| `build/emu_tty.o` | `src/emu_tty.cpp` | TTY device decode, tick, ring buffer |
| `build/emu_dis6502.o` | `src/emu_dis6502.cpp` | Disassembler decode |
| `build/emu_labels.o` | `src/emu_labels.cpp` | Symbol table loading |
| `build/utils.o` | `src/utils.cpp` | Parsing utilities |
| `build/tests/test_stubs.o` | `tests/test_stubs.cpp` | Stubs for gui_console, ImGui |
| `build/tests/test_main.o` | `tests/test_main.cpp` | Test runner |
| `build/tests/test_*.o` | `tests/test_*.cpp` | Individual test modules |

**NOT linked**: `build/main.o`, `build/gui_console.o`, any `build/imgui*.o`.

### 2.5 Build dependency

`make test` depends on the production `.o` files for the source modules. Running `make test` without first running `make` (or `make all`) will fail if the production objects do not exist. This is intentional -- it keeps the Makefile simple. Add a note in usage:

```
# Build and run tests:
make && make test
```

Alternatively, make `test` depend on the relevant `.o` files explicitly (which it already does via `$(TEST_SRC_OBJS)`).

### 2.6 Compiler flags summary

| Build | CXXFLAGS | LIBS |
|-------|----------|------|
| Production | `-std=c++11 -g -Wall -Wformat -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends` + `sdl2-config --cflags` | `-lGL -ldl` + `sdl2-config --libs` |
| Test | `-std=c++11 -g -Wall -Wformat -I$(SRC_DIR) -DN8_TEST_BUILD` | (none) |

The `-DN8_TEST_BUILD` flag is available for conditional compilation in source if needed, but the initial implementation should not require any `#ifdef` in production code.

---

## 3. Test Binary Architecture

### 3.1 No framework

No external test framework (no Google Test, no Catch2). The harness is a plain C++ program with a simple registration mechanism. This matches the project's minimalist style and avoids dependencies.

### 3.2 Test registration

Each test is a function with signature:

```cpp
typedef int (*test_func_t)(void);
```

Returns 0 on pass, nonzero on fail. Tests register via a static array in `test_main.cpp`:

```cpp
// test_main.cpp

#include <cstdio>
#include <cstring>

// Forward declarations of all test functions
extern int test_utils_is_digit();
extern int test_utils_is_hex();
extern int test_utils_my_get_uint_decimal();
extern int test_utils_my_get_uint_hex();
extern int test_utils_range_helper();
extern int test_utils_htoi();
// ... etc.

struct TestEntry {
    const char* name;
    test_func_t func;
};

static TestEntry tests[] = {
    {"utils/is_digit",           test_utils_is_digit},
    {"utils/is_hex",             test_utils_is_hex},
    {"utils/my_get_uint_dec",    test_utils_my_get_uint_decimal},
    {"utils/my_get_uint_hex",    test_utils_my_get_uint_hex},
    {"utils/range_helper",       test_utils_range_helper},
    {"utils/htoi",               test_utils_htoi},
    // emulator tests
    {"emu/init_memory",          test_emu_init_memory},
    {"emu/step_nop",             test_emu_step_nop},
    {"emu/breakpoint_set",       test_emu_breakpoint_set},
    {"emu/breakpoint_hit",       test_emu_breakpoint_hit},
    {"emu/irq_set_clr",          test_emu_irq_set_clr},
    {"emu/bus_decode_framebuf",  test_emu_bus_decode_framebuf},
    {"emu/bus_decode_tty",       test_emu_bus_decode_tty},
    // tty tests
    {"tty/decode_read_status",   test_tty_decode_read_status},
    {"tty/decode_write_data",    test_tty_decode_write_data},
    {"tty/ring_buffer",          test_tty_ring_buffer},
    // dis6502 tests
    {"dis/decode_nop",           test_dis_decode_nop},
    {"dis/decode_lda_imm",       test_dis_decode_lda_imm},
    {"dis/decode_jmp_abs",       test_dis_decode_jmp_abs},
    // labels tests
    {"labels/add_get",           test_labels_add_get},
    {"labels/clear",             test_labels_clear},
    // sentinel
    {nullptr, nullptr}
};

int main(int argc, char* argv[]) {
    int total = 0, passed = 0, failed = 0;
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    for (int i = 0; tests[i].name != nullptr; i++) {
        if (filter && strstr(tests[i].name, filter) == nullptr)
            continue;

        total++;
        int result = tests[i].func();
        if (result == 0) {
            passed++;
            printf("  PASS  %s\n", tests[i].name);
        } else {
            failed++;
            printf("  FAIL  %s (returned %d)\n", tests[i].name, result);
        }
    }

    printf("\n%d/%d passed", passed, total);
    if (failed > 0) printf(", %d FAILED", failed);
    printf("\n");

    return failed > 0 ? 1 : 0;
}
```

### 3.3 Test assertion helpers

Defined in `test_stubs.h` (shared by all test files):

```cpp
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <deque>

// Simple assertion macros
#define T_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("    ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define T_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("    ASSERT_EQ FAILED: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define T_ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("    ASSERT_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", (a), (b), __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

// Access to stub capture state
extern std::deque<std::string>& stub_get_console_buffer();
extern void stub_clear_console_buffer();
```

### 3.4 Output format

Plain console output. No TAP, no JUnit XML. Format:

```
  PASS  utils/is_digit
  PASS  utils/is_hex
  FAIL  utils/my_get_uint_hex (returned 1)
    ASSERT_EQ FAILED: result != expected (test_utils.cpp:42)
  PASS  emu/init_memory
  ...

18/20 passed, 2 FAILED
```

Exit code: 0 if all pass, 1 if any fail. This integrates with `make test` (make fails on nonzero exit).

### 3.5 Filtering

Optional command-line argument filters tests by substring match:

```bash
./n8_test utils       # run only tests with "utils" in name
./n8_test emu/step    # run only test_emu_step_nop
```

---

## 4. Decoupling Strategy

### 4.1 GUI coupling analysis

The following production source files call GUI/ImGui functions:

| Source file | GUI function called | Call sites |
|-------------|-------------------|------------|
| `emulator.cpp` | `gui_con_printmsg(char*)` | Lines 89, 176, 201, 252 (breakpoint hit/log/set messages) |
| `emulator.cpp` | `ImGui::Begin/End/Text/...` | `emulator_show_memdump_window()` (line 267), `emulator_show_status_window()` (line 330) |
| `emulator.cpp` | `gui_show_console_window()` | `emulator_show_console_window()` (line 357) |
| `emu_dis6502.cpp` | `gui_con_printmsg()` | `emu_dis6502_log()` (lines 137, 141) |
| `emu_dis6502.cpp` | `ImGui::Begin/End/Text/...` | `emu_dis6502_window()` (line 153+) |
| `emu_labels.cpp` | `gui_con_printmsg()` | `emu_labels_console_list()` (line 39) |
| `gui_console.cpp` | `ImGui::*` (all) | Entire file is GUI |
| `emu_tty.cpp` | (none) | Pure emulation -- no GUI calls |
| `utils.cpp` | (none) | Pure utility functions |

### 4.2 Stub strategy

**Exclude `gui_console.o`** from the test link. Provide replacement symbols in `test_stubs.cpp`:

```cpp
// test_stubs.cpp
#include <string>
#include <deque>

// ---- gui_console stubs ----
// These replace gui_console.o entirely.

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

This captures all `gui_con_printmsg()` output into an inspectable buffer, enabling tests to verify that emulator debug messages are generated correctly.

### 4.3 ImGui stub

Since `emulator.o` and `emu_dis6502.o` are compiled with ImGui headers and contain calls to ImGui functions, the test linker needs symbols. Add to `test_stubs.cpp`:

```cpp
// ---- ImGui linker stubs ----
// Satisfies unresolved symbols from emulator_show_* and emu_dis6502_window.
// These functions are never called in tests.

namespace ImGui {
    bool Begin(const char*, bool*, int) { return true; }
    void End() {}
    void Text(const char*, ...) {}
    void TextColored(const struct ImVec4&, const char*, ...) {}
    void SameLine(float, float) {}
    bool Checkbox(const char*, bool*) { return false; }
    bool InputText(const char*, char*, unsigned long, int, void*, void*) { return false; }
    void BeginChild(const char*, const struct ImVec2&, bool, int) {}
    void EndChild() {}
    void SetScrollY(float) {}
    float GetScrollMaxY() { return 0.0f; }
    void BeginDisabled(bool) {}
    void EndDisabled() {}
    bool Button(const char*, const struct ImVec2&) { return false; }
}
```

The exact function signatures depend on the ImGui version. Compile, check for unresolved symbols, and add stubs as needed. This is a one-time exercise.

**Important**: These are never called during tests. They exist solely to satisfy the linker. If the signatures drift, the linker will report it and stubs can be updated.

### 4.4 emu_tty.cpp terminal dependency

`emu_tty.cpp` calls `tcgetattr()`, `tcsetattr()`, `select()`, `read()` in `tty_init()` and `tty_kbhit()`/`getch()`. For tests:

- **`tty_init()`** calls `set_conio()` which puts the terminal in raw mode and registers `atexit(tty_reset_term)`. In tests, either:
  - Call `tty_init()` in setup if terminal mode is acceptable for test run, OR
  - Skip `tty_init()` and test only `tty_decode()` and ring buffer behavior by calling `tty_decode()` directly with crafted pin states.
- **`tty_tick()`** calls `tty_kbhit()` which does `select()` on stdin. For emulator-level tests that call `emulator_step()`, `tty_tick()` will be called but it returns quickly when no keyboard input is pending (returns at line 57: `if(!tty_kbhit()) return;`). This is safe in a test environment.

**Decision**: No stub needed for `emu_tty.cpp`. The terminal `select()` call returns immediately with no input pending. If this becomes problematic, `tty_kbhit()` can be wrapped with a `#ifdef N8_TEST_BUILD` that returns 0, but that is not expected to be necessary.

### 4.5 emulator_loadrom() file dependency

`emulator_init()` calls `emulator_loadrom()` which does `fopen("N8firmware", "r")`. Tests must either:

1. Run from the project root directory (where `N8firmware` exists after `make firmware`), OR
2. Provide a test-specific ROM.

**Decision**: Tests run from project root. The Makefile `test` target runs `./n8_test` from the project root. The firmware binary and symbol file (`N8firmware`, `N8firmware.sym`) must exist. The `make test` command sequence:

```bash
make && make firmware && make test
```

For unit tests that do not need the full ROM (e.g., `test_utils_*`), the ROM load happens in the emulator tests' setup, not in every test. Tests that exercise `emulator_step()` will need `emulator_init()` which needs the ROM file.

### 4.6 Changes to production code

**Zero changes to production code required.** All decoupling is handled by the test stubs replacing `gui_console.o` and satisfying ImGui linker symbols.

---

## 5. 6502 Firmware Test Plan (DEFERRED)

> **STATUS: DEFERRED.** This entire section describes a plan for firmware testing that depends on the GDB RSP stub being implemented first (see `docs/gdb-stub-spec-v2.md`). No implementation until the GDB stub is complete and operational.

### 5.1 How GDB RSP enables firmware testing

The GDB RSP stub (spec v2) provides a programmatic interface to:

- **Load firmware** into emulator memory (via memory write commands `M`/`X`)
- **Set breakpoints** at specific addresses (via `Z0` commands)
- **Run the CPU** until a breakpoint hits (via `c` continue command)
- **Read registers** (A, X, Y, SP, PC, P) after execution stops (via `g`/`p` commands)
- **Read/write memory** to inspect ring buffers, zero page, device registers (via `m`/`M` commands)
- **Single-step** through instructions (via `s` command)
- **Reset** the CPU (via `qRcmd,reset`)

This means firmware tests can be scripted as GDB RSP protocol clients that:
1. Connect to the emulator's GDB port (localhost:3333)
2. Load a test ROM into memory
3. Set breakpoints at known addresses (from .sym file)
4. Continue execution
5. When breakpoint hits, read registers/memory and verify expected state
6. Report pass/fail

### 5.2 Test workflow

```
┌─────────────┐    ┌──────────────────┐    ┌──────────────────┐
│ ca65/ld65   │───>│ test_xyz.bin     │───>│ n8_test (or      │
│ (assemble)  │    │ test_xyz.sym     │    │ gdb client)      │
└─────────────┘    └──────────────────┘    └──────────────────┘
                                                    │
                                                    │ GDB RSP
                                                    ▼
                                           ┌──────────────────┐
                                           │ n8 emulator      │
                                           │ (headless or GUI) │
                                           └──────────────────┘
```

Steps per firmware test:
1. **Assemble**: `cl65 -t none --cpu 6502 -C n8.cfg -o test_xyz.bin -Ln test_xyz.sym test_xyz.s`
2. **Launch emulator** (or use already-running instance with GDB stub enabled)
3. **Connect** to GDB RSP port
4. **Load ROM**: Read `test_xyz.bin`, write to emulator memory at $D000+ via `M` packets
5. **Load symbols**: Parse `test_xyz.sym` for breakpoint addresses
6. **Set breakpoints** at test completion/verification points
7. **Reset CPU** via `qRcmd,reset`
8. **Continue** execution via `c`
9. **Wait for breakpoint** (stop reply `T05`)
10. **Read registers/memory** and verify expected values
11. **Report** pass/fail

### 5.3 Firmware components to test

Based on reading the firmware source:

#### 5.3.1 TTY Ring Buffer (`tty.s` — `tty_recv`)

The ring buffer at `rb_base` (32 bytes) with `rb_start`/`rb_end` indices. `tty_recv` writes incoming characters, wrapping at `rb_len`.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-1 | `tty_recv` single char | Write char to A, JSR tty_recv | `rb_base[0]` == char, `rb_end` == 1 |
| FW-2 | `tty_recv` wrap-around | Fill buffer to rb_len-1, then add one more | `rb_end` == 0, `rb_base[0]` == last char |
| FW-3 | `tty_recv` full buffer discard | Fill buffer completely, add one more | Buffer unchanged, extra char discarded |
| FW-4 | `tty_recv` boundary check `in_bounds` | Set `rb_end` == `rb_len`, call `tty_recv` | `rb_end` resets to 0, char stored correctly |

#### 5.3.2 TTY Output (`tty.s` — `_tty_putc`)

Polls `TTY_OUT_CTRL` ($C100) bit 0, then writes char to `TTY_OUT_DATA` ($C101).

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-5 | `_tty_putc` ready | Set A='A', ensure $C100 bit 0 == 0, JSR _tty_putc | $C101 == 'A' |
| FW-6 | `_tty_putc` wait loop | Set $C100 bit 0 == 1, run for N cycles | PC still in @wait loop |

#### 5.3.3 TTY String Output (`tty.s` — `_tty_puts`)

Takes string pointer in A (low)/X (high), prints until NUL.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-7 | `_tty_puts` short string | Load "Hi\0" at $0300, A=#<$0300, X=#>$0300, JSR _tty_puts | Characters appear at $C101 in sequence |
| FW-8 | `_tty_puts` empty string | Load $00 at $0300, call _tty_puts | Returns immediately, no write to $C101 |

#### 5.3.4 TTY Input (`tty.s` — `_tty_getc`, `_tty_peekc`)

`_tty_getc` reads from ring buffer. `_tty_peekc` returns count of available chars.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-9 | `_tty_getc` with data | Pre-fill `rb_base[0]`='X', `rb_end`=1, `rb_start`=0, JSR _tty_getc | A == 'X', `rb_start` == 1 |
| FW-10 | `_tty_getc` empty | `rb_start` == `rb_end` == 0, JSR _tty_getc | A == 0 |
| FW-11 | `_tty_peekc` count | `rb_end`=5, `rb_start`=2, JSR _tty_peekc | A == 3 |
| FW-12 | `_tty_peekc` wrap | `rb_end`=2, `rb_start`=30, `rb_len`=32, JSR _tty_peekc | A == 4 |

#### 5.3.5 Interrupt Handler (`interrupt.s`)

IRQ handler pushes A/X/Y, checks B flag for BRK, reads TTY_IN_CTRL, calls `tty_recv` for each pending char.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-13 | IRQ reads TTY char | Set $C102 (TTY_IN_CTRL) bit 0, put char at $C103, trigger IRQ | Char appears in ring buffer, registers restored |
| FW-14 | IRQ multiple chars | Queue 3 chars via TTY device, trigger IRQ | All 3 chars in ring buffer |
| FW-15 | BRK traps to `brken` | Execute BRK instruction | PC stuck at `brken` infinite loop |

#### 5.3.6 Init / Startup (`init.s`)

Initializes stack pointer, cc65 argument stack, calls `zerobss`, `copydata`, `initlib`, then `_main`.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-16 | Reset vector | Load firmware, reset CPU | PC reaches `_init` address from .sym |
| FW-17 | Stack init | Run past `_init` TXS | SP == $FF |
| FW-18 | Main reached | Breakpoint at `_main` | PC == `_main` address |

#### 5.3.7 Main loop (`main.s`)

Writes to text buffer, prints welcome banner via `_tty_puts`, enters echo loop.

**Test cases (DEFERRED)**:

| ID | Test | Setup | Verify |
|----|------|-------|--------|
| FW-19 | Welcome banner | Breakpoint after second _tty_puts call | Characters output via TTY |
| FW-20 | Echo loop | At `rb_test`, inject char into ring buffer, continue | Same char output via _tty_putc |

### 5.4 Firmware test implementation approach (DEFERRED)

Two options, in order of simplicity:

**Option A: C++ GDB RSP client in the test harness**

Write a minimal GDB RSP client in C++ (connect, send packets, parse responses) as part of the test binary. Firmware tests:
1. Fork/exec the `n8` emulator (or connect to a running one).
2. Connect via TCP to localhost:3333.
3. Send GDB protocol packets.
4. Parse responses.
5. Assert expected values.

This keeps everything in one `n8_test` binary. The firmware test functions live in `tests/test_firmware.cpp`.

**Option B: Python GDB RSP client**

Write firmware tests in Python using a simple RSP client (or `pygdbmi`). Invoked separately from the C++ test runner.

**Recommendation**: Option A. Keeps the unified harness goal. The GDB RSP client is ~200 lines of C++ (connect, `$packet#checksum` framing, hex encode/decode).

### 5.5 Test firmware build (DEFERRED)

Individual test firmware ROMs for isolated testing:

```makefile
# tests/firmware/Makefile
CL65 = cl65
CLFLAGS = -vm -t none -O --cpu 6502 -C ../../firmware/n8.cfg
LIB = ../../firmware/n8.lib

TESTS = test_tty_rb test_tty_putc

all: $(addsuffix .bin, $(TESTS))

%.bin: %.s
	$(CL65) $(CLFLAGS) -o $@ -Ln $*.sym $< $(LIB)

clean:
	rm -f *.bin *.sym *.o *.map
```

Each test ROM contains a minimal startup, the code under test, and a `BRK` or spin at a known address to signal completion. The test harness loads these instead of the production firmware.

---

## 6. Unified Harness Concept

### 6.1 Architecture

```
n8_test (single binary)
├── C++ unit tests (immediate, in-process)
│   ├── test_utils.cpp      — pure function tests
│   ├── test_emulator.cpp   — emulator core tests
│   ├── test_emu_tty.cpp    — TTY device tests
│   ├── test_emu_dis6502.cpp — disassembler tests
│   └── test_emu_labels.cpp — label management tests
│
└── Firmware integration tests (DEFERRED, requires GDB stub)
    └── test_firmware.cpp   — GDB RSP client, loads ROMs, verifies behavior
```

### 6.2 How they coexist

- All tests register in the same `tests[]` array in `test_main.cpp`.
- C++ tests: `"utils/..."`, `"emu/..."`, `"tty/..."`, `"dis/..."`, `"labels/..."`
- Firmware tests: `"fw/tty_recv"`, `"fw/tty_putc"`, `"fw/irq"`, etc.
- Filter by prefix: `./n8_test fw/` runs only firmware tests; `./n8_test utils/` runs only utils.

### 6.3 Firmware test prerequisites

Firmware tests require:
1. The `n8` emulator binary built with GDB stub enabled.
2. Test firmware ROMs assembled (in `tests/firmware/`).
3. The emulator running (firmware tests launch/connect to it).

The test runner can detect if the emulator is not available and skip firmware tests with a `SKIP` status rather than failing.

### 6.4 Future CI integration

When CI is added later:
```yaml
# Conceptual
steps:
  - make all
  - make firmware
  - make test                    # C++ unit tests only
  # DEFERRED:
  - make -C tests/firmware       # Build test ROMs
  - ./n8 --headless --gdb &     # Start emulator in background
  - ./n8_test fw/                # Run firmware tests
```

---

## 7. Design Decisions

### D26: Reuse production `.o` files; stub ImGui at link time
**Decision**: The test binary links the same `build/emulator.o`, `build/emu_dis6502.o` etc. compiled for the production build. ImGui and gui_console symbols are provided by `test_stubs.cpp`.

**Rationale**: Avoids recompiling source files with different flags. Tests exercise the exact same object code that runs in production. ImGui stubs are ~20 lines and never called.

**Alternatives considered**:
- (a) Recompile source files without ImGui includes for tests. Requires a separate set of pattern rules and potentially `#ifdef` guards. More complex Makefile.
- (b) Split `emulator.cpp` into core and GUI parts. Cleaner but modifies production code for test purposes.

### D27: No external test framework
**Decision**: Custom minimal test runner. `test_func_t` functions, static registration array, assert macros.

**Rationale**: Zero dependencies. ~50 lines of infrastructure. The project has no package manager, no CMake, no existing dependency management. Adding Google Test or Catch2 is disproportionate complexity for a project this size.

**Alternatives considered**:
- (a) Google Test. Powerful but requires CMake or manual build integration, downloads/submodules.
- (b) Catch2 (header-only). Lower friction but still ~600KB header, slow compile.
- (c) Unity (C test framework). Reasonable but C-oriented, adds a dependency.

### D28: Plain console output, not TAP
**Decision**: Simple `PASS`/`FAIL` lines with summary count. Exit code 0/1.

**Rationale**: Human-readable. `make test` checks exit code. No tooling needs TAP parsing. Can add TAP later if CI requires it.

**Alternatives considered**:
- (a) TAP (Test Anything Protocol). Standardized but no consumer exists in the project.
- (b) JUnit XML. Only useful for CI systems that parse it.

### D29: Exclude `gui_console.o`, stub its functions
**Decision**: The test binary does not link `gui_console.o`. Its three exported functions (`gui_con_printmsg(char*)`, `gui_con_printmsg(string)`, `gui_con_init()`) plus `gui_show_console_window()` are provided by `test_stubs.cpp`.

**Rationale**: `gui_console.cpp` includes `imgui.h` and is entirely GUI code. The only testable behavior from it is the `gui_con_printmsg()` interface, which production code calls to log debug messages. The stub captures these messages into an inspectable buffer, which is better for testing than a no-op.

**Alternatives considered**:
- (a) Link `gui_console.o` and stub only ImGui. More complex; gui_console.o has many ImGui calls in `gui_show_console_window()`.
- (b) Refactor gui_console.cpp to separate logging from display. Cleaner but modifies production code.

### D30: No `#ifdef` in production code
**Decision**: The test build requires zero changes to existing source files. All decoupling is at link time.

**Rationale**: Production code should not carry test scaffolding. Link-time substitution is clean and invisible. The `-DN8_TEST_BUILD` flag is available as an escape hatch but should not be needed for the initial test set.

**Alternatives considered**:
- (a) Add `#ifdef N8_TEST_BUILD` around ImGui includes in emulator.cpp. Requires conditional compilation, messier.
- (b) Use virtual functions / function pointers for console output. Over-engineered for this codebase.

### D31: `emu_tty.cpp` linked directly, no stub
**Decision**: The real `emu_tty.o` is linked into the test binary without stubbing.

**Rationale**: `emu_tty.cpp` has no GUI dependencies. Its `tty_kbhit()` returns 0 immediately when no keyboard input is pending (typical in test runs). `tty_init()` touches terminal settings, which is the only concern -- tests that call `emulator_init()` will call `tty_init()`, putting the terminal in raw mode with an `atexit` reset. This is acceptable for a local-only test binary.

**Alternatives considered**:
- (a) Stub tty_init() to no-op. Requires either #ifdef or link-time replacement of the entire emu_tty.o, losing the ability to test tty_decode().
- (b) Refactor tty_init() to accept a flag. Modifies production code.

### D32: Tests run from project root directory
**Decision**: `make test` runs `./n8_test` from the project root, where `N8firmware` and `N8firmware.sym` exist.

**Rationale**: `emulator_loadrom()` opens `"N8firmware"` as a relative path. `emu_labels_init()` opens `"N8firmware.sym"`. Both files are copied to the project root by `make -C firmware install`. Running from root avoids needing to change hardcoded paths.

**Alternatives considered**:
- (a) Copy firmware files into a test data directory. Adds complexity, files may get stale.
- (b) Make firmware paths configurable at runtime. Useful but modifies production code.

### D33: Firmware tests use in-process GDB RSP client (DEFERRED)
**Decision**: When firmware tests are implemented, they will use a C++ GDB RSP client compiled into the same `n8_test` binary, communicating with a running `n8` emulator via TCP.

**Rationale**: Keeps everything in one test runner. The GDB RSP client protocol is simple (~200 lines for the subset needed). Having both C++ unit tests and firmware integration tests in one binary with consistent output format is cleaner than managing separate Python scripts.

**Alternatives considered**:
- (a) Python script with pygdbmi. Easier to write but adds a runtime dependency and separate test reporting.
- (b) Shell scripts calling GDB in batch mode. Brittle, hard to parse output.

### D34: Test ROM approach for firmware (DEFERRED)
**Decision**: Write minimal test-specific firmware ROMs (one per component) rather than testing against the full production firmware.

**Rationale**: Isolation. A test ROM for `tty_recv` can set up known state, call `tty_recv`, and `BRK` at a known address. The test harness reads the .sym file for addresses, sets breakpoints, and verifies state. Testing against the full firmware introduces dependencies between components and makes failures hard to diagnose.

**Alternatives considered**:
- (a) Test against production firmware only. Simpler build but tests are integration tests, not unit tests.
- (b) Inject test code via memory writes at runtime. Fragile; depends on memory layout.

### D35: Stub console buffer is inspectable
**Decision**: The `gui_con_printmsg()` stub captures all messages into a `deque<string>` accessible via `stub_get_console_buffer()`.

**Rationale**: Several emulator functions generate debug messages through `gui_con_printmsg()` (breakpoint hit, breakpoint set, label list). Tests should verify these messages are generated correctly. A no-op stub would lose this verification capability.

**Alternatives considered**:
- (a) No-op stub. Simpler but loses testability of debug output.
- (b) Write to stderr. Not inspectable in test assertions.

### D36: `make && make test` build sequence
**Decision**: `make test` depends on the production `.o` files being already built. The user runs `make` first, then `make test`.

**Rationale**: Simplicity. The test Makefile rules do not duplicate the production compilation rules. Production `.o` files are built once and shared. This avoids conflicting pattern rules or needing a recursive make.

**Alternatives considered**:
- (a) Have `test` target depend on `all`. Would always rebuild the main binary before tests.
- (b) Duplicate compilation rules for test-specific objects. Adds Makefile complexity and risks divergence.
