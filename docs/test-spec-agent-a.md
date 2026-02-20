# N8Machine Test Harness Spec -- Agent A: Framework Evaluation & Selection

## 1. Codebase Summary (Relevant to Testing)

**Build system**: Plain Makefile, `g++`/`clang++`, C++11 (`-std=c++11`), no CMake. Build outputs to `build/`. Cross-platform targets (Linux/macOS/Windows) but primary dev on Linux.

**Dependencies**: ImGui (vendored in `imgui/`), SDL2 (system), OpenGL (system). All GUI code lives in `main.cpp` and the `emulator_show_*` functions in `emulator.cpp`.

**Testable core** (no GUI dependency):
- `m6502.h` -- Header-only 6502 CPU emulator (floooh/chips). Activated with `#define CHIPS_IMPL`. Pure C with `extern "C"`. Cycle-accurate tick-level API: `m6502_init()`, `m6502_tick()`, register accessors, pin macros.
- `emulator.cpp` -- Bus decode logic, memory map, device dispatch, IRQ handling. Currently tightly coupled: includes `imgui.h` and `gui_console.h` for the `emulator_show_*` and `gui_con_printmsg` calls. The core tick/bus/IRQ logic is separable.
- `emu_tty.cpp` -- TTY device emulation (keyboard buffer, serial decode). Uses POSIX `termios`/`select` for real terminal IO, but the decode logic (`tty_decode`) is testable given a mock pin bus.
- `utils.cpp` -- Pure utility functions (`my_itoa`, `my_get_uint`, `range_helper`, `emu_is_digit`, `emu_is_hex`, `htoi`). Zero dependencies. Immediately testable as-is.
- `emu_dis6502.cpp` -- Disassembler. Depends on `mem[]` global and `emu_labels`. Core decode function is testable.
- `emu_labels.cpp` -- Label table. Depends on `gui_con_printmsg` for console list output, but core add/get/load logic is testable.
- `machine.h` -- Single constant (`total_memory = 65536`).

**Global state**: `mem[65536]`, `frame_buffer[256]`, `cpu` (m6502_t), `pins` (uint64_t), `tick_count`, `bp_mask[65536]`, etc. All file-scope in `emulator.cpp`. Tests will need to either link against emulator.cpp or access these via the existing API.

**C++ standard**: C++11. Any framework must support C++11.

---

## 2. Framework Survey

### 2.1 doctest

**What**: Single-header C++11 testing framework. ~6800 lines in one header file. Inspired by Catch2, optimized for compile speed.

**Pros**:
- True single-header: copy `doctest.h` into the project, done
- Fastest compile-time overhead of any feature-rich framework (~10ms per TU include)
- C++11 compatible (matches N8Machine)
- Natural assertion syntax with expression decomposition: `CHECK(a == b)` prints both sides on failure
- Supports subcases (like Catch2 sections), test suites, BDD-style
- `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` gives you `main()` for free
- Can be disabled entirely with `DOCTEST_CONFIG_DISABLE` (zero overhead in production)
- No namespace pollution, no extra headers dragged in
- Active maintenance (2.4.11+, last updated 2025)

**Cons**:
- Smaller ecosystem than GoogleTest (fewer IDE integrations, though CLion/VSCode support it)
- No built-in mocking (would need a separate library if ever needed)
- Fewer online examples compared to GoogleTest/Catch2

**Dependency weight**: 1 file, ~280KB. Zero external dependencies.

**Build integration**: Copy header. Add one .cpp file with `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"`. Compile and link test sources. 3 lines in a Makefile.

### 2.2 Catch2 (v3)

**What**: Widely used C++ test framework. v3 is a compiled library (no longer single-header). Requires C++14.

**Pros**:
- Rich feature set: sections, generators, matchers, benchmarking
- Excellent documentation and large community
- String-based test names (readable)
- Good IDE support (CLion, VSCode adapter)

**Cons**:
- **v3 requires C++14** -- N8Machine uses C++11. Would force a compiler flag change.
- **No longer single-header** in v3 -- requires building a library or using CMake's FetchContent
- v2 branch (C++11, single-header) is in maintenance mode, no new features
- Heavier compile times than doctest (~430ms per include for v2 single-header)
- More complex build integration for a Makefile project

**Dependency weight**: v3 is a full library (~100+ files). v2 single-header is ~540KB but deprecated.

**Build integration**: v2 could be dropped in as a header but is EOL. v3 requires building a static library first, adding significant Makefile complexity.

### 2.3 GoogleTest

**What**: Google's C++ testing framework. Compiled library. The industry standard for large C++ projects.

**Pros**:
- Extremely well documented, massive community
- Rich features: parameterized tests, typed tests, death tests, matchers (gmock)
- Built-in mocking framework (gmock)
- Excellent IDE and CI integration
- Stable API, long-term support

**Cons**:
- **Not header-only** -- requires building `gtest-all.cc` and linking. For Makefile projects, you either install system-wide or vendor the source tree (~15k+ lines across multiple files)
- Heavier dependency: needs pthread on Linux
- Test names must be valid C++ identifiers (less readable than string names)
- Macro-heavy API (`TEST_F`, `EXPECT_EQ`, `ASSERT_THAT`)
- Overkill for a small emulator project

**Dependency weight**: ~4MB source tree. Requires compilation into a static library.

**Build integration**: Either `apt install libgtest-dev` + build, or vendor source and add gtest compilation rules to Makefile. Non-trivial either way.

### 2.4 utest.h

**What**: Minimal single-header C/C++ testing framework by Neil Henning. ~1200 lines.

**Pros**:
- Extremely minimal: one header, C89/C++ compatible
- GoogleTest-like API familiar to many developers
- Supports xUnit XML output
- Test filtering from command line
- Zero dependencies

**Cons**:
- No expression decomposition (on failure, you don't see operand values automatically)
- No subcases/sections (each test is flat)
- Minimal documentation compared to other options
- Smaller community, fewer contributors
- No test suites or grouping beyond the two-part `UTEST(set, name)` macro

**Dependency weight**: 1 file, ~60KB. Zero external dependencies.

**Build integration**: Same as doctest -- copy header, compile.

### 2.5 Boost.Test

**What**: Part of Boost. Available as header-only or compiled library.

**Pros**:
- Header-only option exists
- Very mature, well-tested
- Rich feature set (fixtures, datasets, decorators)

**Cons**:
- **Pulls in Boost headers** -- massive dependency for a project that currently has zero Boost usage
- Complex API, verbose setup
- Slow compile times in header-only mode
- Overkill for this project
- Would require installing Boost or vendoring a large subset

**Dependency weight**: Effectively requires Boost. Not viable for "minimal deps."

**Build integration**: Requires Boost on the system. Disqualified.

---

## 3. Selection Criteria

Ranked by importance for N8Machine:

| # | Criterion | Weight | Notes |
|---|-----------|--------|-------|
| 1 | **Simplicity of integration** | Critical | Makefile project, no CMake. Must be copy-and-compile. |
| 2 | **Header-only / minimal files** | Critical | Prefer dropping 1 file into the repo over managing a build dependency. |
| 3 | **C++11 compatible** | Required | Project uses `-std=c++11`. Cannot require C++14+. |
| 4 | **Good assertion output** | High | When a CPU test fails, need to see expected vs actual values without manual printf. |
| 5 | **Zero external dependencies** | High | No Boost, no pthread requirements, no package manager. |
| 6 | **Single test binary output** | Medium | `make test` produces one executable, run it, done. |
| 7 | **Subcases / sections** | Nice | Useful for testing multiple opcodes with shared setup. Not required. |
| 8 | **Mocking support** | Low | Bus/device mocking will be done manually (set memory, check pins). No framework mock needed. |
| 9 | **CI integration** | Low | Local-only for now. No XML/JUnit output requirement. |

---

## 4. Framework Recommendation: doctest

**doctest** is the clear choice.

**Rationale**:
1. **True single-header, C++11**: Drop `doctest.h` into `src/` (or a `test/` include path). Works with the existing `-std=c++11` flag. Zero build system changes beyond adding a test target.
2. **Fastest compile times**: With the number of test files likely to grow (CPU instruction tests alone could be many files), doctest's ~10ms per-include overhead matters vs Catch2's ~430ms.
3. **Expression decomposition**: `CHECK(m6502_a(&cpu) == 0x42)` will print `0x00 == 0x42` on failure. Critical for debugging CPU tests where you need to see register values.
4. **Makefile-native**: No CMake, no FetchContent, no pkg-config. Add 5-10 lines to a Makefile (or a separate `Makefile.test`).
5. **Subcases**: Allow shared CPU setup with per-opcode assertions in the same test, reducing boilerplate.
6. **Pragmatic scope**: Has exactly the features needed (assertions, subcases, test suites, fixtures via constructor/destructor) without the weight of GoogleTest's mocking framework or Catch2's generators.

**Why not the others**:
- **Catch2 v3**: Requires C++14, not single-header. v2 is EOL.
- **GoogleTest**: Requires building a library, threading dependency, overkill.
- **utest.h**: Too minimal -- no expression decomposition means painful debugging of CPU register mismatches.
- **Boost.Test**: Drags in Boost. Disqualified.

---

## 5. Design Decisions

### D1: Test framework is doctest (single-header)

**Decided**: Use doctest as the sole test framework. Vendor `doctest.h` directly in the repository.

**Rationale**: Meets all critical criteria (C++11, header-only, Makefile-friendly, good assertions). Fastest compile times of any feature-rich option. See Section 4 for full comparison.

**Alternatives considered**: GoogleTest (too heavy, needs library build), Catch2 v3 (needs C++14), Catch2 v2 (EOL), utest.h (no expression decomposition), Boost.Test (massive dependency).

### D2: Tests build as a separate binary

**Decided**: Test sources compile into a standalone executable (`n8_test` or similar) that has no dependency on ImGui, SDL2, or OpenGL. The main `n8` binary is unchanged.

**Rationale**: The GUI layer is not under test. Linking against SDL2/ImGui would require a display server, making tests non-headless. The emulation core (CPU, bus, devices, utils) can be compiled independently.

**Alternatives considered**: Embedding tests in the main binary with `DOCTEST_CONFIG_DISABLE` for release (doctest supports this, but adds complexity and still requires GUI deps during test compilation).

### D3: Separate Makefile target, not a separate Makefile

**Decided**: Add a `test` target to the existing `Makefile` (e.g., `make test`). Test-specific variables (sources, objects, flags) are defined in the same file. The test target compiles test sources + required emulator sources (without GUI sources) into the test binary.

**Rationale**: Single build file keeps things simple. The test target only compiles the subset of source files that don't depend on ImGui/SDL. Avoids maintaining two parallel build systems.

**Alternatives considered**: Separate `Makefile.test` (adds a file but no real benefit), recursive make into `test/` directory (over-engineered for this project size).

### D4: Test directory structure is `test/`

**Decided**: All test sources live in a `test/` directory at the project root. One file per test area (e.g., `test_utils.cpp`, `test_cpu.cpp`, `test_bus.cpp`). One `test_main.cpp` with `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.

**Rationale**: Separates test code from production code. The `test_main.cpp` file provides `main()` via doctest so no test file needs to define it. Adding new test files means adding one `.cpp` to the Makefile's `TEST_SOURCES` list.

**Alternatives considered**: Tests alongside sources in `src/` (clutters production code), single monolithic test file (doesn't scale).

### D5: doctest.h lives in `test/`

**Decided**: Place `doctest.h` in `test/` (not `src/` or a top-level `include/`). Test sources include it as `#include "doctest.h"`.

**Rationale**: The test framework header has no business in `src/`. Keeping it in `test/` makes the dependency relationship clear: only test code touches doctest. The Makefile test target adds `-Itest` to its include flags.

**Alternatives considered**: Top-level `include/` or `third_party/` directory (over-structured for one file), `src/` (pollutes production includes).

### D6: Emulator core needs minimal refactoring for testability

**Decided**: To compile emulator core functions without ImGui, the following minimal changes are needed:
- `emulator.cpp` currently `#include "imgui.h"` and calls `ImGui::*` in the `emulator_show_*` functions. The show functions must remain in a separate translation unit (or behind an `#ifdef`) so the core tick/bus/init logic can compile without ImGui.
- `gui_console.h` / `gui_con_printmsg` is called from `emu_labels.cpp` and `emulator.cpp`. For tests, a stub implementation (or compile-time switch) is needed.

**Rationale**: The goal is to test the emulation logic, not the GUI. Rather than a large refactor, the minimum viable approach is to split `emulator.cpp` into core logic (testable) and GUI display (not tested). Alternatively, provide stub implementations of `gui_con_printmsg` in the test build.

**Alternatives considered**: Major refactoring into clean interfaces (premature -- get tests working first, refactor later), mocking ImGui (absurd complexity for no benefit).

### D7: Global state is acceptable in tests (for now)

**Decided**: Tests will use the existing global `mem[]`, `cpu`, `pins` etc. directly. Each test (or subcase) is responsible for reinitializing state via `m6502_init()` and `memset(mem, 0, sizeof(mem))`.

**Rationale**: The emulator uses C-style global state by design (matching the hardware model). Wrapping this in classes for testability would be a significant rewrite with no immediate benefit. doctest's subcases ensure each test path gets fresh setup if structured correctly.

**Alternatives considered**: Encapsulating state in a struct/class (good long-term, premature now), using doctest fixtures (viable and can be adopted incrementally).

### D8: CPU instruction tests use a minimal bus loop

**Decided**: CPU tests will use a local helper function that runs the minimal m6502 tick loop (init, load program into `mem[]`, tick until SYNC, check registers/flags). No ROM file loading, no device dispatch -- just raw CPU + flat memory.

**Rationale**: This isolates CPU behavior from bus decode and device logic. The m6502.h documentation provides exactly this pattern. Test programs are short byte sequences written directly into `mem[]`.

**Alternatives considered**: Using the full `emulator_step()` function (drags in TTY, bus decode, IRQ handling -- too much for CPU unit tests), loading `.bin` test ROMs from files (adds file I/O dependency, harder to maintain than inline byte arrays).

### D9: Test categories map to test suites

**Decided**: Use doctest's `TEST_SUITE` to group tests into logical categories:
- `"utils"` -- utility function tests
- `"cpu"` -- CPU instruction correctness
- `"bus"` -- bus decode and memory map
- `"device"` -- device register behavior (TTY etc.)
- `"integration"` -- multi-component scenarios

**Rationale**: Test suites allow running subsets via `./n8_test -ts="cpu"`. Useful as the test count grows. Maps directly to the testing layers specified in the project requirements.

**Alternatives considered**: Flat test list with naming conventions (works but loses filtering capability), one binary per suite (over-engineered, harder to maintain).

### D10: Firmware testing is deferred but doctest supports it

**Decided**: No firmware tests in the initial implementation. When ready, firmware tests will load a ROM image into `mem[]`, run the tick loop for N cycles, and assert on memory/register state. doctest's test infrastructure supports this without changes.

**Rationale**: Firmware testing requires a stable CPU and bus test layer first. The same test binary and framework will be used -- just a new test suite (`"firmware"`). Test ROMs can be loaded from files or embedded as byte arrays.

**Alternatives considered**: Separate firmware test tooling (unnecessary -- doctest handles it), testing firmware on real hardware only (loses the benefit of having an emulator).
