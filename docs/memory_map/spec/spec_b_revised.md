# Spec B (Revised): Graphics Pipeline, Keyboard Input, and Memory Map Migration

Version: 2.0
Date: 2026-02-22
Revised from: spec_b_graphics_pipeline.md v1.0

---

## Reconciliation Notes

This revision incorporates feedback from peer specs A (bus architecture) and C
(integration/testing). Changes are grouped by topic.

### 1. frame_buffer[] vs mem[] -- Separate Backing Store Confirmed

**Original Spec B** introduced `frame_buffer[]` inside `emu_video.cpp` with
accessor functions (`video_get_framebuffer()`, `video_fb_dirty()`). **Spec A**
also introduces a separate `frame_buffer[]` in `emulator.cpp` as an extern
global. **Spec C** deferred the separation, keeping `mem[]` as backing store
in Phase 3 and noting a future migration.

**Resolution:** The hardware spec (`hardware.md`) explicitly says "CPU
reads/writes are intercepted by bus decode and routed to a separate backing
store (not backed by main RAM)." The user's design intent is clear. The
separation happens in the same phase as the frame buffer expansion (Phase 3).

However, I am dropping the `video_get_framebuffer()` accessor pattern from
v1.0 and instead adopting Spec A's approach: `frame_buffer[]` is a global
`extern uint8_t frame_buffer[N8_FB_SIZE]` declared in `emulator.h`, owned by
`emulator.cpp`. This is simpler, avoids an unnecessary abstraction layer, and
keeps the frame buffer in the same module that does bus decode. The video
module and display renderer access it directly via the extern. Spec A
correctly identified this as the cleaner approach.

Spec B v1.0's `fb_dirty` flag is retained but moved to `emulator.cpp` (the
module that intercepts writes) rather than `emu_video.cpp`. The flag is
exposed via `emulator_fb_dirty()` / `emulator_fb_clear_dirty()`. The dirty
flag is an optimization, not a hardware register, so it belongs in the
emulator bus decode layer.

### 2. Phase Count -- Reduced from 12 to 10

Spec B v1.0 had 12 phases. Spec A has 10. Spec C has 10.

**Resolution:** Merged the following:

- **Old Phase 7 (Font Loading) and Phase 8 (Screen Texture/Rendering)** are
  merged into a single **Phase 7: Video Rendering Pipeline**. The font texture
  atlas from v1.0 is dropped in favor of Spec A's CPU-side rasterization
  approach (which I also described in v1.0 Phase 8). There is no reason to
  create a separate GPU font atlas when CPU rasterization is the chosen path.
  The font data is used directly from the `n8_font[]` array.

- **Old Phase 9 (Cursor Rendering)** is folded into Phase 7 as a subsection.
  Cursor compositing is part of the rasterization function, not a separate
  integration point.

- **Old Phase 10 (Complete Bus Decode Refactor)** is eliminated as a
  standalone phase. The bus decode is built up incrementally across Phases 1-6
  using Spec A's device router pattern. By Phase 6, the bus decode is already
  in final form.

- **Old Phase 12 (Makefile Updates)** is distributed into the phases that add
  new source files (Phases 5 and 6).

New phase count: **10 phases** (0-9), aligned with Specs A and C.

### 3. Constants Header Name

Spec A: `src/n8_memory_map.h` with `#define N8_*` prefix.
Spec B v1.0: `src/machine.h` with `static const` C++ variables, no prefix.
Spec C: `src/hw_regs.h` with `#define HW_*` prefix, plus `firmware/hw_regs.inc`.

**Resolution:** Adopt Spec A's name `src/n8_memory_map.h` with `#define`
macros using the `N8_` prefix. Rationale:

- `#define` macros work in both C and C++ and can be used in preprocessor
  conditionals. `static const` variables cannot.
- The `N8_` prefix is project-specific and avoids collisions.
- `n8_memory_map.h` clearly describes the file's purpose.
- Spec C's parallel `firmware/hw_regs.inc` for assembly is a good idea and
  is adopted. The assembly file uses `.define` directives mirroring the C
  header. However, the firmware already has `devices.inc` / `devices.h` which
  serve this purpose. Rather than adding a new file, update the existing
  `devices.inc` / `devices.h` with the new addresses (as both Spec A Phase 8
  and Spec C Phase 8 describe). The emulator-side constants live in
  `n8_memory_map.h`.

### 4. Device Router vs Per-Device BUS_DECODE

Spec A introduces a device router: a single range check for `$D800-$DFFF`
with slot dispatch via `(addr - 0xD800) >> 5`. Spec B v1.0 used individual
`BUS_DECODE` calls per device. Spec C used individual `BUS_DECODE` calls.

**Resolution:** Adopt Spec A's device router. It is more extensible and avoids
scattered decode blocks. The router is introduced in Phase 1 (IRQ migration)
as a skeleton and populated in subsequent phases.

### 5. Font Data Source

Spec A: "Copy `docs/charset/n8_font.h` to `src/n8_font.h`."
Spec B v1.0: "Copy from `docs/charset/n8_font.h` to `src/n8_font.h`."
Spec C: No specific font handling (rendering not in scope).

**Resolution:** All agree. The font file is `docs/charset/n8_font.h`. It is a
`static const uint8_t n8_font[256][16]` array (256 characters, 16 bytes each,
1-bit-per-pixel, MSB = leftmost pixel). Copy to `src/n8_font.h` so the
emulator build does not depend on `docs/`. This is a build-time copy, not a
code change.

### 6. Rendering Architecture

Spec A and Spec B v1.0 both converge on CPU-side rasterization to a pixel
buffer, then `glTexSubImage2D` upload to an OpenGL texture displayed via
`ImGui::Image()`. Spec A correctly identified the SDL_Renderer conflict
(cannot use SDL_Renderer alongside SDL_GL_CreateContext).

**Resolution:** Use OpenGL directly (`GLuint` texture, `glTexSubImage2D`).
Spec B v1.0 Phase 8 already described this. The font atlas GPU texture from
Phase 7 v1.0 is dropped -- unnecessary with CPU-side rasterization.

### 7. Keyboard: SDL_TEXTINPUT vs SDL_KEYDOWN

Spec B v1.0 used `SDL_KEYDOWN` with manual shift mapping (including a
shift-symbol table). Spec A used the same approach but without the symbol
table, deferring it. Spec C mentioned using SDL's text input event for
printable characters.

**Resolution:** Use `SDL_KEYDOWN` for all keys, with a shift-symbol lookup
table for punctuation. The N8 keyboard emulates a hardware HID-to-ASCII
converter (Teensy 4.1), so the emulator should perform the same conversion.
`SDL_TEXTINPUT` introduces complications with IME and composing sequences
that don't match the hardware model. Keep the door open by noting
`SDL_TEXTINPUT` as a future refinement in Open Questions.

### 8. ROM Base Migration Phase Ordering

Spec A: Phase 7 (after keyboard).
Spec B v1.0: Phase 4 (after frame buffer expansion, before video registers).
Spec C: Phase 6 (after keyboard).

**Resolution:** Adopt Spec A/C ordering: ROM migration after all device
registers are implemented. Rationale: the ROM migration is a firmware-level
breaking change (requires firmware rebuild). Doing it late means all device
register tests are stable before touching the ROM layout. Spec B v1.0's
early placement was premature.

### 9. Scroll Fill Character

Spec A fills cleared scroll rows with `0x20` (space). Spec B v1.0 fills
with `0x00` (null). The hardware spec does not specify.

**Resolution:** Use `0x00`. In the N8 font, character `$00` is a blank glyph
(all zeros). Character `$20` (space) is also blank in most fonts. Using
`0x00` is more "hardware-like" (clearing memory to zero) and matches what
`memset(buf, 0, n)` naturally produces. Both specs' scroll implementations
are functionally equivalent for rendering since both characters are visually
blank.

### 10. GDB Callback Update for Frame Buffer

Spec A correctly identified that `gdb_read_mem()` / `gdb_write_mem()` in
`main.cpp` must redirect `$C000-$CFFF` to `frame_buffer[]` once the
backing store is separated from `mem[]`. Spec B v1.0 missed this. Spec C
deferred the separation entirely.

**Resolution:** Include the GDB callback fix in Phase 3 (frame buffer
expansion), as Spec A describes.

### 11. Test Case Numbering

Spec C has a comprehensive test case registry with sequential IDs
(T110-T184) that avoid collisions with existing tests (T01-T101a). Spec A
uses descriptive names (T_IRQ_01, T_FB_01, etc.). Spec B v1.0 used
descriptive names (T_VID_01, T_KBD_01, etc.).

**Resolution:** Adopt Spec C's sequential numbering convention for the test
registry (T110+) since it avoids ID collisions with existing tests. Use
descriptive names within the spec for readability, and note the corresponding
T-number.

### 12. Spec C's Legacy Compatibility / Dual-Path ROM Loader

Spec C Phase 6 introduces a dual-path `emulator_loadrom()` that auto-detects
8KB vs 12KB binaries. Spec A recommends always loading at `$D000` for backward
compatibility. Spec B v1.0 moved the load address to `$E000` cleanly.

**Resolution:** Add the dual-path loader as a transitional measure, then
remove it in Phase 9 (cleanup). This allows the emulator to load old firmware
binaries during the transition. Spec C's approach is pragmatic.

### 13. Strengths Retained from Spec B v1.0

- **Performance budget table** -- quantifies rendering overhead. Retained.
- **Data flow diagrams** for video and keyboard pipelines. Retained.
- **Dirty flag optimization** -- skip rasterization when frame buffer
  unchanged. Retained (with cursor flash caveat).
- **Detailed shift-symbol lookup table** for keyboard. Retained.
- **Cursor rendering with frame-counter-based flash** (vs SDL_GetTicks).
  Frame counting is deterministic and avoids timer dependencies. Retained.
- **Phosphor color as a UI-selectable option** (not a hardware register).
  Retained as a note.
- **`ImGui::GetContentRegionAvail()` scaling logic** for the display window.
  Retained.

---

## Scope

This specification covers:

1. **Migrate emulator to the proposed memory map** (bus decode, IRQ, TTY,
   frame buffer, device registers, ROM layout, firmware, GDB stub)
2. **Implement video rendering pipeline** (font, CPU-side rasterization,
   OpenGL texture, cursor, display window)
3. **Implement keyboard input** (SDL2 key events, device registers, IRQ)

All three concerns share the bus decode layer and are sequenced so each
phase produces a testable, runnable emulator.

---

## Current State Summary

### Memory Map (as-built)

```
$FFFA-$FFFF  Vectors (NMI/RESET/IRQ)
$D000-$FFF9  ROM (12KB flat, loaded from "N8firmware")
$C100-$C10F  TTY device (4 registers, 16-byte decode window)
$C000-$C0FF  Frame buffer (256 bytes, raw mem[])
$0200-$BFFF  RAM
$0100-$01FF  Hardware Stack
$0000-$00FF  Zero Page (IRQ flags at $00FF)
```

### Bus Decode (`emulator.cpp`)

```cpp
BUS_DECODE(addr, 0x0000, 0xFF00) { /* ZP monitor (logging only) */ }
BUS_DECODE(addr, 0xFFF0, 0xFFF0) { /* Vector access logging */ }
BUS_DECODE(addr, 0xC100, 0xFFF0) { /* TTY: 16-byte window */ }
```

Frame buffer at `$C000` has no bus decode -- reads/writes go directly to
`mem[]`. No ROM write protection. No video hardware. No keyboard device.

### Font Data

`docs/charset/n8_font.h` -- 256 characters, 8x16 pixels, 1-bit-per-pixel
(MSB = leftmost pixel). `static const uint8_t n8_font[256][16]`. CC0 license.
Copy to `src/n8_font.h` for build isolation.

### GUI State

SDL2 + OpenGL3 + Dear ImGui with docking. The main loop renders only ImGui
debug windows (memory, registers, breakpoints, disassembly). No emulated
display window exists.

### Firmware (cc65)

Device addresses hardcoded in `firmware/devices.inc` and `firmware/devices.h`.
Linker config `firmware/n8.cfg`: ROM at `$D000`, RAM at `$0200`, ZP at `$0000`.
IRQ handler in `interrupt.s` references TTY registers. `zp.inc` defines
`ZP_IRQ` at `$FF`.

### Tests (doctest)

221 tests passing (349 assertions). Existing test suites: `test_bus.cpp`,
`test_tty.cpp`, `test_integration.cpp`, `test_gdb_protocol.cpp`,
`test_gdb_callbacks.cpp`, `test_cpu.cpp`, `test_utils.cpp`.

---

## Target Memory Map

From `hardware.md`:

```
$FFE0-$FFFF  Vectors + Bank Switch (32 bytes)
$FE00-$FFDF  Kernel Entry (480 bytes)
$F000-$FDFF  Kernel Implementation (3.5 KB)
$E000-$EFFF  Monitor / stdlib (4 KB)
$D800-$DFFF  Device Registers (2 KB, 32-byte device slots)
$D000-$D7FF  Dev Bank (2 KB)
$C000-$CFFF  Frame Buffer (4 KB, separate backing store)
$0400-$BFFF  Program RAM (47 KB)
$0200-$03FF  Reserved (512 bytes)
$0100-$01FF  Hardware Stack
$0000-$00FF  Zero Page (IRQ flags REMOVED from here)
```

### Device Register Slots (`$D800-$DFFF`)

| Base    | Slot | Device       | Registers |
|--------:|-----:|:-------------|:----------|
| `$D800` | 0    | System/IRQ   | 1 (IRQ_FLAGS) |
| `$D820` | 1    | TTY          | 4 (OUT_STATUS, OUT_DATA, IN_STATUS, IN_DATA) |
| `$D840` | 2    | Video Control| 8 (MODE, WIDTH, HEIGHT, STRIDE, OPER, CURSOR, CURCOL, CURROW) |
| `$D860` | 3    | Keyboard     | 3 (KBD_DATA, KBD_STATUS/ACK, KBD_CTRL) |

---

## Phase Breakdown

### Phase 0: Machine Constants Header

**Goal:** Centralize all memory map addresses and bus decode masks in a single
header. Eliminate magic numbers. Every subsequent phase uses these constants.

**Entry criteria:** Current `make test` passes (221 tests).
**Exit criteria:** All tests still pass. No magic numbers remain in
`emulator.cpp`, `emu_tty.cpp`, or test files for memory-mapped addresses.

**New file: `src/n8_memory_map.h`**

```cpp
#pragma once
#include <stdint.h>

// ============================================================
// N8 Machine Memory Map Constants
// ============================================================

// --- Zero Page & Stack ---
#define N8_ZP_START        0x0000
#define N8_ZP_SIZE         0x0100
#define N8_STACK_START     0x0100
#define N8_STACK_SIZE      0x0100

// --- RAM ---
#define N8_RAM_START       0x0200
#define N8_RAM_END         0xBFFF

// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x0100   // Phase 0: current 256 bytes
#define N8_FB_MASK         0xFF00   // For BUS_DECODE: matches $C000-$C0FF

// --- TTY Device ---
#define N8_TTY_BASE        0xC100   // Phase 0: current location
#define N8_TTY_MASK        0xFFF0   // 16-byte window
#define N8_TTY_REG_COUNT   4

// --- IRQ Register ---
#define N8_IRQ_ADDR        0x00FF   // Phase 0: current location

// --- IRQ Bits ---
#define N8_IRQ_BIT_TTY     1
#define N8_IRQ_BIT_KBD     2

// --- ROM ---
#define N8_ROM_BASE        0xD000
#define N8_ROM_SIZE        0x3000   // 12KB (Phase 0: current)

// --- Vectors ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE

// --- Font ---
#define N8_FONT_CHARS      256
#define N8_FONT_WIDTH      8
#define N8_FONT_HEIGHT     16
```

**Code changes:** Replace all magic numbers in `emulator.cpp`, `emu_tty.cpp`,
test files with the corresponding constants. (Same detailed list as Spec A
Phase 0.)

**Tests:** No new tests. Pure refactor. `make test` must pass unchanged.

**Verification:**
```bash
grep -rn '0x00FF\|0xC000\|0xC100\|0xD000' src/emulator.cpp src/emu_tty.cpp
# Should return zero hits (except comments)
```

---

### Phase 1: IRQ Register Migration (`$00FF` to `$D800`)

**Goal:** Move IRQ flags from zero page `$00FF` to device register space at
`$D800`. Introduce the device register router skeleton.

**Entry criteria:** Phase 0 complete, all tests pass.
**Exit criteria:** IRQ flags live at `$D800`. Old address `$00FF` is ordinary
RAM. Device router handles `$D800-$DFFF`. All tests updated and passing.

**Constants update (`n8_memory_map.h`):**

```cpp
// --- Device Register Space ---
#define N8_DEV_BASE        0xD800
#define N8_DEV_SIZE        0x0800   // 2KB total
#define N8_DEV_SLOT_SIZE   0x0020   // 32 bytes per device

// --- System / IRQ ---
#define N8_IRQ_BASE        0xD800
#define N8_IRQ_ADDR        0xD800   // Replaces 0x00FF
#define N8_IRQ_MASK        0xFFE0   // 32-byte decode window
```

**Device router** (new in `emulator.cpp`):

```cpp
// Device register space $D800-$DFFF
if (addr >= N8_DEV_BASE && addr < (N8_DEV_BASE + N8_DEV_SIZE)) {
    uint16_t dev_offset = addr - N8_DEV_BASE;
    uint8_t  slot = dev_offset >> 5;    // 0-63
    uint8_t  reg  = dev_offset & 0x1F;  // 0-31

    switch (slot) {
        case 0:  // $D800: System / IRQ
            dev_irq_decode(pins, reg);
            break;
        // Future: case 1 = TTY, case 2 = Video, case 3 = Keyboard
        default:
            if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
            break;
    }
}
```

The router dispatches to per-device handlers based on slot index. Reserved
slots read as `$00` and ignore writes. This pattern (from Spec A) is cleaner
than per-device `BUS_DECODE` blocks and scales to future devices.

**IRQ mechanism:** `IRQ_CLR()` and `IRQ_SET()` macros update `mem[N8_IRQ_ADDR]`
(now at `$D800`). The clear-and-reassert-per-tick behavior is unchanged.
The device decode reads/writes `mem[N8_IRQ_ADDR]` directly. Firmware will
use `LDA $D800` (absolute, 4 cycles) instead of `LDA $FF` (ZP, 3 cycles) --
acceptable since IRQ polling is infrequent.

**Test changes:**

Update all `mem[0x00FF]` references in `test_utils.cpp`, `test_tty.cpp` to
use `mem[N8_IRQ_ADDR]`.

New tests (T110-T113):

| ID   | Description |
|------|-------------|
| T110 | Write to `$D800` sets IRQ flags |
| T111 | Read `$D800` returns current IRQ flags |
| T112 | IRQ line asserted when `$D800` non-zero after tick |
| T113 | Old address `$00FF` is now ordinary RAM (independent of IRQ) |

**Risks:** Firmware will reference wrong IRQ address until Phase 8. Acceptable
because tests use inline programs, not the real firmware.

---

### Phase 2: TTY Device Migration (`$C100` to `$D820`)

**Goal:** Move TTY device registers from `$C100` to `$D820`. Integrate TTY
into the device router.

**Entry criteria:** Phase 1 complete, all tests pass.
**Exit criteria:** TTY registers decode at `$D820-$D823`. Old address `$C100`
is ordinary RAM. All tests updated and passing.

**Constants update:**

```cpp
#define N8_TTY_BASE        0xD820   // Was 0xC100
#define N8_TTY_MASK        0xFFE0   // 32-byte decode window
#define N8_TTY_SLOT        1
```

**Code changes:**

Remove standalone TTY `BUS_DECODE` block. Add to device router:

```cpp
case 1:  // $D820: TTY
    tty_decode(pins, reg);
    break;
```

The `tty_decode()` function is address-agnostic (takes register offset 0-3).
No changes to `emu_tty.cpp`.

**Test changes:**

Update pin addresses in `test_tty.cpp`, `test_gdb_callbacks.cpp`. New tests
(T114-T116):

| ID   | Description |
|------|-------------|
| T114 | TTY read at old `$C100` does NOT trigger tty_decode |
| T115 | TTY read at `$D820` returns OUT_STATUS |
| T116 | TTY write at `$D821` sends character |

---

### Phase 3: Frame Buffer Expansion (256B to 4KB)

**Goal:** Expand frame buffer from 256 bytes to 4KB at `$C000-$CFFF`.
Introduce a separate `frame_buffer[]` backing store (not `mem[]`). Update
GDB callbacks to handle the separation.

**Entry criteria:** Phase 2 complete, all tests pass.
**Exit criteria:** Frame buffer is 4KB, backed by `frame_buffer[]`. Bus decode
intercepts `$C000-$CFFF`. GDB read/write callbacks handle redirection.

**Constants update:**

```cpp
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4KB (was 0x0100)
#define N8_FB_END          0xCFFF
```

**Backing store (`emulator.cpp`):**

```cpp
uint8_t frame_buffer[N8_FB_SIZE] = { };
static bool fb_dirty = true;
```

Declared in `emulator.h`:
```cpp
extern uint8_t frame_buffer[];
```

**Bus decode:** Frame buffer intercept runs **before** the generic `mem[]`
access. If the address is in `$C000-$CFFF`, read/write `frame_buffer[]`
and skip `mem[]`:

```cpp
bool fb_access = (addr >= N8_FB_BASE && addr <= N8_FB_END);
if (fb_access) {
    uint16_t fb_offset = addr - N8_FB_BASE;
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, frame_buffer[fb_offset]);
    } else {
        uint8_t val = M6502_GET_DATA(pins);
        if (frame_buffer[fb_offset] != val) {
            frame_buffer[fb_offset] = val;
            fb_dirty = true;
        }
    }
}

// Generic RAM/ROM access (skip if frame buffer handled it)
if (!fb_access) {
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, mem[addr]);
    } else {
        mem[addr] = M6502_GET_DATA(pins);
    }
}
```

The dirty flag is set only when the value actually changes (comparison check).
This avoids unnecessary rasterization when firmware writes the same value.

**Dirty flag API (`emulator.h`):**

```cpp
bool emulator_fb_dirty();
void emulator_fb_clear_dirty();
```

**GDB callback fix (`main.cpp`):**

```cpp
static uint8_t gdb_read_mem(uint16_t addr) {
    if (addr >= N8_FB_BASE && addr <= N8_FB_END)
        return frame_buffer[addr - N8_FB_BASE];
    return mem[addr];
}

static void gdb_write_mem(uint16_t addr, uint8_t val) {
    if (addr >= N8_FB_BASE && addr <= N8_FB_END)
        frame_buffer[addr - N8_FB_BASE] = val;
    else
        mem[addr] = val;
}
```

**Reset:** `emulator_reset()` zeroes `frame_buffer[]` with
`memset(frame_buffer, 0, N8_FB_SIZE)`.

**Test changes:**

Update T64-T67, T99 to check `frame_buffer[]` instead of `mem[]`. New tests
(T117-T120):

| ID   | Description |
|------|-------------|
| T117 | Write to `$C100` stores in mem (no longer TTY, now part of FB) |
| T118 | Write to `$C7CF` (last active byte in 80x25) stores correctly |
| T119 | Write to `$CFFF` (last byte of 4KB buffer) stores correctly |
| T120 | Frame buffer does NOT write to `mem[]` (`mem[$C000]` remains 0) |

**Risks:**

- **Structural change to `emulator_step()`:** The intercept-before-RAM pattern
  changes control flow. All existing bus decode blocks must be verified.
- **GDB stub:** Fixed in this phase (see above).

---

### Phase 4: Video Control Registers (`$D840-$D847`)

**Goal:** Implement the video control register block. No rendering yet -- just
registers as readable/writable device state, plus scroll operations on
`frame_buffer[]`.

**Entry criteria:** Phase 3 complete, all tests pass.
**Exit criteria:** Video registers at `$D840-$D847` are bus-accessible. Mode
write auto-sets defaults. Scroll operations execute on `frame_buffer[]`.

**Constants update:**

```cpp
// --- Video Control ---
#define N8_VID_BASE        0xD840
#define N8_VID_SLOT        2
#define N8_VID_MODE        0x00     // Register offsets within slot
#define N8_VID_WIDTH       0x01
#define N8_VID_HEIGHT      0x02
#define N8_VID_STRIDE      0x03
#define N8_VID_OPER        0x04
#define N8_VID_CURSOR      0x05
#define N8_VID_CURCOL      0x06
#define N8_VID_CURROW      0x07

// VID_MODE values
#define N8_VIDMODE_TEXT_DEFAULT  0x00
#define N8_VIDMODE_TEXT_CUSTOM   0x01

// VID_OPER values
#define N8_VIDOP_SCROLL_UP      0x00
#define N8_VIDOP_SCROLL_DOWN    0x01
#define N8_VIDOP_SCROLL_LEFT    0x02
#define N8_VIDOP_SCROLL_RIGHT   0x03

// Default dimensions
#define N8_VID_DEFAULT_WIDTH    80
#define N8_VID_DEFAULT_HEIGHT   25
```

**New files: `src/emu_video.h` and `src/emu_video.cpp`**

The video module owns an 8-byte register file (`vid_regs[8]`) and provides
`video_init()`, `video_reset()`, `video_decode()`, plus accessor functions
for the rendering pipeline.

Implementation follows Spec A Phase 4 closely: register decode via switch on
offset 0-7, mode write auto-sets dimensions, VID_OPER triggers scroll on
`frame_buffer[]`, VID_OPER reads return 0.

**Scroll fill character:** `0x00` (null/blank glyph). See Reconciliation
Note 9.

**Scroll safety:** Clamp `stride * height` to `N8_FB_SIZE` before any
memmove/memset to prevent buffer overrun when width/height/stride are set
to arbitrary values by firmware.

**Device router integration:**

```cpp
case 2:  // $D840: Video Control
    video_decode(pins, reg);
    break;
```

**Accessor functions** (for Phase 7 rendering):

```cpp
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
```

**Build changes:** Add `emu_video.cpp` to `SOURCES` and `TEST_SRC_OBJS` in
Makefile.

**Tests (T130-T145):**

| ID   | Description |
|------|-------------|
| T130 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | Read VID_MODE returns `$00` after reset |
| T132 | Write VID_MODE=`$00` auto-sets 80/25/80 |
| T133 | Write VID_MODE=`$01` retains current dimensions |
| T134 | Write/read VID_WIDTH |
| T135 | Write/read VID_HEIGHT |
| T136 | Write/read VID_STRIDE |
| T137 | VID_OPER=`$00` scroll up: row 0 gets row 1, last row zeroed |
| T138 | VID_OPER=`$01` scroll down: row 1 gets row 0, first row zeroed |
| T139 | Read VID_OPER returns 0 (write-only) |
| T140 | Write/read VID_CURSOR |
| T141 | Write/read VID_CURCOL |
| T142 | Write/read VID_CURROW |
| T143 | Phantom registers (`$D848-$D85F`) read 0, write no-op |
| T144 | VID_OPER=`$02` scroll left |
| T145 | VID_OPER=`$03` scroll right |

---

### Phase 5: Keyboard Device (`$D860-$D862`)

**Goal:** Implement keyboard input: SDL2 key events to ASCII/extended code
translation to device registers at `$D860` with optional IRQ.

**Entry criteria:** Phase 4 complete, all tests pass.
**Exit criteria:** Keyboard input works via `$D860-$D862` registers. IRQ
bit 2 fires on keypress when enabled. All tests passing.

**Constants update:**

```cpp
// --- Keyboard ---
#define N8_KBD_BASE        0xD860
#define N8_KBD_SLOT        3
#define N8_KBD_DATA        0x00     // Register offsets
#define N8_KBD_STATUS      0x01
#define N8_KBD_ACK         0x01     // Same offset, write = ACK
#define N8_KBD_CTRL        0x02

// KBD_STATUS bits
#define N8_KBD_STAT_AVAIL    0x01
#define N8_KBD_STAT_OVERFLOW 0x02
#define N8_KBD_STAT_SHIFT    0x04
#define N8_KBD_STAT_CTRL     0x08
#define N8_KBD_STAT_ALT      0x10
#define N8_KBD_STAT_CAPS     0x20

// KBD_CTRL bits
#define N8_KBD_CTRL_IRQ_EN   0x01
```

**New files: `src/emu_keyboard.h` and `src/emu_keyboard.cpp`**

Internal state: `kbd_data`, `kbd_status`, `kbd_ctrl`, `kbd_modifier`.

API:
- `keyboard_init()`, `keyboard_reset()`
- `keyboard_decode(pins, reg)` -- bus decode handler
- `keyboard_key_down(keycode, modifiers)` -- called from SDL event loop
- `keyboard_update_modifiers(modifiers)` -- updates live modifier bits
- `keyboard_inject_key(keycode, modifiers)` -- test helper (alias for
  `keyboard_key_down`)

Behavior per `hardware.md`:
- `kbd_key_down`: if DATA_AVAIL already set, set OVERFLOW. Store keycode,
  update modifiers, set DATA_AVAIL. If IRQ enabled, assert IRQ bit 2.
- Read KBD_DATA: return `kbd_data`.
- Read KBD_STATUS: return `(kbd_status & 0x03) | (kbd_modifier & 0x3C)`.
  Modifier bits (2-5) reflect live state independent of DATA_AVAIL.
- Write KBD_ACK (offset 1): clear DATA_AVAIL and OVERFLOW, deassert IRQ.
- Write KBD_CTRL: set/clear IRQ enable (bit 0 only).
- Write KBD_DATA: ignored (read-only register).
- Registers 3-31: reserved, read 0, write no-op.

**Device router integration:**

```cpp
case 3:  // $D860: Keyboard
    keyboard_decode(pins, reg);
    break;
```

**SDL2 key translation (`main.cpp`):**

```cpp
if (event.type == SDL_KEYDOWN && !event.key.repeat
    && !io.WantCaptureKeyboard) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods = sdl_to_n8_modifiers(SDL_GetModState());
    if (n8_key != 0) {
        keyboard_key_down(n8_key, mods);
    }
}
```

Key points:
- `!event.key.repeat` filters out auto-repeat events (the N8 keyboard is
  single-shot; no hardware auto-repeat).
- `!io.WantCaptureKeyboard` prevents keystrokes meant for ImGui text fields
  from reaching the emulated keyboard.

**`sdl_to_n8_keycode()` function:**

Handles three categories:
1. **Ctrl+letter** -> `$01-$1A` (control characters)
2. **Printable ASCII** (`SDLK_SPACE` through `SDLK_z`) with shift mapping
   for letters (a->A) and symbols (1->!, etc.)
3. **Special keys** (Return, Backspace, Tab, Escape) and **extended keys**
   (`$80+`: arrow keys, Home/End/PgUp/PgDn, Insert/Delete, F1-F12)

The shift-symbol table maps `SDLK_1`->`!`, `SDLK_2`->`@`, etc. for the
standard US keyboard layout.

**`sdl_to_n8_modifiers()` function:**

Maps SDL modifier flags to N8 KBD_STATUS bits:
- `KMOD_SHIFT` -> bit 2
- `KMOD_CTRL` -> bit 3
- `KMOD_ALT` -> bit 4
- `KMOD_CAPS` -> bit 5

**Build changes:** Add `emu_keyboard.cpp` to `SOURCES` and `TEST_SRC_OBJS`.

**Tests (T150-T164):**

| ID   | Description |
|------|-------------|
| T150 | `keyboard_reset()` clears all state |
| T151 | `keyboard_inject_key` sets DATA_AVAIL |
| T152 | Read KBD_DATA returns injected key code |
| T153 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | Write KBD_ACK clears DATA_AVAIL and OVERFLOW |
| T155 | Second inject before ACK sets OVERFLOW, data = second key |
| T156 | IRQ enable + inject sets IRQ bit 2 |
| T157 | IRQ disable + inject does NOT set IRQ |
| T158 | ACK deasserts IRQ bit 2 |
| T159 | Modifier bits reflect in KBD_STATUS |
| T160 | Modifiers update even when DATA_AVAIL=0 |
| T161 | Reserved registers (`$D863-$D867`) read 0, write no-op |
| T162 | Extended key code (`$80` = Up Arrow) stored correctly |
| T163 | Function key (`$90` = F1) stored correctly |
| T164 | Bus decode: 6502 program writes KBD_ACK, clears status |

**Risks:**
- **ImGui keyboard conflict:** The `io.WantCaptureKeyboard` guard is
  essential. A dedicated "keyboard capture" toggle for the display window
  may be needed for full-screen workflows. Deferred to future work.
- **Shift/symbol mapping:** US layout only. International layouts deferred.

---

### Phase 6: ROM Layout Migration

**Goal:** Update ROM from 12KB at `$D000` to 8KB at `$E000-$FFFF`. Establish
Dev Bank at `$D000-$D7FF`. Add optional ROM write protection.

**Entry criteria:** Phase 5 complete, all tests pass.
**Exit criteria:** ROM loads at `$E000`. Code executes from all ROM segments.
Dev Bank at `$D000-$D7FF` is writable RAM. `$E000-$FFFF` is optionally
write-protected.

**Constants update:**

```cpp
// --- ROM Regions ---
#define N8_ROM_BASE        0xE000   // Start of ROM (was 0xD000)
#define N8_ROM_END         0xFFFF
#define N8_ROM_SIZE        0x2000   // 8KB total

#define N8_ROM_MONITOR     0xE000   // Monitor / stdlib
#define N8_ROM_KERNEL_IMPL 0xF000   // Kernel Implementation
#define N8_ROM_KERNEL_ENTRY 0xFE00  // Kernel Entry

// --- Dev Bank ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_SIZE    0x0800   // 2KB
#define N8_DEVBANK_END     0xD7FF
```

**ROM loader (`emulator_loadrom()`):**

Dual-path for transitional compatibility (removed in Phase 9):

```cpp
void emulator_loadrom() {
    FILE *fp = fopen(rom_file, "r");
    if (!fp) { printf("ROM not found\n"); return; }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint16_t load_addr;
    if (size <= N8_ROM_SIZE) {
        load_addr = N8_ROM_BASE;   // $E000 (new 8KB layout)
    } else {
        load_addr = 0xD000;        // Legacy 12KB layout
    }

    printf("Loading ROM at $%04X (%ld bytes)\n", load_addr, size);
    fread(&mem[load_addr], 1, size, fp);
    fclose(fp);
}
```

**ROM write protection (optional):**

```cpp
// In bus decode, after device router, before generic mem[] handler:
if (addr >= N8_ROM_BASE && !(pins & M6502_RW)) {
    // Silently ignore writes to ROM
    handled = true;
}
```

**Dev Bank:** `$D000-$D7FF` is plain RAM. No special bus decode needed --
`mem[$D000-$D7FF]` is readable/writable via the generic handler. The old
ROM data from the firmware binary that lands here is just padding, harmless
since it is writable.

**Tests (T170-T174):**

| ID   | Description |
|------|-------------|
| T170 | `emulator_loadrom()` loads 8KB binary at `$E000` |
| T171 | Code at `$E000` is executable (NOP sled, PC advances) |
| T172 | Reset vector at `$FFFC-$FFFD` works (within ROM region) |
| T173 | Dev Bank `$D000-$D7FF` is writable RAM |
| T174 | Legacy loadrom: >8KB binary loads at `$D000` (transition) |

**Risks:**
- **Firmware breakage:** Old firmware will not work with the new emulator
  until Phase 8. During Phases 6-7, tests use inline programs.
- **Backward compatibility:** The dual-path loader handles both old and new
  firmware binaries.

---

### Phase 7: Video Rendering Pipeline

**Goal:** Render the frame buffer to an OpenGL texture using the N8 font,
displayed in an ImGui window. Includes cursor rendering.

**Entry criteria:** Phase 6 complete, all tests pass.
**Exit criteria:** An ImGui window ("N8 Display") shows the 80x25 text frame
buffer using the N8 font. Cursor rendering works (steady, flash, underline,
block). No test regressions.

**Architecture: CPU-side Rasterization**

```
frame_buffer[4096]  -->  screen_pixels[640x400]  -->  GL texture  -->  ImGui::Image()
     ^                         ^                         ^
   CPU writes            n8_font[256][16]          glTexSubImage2D
                         video_regs                (each frame, if dirty)
                         cursor state
```

CPU-side rasterization is chosen over GPU glyph rendering for simplicity:

1. A tight nested loop fills 640x400 RGBA pixels from the font data.
2. Dirty tracking skips rasterization when `frame_buffer[]` is unchanged.
3. Cursor overlay is composited in the same pass.
4. No shader complexity, no vertex buffers, no draw call overhead.

At 640x400 x 4 bytes = 1MB per frame, the texture upload via
`glTexSubImage2D` is trivial for modern GPUs.

**New files: `src/emu_display.h` and `src/emu_display.cpp`**

```cpp
// emu_display.h
#pragma once
#include <stdint.h>

void display_init();           // Create GL texture (call after GL context)
void display_render();         // Rasterize + upload (call each frame)
uint32_t display_get_texture();// GLuint for ImGui::Image()
int display_get_pixel_width(); // 640
int display_get_pixel_height();// 400
void display_shutdown();       // Cleanup
```

**Rasterization (`emu_display.cpp`):**

```cpp
#include "emu_display.h"
#include "emu_video.h"
#include "n8_memory_map.h"
#include "n8_font.h"
#include <SDL_opengl.h>
#include <cstring>

extern uint8_t frame_buffer[];

#define DISPLAY_W (N8_VID_DEFAULT_WIDTH * N8_FONT_WIDTH)    // 640
#define DISPLAY_H (N8_VID_DEFAULT_HEIGHT * N8_FONT_HEIGHT)  // 400

static GLuint   gl_texture = 0;
static uint32_t screen_pixels[DISPLAY_W * DISPLAY_H];

// Phosphor colors (ARGB / BGRA depending on platform)
static const uint32_t COLOR_FG = 0xFF33FF33;  // Green phosphor
static const uint32_t COLOR_BG = 0xFF000000;  // Black

static uint32_t cursor_frame_counter = 0;

void display_init() {
    glGenTextures(1, &gl_texture);
    glBindTexture(GL_TEXTURE_2D, gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DISPLAY_W, DISPLAY_H,
                 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    memset(screen_pixels, 0, sizeof(screen_pixels));
}

void display_render() {
    uint8_t width  = video_get_width();
    uint8_t height = video_get_height();
    uint8_t stride = video_get_stride();

    // Rasterize character cells
    for (int row = 0; row < height && row < N8_VID_DEFAULT_HEIGHT; row++) {
        for (int col = 0; col < width && col < N8_VID_DEFAULT_WIDTH; col++) {
            uint8_t ch = frame_buffer[row * stride + col];
            const uint8_t* glyph = n8_font[ch];

            int px_x = col * N8_FONT_WIDTH;
            int px_y = row * N8_FONT_HEIGHT;

            for (int gy = 0; gy < N8_FONT_HEIGHT; gy++) {
                uint8_t glyph_row = glyph[gy];
                uint32_t* dest = &screen_pixels[(px_y + gy) * DISPLAY_W + px_x];
                for (int gx = 0; gx < N8_FONT_WIDTH; gx++) {
                    bool pixel_on = (glyph_row >> (7 - gx)) & 1;
                    dest[gx] = pixel_on ? COLOR_FG : COLOR_BG;
                }
            }
        }
    }

    // Cursor compositing
    rasterize_cursor(width, height);
    cursor_frame_counter++;

    // Upload to GPU
    glBindTexture(GL_TEXTURE_2D, gl_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_W, DISPLAY_H,
                    GL_BGRA, GL_UNSIGNED_BYTE, screen_pixels);
}
```

**Cursor rendering (within `emu_display.cpp`):**

```cpp
static void rasterize_cursor(uint8_t width, uint8_t height) {
    uint8_t cursor_style = video_get_cursor_style();
    uint8_t mode  = cursor_style & 0x03;
    bool    block = (cursor_style >> 2) & 1;
    uint8_t rate  = (cursor_style >> 3) & 0x03;
    uint8_t col   = video_get_cursor_col();
    uint8_t row   = video_get_cursor_row();

    if (mode == 0) return;  // Cursor off

    // Flash logic (frame-counter based, deterministic)
    if (mode == 2) {
        uint32_t period;
        switch (rate) {
            case 0:  return;           // Rate off
            case 1:  period = 60; break; // ~1 Hz at 60 FPS
            case 2:  period = 30; break; // ~2 Hz
            case 3:  period = 15; break; // ~4 Hz
            default: period = 60;
        }
        if ((cursor_frame_counter % period) >= (period / 2)) {
            return;  // Off half of flash cycle
        }
    }

    // Bounds check
    if (col >= width || row >= height) return;

    int px_x = col * N8_FONT_WIDTH;
    int px_y = row * N8_FONT_HEIGHT;

    if (block) {
        // Block cursor: invert entire cell
        for (int gy = 0; gy < N8_FONT_HEIGHT; gy++) {
            uint32_t* dest = &screen_pixels[(px_y + gy) * DISPLAY_W + px_x];
            for (int gx = 0; gx < N8_FONT_WIDTH; gx++) {
                dest[gx] = (dest[gx] == COLOR_FG) ? COLOR_BG : COLOR_FG;
            }
        }
    } else {
        // Underline cursor: fill bottom 2 rows with fg color
        for (int gy = N8_FONT_HEIGHT - 2; gy < N8_FONT_HEIGHT; gy++) {
            uint32_t* dest = &screen_pixels[(px_y + gy) * DISPLAY_W + px_x];
            for (int gx = 0; gx < N8_FONT_WIDTH; gx++) {
                dest[gx] = COLOR_FG;
            }
        }
    }
}
```

**Dirty flag integration (`main.cpp`):**

```cpp
// In render loop, after emulator_step() time slice:
bool need_render = emulator_fb_dirty();
uint8_t cursor_mode = video_get_cursor_style() & 0x03;
if (cursor_mode >= 1) need_render = true;  // Cursor visible or flashing

if (need_render) {
    display_render();
    emulator_fb_clear_dirty();
}

// ImGui display window
static bool show_display = true;
if (show_display) {
    ImGui::Begin("N8 Display", &show_display);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale_x = avail.x / (float)DISPLAY_W;
    float scale_y = avail.y / (float)DISPLAY_H;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1.0f) scale = 1.0f;
    ImVec2 size((float)DISPLAY_W * scale, (float)DISPLAY_H * scale);

    ImGui::Image((ImTextureID)(intptr_t)display_get_texture(), size);
    ImGui::End();
}
```

**Font file:** Copy `docs/charset/n8_font.h` to `src/n8_font.h`. The font is
`static const` so it compiles into the display module only. The `src/` copy
ensures the emulator build does not depend on `docs/`.

**Build changes:** Add `emu_display.cpp` to emulator `SOURCES` only (not
`TEST_SRC_OBJS`). The display module depends on OpenGL, which is not available
in the test build. Display functionality is pure output with no bus decode
logic -- tests interact with `frame_buffer[]` and `video_decode()` directly.

**Tests:**

No new automated tests for the rendering pipeline (it is visual output).

Verification:
1. Manual: Write a test firmware that fills `$C000-$C7CF` with sequential
   character codes. Verify all 256 glyphs render correctly.
2. Verify cursor modes: steady underline, steady block, flash at each rate.
3. Verify the display window scales correctly with `ImGui::GetContentRegionAvail()`.

**Risks:**
- **OpenGL context state:** Ensure texture upload does not interfere with
  ImGui's OpenGL state. Call `glBindTexture` before ImGui render pass, or
  save/restore state.
- **Performance:** 1MB texture upload at 60fps = 60MB/s. Negligible on
  modern hardware.

---

### Phase 8: Firmware Migration

**Goal:** Update cc65 linker configs and firmware source files to use new
device register addresses and ROM layout.

**Entry criteria:** Phase 7 complete, emulator with display and keyboard
working. All tests pass.
**Exit criteria:** `make firmware` succeeds with new addresses and ROM layout.
Firmware runs correctly on updated emulator.

**Linker config (`firmware/n8.cfg`):**

```
MEMORY {
    ZP:   start =    $0, size =  $100, type = rw, define = yes;
    RAM:  start =  $400, size = $BC00, define = yes;
    ROM:  start = $E000, size = $2000, file = %O;
}

SEGMENTS {
    ZEROPAGE: load = ZP,  type = zp,  define   = yes;
    DATA:     load = ROM, type = rw,  define   = yes, run = RAM;
    BSS:      load = RAM, type = bss, define   = yes;
    HEAP:     load = RAM, type = bss, optional = yes;
    STARTUP:  load = ROM, type = ro;
    ONCE:     load = ROM, type = ro,  optional = yes;
    CODE:     load = ROM, type = ro;
    RODATA:   load = ROM, type = ro;
    VECTORS:  load = ROM, type = ro,  start    = $FFFA;
}

FEATURES {
    CONDES:    segment = STARTUP,
               type    = constructor,
               label   = __CONSTRUCTOR_TABLE__,
               count   = __CONSTRUCTOR_COUNT__;
    CONDES:    segment = STARTUP,
               type    = destructor,
               label   = __DESTRUCTOR_TABLE__,
               count   = __DESTRUCTOR_COUNT__;
}

SYMBOLS {
    __STACKSIZE__:  type = weak, value = $0200;
}
```

Key changes: ROM at `$E000` (8KB), RAM starts at `$0400`.

**Playground linker configs** (`firmware/playground/playground.cfg`,
`firmware/gdb_playground/playground.cfg`): Same ROM change.

**Firmware source changes:**

`firmware/devices.inc`:
```asm
.define IRQ_FLAGS     $D800
.define TTY_OUT_CTRL  $D820
.define TTY_OUT_DATA  $D821
.define TTY_IN_CTRL   $D822
.define TTY_IN_DATA   $D823
.define VID_MODE      $D840
.define VID_WIDTH     $D841
.define VID_HEIGHT    $D842
.define VID_STRIDE    $D843
.define VID_OPER      $D844
.define VID_CURSOR    $D845
.define VID_CURCOL    $D846
.define VID_CURROW    $D847
.define KBD_DATA      $D860
.define KBD_STATUS    $D861
.define KBD_ACK       $D861
.define KBD_CTRL      $D862
.define FB_BASE       $C000
```

`firmware/devices.h`: C-preprocessor mirror of the above.

`firmware/zp.inc`: Remove `ZP_IRQ $FF`. Add comment noting IRQ flags moved
to `$D800`.

`firmware/interrupt.s`, `firmware/tty.s`, `firmware/main.s`: These use
symbolic names from `devices.inc`. Only the definitions change, not the
source code (unless `TXT_BUFF` references need updating to `FB_BASE`).

**Playground programs:** Update local TTY equates in `mon1.s`, `mon2.s`,
`mon3.s`, `test_tty.s` from `$C100-$C103` to `$D820-$D823`.

**Tests:** No new automated tests. The firmware build (`make firmware`)
is the gate. Manual smoke test: boot emulator, verify banner, echo loop.

---

### Phase 9: GDB Stub Update and Cleanup

**Goal:** Update GDB memory map XML. Remove legacy compatibility code.
Final validation.

**Entry criteria:** Phase 8 complete, firmware builds and runs.
**Exit criteria:** GDB memory map matches new layout. No legacy addresses in
codebase. All tests pass. End-to-end validation complete.

**GDB stub changes (`gdb_stub.cpp`):**

```cpp
static const char memory_map_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE memory-map SYSTEM \"gdb-memory-map.dtd\">\n"
    "<memory-map>\n"
    "  <memory type=\"ram\"  start=\"0x0000\" length=\"0xC000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC000\" length=\"0x1000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xD000\" length=\"0x0800\"/>\n"
    "  <memory type=\"ram\"  start=\"0xD800\" length=\"0x0800\"/>\n"
    "  <memory type=\"rom\"  start=\"0xE000\" length=\"0x2000\"/>\n"
    "</memory-map>\n";
```

Device register reads via GDB return `mem[addr]` (not device decode), avoiding
side effects.

**Cleanup:**

1. Remove dual-path ROM loader from `emulator_loadrom()` -- always load at
   `N8_ROM_BASE`.
2. Remove any legacy address constants from `n8_memory_map.h`.
3. Update `CLAUDE.md` memory map documentation.
4. Verify `grep -rn '0x00FF\|0xC100\|0xD000' src/` returns no stale
   references (except comments).

**GDB tests (T180-T184):**

| ID   | Description |
|------|-------------|
| T180 | Memory map XML contains `$C000` RAM region (4KB FB) |
| T181 | Memory map XML contains `$D800` RAM region (devices) |
| T182 | Memory map ROM at `$E000` length `$2000` |
| T183 | GDB read at `$D840` returns value (no side effects) |
| T184 | GDB write at `$C000` modifies `frame_buffer[0]` |

**End-to-end validation scenarios:**

| ID     | Scenario | Pass Criteria |
|--------|----------|---------------|
| E2E-01 | Boot firmware, TTY echo loop | Characters echoed |
| E2E-02 | GDB connect, `regs`, `step` | Registers correct |
| E2E-03 | GDB `load` playground at `$E000` | Program executes |
| E2E-04 | GDB breakpoint at label | Halt at correct PC |
| E2E-05 | GDB `read $D800 20` | Device registers readable |
| E2E-06 | GDB `read $C000 100` | Frame buffer readable |
| E2E-07 | Video scroll via program | Frame buffer shifted |
| E2E-08 | Keyboard inject + IRQ | CPU vectors to handler |
| E2E-09 | Reset via GDB | CPU restarts at `$FFFC` vector |
| E2E-10 | Display window shows characters | Visual verification |

---

## Data Flow Diagrams

### Video Rendering Pipeline

```
                    +--------------------------------------+
                    |           CPU (m6502_tick)            |
                    |                                      |
                    |  STA $C000  --> bus decode            |
                    |  STA $D844  --> video_decode          |
                    +------+------------------+------------+
                           |                  |
                           v                  v
                  +------------+      +---------------+
                  | frame_     |      | video regs    |
                  | buffer[]   |      | (mode, cursor,|
                  | (4096 B)   |      |  width, etc.) |
                  +-----+------+      +-------+-------+
                        |                     |
                        v                     v
              +-----------------------------------------+
              |       display_render()                  |
              |                                         |
              |  for each cell:                         |
              |    ch = frame_buffer[row * stride + col] |
              |    glyph = n8_font[ch]                  |
              |    blit 8x16 pixels -> screen_pixels    |
              |                                         |
              |  composite cursor overlay               |
              +-------------------+---------------------+
                                  |
                                  v
                       +------------------+
                       | screen_pixels[]  |
                       | (640x400 RGBA)   |
                       +--------+---------+
                                |
                                v  glTexSubImage2D()
                       +------------------+
                       | gl_texture       |
                       | (GL_TEXTURE_2D)  |
                       +--------+---------+
                                |
                                v  ImGui::Image()
                       +------------------+
                       |  ImGui Window    |
                       | "N8 Display"     |
                       +------------------+
```

### Keyboard Input Pipeline

```
  +----------------------------------+
  |  Physical Keyboard (USB HID)     |
  +----------------+-----------------+
                   |
                   v  OS -> SDL2
  +----------------------------------+
  |  SDL_KEYDOWN event               |
  |  keysym.sym + keysym.mod         |
  |  (filtered: !repeat, !ImGui)     |
  +----------------+-----------------+
                   |
                   v  sdl_to_n8_keycode()
  +----------------------------------+
  |  N8 Key Code ($00-$FF)           |
  |  + modifier byte (bits 2-5)      |
  +----------------+-----------------+
                   |
                   v  keyboard_key_down()
  +----------------------------------+
  |  Keyboard Registers              |
  |  KBD_DATA   = keycode            |
  |  KBD_STATUS |= DATA_AVAIL       |
  |  (if CTRL.0) IRQ bit 2 asserted  |
  +----------------+-----------------+
                   |
                   v  CPU reads $D860
  +----------------------------------+
  |  6502 Firmware                   |
  |  LDA $D860  -> key code          |
  |  STA $D861  -> acknowledge       |
  +----------------------------------+
```

---

## File Inventory

### New Files

| File | Phase | Purpose |
|------|-------|---------|
| `src/n8_memory_map.h` | 0 | All memory map constants (`#define N8_*`) |
| `src/emu_video.h` | 4 | Video control register API |
| `src/emu_video.cpp` | 4 | Video registers, scroll operations |
| `src/emu_keyboard.h` | 5 | Keyboard device API |
| `src/emu_keyboard.cpp` | 5 | Keyboard registers, IRQ |
| `src/emu_display.h` | 7 | Display rendering API |
| `src/emu_display.cpp` | 7 | CPU rasterizer, GL texture, cursor |
| `src/n8_font.h` | 7 | Font data (copy from `docs/charset/n8_font.h`) |
| `test/test_video.cpp` | 4 | Video register tests |
| `test/test_keyboard.cpp` | 5 | Keyboard device tests |

### Modified Files

| File | Phases | Changes |
|------|--------|---------|
| `src/emulator.cpp` | 0-6 | Constants, IRQ macros, bus decode, frame buffer, device router, ROM loader |
| `src/emulator.h` | 3 | `extern frame_buffer[]`, dirty flag API |
| `src/emu_tty.cpp` | 0 | Use constants for IRQ bit |
| `src/main.cpp` | 3,5,7 | GDB callbacks, keyboard SDL events, display window |
| `src/gdb_stub.cpp` | 9 | Memory map XML |
| `Makefile` | 4,5,7 | New source files |
| `firmware/n8.cfg` | 8 | ROM at `$E000`, RAM at `$0400` |
| `firmware/devices.inc` | 8 | New device addresses |
| `firmware/devices.h` | 8 | New device addresses |
| `firmware/zp.inc` | 8 | Remove `ZP_IRQ` |
| `firmware/playground/playground.cfg` | 8 | ROM at `$E000` |
| `firmware/gdb_playground/playground.cfg` | 8 | ROM at `$E000` |
| `firmware/playground/mon1.s` | 8 | TTY equates |
| `firmware/playground/mon2.s` | 8 | TTY equates |
| `firmware/playground/mon3.s` | 8 | TTY equates |
| `firmware/gdb_playground/test_tty.s` | 8 | TTY equates |
| `test/test_helpers.h` | 0 | Include `n8_memory_map.h` |
| `test/test_utils.cpp` | 1 | IRQ address |
| `test/test_tty.cpp` | 1,2 | IRQ + TTY addresses |
| `test/test_bus.cpp` | 1,2,3 | New bus decode tests |
| `test/test_gdb_callbacks.cpp` | 1,2 | Address updates |
| `test/test_gdb_protocol.cpp` | 9 | Memory map XML test |
| `test/test_integration.cpp` | 3,6 | Frame buffer, ROM tests |

---

## Performance Budget

| Operation | Per Frame | Target |
|-----------|-----------|--------|
| CPU ticks (~13ms slice @ 1 MHz) | ~13,000 ticks | Existing, unchanged |
| Frame buffer rasterize (when dirty) | 640x400 px = 256K writes | < 0.5 ms |
| Cursor composite | 8x16 px = 128 writes | < 0.01 ms |
| Texture upload (`glTexSubImage2D`) | 1 MB | < 0.3 ms |
| `ImGui::Image` draw | 1 quad | < 0.01 ms |
| SDL2 key event processing | ~0 per frame (event-driven) | < 0.01 ms |
| **Total rendering overhead** | | **< 1 ms** |

The rendering overhead is negligible compared to the 13 ms frame budget.

---

## Test Count Progression

| Phase | New Tests | Running Total |
|------:|----------:|--------------:|
| Baseline | -- | 221 |
| 0 (Constants) | 0 | 221 |
| 1 (IRQ move) | 4 | 225 |
| 2 (TTY move) | 3 | 228 |
| 3 (FB expand) | 4 | 232 |
| 4 (Video regs) | 16 | 248 |
| 5 (Keyboard) | 15 | 263 |
| 6 (ROM split) | 5 | 268 |
| 7 (Display) | 0 (visual) | 268 |
| 8 (Firmware) | 0 (build gate) | 268 |
| 9 (GDB + cleanup) | 5 | 273 |
| **Total** | **52 new** | **273 + 10 E2E manual** |

---

## Dependency Graph

```
Phase 0 --- Machine Constants Header (n8_memory_map.h)
   |
   v
Phase 1 --- IRQ Migration ($00FF -> $D800)
   |         + Device register router skeleton
   v
Phase 2 --- TTY Migration ($C100 -> $D820)
   |         + Plug into device router
   v
Phase 3 --- Frame Buffer Expansion (256B -> 4KB)
   |         + Separate backing store (frame_buffer[])
   |         + GDB callback update
   v
Phase 4 --- Video Control Registers ($D840)
   |         + Scroll operations on frame_buffer[]
   v
Phase 5 --- Keyboard Device ($D860)
   |         + SDL2 key translation
   |         + IRQ bit 2
   v
Phase 6 --- ROM Layout Migration ($D000 -> $E000)
   |         + Dev Bank recognition
   |         + Dual-path ROM loader (transitional)
   v
Phase 7 --- Video Rendering Pipeline
   |         + Font integration (n8_font.h)
   |         + CPU rasterization + GL texture
   |         + Cursor rendering
   |         + ImGui display window
   v
Phase 8 --- Firmware Migration
   |         + cc65 linker configs
   |         + Assembly source address updates
   |         + Playground program updates
   v
Phase 9 --- GDB Stub Update + Cleanup
             + Memory map XML
             + Remove legacy ROM loader
             + End-to-end validation
```

Phases 4 and 5 depend on Phase 3 (frame buffer) but are independent of each
other. They could be parallelized if needed, though sequential is recommended
for clean testing.

Phase 7 depends on Phases 4-5 (video registers, keyboard) but could start
as soon as Phase 4 is complete (keyboard is not strictly required for
rendering, only for input).

Phase 8 depends on ALL prior phases -- firmware must use all new addresses
simultaneously.

---

## Risk Assessment

| Phase | Risk | Impact | Mitigation |
|-------|------|--------|------------|
| 0 | Very Low | None (pure refactor) | `make test` before/after |
| 1 | Low | IRQ mechanism | Tests cover IRQ path; firmware not yet updated |
| 2 | Low | TTY addressing | Self-contained; tests comprehensive |
| 3 | **Medium** | Structural change to `emulator_step()` | Thorough bus region tests; GDB callbacks fixed |
| 4 | Low | New module, isolated | Unit tests for all register behaviors |
| 5 | Low | New device, known pattern | Unit tests; SDL translation is isolatable |
| 6 | Low | ROM layout | Dual-path loader; backward compatible |
| 7 | **Medium** | OpenGL integration | Manual visual verification; no logic tests |
| 8 | **Medium** | Firmware source changes | `make firmware` as gate; playground rebuild |
| 9 | Very Low | Static XML + cleanup | GDB test suite; E2E validation |

**Rollback:** Each phase is a separate commit. Rollback = `git revert`. At
the end of every phase, `make test` must pass. Tag the repo before Phase 1:
`git tag pre-migration-v1`.

---

## Open Questions and Deferred Items

1. **Text Custom mode (`$01`):** Width/height constraints, maximum frame
   buffer usage, and stride behavior are TBD in the hardware spec. The
   register infrastructure is in place; behavior is deferred.

2. **Phosphor color selection:** Amber, green, and white phosphor are UI
   options (dropdown in Emulator Control window). Not a hardware register.
   The `COLOR_FG` constant in `emu_display.cpp` is easily swappable at
   runtime.

3. **Font ROM bank switching:** Hardware spec says "deferred to future work
   (likely via bank switching into a Dev Bank)." The `$D000-$D7FF` dev bank
   is reserved for this. Current implementation bakes the font into the
   emulator binary.

4. **Frame buffer bank switching:** The separate `frame_buffer[]` backing
   store already supports this architecture.

5. **TTY vs Keyboard coexistence:** TTY reads host stdin (debug channel).
   Keyboard reads SDL key events (emulated hardware). Both remain active.
   Firmware chooses which input path to use.

6. **`SDL_TEXTINPUT` for printable characters:** The current `SDL_KEYDOWN`
   approach with manual shift mapping matches the hardware model (Teensy HID
   converter). `SDL_TEXTINPUT` could be a future refinement for better IME
   support, but introduces complexity that does not match the hardware.

7. **International keyboard layouts:** Only US layout is supported initially.
   The shift-symbol table can be extended or made configurable.

8. **ImGui keyboard focus / capture mode:** The `!io.WantCaptureKeyboard`
   guard works for the debugger UI. A "keyboard capture" toggle for
   full-screen emulated display may be needed later.

9. **ROM write protection:** Implemented as optional in Phase 6. If it
   causes test friction (tests that load code into ROM region), it can be
   made configurable or deferred.
