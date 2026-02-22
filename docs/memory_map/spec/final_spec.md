# N8 Machine — Memory Map Migration & Device Implementation Spec

**Version:** 1.0 (Final Synthesis)
**Date:** 2026-02-22
**Status:** Ready for implementation

This spec synthesizes the best of three specialist perspectives (bus architecture, graphics pipeline, integration/testing) and incorporates findings from two independent reviews (correctness, implementability). See `report.md` for the full analysis.

---

## Table of Contents

1. [Scope](#1-scope)
2. [Migration Overview](#2-migration-overview)
3. [Phase 0 — Constants Header](#3-phase-0--constants-header)
4. [Phase 1 — IRQ Migration + Device Router](#4-phase-1--irq-migration--device-router)
5. [Phase 2 — TTY Device Migration](#5-phase-2--tty-device-migration)
6. [Phase 3 — Frame Buffer Expansion](#6-phase-3--frame-buffer-expansion)
7. [Phase 4 — ROM Layout Migration](#7-phase-4--rom-layout-migration)
8. [Phase 5 — Video Control Registers](#8-phase-5--video-control-registers)
9. [Phase 6 — Keyboard Registers](#9-phase-6--keyboard-registers)
10. [Phase 7 — GDB Stub Update](#10-phase-7--gdb-stub-update)
11. [Phase 8 — Firmware Source Migration](#11-phase-8--firmware-source-migration)
12. [Phase 9 — Playground Migration](#12-phase-9--playground-migration)
13. [Phase 10 — End-to-End Validation & Cleanup](#13-phase-10--end-to-end-validation--cleanup)
14. [Rendering Pipeline (Separate Effort)](#14-rendering-pipeline-separate-effort)
15. [Test Case Registry](#15-test-case-registry)
16. [Risk Matrix](#16-risk-matrix)
17. [File Inventory](#17-file-inventory)
18. [Rollback Procedures](#18-rollback-procedures)

---

## 1. Scope

This spec covers two tasks:

1. **Migrate the emulator to the proposed memory map** — IRQ, TTY, frame buffer, ROM layout, device register space, firmware, GDB stub.
2. **Implement video control registers and keyboard input** — register decode, scroll operations, SDL2 key translation, IRQ.

**Explicitly out of scope:** The video rendering pipeline (font loading, CPU rasterization, OpenGL texture, cursor compositing, ImGui display window). This is documented as a separate follow-on effort in [Section 14](#14-rendering-pipeline-separate-effort), using Spec B's architecture.

### Migration Principles

1. **One device at a time.** Each phase moves or adds exactly one hardware entity. All prior tests must still pass.
2. **Test-first.** New test cases are defined before the code that makes them pass.
3. **Firmware follows emulator.** Emulator bus decode moves first; firmware addresses update in Phase 8.
4. **Each phase is a commit.** Rollback is `git revert`. At the end of every phase, `make test` passes.

---

## 2. Migration Overview

### Current Memory Map

| Address | Region | Size |
|--------:|:-------|-----:|
| `$0000-$00FF` | Zero Page (IRQ flags at `$FF`) | 256 |
| `$0100-$01FF` | Hardware Stack | 256 |
| `$0200-$BFFF` | RAM | 48,640 |
| `$C000-$C0FF` | Frame Buffer (256B, raw `mem[]`) | 256 |
| `$C100-$C10F` | TTY Device | 16 |
| `$D000-$FFFF` | ROM (12KB) | 12,288 |

### Target Memory Map

| Address | Region | Size |
|--------:|:-------|-----:|
| `$0000-$00FF` | Zero Page | 256 |
| `$0100-$01FF` | Hardware Stack | 256 |
| `$0200-$03FF` | Reserved | 512 |
| `$0400-$BFFF` | Program RAM | 47 KB |
| `$C000-$CFFF` | Frame Buffer (4KB, separate backing store) | 4,096 |
| `$D000-$D7FF` | Dev Bank | 2 KB |
| `$D800-$D81F` | System/IRQ (slot 0) | 32 |
| `$D820-$D83F` | TTY (slot 1) | 32 |
| `$D840-$D85F` | Video Control (slot 2) | 32 |
| `$D860-$D87F` | Keyboard (slot 3) | 32 |
| `$D880-$DFFF` | Future Devices | varies |
| `$E000-$EFFF` | Monitor / stdlib | 4 KB |
| `$F000-$FDFF` | Kernel Implementation | 3.5 KB |
| `$FE00-$FFDF` | Kernel Entry | 480 |
| `$FFE0-$FFFF` | Vectors + Bank Switch | 32 |

### Device Register Slots (`$D800-$DFFF`)

| Base | Slot | Device | Registers |
|-----:|-----:|:-------|:----------|
| `$D800` | 0 | System/IRQ | 1 (IRQ_FLAGS) |
| `$D820` | 1 | TTY | 4 (OUT_STATUS, OUT_DATA, IN_STATUS, IN_DATA) |
| `$D840` | 2 | Video Control | 8 (MODE, WIDTH, HEIGHT, STRIDE, OPER, CURSOR, CURCOL, CURROW) |
| `$D860` | 3 | Keyboard | 3 (KBD_DATA, KBD_STATUS/ACK, KBD_CTRL) |

### Phase Summary

| Phase | Description | New Tests | Total |
|------:|:------------|----------:|------:|
| 0 | Constants header | 0 | 221 |
| 1 | IRQ migration + device router | 4 | 225 |
| 2 | TTY migration | 3 | 228 |
| 3 | Frame buffer expansion | 7 | 235 |
| 4 | ROM layout migration | 7 | 242 |
| 5 | Video control registers | 17 | 259 |
| 6 | Keyboard registers | 17 | 276 |
| 7 | GDB stub update | 5 | 281 |
| 8 | Firmware source migration | 0 (build gate) | 281 |
| 9 | Playground migration | 0 (build gate) | 281 |
| 10 | E2E validation & cleanup | 10 (manual) | 281 + 10 |

### Key Architectural Decisions

| Decision | Choice | Rationale |
|:---------|:-------|:----------|
| Constants header | `src/n8_memory_map.h` (`#define N8_*`) | Dedicated file, `#define` for C/C++ compat |
| Assembly constants | `firmware/n8_memory_map.inc` | Mirrors C++ header name |
| Device routing | Slot-based router: `slot = (addr - $D800) >> 5` | Clean, extensible, 1 case per device |
| Frame buffer backing | Separate `frame_buffer[]` in Phase 3 | Per `hardware.md`; avoids Phase 7 rewrite |
| `kbd_tick()` | Included | Required by IRQ clear-reassert architecture |
| ROM migration timing | Phase 4 (before video/kbd) | Clears device space before wiring devices |
| ROM write protection | CPU bus writes to `$E000+` silently ignored | 3 lines; prevents firmware bugs |
| Scroll fill character | `0x00` | Hardware-like (memory clear to zero) |
| `N8_RAM_START` | `0x0400` | Matches proposed memory map reserved region |
| Keyboard file naming | `emu_kbd.*` | Consistent with `emu_tty.*` |

---

## 3. Phase 0 — Constants Header

**Goal:** Create a single source of truth for all hardware addresses.

**Entry criteria:** `make test` passes (221 tests).
**Exit criteria:** New header exists. `make` and `make test` pass unchanged. No magic numbers replaced yet.

### New file: `src/n8_memory_map.h`

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
#define N8_RAM_START       0x0400
#define N8_RAM_END         0xBFFF

// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4 KB
#define N8_FB_END          0xCFFF

// --- Device Register Space ---
#define N8_DEV_BASE        0xD800
#define N8_DEV_SIZE        0x0800   // 2 KB total
#define N8_DEV_END         0xDFFF
#define N8_DEV_SLOT_SIZE   0x0020   // 32 bytes per device slot

// --- System / IRQ (slot 0) ---
#define N8_IRQ_BASE        0xD800
#define N8_IRQ_FLAGS       0xD800
#define N8_IRQ_SLOT        0

// --- IRQ Bits ---
#define N8_IRQ_BIT_TTY     1
#define N8_IRQ_BIT_KBD     2

// --- TTY Device (slot 1) ---
#define N8_TTY_BASE        0xD820
#define N8_TTY_OUT_STATUS  0xD820
#define N8_TTY_OUT_DATA    0xD821
#define N8_TTY_IN_STATUS   0xD822
#define N8_TTY_IN_DATA     0xD823
#define N8_TTY_SLOT        1

// --- Video Control (slot 2) ---
#define N8_VID_BASE        0xD840
#define N8_VID_MODE        0x00     // Register offsets within slot
#define N8_VID_WIDTH       0x01
#define N8_VID_HEIGHT      0x02
#define N8_VID_STRIDE      0x03
#define N8_VID_OPER        0x04
#define N8_VID_CURSOR      0x05
#define N8_VID_CURCOL      0x06
#define N8_VID_CURROW      0x07
#define N8_VID_SLOT        2

// VID_MODE values
#define N8_VIDMODE_TEXT_DEFAULT  0x00
#define N8_VIDMODE_TEXT_CUSTOM   0x01

// VID_OPER values
#define N8_VIDOP_SCROLL_UP      0x00
#define N8_VIDOP_SCROLL_DOWN    0x01
#define N8_VIDOP_SCROLL_LEFT    0x02
#define N8_VIDOP_SCROLL_RIGHT   0x03

// Default text mode dimensions
#define N8_VID_DEFAULT_WIDTH    80
#define N8_VID_DEFAULT_HEIGHT   25

// --- Keyboard (slot 3) ---
#define N8_KBD_BASE        0xD860
#define N8_KBD_DATA        0x00     // Register offsets within slot
#define N8_KBD_STATUS      0x01     // Read
#define N8_KBD_ACK         0x01     // Write (same offset)
#define N8_KBD_CTRL        0x02
#define N8_KBD_SLOT        3

// KBD_STATUS bits
#define N8_KBD_STAT_AVAIL    0x01
#define N8_KBD_STAT_OVERFLOW 0x02
#define N8_KBD_STAT_SHIFT    0x04
#define N8_KBD_STAT_CTRL     0x08
#define N8_KBD_STAT_ALT      0x10
#define N8_KBD_STAT_CAPS     0x20

// KBD_CTRL bits
#define N8_KBD_CTRL_IRQ_EN   0x01

// --- Dev Bank ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_SIZE    0x0800
#define N8_DEVBANK_END     0xD7FF

// --- ROM ---
#define N8_ROM_BASE        0xE000
#define N8_ROM_SIZE        0x2000   // 8 KB
#define N8_ROM_END         0xFFFF

// --- Vectors (within ROM) ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE

// --- Font ---
#define N8_FONT_CHARS      256
#define N8_FONT_WIDTH      8
#define N8_FONT_HEIGHT     16

// --- Legacy addresses (removed in Phase 10) ---
#define N8_LEGACY_IRQ_ADDR   0x00FF
#define N8_LEGACY_TTY_BASE   0xC100
#define N8_LEGACY_FB_SIZE    0x0100
#define N8_LEGACY_ROM_BASE   0xD000
#define N8_LEGACY_ROM_SIZE   0x3000
```

### New file: `firmware/n8_memory_map.inc`

```asm
; n8_memory_map.inc -- device register addresses for cc65 assembly
; Mirror of src/n8_memory_map.h

.define N8_IRQ_FLAGS      $D800

.define N8_TTY_OUT_STATUS $D820
.define N8_TTY_OUT_DATA   $D821
.define N8_TTY_IN_STATUS  $D822
.define N8_TTY_IN_DATA    $D823

.define N8_VID_MODE       $D840
.define N8_VID_WIDTH      $D841
.define N8_VID_HEIGHT     $D842
.define N8_VID_STRIDE     $D843
.define N8_VID_OPER       $D844
.define N8_VID_CURSOR     $D845
.define N8_VID_CURCOL     $D846
.define N8_VID_CURROW     $D847

.define N8_KBD_DATA       $D860
.define N8_KBD_STATUS     $D861
.define N8_KBD_ACK        $D861
.define N8_KBD_CTRL       $D862

.define N8_FB_BASE        $C000
```

### Code changes

- Add `#include "n8_memory_map.h"` to `test/test_helpers.h`.
- No behavior changes. No magic number replacements yet.

### Tests

None. This phase only adds files.

### Phase gate

- `make` compiles cleanly with the new header present but not yet included by production code.
- `make test`: **221 passed, 0 failed**.

---

## 4. Phase 1 — IRQ Migration + Device Router

**Goal:** Move IRQ flags from `$00FF` to `$D800`. Introduce the device register router skeleton.

**Entry criteria:** Phase 0 complete, all tests pass.
**Exit criteria:** IRQ flags live at `$D800`. Old address `$00FF` is ordinary RAM. Device router handles `$D800-$DFFF`. All tests updated and passing.

### Emulator changes

**`src/emulator.cpp`:**

1. `#include "n8_memory_map.h"`.
2. Replace all magic numbers for IRQ, TTY, frame buffer, ROM with constants.
3. Update IRQ macros:
   ```cpp
   #define IRQ_CLR() mem[N8_IRQ_FLAGS] = 0x00;
   #define IRQ_SET(bit) mem[N8_IRQ_FLAGS] = (mem[N8_IRQ_FLAGS] | (0x01 << bit))
   ```
4. Add the device register router:

```cpp
// Device register space: $D800-$DFFF
if (addr >= N8_DEV_BASE && addr < (N8_DEV_BASE + N8_DEV_SIZE)) {
    uint16_t dev_offset = addr - N8_DEV_BASE;
    uint8_t  slot = dev_offset >> 5;    // 0-63
    uint8_t  reg  = dev_offset & 0x1F;  // 0-31

    switch (slot) {
        case N8_IRQ_SLOT:  // $D800: System / IRQ
            if (pins & M6502_RW) {
                M6502_SET_DATA(pins, mem[N8_IRQ_FLAGS]);
            }
            // Writes land in mem[] via generic handler (same backing store)
            break;
        default:
            // Reserved slots: read returns $00, write ignored
            if (pins & M6502_RW) {
                M6502_SET_DATA(pins, 0x00);
            }
            break;
    }
}
```

**`src/emu_tty.cpp`:** `#include "n8_memory_map.h"`. Replace `emu_set_irq(1)` / `emu_clr_irq(1)` with `N8_IRQ_BIT_TTY`.

### Test changes

Update all `mem[0x00FF]` references in `test_utils.cpp`, `test_tty.cpp` to `mem[N8_IRQ_FLAGS]`.

**New tests (`test/test_bus.cpp`):**

| ID | Description |
|----|-------------|
| T110 | Write `$D800` stores value, read returns it |
| T111 | `IRQ_CLR()` clears `mem[N8_IRQ_FLAGS]` every tick |
| T112 | IRQ line asserted when `N8_IRQ_FLAGS` non-zero after tick |
| T113 | Old address `$00FF` no longer acts as IRQ register |

### Phase gate

- All 221 existing tests pass (with updated address constant).
- T110-T113 (4 new tests) pass.
- **Total: 225 tests.**

### Rollback

Revert IRQ macros to `0x00FF`. Remove device router block. Revert test addresses.

---

## 5. Phase 2 — TTY Device Migration

**Goal:** Move TTY device registers from `$C100` to `$D820`. Integrate into device router as slot 1.

**Entry criteria:** Phase 1 complete, all tests pass.
**Exit criteria:** TTY registers decode at `$D820-$D823`. Old address `$C100` is ordinary RAM. All tests updated and passing.

### Emulator changes

**`src/emulator.cpp`:**

Remove standalone TTY `BUS_DECODE` block. Add to device router:

```cpp
case N8_TTY_SLOT:  // $D820: TTY
    tty_decode(pins, reg);
    break;
```

`tty_decode()` signature is unchanged — it takes a relative register offset (0-3) and is address-agnostic. No changes to `emu_tty.cpp`.

### Test changes

Update all `make_read_pins(0xC1xx)` / `make_write_pins(0xC1xx, ...)` in `test_tty.cpp` and `test_gdb_callbacks.cpp` to use `N8_TTY_OUT_STATUS` / etc.

**New tests:**

| ID | Description |
|----|-------------|
| T114 | TTY read at old `$C100` does NOT trigger `tty_decode` |
| T115 | TTY read at `$D820` returns OUT_STATUS (`$00`) |
| T116 | TTY write at `$D821` sends character |

### Phase gate

- All prior tests pass (with updated addresses).
- T114-T116 pass.
- **Total: 228 tests.**

### Rollback

Revert router to remove TTY case. Restore standalone `BUS_DECODE` block. Revert test addresses.

---

## 6. Phase 3 — Frame Buffer Expansion

**Goal:** Expand frame buffer from 256B to 4KB at `$C000-$CFFF`. Introduce separate `frame_buffer[]` backing store per `hardware.md`. Update GDB callbacks.

**Entry criteria:** Phase 2 complete, all tests pass.
**Exit criteria:** Frame buffer is 4KB, backed by `frame_buffer[4096]`. Bus decode intercepts `$C000-$CFFF` and routes to backing store. `mem[$C000-$CFFF]` is not used. GDB callbacks handle redirection. All tests updated and passing.

### Design rationale

`hardware.md` states: "CPU reads/writes are intercepted by bus decode and routed to a separate backing store (not backed by main RAM)." Introducing this now (not later) means:
- All subsequent phases (video scroll operations, future rendering pipeline) assume `frame_buffer[]` exists.
- The GDB callback fix is small and contained.
- Tests are updated mechanically (`mem[0xC0xx]` → `frame_buffer[xx]`).

### Emulator changes

**`src/emulator.cpp`:**

1. Add backing store:
   ```cpp
   uint8_t frame_buffer[N8_FB_SIZE] = { };
   bool fb_dirty = true;
   ```

2. Add bus decode for frame buffer. This must run **before** the generic `mem[]` read/write:

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
       if (BUS_READ) {
           M6502_SET_DATA(pins, mem[addr]);
       } else {
           mem[addr] = M6502_GET_DATA(pins);
       }
   }
   ```

   The dirty flag comparison check (`!= val`) avoids unnecessary rasterization when firmware writes the same value.

3. Add to `emulator_reset()`:
   ```cpp
   memset(frame_buffer, 0, N8_FB_SIZE);
   fb_dirty = true;
   ```

**`src/emulator.h`:**
```cpp
extern uint8_t frame_buffer[];
extern bool fb_dirty;
```

**`src/main.cpp` (GDB callbacks):**
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

### Test changes

**Modified:**
- T64-T67: `mem[0xC0xx]` → `frame_buffer[xx]`
- T99: `mem[0xC000]` / `mem[0xC001]` → `frame_buffer[0]` / `frame_buffer[1]`

**New tests:**

| ID | Description |
|----|-------------|
| T117 | Write to `$C100` stores in `frame_buffer[0x100]` (no longer TTY) |
| T118 | Write to `$C7CF` (end of 80x25 active area) stores correctly |
| T119 | Write to `$CFFF` (last byte of 4KB FB) stores correctly |
| T120 | Read from `$C500` returns previously written value (round-trip) |
| T121 | Frame buffer write does NOT appear in `mem[]` (`mem[$C000]` stays 0) |
| T122 | `emulator_reset()` clears frame buffer to all zeros |
| T123 | `fb_dirty` flag set after write, clearable |

### EmulatorFixture update

The fixture constructor must `memset(frame_buffer, 0, N8_FB_SIZE)` and set `fb_dirty = true` to ensure clean state for each test.

### Phase gate

- All prior tests pass (with updated references).
- T117-T123 (7 new tests) pass.
- **Total: 235 tests.**

### Rollback

Remove `frame_buffer[]` declaration. Restore generic `mem[]` access for all addresses. Revert GDB callbacks. Revert test references.

---

## 7. Phase 4 — ROM Layout Migration

**Goal:** Move ROM from `$D000-$FFFF` (12KB) to `$E000-$FFFF` (8KB), freeing `$D000-$DFFF` for dev bank and device registers. Add ROM write protection on CPU bus.

**Entry criteria:** Phase 3 complete, all tests pass.
**Exit criteria:** ROM loads at `$E000`. Dev bank `$D000-$D7FF` is writable RAM. CPU writes to `$E000-$FFFF` are silently ignored. All tests pass.

### Rationale for Phase 4 (before video/keyboard)

The device register space at `$D800-$DFFF` overlaps the current ROM region. Until ROM is moved, `emulator_loadrom()` writes firmware bytes into `mem[$D800-$DFFF]`. While the device router overrides reads, having stale firmware bytes in the device address range is confusing and could mask bus decode bugs during development. Moving ROM early clears the space cleanly.

### Emulator changes

**`src/emulator.cpp` — `emulator_loadrom()`:**

Dual-path loader for backward compatibility during the transition (removed in Phase 10):

```cpp
void emulator_loadrom() {
    FILE *fp = fopen(rom_file, "r");
    if (!fp) {
        printf("ERROR: Cannot open ROM file '%s'\r\n", rom_file);
        fflush(stdout);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint16_t load_addr;
    if (size <= N8_ROM_SIZE) {
        load_addr = N8_ROM_BASE;       // $E000 (new 8KB layout)
    } else {
        load_addr = N8_LEGACY_ROM_BASE; // $D000 (legacy 12KB)
    }

    printf("Loading ROM at $%04X (%ld bytes)\r\n", load_addr, size);
    fflush(stdout);

    fread(&mem[load_addr], 1, size, fp);
    fclose(fp);
}
```

**`src/emulator.cpp` — ROM write protection:**

In the generic write path, silently drop CPU writes to ROM:

```cpp
if (!fb_access) {
    if (BUS_READ) {
        M6502_SET_DATA(pins, mem[addr]);
    } else {
        if (addr < N8_ROM_BASE) {
            mem[addr] = M6502_GET_DATA(pins);
        }
        // else: silently ignore CPU writes to ROM ($E000-$FFFF)
    }
}
```

Direct `mem[]` writes (tests, `emulator_loadrom()`, GDB `write_mem`) are unaffected — only CPU bus writes are blocked.

### Test changes

Existing tests that `load_at(0xD000, ...)` via `EmulatorFixture` still work — `$D000` is now dev bank (writable RAM), and the CPU executes from it identically.

**New tests:**

| ID | Description |
|----|-------------|
| T170 | `emulator_loadrom()` places 8KB binary at `$E000` |
| T171 | Code at `$E000` is executable (NOP sled, PC advances) |
| T172 | Reset vector at `$FFFC-$FFFD` works from ROM region |
| T173 | RAM at `$0400` is writable |
| T174 | Legacy loadrom: >8KB binary loads at `$D000` (transition) |
| T175 | CPU write to `$E000` is silently ignored (ROM protection) |
| T176 | Dev bank `$D000-$D7FF` is writable RAM |

### Phase gate

- All prior 235 tests pass.
- T170-T176 (7 new tests) pass.
- **Total: 242 tests.**

### Rollback

Revert `emulator_loadrom()`. Remove ROM write protection. Revert constants.

---

## 8. Phase 5 — Video Control Registers

**Goal:** Implement video control registers at `$D840-$D85F`. Plug into device router as slot 2.

**Entry criteria:** Phase 4 complete, all tests pass.
**Exit criteria:** Video registers at `$D840-$D847` are bus-accessible. Mode write auto-sets defaults. Scroll operations execute on `frame_buffer[]`. All tests passing.

### New file: `src/emu_video.h`

```cpp
#pragma once
#include <cstdint>

void video_init();
void video_reset();
void video_decode(uint64_t& pins, uint8_t dev_reg);

// State accessors (for future rendering pipeline)
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
```

### New file: `src/emu_video.cpp`

Internal state: an 8-byte register file.

```cpp
#include "emu_video.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"
#include <cstring>

extern uint8_t frame_buffer[];

static uint8_t vid_regs[8] = { 0 };

void video_init() { video_reset(); }

void video_reset() {
    memset(vid_regs, 0, sizeof(vid_regs));
    vid_regs[N8_VID_MODE]   = N8_VIDMODE_TEXT_DEFAULT;
    vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
    vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
}

static void video_apply_mode(uint8_t mode) {
    if (mode == N8_VIDMODE_TEXT_DEFAULT) {
        vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
        vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
        vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
    }
    // N8_VIDMODE_TEXT_CUSTOM: retain current values
}

// Guard: clamp stride*height to FB_SIZE to prevent overrun
static int safe_rows() {
    int s = vid_regs[N8_VID_STRIDE];
    int h = vid_regs[N8_VID_HEIGHT];
    if (s == 0) return 0;
    int max_rows = N8_FB_SIZE / s;
    return (h < max_rows) ? h : max_rows;
}

static void video_scroll_up() {
    int w = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    memmove(frame_buffer, frame_buffer + w, w * (h - 1));
    memset(frame_buffer + w * (h - 1), 0x00, w);
}

static void video_scroll_down() {
    int w = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    memmove(frame_buffer + w, frame_buffer, w * (h - 1));
    memset(frame_buffer, 0x00, w);
}

static void video_scroll_left() {
    int w = vid_regs[N8_VID_WIDTH];
    int s = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0) return;
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x00;
    }
}

static void video_scroll_right() {
    int w = vid_regs[N8_VID_WIDTH];
    int s = vid_regs[N8_VID_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0) return;
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line + 1, line, w - 1);
        line[0] = 0x00;
    }
}

void video_decode(uint64_t& pins, uint8_t reg) {
    if (reg > 7) {
        // Phantom registers: read 0, write no-op
        if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
        return;
    }

    if (pins & M6502_RW) {
        // Read
        if (reg == N8_VID_OPER) {
            M6502_SET_DATA(pins, 0x00);  // Write-only; reads return 0
        } else {
            M6502_SET_DATA(pins, vid_regs[reg]);
        }
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_VID_MODE:
                vid_regs[reg] = val;
                video_apply_mode(val);
                break;
            case N8_VID_OPER:
                // Write-once trigger, does not latch
                switch (val) {
                    case N8_VIDOP_SCROLL_UP:    video_scroll_up();    break;
                    case N8_VIDOP_SCROLL_DOWN:  video_scroll_down();  break;
                    case N8_VIDOP_SCROLL_LEFT:  video_scroll_left();  break;
                    case N8_VIDOP_SCROLL_RIGHT: video_scroll_right(); break;
                }
                break;
            default:
                vid_regs[reg] = val;
                break;
        }
    }
}

uint8_t video_get_mode()         { return vid_regs[N8_VID_MODE]; }
uint8_t video_get_width()        { return vid_regs[N8_VID_WIDTH]; }
uint8_t video_get_height()       { return vid_regs[N8_VID_HEIGHT]; }
uint8_t video_get_stride()       { return vid_regs[N8_VID_STRIDE]; }
uint8_t video_get_cursor_style() { return vid_regs[N8_VID_CURSOR]; }
uint8_t video_get_cursor_col()   { return vid_regs[N8_VID_CURCOL]; }
uint8_t video_get_cursor_row()   { return vid_regs[N8_VID_CURROW]; }
```

### Emulator integration

```cpp
#include "emu_video.h"

// In device router switch:
case N8_VID_SLOT:  // $D840: Video Control
    video_decode(pins, reg);
    break;
```

Call `video_init()` from `emulator_init()`. Call `video_reset()` from `emulator_reset()`.

### Build changes

Add `$(SRC_DIR)/emu_video.cpp` to `SOURCES` and `$(BUILD_DIR)/emu_video.o` to `TEST_SRC_OBJS`.

### New test file: `test/test_video.cpp`

| ID | Description |
|----|-------------|
| T130 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | Read VID_MODE (`$D840`) returns `$00` after reset |
| T132 | Write VID_MODE=`$00` auto-sets 80/25/80 |
| T133 | Write VID_MODE=`$01` retains current dims |
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
| T146 | Scroll with stride*height > `N8_FB_SIZE` does not corrupt memory |

Tests T137-T138, T144-T145 use `EmulatorFixture`. Pre-fill `frame_buffer[]`, trigger scroll via program (`STA` to `$D844`), verify `frame_buffer[]` contents.

### Phase gate

- All prior 242 tests pass.
- T130-T146 (17 new tests) pass.
- **Total: 259 tests.**

### Rollback

Remove `emu_video.cpp`, `emu_video.h`. Remove router case. Remove `test_video.cpp`. Revert Makefile.

---

## 9. Phase 6 — Keyboard Registers

**Goal:** Implement keyboard register block at `$D860-$D87F`. Plug into device router as slot 3. Include `kbd_tick()` for IRQ reassertion.

**Entry criteria:** Phase 5 complete, all tests pass.
**Exit criteria:** Keyboard registers at `$D860-$D862` are bus-accessible. IRQ bit 2 fires on keypress when enabled. `kbd_tick()` reasserts IRQ each tick. All tests passing.

### Critical: `kbd_tick()` requirement

The IRQ mechanism works by `IRQ_CLR()` zeroing all flags every tick, then devices reassert. Without `kbd_tick()`:

1. SDL event fires between frames → `kbd_inject_key()` sets IRQ bit 2
2. Next `emulator_step()` → `IRQ_CLR()` zeros `mem[$D800]`
3. `tty_tick()` reasserts TTY bit. **Nothing reasserts keyboard bit.**
4. IRQ check sees `mem[$D800] == 0` → keyboard interrupt lost

The TTY already has `tty_tick()` for exactly this reason. The keyboard must follow the same pattern.

### New file: `src/emu_kbd.h`

```cpp
#pragma once
#include <cstdint>

void kbd_init();
void kbd_reset();
void kbd_decode(uint64_t& pins, uint8_t dev_reg);
void kbd_tick();

// Host-side injection (from SDL event loop)
void kbd_inject_key(uint8_t keycode, uint8_t modifiers);

// Test helpers
uint8_t kbd_get_data();
uint8_t kbd_get_status();
bool kbd_data_available();
```

### New file: `src/emu_kbd.cpp`

```cpp
#include "emu_kbd.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"

static uint8_t kbd_data   = 0x00;
static uint8_t kbd_status = 0x00;
static uint8_t kbd_ctrl   = 0x00;

void kbd_init()  { kbd_reset(); }

void kbd_reset() {
    kbd_data   = 0x00;
    kbd_status = 0x00;
    kbd_ctrl   = 0x00;
    emu_clr_irq(N8_IRQ_BIT_KBD);
}

void kbd_inject_key(uint8_t keycode, uint8_t modifiers) {
    if (kbd_status & N8_KBD_STAT_AVAIL) {
        kbd_status |= N8_KBD_STAT_OVERFLOW;
    }
    kbd_data = keycode;
    kbd_status = (kbd_status & ~0x3C) | (modifiers & 0x3C);
    kbd_status |= N8_KBD_STAT_AVAIL;

    if (kbd_ctrl & N8_KBD_CTRL_IRQ_EN) {
        emu_set_irq(N8_IRQ_BIT_KBD);
    }
}

void kbd_tick() {
    // Reassert IRQ if data available and IRQ enabled.
    // Required because IRQ_CLR() zeros all flags every tick.
    if ((kbd_status & N8_KBD_STAT_AVAIL) &&
        (kbd_ctrl & N8_KBD_CTRL_IRQ_EN)) {
        emu_set_irq(N8_IRQ_BIT_KBD);
    }
}

void kbd_decode(uint64_t& pins, uint8_t reg) {
    if (pins & M6502_RW) {
        // Read
        uint8_t val = 0x00;
        switch (reg) {
            case N8_KBD_DATA:   val = kbd_data;   break;
            case N8_KBD_STATUS: val = kbd_status;  break;
            case N8_KBD_CTRL:   val = kbd_ctrl;    break;
            default:            val = 0x00;        break;
        }
        M6502_SET_DATA(pins, val);
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_KBD_ACK:  // offset 1 write = acknowledge
                kbd_status &= ~(N8_KBD_STAT_AVAIL | N8_KBD_STAT_OVERFLOW);
                emu_clr_irq(N8_IRQ_BIT_KBD);
                break;
            case N8_KBD_CTRL:
                kbd_ctrl = val & N8_KBD_CTRL_IRQ_EN;
                break;
            default:
                break;  // KBD_DATA is read-only
        }
    }
}

uint8_t kbd_get_data()    { return kbd_data; }
uint8_t kbd_get_status()  { return kbd_status; }
bool kbd_data_available() { return (kbd_status & N8_KBD_STAT_AVAIL) != 0; }
```

### Emulator integration

```cpp
#include "emu_kbd.h"

// In device router switch:
case N8_KBD_SLOT:  // $D860: Keyboard
    kbd_decode(pins, reg);
    break;
```

Add `kbd_tick()` call in `emulator_step()` next to `tty_tick(pins)`.
Add `kbd_init()` to `emulator_init()`, `kbd_reset()` to `emulator_reset()`.

### SDL2 key translation (`src/main.cpp`)

```cpp
#include "emu_kbd.h"

if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard
    && !event.key.repeat) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods   = sdl_to_n8_modifiers(SDL_GetModState());
    if (n8_key != 0) {
        kbd_inject_key(n8_key, mods);
    }
}
```

Key design points:
- `!event.key.repeat` filters SDL auto-repeat (N8 keyboard is single-shot).
- `!io.WantCaptureKeyboard` prevents ImGui input fields from leaking keys.

**`sdl_to_n8_keycode()`** handles:
1. **Ctrl+letter** → `$01-$1A` (control characters)
2. **Printable ASCII** with shift mapping for letters and symbols (complete shifted-symbol table: `1→!`, `2→@`, etc.)
3. **Special keys** (Return=`$0D`, Backspace=`$08`, Tab=`$09`, Escape=`$1B`)
4. **Extended keys** (`$80+`: arrows, Home/End/PgUp/PgDn, Insert/Delete, F1-F12)

**`sdl_to_n8_modifiers()`** maps SDL modifier flags:
- `KMOD_SHIFT` → bit 2, `KMOD_CTRL` → bit 3, `KMOD_ALT` → bit 4, `KMOD_CAPS` → bit 5

### Build changes

Add `$(SRC_DIR)/emu_kbd.cpp` to `SOURCES` and `$(BUILD_DIR)/emu_kbd.o` to `TEST_SRC_OBJS`.

### New test file: `test/test_kbd.cpp`

| ID | Description |
|----|-------------|
| T150 | `kbd_reset()` clears data, status, ctrl |
| T151 | `kbd_inject_key(0x41, 0)` sets DATA_AVAIL in status |
| T152 | Read KBD_DATA returns injected key code |
| T153 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | Write KBD_ACK clears DATA_AVAIL and OVERFLOW |
| T155 | Second inject before ACK sets OVERFLOW, data=second key |
| T156 | KBD_CTRL bit 0 enables IRQ; inject sets IRQ bit 2 |
| T157 | KBD_CTRL bit 0 clear: inject does NOT set IRQ |
| T158 | ACK deasserts IRQ bit 2 |
| T159 | Modifier bits (SHIFT, CTRL, ALT, CAPS) reflect in status |
| T160 | Modifiers update even when DATA_AVAIL=0 |
| T161 | Reserved registers (`$D863-$D867`) read 0, write no-op |
| T162 | Extended key code (`$80` = Up Arrow) stored correctly |
| T163 | Function key (`$90` = F1) stored correctly |
| T164 | Bus decode: program writes KBD_ACK via STA, clears status |
| T165 | `kbd_tick()` reasserts IRQ when data avail + IRQ enabled |
| T166 | `kbd_tick()` does NOT assert IRQ when IRQ disabled |

### Phase gate

- All prior 259 tests pass.
- T150-T166 (17 new tests) pass.
- **Total: 276 tests.**

### Rollback

Remove `emu_kbd.cpp`, `emu_kbd.h`. Remove router case and tick call. Remove `test_kbd.cpp`. Revert Makefile.

---

## 10. Phase 7 — GDB Stub Update

**Goal:** Update GDB memory map XML to reflect the new layout. Verify GDB memory access to device registers and frame buffer.

**Entry criteria:** Phase 6 complete, all tests pass.
**Exit criteria:** GDB memory map XML matches new layout. All GDB tests pass.

### Code changes

**`src/gdb_stub.cpp` — `memory_map_xml[]`:**

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

Regions: `$0000-$BFFF` RAM, `$C000-$CFFF` frame buffer (GDB reads/writes go to `frame_buffer[]` via callback fix from Phase 3), `$D000-$D7FF` dev bank, `$D800-$DFFF` device registers (GDB reads `mem[addr]` to avoid side effects), `$E000-$FFFF` ROM.

### Test changes

**Modified:** T58 — Memory map XML test updated for `0xE000`.

**New tests:**

| ID | Description |
|----|-------------|
| T180 | Memory map XML contains `$C000` RAM region (4KB FB) |
| T181 | Memory map XML contains `$D800` RAM region (device space) |
| T182 | Memory map ROM at `$E000` with length `$2000` |
| T183 | GDB read at `$D840` returns `mem[]` value (no side effects) |
| T184 | GDB write at `$C000` writes to `frame_buffer[]` |

### Phase gate

- All prior 276 tests pass (T58 updated).
- T180-T184 (5 new tests) pass.
- **Total: 281 tests.**

### Rollback

Revert `memory_map_xml[]`. Revert T58.

---

## 11. Phase 8 — Firmware Source Migration

**Goal:** Update all firmware source files to use new device addresses from `n8_memory_map.inc`.

**Entry criteria:** Phase 7 complete, all tests pass.
**Exit criteria:** `make firmware` succeeds. Boot banner displays. Echo loop works.

### cc65 linker config: `firmware/n8.cfg`

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

Key changes: ROM at `$E000` (8KB, was `$D000` 12KB). RAM starts at `$0400` (was `$0200`).

### Firmware source changes

**`firmware/devices.inc`** — include `n8_memory_map.inc` and provide legacy aliases:

```asm
.include "n8_memory_map.inc"

; Legacy aliases for transitional compatibility
.define TXT_CTRL         N8_FB_BASE
.define TXT_BUFF         N8_FB_BASE+1

.define TTY_OUT_CTRL     N8_TTY_OUT_STATUS
.define TTY_OUT_DATA     N8_TTY_OUT_DATA
.define TTY_IN_CTRL      N8_TTY_IN_STATUS
.define TTY_IN_DATA      N8_TTY_IN_DATA
```

**`firmware/devices.h`** — updated C-preprocessor definitions:

```c
#ifndef __DEVICES__
#define __DEVICES__

#define   TXT_CTRL   $C000
#define   TXT_BUFF   $C001

#define   TTY_OUT_CTRL $D820
#define   TTY_OUT_DATA $D821
#define   TTY_IN_CTRL  $D822
#define   TTY_IN_DATA  $D823

#endif
```

**`firmware/zp.inc`:** Remove `ZP_IRQ $FF`. IRQ flags are now at `$D800`, not in zero page.

**Other firmware files** (`interrupt.s`, `tty.s`, `main.s`, etc.): These use symbolic names from `devices.inc`. The aliases ensure no source changes are needed initially.

### Phase gate

- `make firmware` produces `N8firmware` without errors.
- `make test`: **281 passed, 0 failed**.
- Manual: `./n8` boots, banner prints via TTY, echo loop works.

### Rollback

Restore original `n8.cfg`, `devices.inc`, `devices.h`, `zp.inc`.

---

## 12. Phase 9 — Playground Migration

**Goal:** Update all playground and gdb_playground programs to use new addresses and linker configs.

**Entry criteria:** Phase 8 complete, firmware builds and runs.
**Exit criteria:** All playground programs build. GDB playground tests pass via n8gdb.

### Files requiring changes

| File | Changes |
|------|---------|
| `firmware/playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/playground/mon1.s` | TTY equates → `$D820-$D823` |
| `firmware/playground/mon2.s` | TTY equates → `$D820-$D823` |
| `firmware/playground/mon3.s` | TTY + TXT_BASE equates updated |
| `firmware/gdb_playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/gdb_playground/test_tty.s` | TTY equates → `$D820-$D821` |

### Phase gate

- All playground programs build.
- `make test`: **281 passed, 0 failed**.
- Manual: `n8gdb repl` with `test_tty` sends output correctly.

### Rollback

Restore original equates and `playground.cfg` files.

---

## 13. Phase 10 — End-to-End Validation & Cleanup

**Goal:** Final validation. Remove legacy compatibility code and transition aliases.

### Cleanup tasks

1. Remove `N8_LEGACY_*` defines from `n8_memory_map.h`.
2. Remove dual-path legacy loadrom from `emulator_loadrom()` (always load at `$E000`).
3. Remove legacy alias defines from `firmware/devices.inc` (replace with direct `N8_*` symbols throughout firmware source).
4. Update `CLAUDE.md` memory map documentation.
5. Verify: `grep -rn '0x00FF\|0xC100\|0xD000' src/` returns no stale references (except comments).

### End-to-end test scenarios

| ID | Scenario | Pass Criteria |
|----|----------|---------------|
| E2E-01 | Boot firmware, TTY echo loop | Characters echoed to stdout |
| E2E-02 | GDB connect, `regs`, `step` | Registers read correctly |
| E2E-03 | GDB `load` playground at `$E000` | Program executes |
| E2E-04 | GDB breakpoint at label | Execution halts at correct PC |
| E2E-05 | GDB `read $D800 20` | Device register space readable |
| E2E-06 | GDB `read $C000 100` | Frame buffer readable |
| E2E-07 | Video scroll up via program | `frame_buffer[]` shifted correctly |
| E2E-08 | Keyboard inject + IRQ | CPU vectors to IRQ handler |
| E2E-09 | Reset via GDB | CPU restarts at `$FFFC` vector |
| E2E-10 | Full mon3 session | All commands work with new TTY addresses |

### Final gate

- `make clean && make && make firmware && make test` all succeed.
- **281 automated tests, 0 failures.**
- All 10 E2E scenarios pass manually.
- No legacy addresses remain in the codebase.
- Tag: `git tag memory-map-v2`

---

## 14. Rendering Pipeline (Separate Effort)

The video rendering pipeline is explicitly out of scope for this migration spec but is documented here for continuity. Use Spec B's architecture when implementing.

### Architecture

```
frame_buffer[4096]  -->  screen_pixels[640x400]  -->  GL texture  -->  ImGui::Image()
     ^                         ^                         ^
   CPU writes            n8_font[256][16]          glTexSubImage2D
                         video_regs                (each frame, if dirty)
                         cursor state
```

### Key design points (from Spec B)

- **CPU-side rasterization** to `uint32_t screen_pixels[640*400]`, no shader complexity.
- **Dirty flag optimization:** Skip rasterization when `fb_dirty` is false and cursor is not flashing.
- **Font source:** Copy `docs/charset/n8_font.h` to `src/n8_font.h` for build isolation.
- **OpenGL texture:** `glGenTextures` + `glTexSubImage2D` (not `SDL_Renderer`, which conflicts with the GL context).
- **Cursor:** Frame-counter-based flash (deterministic, no timer dependency). Modes: off, steady, flash. Shapes: underline, block.
- **Phosphor color:** `0xFF33FF33` (green phosphor) as default, UI-selectable.
- **ImGui window:** "N8 Display" with `ImGui::GetContentRegionAvail()` scaling, `GL_NEAREST` filtering.
- **`emu_display.cpp`** excluded from test build (OpenGL dependency).

### Performance budget

| Operation | Per Frame | Target |
|-----------|-----------|--------|
| Rasterize 80x25 chars | 256K pixel writes | < 0.5 ms |
| Cursor composite | 128 pixel writes | < 0.01 ms |
| Texture upload | 1 MB | < 0.3 ms |
| **Total overhead** | | **< 1 ms** |

### New files (when implemented)

- `src/emu_display.h`, `src/emu_display.cpp` — Rendering engine
- `src/n8_font.h` — Font data (copy from `docs/charset/`)

---

## 15. Test Case Registry

### Naming convention

- `Tnnn` — automated unit/integration tests (doctest). Sequential from T110 to avoid collision with existing T01-T101a.
- `E2E-nn` — manual end-to-end validation scenarios.

### New tests by file

**`test/test_bus.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T110 | 1 | Write/read `$D800` (IRQ_FLAGS device space) |
| T111 | 1 | `IRQ_CLR()` clears flags every tick |
| T112 | 1 | IRQ line asserted when flags non-zero after tick |
| T113 | 1 | Old `$00FF` no longer acts as IRQ register |
| T114 | 2 | TTY read at old `$C100` does NOT trigger `tty_decode` |
| T115 | 2 | TTY read at `$D820` returns OUT_STATUS |
| T116 | 2 | TTY write at `$D821` sends character |
| T117 | 3 | Write to `$C100` stores in `frame_buffer[0x100]` |
| T118 | 3 | Write to `$C7CF` stores correctly |
| T119 | 3 | Write to `$CFFF` stores correctly |
| T120 | 3 | Read from `$C500` returns written value |
| T121 | 3 | Frame buffer write does NOT appear in `mem[]` |
| T122 | 3 | `emulator_reset()` clears frame buffer |
| T123 | 3 | `fb_dirty` flag set on write, clearable |

**`test/test_video.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T130 | 5 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | 5 | Read VID_MODE returns `$00` after reset |
| T132 | 5 | Write VID_MODE=`$00` auto-sets 80/25/80 |
| T133 | 5 | Write VID_MODE=`$01` retains current dims |
| T134 | 5 | Write/read VID_WIDTH |
| T135 | 5 | Write/read VID_HEIGHT |
| T136 | 5 | Write/read VID_STRIDE |
| T137 | 5 | Scroll up: row 0 gets row 1, last row zeroed |
| T138 | 5 | Scroll down: row 1 gets row 0, first row zeroed |
| T139 | 5 | Read VID_OPER returns 0 (write-only) |
| T140 | 5 | Write/read VID_CURSOR |
| T141 | 5 | Write/read VID_CURCOL |
| T142 | 5 | Write/read VID_CURROW |
| T143 | 5 | Phantom registers (`$D848-$D85F`) read 0, write no-op |
| T144 | 5 | Scroll left |
| T145 | 5 | Scroll right |
| T146 | 5 | Scroll with oversized stride*height is safe |

**`test/test_kbd.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T150 | 6 | `kbd_reset()` clears state |
| T151 | 6 | `kbd_inject_key` sets DATA_AVAIL |
| T152 | 6 | Read KBD_DATA returns key code |
| T153 | 6 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | 6 | Write KBD_ACK clears flags |
| T155 | 6 | Second inject sets OVERFLOW |
| T156 | 6 | IRQ enable + inject sets IRQ bit 2 |
| T157 | 6 | IRQ disable + inject does NOT set IRQ |
| T158 | 6 | ACK deasserts IRQ bit 2 |
| T159 | 6 | Modifier bits reflect in status |
| T160 | 6 | Modifiers update without DATA_AVAIL |
| T161 | 6 | Reserved regs read 0, write no-op |
| T162 | 6 | Extended key code (`$80`) stored |
| T163 | 6 | Function key (`$90`) stored |
| T164 | 6 | Bus decode: program writes KBD_ACK |
| T165 | 6 | `kbd_tick()` reasserts IRQ when data avail + IRQ enabled |
| T166 | 6 | `kbd_tick()` does NOT assert IRQ when disabled |

**`test/test_integration.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T170 | 4 | `emulator_loadrom()` places 8KB at `$E000` |
| T171 | 4 | Code at `$E000` is executable |
| T172 | 4 | Reset vector in ROM region works |
| T173 | 4 | RAM at `$0400` writable |
| T174 | 4 | Legacy loadrom (>8KB at `$D000`) |
| T175 | 4 | CPU write to `$E000` silently ignored |
| T176 | 4 | Dev bank `$D000-$D7FF` is writable RAM |

**`test/test_gdb_protocol.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T180 | 7 | Memory map has `$C000` RAM (4KB FB) |
| T181 | 7 | Memory map has `$D800` RAM (device) |
| T182 | 7 | Memory map ROM at `$E000` length `$2000` |
| T183 | 7 | GDB read at `$D840` returns `mem[]` value |
| T184 | 7 | GDB write at `$C000` writes to `frame_buffer[]` |

### Existing test modifications

Tests modified in place (same ID, updated addresses):

| ID | Phase | Change |
|----|-------|--------|
| T64-T67 | 3 | `mem[0xC0xx]` → `frame_buffer[xx]` |
| T69, T69a, T69b, T70, T70a | 1 | `mem[0x00FF]` → `mem[N8_IRQ_FLAGS]` |
| T71-T78a | 2 | `0xC1xx` pin addresses → `0xD82x` |
| T76 | 1 | `mem[0x00FF]` → `mem[N8_IRQ_FLAGS]` |
| T58 | 7 | Memory map XML check for `0xE000` |
| T99 | 3 | `mem[0xC000]` → `frame_buffer[0]` |

---

## 16. Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Bus decode mask error causes phantom addresses | Medium | High | Boundary tests at mask edges per phase |
| Structural change to `emulator_step()` (Phase 3) | Medium | High | Thorough test coverage; T62-T67 as regression |
| cc65 segment overflow in 8KB ROM | Low | Medium | Current firmware ~1KB. Monitor `.map` output. |
| IRQ timing regression (`$D800` vs `$00FF`) | Low | High | Tick-level tests T111, T112 |
| Keyboard IRQ lost due to `IRQ_CLR()` | Medium | Medium | `kbd_tick()` per tick. Tests T165-T166 |
| Video scroll corrupts frame buffer | Medium | Medium | `safe_rows()` clamp. Test T146 |
| GDB device register reads trigger side effects | Low | Medium | GDB reads `mem[addr]` directly, bypassing decode |
| Firmware won't build with new linker config | Low | High | `make firmware` is a phase gate |
| Legacy loadrom dual-path is fragile | Medium | Low | Temporary; removed in Phase 10. Tested by T174 |
| `EmulatorFixture` not updated for `frame_buffer[]` | Medium | Medium | Explicit `memset(frame_buffer, 0, N8_FB_SIZE)` in constructor |

---

## 17. File Inventory

### New files

| File | Phase | Purpose |
|------|-------|---------|
| `src/n8_memory_map.h` | 0 | All memory map constants |
| `firmware/n8_memory_map.inc` | 0 | Assembly mirror of constants |
| `src/emu_video.h` | 5 | Video control register API |
| `src/emu_video.cpp` | 5 | Video registers, scroll operations |
| `src/emu_kbd.h` | 6 | Keyboard device API |
| `src/emu_kbd.cpp` | 6 | Keyboard registers, IRQ, `kbd_tick()` |
| `test/test_video.cpp` | 5 | Video register tests |
| `test/test_kbd.cpp` | 6 | Keyboard device tests |

### Modified files

| File | Phases | Changes |
|------|--------|---------|
| `src/emulator.cpp` | 1-4 | Constants, IRQ macros, device router, frame buffer, ROM loader, write protection |
| `src/emulator.h` | 3 | `extern frame_buffer[]`, `fb_dirty` |
| `src/emu_tty.cpp` | 1 | Use constants for IRQ bit |
| `src/main.cpp` | 3, 6 | GDB callbacks, keyboard SDL events |
| `src/gdb_stub.cpp` | 7 | Memory map XML |
| `Makefile` | 5, 6 | New source files |
| `firmware/n8.cfg` | 8 | ROM at `$E000`, RAM at `$0400` |
| `firmware/devices.inc` | 8 | Include `n8_memory_map.inc`, legacy aliases |
| `firmware/devices.h` | 8 | New device addresses |
| `firmware/zp.inc` | 8 | Remove `ZP_IRQ` |
| `firmware/playground/playground.cfg` | 9 | ROM at `$E000` |
| `firmware/gdb_playground/playground.cfg` | 9 | ROM at `$E000` |
| `firmware/playground/mon1.s` | 9 | TTY equates |
| `firmware/playground/mon2.s` | 9 | TTY equates |
| `firmware/playground/mon3.s` | 9 | TTY equates |
| `firmware/gdb_playground/test_tty.s` | 9 | TTY equates |
| `test/test_helpers.h` | 0 | Include `n8_memory_map.h` |
| `test/test_utils.cpp` | 1 | IRQ address |
| `test/test_tty.cpp` | 1, 2 | IRQ + TTY addresses |
| `test/test_bus.cpp` | 1, 2, 3 | New bus decode tests |
| `test/test_gdb_callbacks.cpp` | 1, 2 | Address updates |
| `test/test_gdb_protocol.cpp` | 7 | Memory map XML test |
| `test/test_integration.cpp` | 3, 4 | Frame buffer, ROM tests |

---

## 18. Rollback Procedures

### Per-phase rollback

Each phase is a separate commit. Rollback is `git revert`.

**General procedure:**

1. `git stash` any uncommitted work.
2. `git log --oneline -10` to find last known-good commit.
3. `git revert <bad-commit>`.
4. `make clean && make && make test` to verify.

### Phase-specific notes

| Phase | Complexity | Notes |
|-------|-----------|-------|
| 0 | Trivial | Delete new files. No behavior change. |
| 1 | Simple | Revert IRQ macros + router. ~8 test updates. |
| 2 | Simple | Revert router case. Restore `BUS_DECODE` block. |
| 3 | Medium | Structural change to `emulator_step()`. Revert `frame_buffer[]`, GDB callbacks. |
| 4 | Simple | Revert `emulator_loadrom()`, ROM protection, constants. |
| 5 | Simple | Remove `emu_video.*`, `test_video.cpp`. Remove router case. |
| 6 | Simple | Remove `emu_kbd.*`, `test_kbd.cpp`. Remove router case + tick. |
| 7 | Trivial | Revert `memory_map_xml[]`. |
| 8 | Simple | Restore original firmware configs + source. |
| 9 | Simple | Restore original playground equates + configs. |
| 10 | Trivial | Re-add legacy defines. Restore dual-path loader. |
