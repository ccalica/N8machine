# Implementability Review: Specs A, B, and C (Revised)

> Reviewer perspective: Practical implementer about to build each phase against the actual codebase.

**Codebase snapshot:** 221 tests passing, `emulator.cpp` at ~400 lines, `emu_tty.cpp` at ~140 lines, `machine.h` at 7 lines.

---

## 1. Phase Sizing

All three specs converge on 10 phases (Spec C has 11, splitting playground and E2E into separate phases). The granularity is appropriate.

**Good sizing:**
- Phase 0 (constants) is zero-risk, zero-behavior-change. Correct to keep it minimal.
- Phase 1 (IRQ) and Phase 2 (TTY) are small, well-scoped device moves. Each touches one subsystem.
- Phase 5/6 (video/keyboard) are the largest phases by new code but are self-contained new modules with clear interfaces.

**Sizing concerns:**

- **Phase 7 in Spec A is overloaded.** It introduces the separate `frame_buffer[]` backing store, the `handled` flag control flow refactor in `emulator_step()`, the rendering pipeline (emu_display.cpp), OpenGL texture management, ImGui window, font integration, cursor compositing, GDB callback updates, AND scroll function updates -- all in one phase. This is too much. The structural change to `emulator_step()` alone deserves isolation because it touches every bus decode path. Spec B and Spec C correctly split frame buffer separation (Phase 3) from rendering (Phase 7/out-of-scope), which is safer.

- **Spec C's Phase 10 (E2E) is a phase that adds no code.** It is purely manual testing and cleanup. This is fine as a validation gate but should not be counted as an "implementation phase" for effort estimation.

- **Spec A's Phase 4 (ROM migration) and Phase 8 (firmware migration) are cleanly scoped.** The dual-path loader is a pragmatic bridge. Spec C agrees. Spec B places ROM migration later (Phase 6), which is also fine but delays the point at which `$D800-$DFFF` is free of ROM overlap confusion.

**Verdict:** Phase sizing is acceptable across all specs. Spec A Phase 7 should be split into two sub-phases (bus decode refactor + rendering pipeline). Spec B and Spec C already do this implicitly.

---

## 2. Code Sample Accuracy

I verified code samples against the actual codebase. Findings:

### Spec A

- **`emulator_loadrom()` null check:** The current code (`emulator.cpp:63`) does `FILE *fp = fopen(rom_file, "r");` with no null check. Spec A's Phase 4 dual-path loader adds `if (!fp) { printf(...); return; }`. Good -- this is a real bug fix folded into the migration.

- **BUS_DECODE macro usage is accurate.** The current pattern `BUS_DECODE(addr, 0xC100, 0xFFF0)` at line 149 matches Spec A's description. The replacement with `N8_TTY_BASE` / `N8_TTY_MASK` is correct.

- **IRQ macro operator precedence:** Current code is `mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)`. The `<<` binds tighter than `|`, so this works, but Spec A's revised version adds explicit parens: `(0x01 << bit)`. Minor improvement.

- **`emu_clr_irq()` implementation:** Current (`emulator.cpp:57`): `mem[0x00FF] = (mem[0x00FF] & ~(0x01 << bit))`. Spec A does not show the clr function updated, but the constant replacement (`0x00FF` -> `N8_IRQ_ADDR`) applies. Correct.

- **Scroll functions reference `mem[N8_FB_BASE]` in Phase 5 but `frame_buffer[]` separation does not happen until Phase 7.** This is CORRECT in Spec A's revised timeline because Spec A defers frame_buffer separation to Phase 7. The scroll code in Phase 5 uses `&mem[N8_FB_BASE]` which works against the `mem[]` backing store. When Phase 7 introduces `frame_buffer[]`, the scroll functions must be updated. Spec A explicitly calls this out: "The scroll functions in `emu_video.cpp` must now operate on `frame_buffer[]` instead of `mem[N8_FB_BASE..]`." This is correct but represents a Phase 7 code churn risk.

- **`video_decode()` signature:** `void video_decode(uint64_t &pins, uint8_t reg)` -- matches the pattern used by `tty_decode()`. Accurate.

- **Keyboard: `sdl_to_n8_keycode()` returns `(uint8_t)c` for printable ASCII.** SDL_Keycode values for `SDLK_a` through `SDLK_z` are the same as ASCII lowercase. `SDLK_SPACE` is 32. This is correct because SDL keycodes for printable characters match their ASCII values.

- **ISSUE: Spec A's Phase 6 keyboard module uses `emu_keyboard.h`/`emu_keyboard.cpp` naming.** The test file is `test/test_keyboard.cpp`. This is fine but inconsistent with the existing naming convention (`emu_tty.h`/`emu_tty.cpp` -- short device name). Spec C uses `emu_kbd.h`/`emu_kbd.cpp` which is more consistent. Minor.

### Spec B

- **Device router code sample is accurate.** The slot computation `dev_offset >> 5` correctly maps 32-byte windows. `0xD820 - 0xD800 = 0x20`, `0x20 >> 5 = 1`. Correct for all four device slots.

- **`fb_dirty` comparison check:** Spec B Phase 3 includes `if (frame_buffer[fb_offset] != val)` before setting dirty. This is a micro-optimization that avoids rasterization when firmware writes the same value repeatedly. The optimization is valid but adds a branch per write. For a 1 MHz CPU, this is negligible. Worth including.

- **`display_render()` uses `GL_BGRA` for pixel format.** This assumes the platform byte order. On x86 (little-endian), `uint32_t COLOR_FG = 0xFF33FF33` with `GL_BGRA` would render as B=0x33, G=0xFF, R=0x33, A=0xFF -- which is green. Correct for the intended phosphor color.

- **ISSUE: Spec B uses `n8_memory_map.h` as the constants header name but does not show `machine.h` being removed or deprecated.** The existing `machine.h` defines `const int total_memory = 65536;` which is used in `emulator.cpp` via `#include "machine.h"`. If `n8_memory_map.h` is added, `machine.h` should either be absorbed into it or remain. Spec B does not address this. Spec A addresses it by expanding `machine.h` in place. This is a gap.

### Spec C

- **`kbd_tick()` code sample is critical and correct.** The function `kbd_tick()` checks `(kbd_status & N8_KBD_STAT_AVAIL) && (kbd_ctrl & N8_KBD_CTRL_IRQ_EN)` and calls `emu_set_irq(N8_IRQ_BIT_KBD)`. This is the exact pattern used by `tty_tick()` (`emu_tty.cpp:52-53`): check for data availability, then call `emu_set_irq()`. The IRQ clear-reassert-per-tick architecture REQUIRES this for any device that holds state between ticks.

- **ROM write protection code sample:** `if (addr < N8_ROM_BASE) { mem[addr] = ... }` placed in the generic write path. This is correct and clean. CPU writes to `$E000+` are dropped. Direct `mem[]` writes (from `load_at()`, `emulator_loadrom()`) bypass the bus decode entirely and still work. The existing `EmulatorFixture::load_at()` writes directly to `mem[]` so tests are unaffected.

- **ISSUE: Spec C Phase 0 says "No behavior changes. No magic number replacements yet."** But it adds `#include "n8_memory_map.h"` to `test_helpers.h`. This is fine (a header include with no actual usage is harmless) but the "no magic number replacements" language is misleading since Phase 1 immediately starts using the constants. The intent is clear but the wording could confuse the implementer about whether Phase 0 also does the replacements. It does not -- Phase 1 does.

- **Spec C `video_scroll_up()` uses `memmove()` instead of `memcpy()`.** The current Spec A sample uses `memcpy()` for non-overlapping row copies. Spec C uses `memmove()` which is safe for overlapping regions. For scroll up, source is `frame_buffer + w` and dest is `frame_buffer` -- these DO overlap when rows are contiguous. **Spec C's use of `memmove()` is correct; Spec A's use of `memcpy()` in `video_scroll_up()` is a bug.** With `memcpy()`, the copy from row 1 to row 0 could corrupt row 1's data before it is used to copy row 2 to row 1 (implementation-defined behavior). In practice, most `memcpy()` implementations copy forward, so scroll-up with `memcpy()` works by accident on x86. But `memmove()` is correct by specification.

**Verdict on `memcpy` vs `memmove`:** Spec A's scroll-up/down functions use per-row `memcpy()` (non-overlapping within each row copy). Looking more carefully at Spec A: it copies `fb + (row+1)*w` to `fb + row*w` in a loop. Each individual `memcpy()` call copies between non-overlapping regions (source is the next row, destination is the current row). This is correct. Spec C does the entire scroll in one `memmove(frame_buffer, frame_buffer + w, w * (h - 1))` which is also correct. Both approaches work. Spec C's is more concise; Spec A's is more readable.

---

## 3. Dependency Chain

### Cross-Phase Dependencies

All three specs agree on the core dependency chain:

```
Phase 0 (constants) -> Phase 1 (IRQ) -> Phase 2 (TTY) -> Phase 3 (FB)
```

Phases 4 (video) and 5 (keyboard) depend on Phase 3 but are independent of each other. All specs note this.

**Where the specs diverge:**

| Topic | Spec A | Spec B | Spec C |
|-------|--------|--------|--------|
| ROM migration | Phase 4 (before video/kbd) | Phase 6 (after kbd) | Phase 6 (after kbd) |
| Rendering | Phase 7 (after kbd) | Phase 7 (after ROM) | Out of scope |
| GDB stub | Phase 9 (last) | Phase 9 (last) | Phase 7 (after ROM) |

**Analysis:**

- **Spec A places ROM migration at Phase 4, before video and keyboard.** The rationale is that `$D800-$DFFF` overlaps the current ROM region, so loading firmware writes padding into device register addresses. This is a valid concern but not a functional problem -- the device router checks addresses at decode time, and the padding bytes in `mem[$D800]` are overwritten by `IRQ_CLR()` every tick. The concern is about developer confusion, not correctness.

- **Spec B and C place ROM migration after all device registers.** The rationale is that ROM migration is a "firmware-breaking" change and should be deferred until all device logic is tested. This is pragmatic -- during Phases 1-5, all tests use inline programs loaded via `EmulatorFixture::load_at()`, not the real firmware.

- **Recommendation: Spec A's early ROM migration (Phase 4) is slightly better** because it means device register tests in Phases 5-6 run with `$D000-$D7FF` already recognized as dev bank and `$D800-$DFFF` not polluted by ROM loader padding. The "firmware breakage" concern is irrelevant during Phases 1-7 because tests never use the real firmware.

### Intra-Phase Dependencies

- **Phase 3 (FB expansion) depends on Phase 2 (TTY move).** Without removing the TTY decode at `$C100`, the expanded frame buffer collides with TTY. All specs recognize this.

- **Phase 7 (rendering) depends on Phases 4 and 5 (video + keyboard).** In Spec A, it also depends on Phase 3 because it introduces frame_buffer[] separation. In Specs B and C, Phase 3 already does the separation, so Phase 7 only adds rendering.

- **Phase 8 (firmware) depends on ALL prior phases.** Firmware must use all new addresses simultaneously. There is no incremental firmware migration path. All specs agree.

### Missing Dependencies

- **`emu_tty.cpp` does not include `machine.h` or `n8_memory_map.h`.** Spec A Phase 0 says to add `#include "machine.h"` to `emu_tty.cpp` and replace `emu_set_irq(1)` with `emu_set_irq(N8_IRQ_BIT_TTY)`. This is correct but means `emu_tty.cpp` gains a dependency on the constants header. Currently it only includes `emu_tty.h`, `emulator.h`, and `m6502.h`. The include path in the Makefile (`-I$(SRC_DIR)`) covers this, so no build change needed.

- **`test_helpers.h` does not include `machine.h`.** All specs propose adding the constants header include to `test_helpers.h`. This creates a transitive dependency: every test file that includes `test_helpers.h` now also includes the constants header. Correct and necessary.

---

## 4. Test Feasibility

### Test Count Comparison

| Phase | Spec A | Spec B | Spec C |
|------:|-------:|-------:|-------:|
| 0 | 0 | 0 | 0 |
| 1 (IRQ) | 4 | 4 | 4 |
| 2 (TTY) | 3 | 3 | 3 |
| 3 (FB) | 4 | 4 | 7 |
| 4/5 (Video) | 16 | 16 | 17 |
| 5/6 (Keyboard) | 15 | 15 | 17 |
| 6/7 (ROM) | 5 | 5 | 7 |
| 7 (Rendering) | 4 | 0 | -- |
| 9 (GDB) | 4 | 5 | 5 |
| **Total new** | **55** | **52** | **60** |
| **Running total** | **276** | **273** | **281** |

Spec C has the most tests because it adds:
- T121 (FB write does NOT appear in `mem[]`) -- important regression test.
- T122 (`emulator_reset()` clears frame buffer) -- important lifecycle test.
- T123 (`fb_dirty` flag) -- optimization verification.
- T146 (scroll buffer overflow guard) -- safety test.
- T165-T166 (`kbd_tick()` IRQ reassertion) -- critical correctness tests.
- T175 (ROM write protection) -- new feature test.
- T176 (dev bank writable) -- boundary test.

### Test Feasibility Issues

- **EmulatorFixture needs updating for `frame_buffer[]`.** After Phase 3, `EmulatorFixture` must `memset(frame_buffer, 0, N8_FB_SIZE)` in its constructor. None of the specs explicitly call this out, though it is implied by the test changes. The current constructor memsets `mem[]`, `bp_mask[]`, and `wp_mask[]`. Adding `frame_buffer[]` and `fb_dirty` is straightforward.

- **Video and keyboard device fixtures.** Tests T130-T145 and T150-T166 call device functions directly (`video_decode()`, `kbd_decode()`, etc.) as well as through the bus decode (via `EmulatorFixture::step_n()`). The direct-call tests need no fixture changes. The bus-decode tests need the device to be initialized -- this happens in `emulator_init()` which is NOT called by `EmulatorFixture`. **This is a gap.** `EmulatorFixture` calls `m6502_init()` but does not call `video_init()` or `kbd_init()`. After Phase 4, bus-decode tests for video will fail unless `EmulatorFixture` is updated to initialize the new devices.

  **Fix:** Either (a) add `video_init()` and `kbd_init()` calls to `EmulatorFixture` constructor when those phases land, or (b) add them to a test-only init function. Option (a) is simpler.

  Spec A does say "Add `video_init()` to `emulator_init()`. Add `video_reset()` to `emulator_reset()`." But `EmulatorFixture` does NOT call `emulator_init()` or `emulator_reset()` -- it manually initializes the CPU and clears memory. This is a real gap that would cause test failures.

- **T170 (code at `$E000` is executable) and T174 (legacy loadrom).** These tests would need to either (a) call `emulator_loadrom()` with a test binary, or (b) write code directly to `mem[$E000]` via `load_at()`. Option (b) is simpler and matches the existing pattern. Spec A and C describe the test as "NOP sled, PC advances" which uses `load_at()`. Feasible.

- **T175 (ROM write protection) requires running CPU instructions.** A program that does `STA $E000` should NOT change `mem[$E000]`. But the test loads code at `$D000` via `load_at()` (direct mem write, not bus decode), which writes to `mem[$D000]`. The STA instruction runs through bus decode and the write protection blocks it. This is testable and correct.

- **GDB tests T183-T184 use the GDB stub test infrastructure.** The existing `test_gdb_protocol.cpp` and `test_gdb_callbacks.cpp` already use `gdb_stub_feed_byte()` and `gdb_stub_process_packet()`. Adding tests for device register reads via GDB is feasible using the existing infrastructure.

---

## 5. Build System

### Makefile Changes Required

- **New source files:** `emu_video.cpp`, `emu_keyboard.cpp` (or `emu_kbd.cpp`) must be added to both `SOURCES` (main build) and `TEST_SRC_OBJS` (test build). All specs identify this.

- **Display module exclusion:** `emu_display.cpp` must be added to `SOURCES` only, NOT `TEST_SRC_OBJS`, because it depends on OpenGL which is not linked in the test build. Specs A and B correctly identify this. Spec C does not have a display module (rendering is out of scope).

- **Current `TEST_SRC_OBJS` pattern:**
  ```makefile
  TEST_SRC_OBJS = $(BUILD_DIR)/emulator.o $(BUILD_DIR)/emu_tty.o \
                  $(BUILD_DIR)/emu_dis6502.o $(BUILD_DIR)/emu_labels.o \
                  $(BUILD_DIR)/utils.o $(TEST_BUILD_DIR)/gdb_stub.o
  ```
  New entries would be:
  ```makefile
  TEST_SRC_OBJS += $(BUILD_DIR)/emu_video.o $(BUILD_DIR)/emu_kbd.o
  ```

- **ISSUE: The production build compiles source files in `$(BUILD_DIR)` with `CXXFLAGS` which includes `-DENABLE_GDB_STUB=1` and ImGui/SDL includes.** The test build reuses `$(BUILD_DIR)/emulator.o` (compiled with production flags) but links without SDL/ImGui/OpenGL libs. This works because `emulator.cpp` does `#include "imgui.h"` but only uses ImGui in `emulator_show_*` functions which are never called from tests (they are satisfied by link-time stubs in `test_stubs.cpp`).

  New modules `emu_video.cpp` and `emu_kbd.cpp` should NOT include ImGui or SDL headers. They are pure device logic. This is correct in all specs. `emu_display.cpp` includes OpenGL and ImGui headers, which is why it is excluded from the test build.

- **ISSUE: `emulator.o` is compiled ONCE with production flags and reused in tests.** If `emulator.cpp` gains `#include "emu_video.h"` and `#include "emu_keyboard.h"`, the production build of `emulator.o` will attempt to resolve these headers. Since both files are in `src/`, and the Makefile has `-I$(SRC_DIR)` only in `TEST_CXXFLAGS` (not `CXXFLAGS`), this would fail.

  **Wait -- checking the Makefile:** `CXXFLAGS` does NOT have `-I$(SRC_DIR)`. The production build compiles `$(SRC_DIR)/%.cpp` via the pattern rule `$(BUILD_DIR)/%.o:$(SRC_DIR)/%.cpp` with `$(CXXFLAGS)`. Since the source files use `#include "emulator.h"` (not `#include "src/emulator.h"`), the compiler resolves these via the default search path (same directory as the source file). Since `emu_video.h` and `emu_keyboard.h` will be in `src/` alongside `emulator.cpp`, the include will resolve correctly.

  Confirmed: this is not a problem. The compiler's `-c -o $@ $<` with `$< = src/emulator.cpp` will search `src/` for quoted includes.

- **New test files:** `test/test_video.cpp` and `test/test_keyboard.cpp` (or `test_kbd.cpp`) are automatically picked up by `TEST_SOURCES = $(wildcard $(TEST_DIR)/*.cpp)`. No Makefile change needed for test source files.

- **Missing from all specs: The production build's `SOURCES` uses explicit file lists, not wildcards.** Each new `.cpp` file in `src/` must be explicitly added to `SOURCES`. All specs mention this for device modules but Spec A is the most explicit about the exact Makefile lines.

---

## 6. Which Spec to Follow (Per Phase)

### Phase 0: Constants Header

**Follow: Spec A for content, with adjustments.**

- Spec A proposes expanding `machine.h`. Specs B and C propose a new `n8_memory_map.h`.
- **Recommendation: Use a new `n8_memory_map.h` file (Spec B/C approach).** Rationale: `machine.h` currently has one line (`const int total_memory = 65536;`). Stuffing 60+ `#define` macros into it changes its character entirely. A dedicated file with a descriptive name is cleaner. The existing `#include "machine.h"` in `emulator.cpp` stays; add a new `#include "n8_memory_map.h"` alongside it.
- Use Spec C's header content -- it is the most complete, including `N8_LEGACY_*` defines for the transition period, `N8_DEV_SLOT_SIZE`, slot numbers, and `N8_FB_END`.
- Create `firmware/n8_memory_map.inc` per Spec C (Spec A calls it `firmware/hw_regs.inc`; the `n8_memory_map` name is more consistent).

### Phase 1: IRQ Migration

**Follow: Spec C for the device router approach, Spec A for code samples.**

- Spec C introduces the slot-based device router in Phase 1. Spec A's reconciliation notes say "use individual BUS_DECODE macros, not a router" but the original Spec A described the router. The revised Spec A contradicts itself -- the reconciliation says no router, but the rest of the spec does not show how four devices would be decoded without one.
- **Recommendation: Use the device router (Spec C's approach).** Four devices in `$D800-$DFFF` with individual `BUS_DECODE` macros would require four separate blocks, each computing a register offset. The router is a single range check with a clean switch.
- Use Spec A's test descriptions for T110-T113 (they are identical across all specs).

### Phase 2: TTY Migration

**Follow: Any spec -- they are identical.** The TTY module is address-agnostic. The change is one router case line and test address updates.

### Phase 3: Frame Buffer Expansion

**Follow: Spec C for scope, Spec B for the dirty flag optimization.**

- **Spec A defers `frame_buffer[]` separation to Phase 7. Spec B and C do it in Phase 3.**
- **Recommendation: Separate in Phase 3 (Spec B/C).** `hardware.md` says "routed to a separate backing store (not backed by main RAM)." Implementing this early means all subsequent phases (video scroll ops in Phase 4) operate on `frame_buffer[]` from the start, avoiding the Phase 7 churn of rewriting scroll functions. This reduces total work.
- Include Spec B's `fb_dirty` comparison check (`if (frame_buffer[fb_offset] != val)`).
- Include Spec C's extra tests: T121 (mem[] isolation), T122 (reset clears FB), T123 (fb_dirty).
- Include Spec A/C's GDB callback update in this phase.

### Phase 4: Video Control Registers

**Follow: Spec C for implementation, Spec A for scroll function code samples.**

- All specs produce nearly identical `emu_video.cpp` code. Spec A has the most detailed scroll implementation with the `safe_rows()` guard function. Spec C's implementation is more concise (inline clamp check).
- **Recommendation: Use Spec A's `safe_rows()` pattern -- it is clearer and reusable across all four scroll functions.** But use `frame_buffer[]` directly (not `&mem[N8_FB_BASE]`) since Phase 3 already separated the backing store.
- Include Spec C's T146 (scroll buffer overflow safety test) -- it is the only spec that explicitly tests the guard.

### Phase 5: Keyboard Registers

**Follow: Spec C.** It is the only spec that includes `kbd_tick()`.

- **This is a critical correctness issue.** The IRQ mechanism works by `IRQ_CLR()` zeroing ALL flags every tick. Devices must reassert their IRQ bits each tick. The TTY already does this in `tty_tick()` (`emu_tty.cpp:52-53`). The keyboard MUST do the same, or keyboard IRQs will be lost after one tick.
- Spec C's reconciliation note 11 shows the reviewer initially agreeing with Specs A/B (no `kbd_tick()`), then realizing the error and correcting. The reasoning is transparent and correct.
- Use Spec C's `emu_kbd.h`/`emu_kbd.cpp` naming (shorter, consistent with `emu_tty`).
- Include T165-T166 (kbd_tick IRQ reassertion tests) -- they verify the most important correctness property of the keyboard device.
- For SDL key translation, use Spec A's complete code sample (it has the full shifted-symbol table).
- From Spec B: filter `event.key.repeat != 0` to ignore auto-repeat. This is missing from Spec A but present in both Specs B and C.

### Phase 6: ROM Layout Migration

**Follow: Spec C for scope (includes ROM write protection), Spec A for the dual-path loader code.**

- Spec C adds ROM write protection in this phase. Spec A defers it. Spec B makes it optional.
- **Recommendation: Include ROM write protection.** It is 3 lines of code (`if (addr < N8_ROM_BASE) { mem[addr] = ... }`) and prevents a class of firmware bugs (accidental ROM writes). It does not affect tests because `EmulatorFixture::load_at()` writes directly to `mem[]`, bypassing bus decode.
- Use the single ROM linker config (Spec A/B approach, not Spec C's original four-region split). The revised Spec C already agrees with this.

### Phase 7: GDB Stub Update

**Follow: Spec C** (it has the most comprehensive GDB tests, T180-T184).

- All specs produce identical `memory_map_xml[]` content.
- Spec C adds T184 (GDB write to `$C000` writes to `frame_buffer[]`) which verifies the Phase 3 GDB callback fix. Important regression test.

### Phase 8: Firmware Source Migration

**Follow: Spec A for devices.inc structure, Spec C for the explicit file list.**

- Spec A provides `devices.inc` with legacy aliases and a clean `hw_regs.inc` include. Spec C does the same with `n8_memory_map.inc`.
- Both approaches work. Use `n8_memory_map.inc` for consistency with the header file name.
- Spec C provides explicit file-by-file change lists with rollback procedures.

### Phase 9: Playground Migration and Cleanup

**Follow: Spec C** (it separates playground migration from cleanup, which is cleaner for git history).

### Rendering Pipeline

**Follow: Spec B for the detailed rendering architecture** (data flow diagrams, performance budget, cursor compositing). Use Spec A's code samples for `emu_display.cpp` as a starting point. Spec B's `rasterize_cursor()` function is more complete (handles flash rate via frame counter).

**Note:** The rendering pipeline should be implemented AFTER all bus architecture phases are complete, as a separate effort. Spec C correctly excludes it from scope.

---

## 7. Missing Implementation Details

### 7.1 EmulatorFixture Initialization Gap

As noted in section 4, `EmulatorFixture` does not call `emulator_init()`. When `video_init()` and `kbd_init()` are added to `emulator_init()`, the fixture constructor must also initialize these devices. Otherwise, bus-decode tests that trigger video or keyboard decode will operate on uninitialized state.

**Fix:** Add to `EmulatorFixture` constructor:
```cpp
video_init();   // After Phase 4
kbd_init();     // After Phase 5
```

Or, better: add a `video_reset()` and `kbd_reset()` call (which the specs already put in `emulator_reset()`). The fixture does not call `emulator_reset()` either, so the resets must be called explicitly.

### 7.2 emu_tty.cpp Interaction with Constants

The current `emu_tty.cpp` uses hardcoded `emu_set_irq(1)` and `emu_clr_irq(1)`. Spec A says to replace with `emu_set_irq(N8_IRQ_BIT_TTY)`. To use this constant, `emu_tty.cpp` needs `#include "n8_memory_map.h"` (or `machine.h` if using Spec A's approach). Spec A mentions this; Specs B and C do not explicitly call it out.

### 7.3 GDB Device Register Reads

All specs say GDB reads of `$D800-$DFFF` should return `mem[addr]` to avoid side effects. But after Phase 1, the device router writes to `mem[N8_IRQ_FLAGS]` only for the IRQ device (which is backed by `mem[]`). For TTY, video, and keyboard, the device state is in module-local variables, NOT in `mem[]`. So `gdb_read_mem($D840)` would return whatever was in `mem[$D840]` from the ROM loader padding, not the actual VID_MODE register value.

**This is a known limitation.** The specs acknowledge it as acceptable for a debugger (you see the last bus-level value, not the device's internal state). For full device register visibility via GDB, a dedicated callback would be needed, but this is out of scope.

### 7.4 `tty_init()` Calls `set_conio()` (Raw Terminal Mode)

The current `tty_init()` function calls `set_conio()` which puts the terminal into raw mode. In the test binary, `emulator_init()` is never called, but `tty_init()` is also never called from tests. `EmulatorFixture` calls `tty_reset()` (which clears the buffer and IRQ, but does not call `set_conio()`). This is correct for tests.

When `video_init()` and `kbd_init()` are added, ensure they have no host-side side effects (terminal mode, SDL init, etc.). The code samples in all specs show these init functions only setting register defaults, which is correct.

### 7.5 `emulator_show_status_window()` Reference to `mem[0x00FF]`

At `emulator.cpp:386`: `ImGui::Text(" IRQ: %2d %2d Last PC: %4.4x", ... (int) (mem[0x00FF] != 0), ...)`. This must be updated to `mem[N8_IRQ_FLAGS]` in Phase 1. Spec A mentions this; Specs B and C do not explicitly list it. The implementer must grep for all `0x00FF` references.

### 7.6 Rendering Pipeline and Test Build Boundary

The `emu_display.cpp` module includes OpenGL and ImGui headers. It must be excluded from `TEST_SRC_OBJS`. But it references `frame_buffer[]` (declared in `emulator.h`) and `video_get_*()` functions (declared in `emu_video.h`). These dependencies are satisfied in the test build via the production `.o` files already in `TEST_SRC_OBJS`. No issues here.

However, `emu_display.cpp` also needs `n8_font.h`. If `n8_font.h` is `static const`, it only compiles into the TU that includes it. No linker issues. Correct.

### 7.7 `machine.h` Disposition

If a new `n8_memory_map.h` is created (Spec B/C approach), `machine.h` still exists with `const int total_memory = 65536;`. This constant is not used as a `#define` anywhere, but it might be referenced. A grep shows it is only in `machine.h` itself. The `mem[]` array is declared as `uint8_t mem[(1<<16)]` in `emulator.cpp`, not using `total_memory`. So `machine.h` can remain as-is indefinitely, or `total_memory` can be moved to `n8_memory_map.h` as `#define N8_TOTAL_MEMORY 65536`. Low priority.

---

## 8. Effort Estimate

Assuming one implementer, familiar with the codebase, working in focused sessions:

| Phase | Effort | Notes |
|-------|--------|-------|
| 0 (Constants) | 2 hours | Create header, add includes, verify compile. Mechanical. |
| 1 (IRQ move) | 3 hours | Replace constants, add router skeleton, write 4 tests, verify. |
| 2 (TTY move) | 2 hours | Move one router case, update test addresses, write 3 tests. |
| 3 (FB expand) | 4 hours | Structural change to emulator_step(), frame_buffer[] separation, GDB callbacks, 7 tests. **Highest risk phase.** |
| 4 (Video regs) | 6 hours | New module (emu_video.cpp), 4 scroll functions, 17 tests. Largest new code. |
| 5 (Keyboard) | 5 hours | New module (emu_kbd.cpp), kbd_tick(), 17 tests, SDL key translation. |
| 6 (ROM move) | 4 hours | Dual-path loader, ROM write protection, linker config, 7 tests. |
| 7 (GDB stub) | 1 hour | XML string update, 5 tests. |
| 8 (Firmware) | 3 hours | devices.inc, zp.inc, devices.h updates. Build verification. Manual smoke test. |
| 9 (Playground) | 2 hours | Update equates in 4-6 files. Build verification. |
| 10 (E2E/cleanup) | 3 hours | Remove legacy code. Run all 10 E2E scenarios manually. |
| **Rendering** | **8 hours** | emu_display.cpp, GL texture, font copy, ImGui window, cursor. Separate effort. |
| **Total** | **~43 hours** | Including rendering. ~35 hours without rendering. |

This is roughly 5 working days without rendering, 6 with rendering. Conservative -- an implementer deeply familiar with the codebase could do it in 3-4 days.

---

## 9. Spec Convergence

The three specs converge on all fundamental architectural decisions after reconciliation. Remaining disagreements are minor:

| Topic | Spec A (Revised) | Spec B (Revised) | Spec C (Revised) | Resolution |
|-------|-------------------|-------------------|-------------------|------------|
| Constants file | Expand `machine.h` | New `n8_memory_map.h` | New `n8_memory_map.h` | Use `n8_memory_map.h` (B/C) |
| Device router | No router (BUS_DECODE per device) | Router (slot dispatch) | Router (slot dispatch) | Use router (B/C) |
| frame_buffer[] timing | Phase 7 | Phase 3 | Phase 3 | Phase 3 (B/C) |
| kbd_tick() | Not included | Not included | Included | Include (C is correct) |
| ROM write protection | Deferred | Optional | Phase 6 | Phase 6 (C) |
| ROM migration timing | Phase 4 (before video) | Phase 6 (after kbd) | Phase 6 (after kbd) | Phase 4 (A -- reduces confusion) |
| Rendering pipeline | In scope (Phase 7) | In scope (Phase 7) | Out of scope | Separate effort after bus migration |
| Keyboard file naming | `emu_keyboard.*` | `emu_keyboard.*` | `emu_kbd.*` | `emu_kbd.*` (consistent with `emu_tty`) |
| scroll memset fill | 0x00 (reconciled to B) | 0x00 | 0x00 | 0x00 (all agree) |
| fb_dirty comparison | Phase 7 | Phase 3 | Phase 3 | Phase 3 (B/C) |

**Key convergence point:** All three specs agree on the target memory map, device register layout, register semantics, test structure, and rollback strategy. The disagreements are about sequencing and modularity, not about what gets built.

---

## 10. Risk of Over-Engineering

### What is appropriately engineered:
- The device router (4 devices, 32-byte slots, switch dispatch) -- this is the right abstraction level for the hardware.
- The `frame_buffer[]` separation -- required by the hardware spec.
- The `fb_dirty` flag -- necessary for the rendering pipeline optimization.
- The `kbd_tick()` function -- required by the IRQ architecture.
- ROM write protection -- 3 lines of code, prevents a class of bugs.
- Dual-path ROM loader -- necessary for the transition period.

### What approaches over-engineering:
- **Spec C's four-region ROM linker config** (`ROM_MON`, `ROM_IMPL`, `ROM_ENTRY`, `ROM_VEC`). The current firmware is ~1KB. A single 8KB ROM region is sufficient. All three revised specs agree on this, so the risk is mitigated.
- **Spec B v1.0's GPU font atlas.** Dropped in v2.0 in favor of CPU rasterization. Correct decision.
- **Spec B's `DISPLAY_W` / `DISPLAY_H` macros using `N8_VID_DEFAULT_WIDTH * N8_FONT_WIDTH`.** This is technically correct but adds indirection for a constant (640). A simple `#define DISPLAY_W 640` is clearer and the math is obvious to anyone who reads "80 columns x 8 pixels."
- **The `N8_LEGACY_*` defines in Spec C's header.** These are used only in the dual-path ROM loader. Four extra `#define`s in a 60-line header is not over-engineering, but they should have a clear "remove after Phase 10" comment.

### What is under-engineered:
- **Spec A's omission of `kbd_tick()`.** This is a correctness bug, not just a missing optimization.
- **All specs under-specify the `EmulatorFixture` update** needed for device initialization.
- **No spec addresses what happens when firmware writes to VID_OPER=`$00` (scroll up) before any text is written.** The scroll functions will operate on an all-zeros `frame_buffer[]` and produce all-zeros output. Harmless but worth a sentence in the spec.

---

## Recommended Implementation Path

Follow this composite plan, cherry-picking from each spec where it is strongest:

### Phase 0: Constants Header
- Create `src/n8_memory_map.h` using **Spec C's content** (most complete, includes legacy defines and slot numbers).
- Create `firmware/n8_memory_map.inc` per **Spec C**.
- Add `#include "n8_memory_map.h"` to `test/test_helpers.h`.
- Keep `src/machine.h` as-is (do not modify or remove).
- **No magic number replacements.** Those happen in Phase 1.

### Phase 1: IRQ Migration + Device Router
- Follow **Spec C** for the device router skeleton and IRQ decode.
- Replace magic numbers in `emulator.cpp`, `emu_tty.cpp` (add `#include "n8_memory_map.h"`).
- Update `emulator_show_status_window()` `mem[0x00FF]` reference.
- Tests T110-T113 per all specs (identical).
- Update `EmulatorFixture` if needed (the IRQ address change is handled by the constant).

### Phase 2: TTY Migration
- Follow **any spec** (identical across all three). Add router case 1.
- Update test addresses.
- Tests T114-T116.

### Phase 3: Frame Buffer Expansion + Separation
- Follow **Spec C** for scope (7 tests including T121, T122, T123).
- Follow **Spec B** for the `fb_dirty` comparison optimization.
- Introduce `frame_buffer[N8_FB_SIZE]` in `emulator.cpp`.
- Add `extern frame_buffer[]` and `extern fb_dirty` to `emulator.h`.
- Restructure `emulator_step()` bus decode: frame buffer intercept BEFORE generic `mem[]`.
- Update GDB callbacks in `main.cpp`.
- Update `EmulatorFixture` to `memset(frame_buffer, 0, N8_FB_SIZE)`.
- Update existing tests (T64-T67, T99) to use `frame_buffer[]`.

### Phase 4: ROM Migration (moved earlier per Spec A's rationale)
- Follow **Spec A** for timing (before video/keyboard).
- Follow **Spec C** for the dual-path loader and ROM write protection.
- Use single 8KB ROM linker config.
- Tests T170-T176.

### Phase 5: Video Control Registers
- Follow **Spec A** for code samples (scroll functions with `safe_rows()` guard).
- Follow **Spec C** for test T146 (overflow safety).
- Scroll functions operate on `frame_buffer[]` (already separated in Phase 3).
- Add router case 2.
- Update `EmulatorFixture` to call `video_init()`.
- Tests T130-T146 (17 tests).

### Phase 6: Keyboard Registers
- Follow **Spec C** for `emu_kbd.h`/`emu_kbd.cpp` with `kbd_tick()`.
- Follow **Spec A/B** for SDL key translation with full shifted-symbol table.
- Filter `event.key.repeat != 0` per **Spec B**.
- Add router case 3.
- Add `kbd_tick()` call in `emulator_step()` next to `tty_tick()`.
- Update `EmulatorFixture` to call `kbd_init()`.
- Tests T150-T166 (17 tests, including T165-T166 for `kbd_tick()`).

### Phase 7: GDB Stub Update
- Follow **Spec C** (T180-T184, 5 tests including T184 for frame buffer GDB write).
- Update `memory_map_xml[]`.
- Update T58 expected values.

### Phase 8: Firmware Source Migration
- Follow **Spec C** for the file list and `devices.inc` structure (include `n8_memory_map.inc`, legacy aliases).
- Remove `ZP_IRQ` from `zp.inc`.
- `make firmware` as gate.

### Phase 9: Playground Migration
- Follow **Spec C** for the explicit file list (mon1.s, mon2.s, mon3.s, test_tty.s, both playground.cfg files).

### Phase 10: Cleanup + E2E
- Remove `N8_LEGACY_*` defines.
- Remove dual-path ROM loader.
- Remove legacy aliases from `devices.inc`.
- Run E2E-01 through E2E-10 manually.
- Tag `git tag memory-map-v2`.

### Rendering Pipeline (Separate Effort)
- Follow **Spec B** for architecture (data flow diagrams, performance budget, cursor compositing with frame counter).
- Follow **Spec A** for `emu_display.cpp` code samples.
- Copy `docs/charset/n8_font.h` to `src/n8_font.h`.
- Exclude `emu_display.cpp` from `TEST_SRC_OBJS`.

### Expected Final Test Count

| Phase | New | Total |
|------:|----:|------:|
| 0 | 0 | 221 |
| 1 | 4 | 225 |
| 2 | 3 | 228 |
| 3 | 7 | 235 |
| 4 | 7 | 242 |
| 5 | 17 | 259 |
| 6 | 17 | 276 |
| 7 | 5 | 281 |
| 8-10 | 0 | 281 |
| **Total** | **60 new** | **281 automated + 10 E2E manual** |
