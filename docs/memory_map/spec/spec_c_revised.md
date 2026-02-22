# Spec C (Revised): Integration, Testing & Firmware Migration

> Covers the full incremental migration from the current memory map to the
> proposed layout (see `docs/memory_map/hardware.md`), plus implementation
> of the video control registers and keyboard device.

**Baseline:** 221 tests passing (349 assertions), 0 failures.

---

## Reconciliation Notes

This revision incorporates insights from all three specialist specs.
Below is what changed and why, organized by topic.

### 1. Constants Header Naming

- **Spec A** proposed `src/n8_memory_map.h` with `N8_` prefixed `#define` macros.
- **Spec B** proposed expanding the existing `src/machine.h` with `static const` C++ variables (no prefix).
- **Original Spec C** proposed a new `src/hw_regs.h` with `HW_` prefixed `#define` macros plus a parallel `firmware/hw_regs.inc`.

**Decision: Use `src/n8_memory_map.h` with `N8_` prefixed `#define` macros.**

Rationale:
- `#define` macros work in both C and C++ translation units (unlike `static const`). The `m6502.h` vendored header is C-compatible, and some test code may need C compatibility.
- The `N8_` prefix is descriptive and avoids collisions. `HW_` is too generic.
- `machine.h` already exists with one constant (`total_memory`). Renaming/replacing it would add unnecessary churn. A new dedicated file is cleaner.
- The parallel `firmware/hw_regs.inc` idea from Spec C is retained but renamed to `firmware/n8_memory_map.inc` for consistency.

Adopted from: Spec A (file name, prefix convention). Spec C (parallel asm include).

### 2. frame_buffer[] vs mem[] — Taking a Clear Position

- **Spec A** (Phase 3) introduces a separate `frame_buffer[N8_FB_SIZE]` array, intercepts `$C000-$CFFF` bus accesses before the generic `mem[]` handler, and skips `mem[]` for that range. Also updates GDB read/write callbacks in the same phase.
- **Spec B** (Phases 3+5) does the same: separate backing store, `fb_dirty` flag, skip `mem[]`.
- **Original Spec C** deferred the separation, keeping `mem[]` as the backing store for the expanded frame buffer, arguing it avoids test breakage and GDB complications.

**Decision: Introduce `frame_buffer[]` as a separate backing store in Phase 3, per Specs A and B.**

Rationale:
- The hardware spec (`hardware.md`) explicitly states: "CPU reads/writes are intercepted by bus decode and routed to a separate backing store (not backed by main RAM)." Deferring this contradicts the spec we are implementing.
- The test breakage concern is manageable: tests that previously checked `mem[0xC000]` simply change to `frame_buffer[0]`. This is a small, mechanical update.
- The GDB callback fix is small (redirect `$C000-$CFFF` reads/writes to `frame_buffer[]` in `gdb_read_mem`/`gdb_write_mem`). Spec A includes this in Phase 3 and it is the right place.
- The `fb_dirty` flag from Spec B is a good addition for the future rendering pipeline, but is not strictly needed for the bus architecture migration. We include it as an optimization hook but do not gate any behavior on it in this spec.
- Separating the backing store early means all subsequent phases (video control scroll operations, future rendering pipeline) can assume `frame_buffer[]` exists.

Adopted from: Spec A (clean separation in Phase 3, GDB callback fix). Spec B (`fb_dirty` flag).

### 3. Device Router Architecture

- **Spec A** (Phase 1) introduces a slot-based device router: compute `slot = (addr - N8_DEV_BASE) >> 5`, then `switch(slot)`. New devices plug in as new `case` entries. Single range check for all of `$D800-$DFFF`.
- **Spec B** (Phase 10) uses a linear `if/else if` chain with explicit offset ranges. Achieves the same result but is less structured.
- **Original Spec C** used individual `BUS_DECODE` macros per device (same as current code style).

**Decision: Adopt Spec A's slot-based device router.**

Rationale:
- The slot router is a single entry point for all device registers. Adding a new device is one `case` line.
- It replaces N separate `BUS_DECODE` blocks with one range check + switch, which is cleaner and faster.
- The existing `BUS_DECODE` macro pattern works but does not compose well when you have 4+ devices in the same address region.
- Spec B's `if/else if` chain achieves similar extensibility but the slot computation is more elegant and self-documenting.

The router is introduced in Phase 1 (with the IRQ device as slot 0). Each subsequent device phase adds a `case` to the switch.

Adopted from: Spec A (slot-based router design).

### 4. Phase Ordering

- **Spec A:** 0-Constants, 1-IRQ, 2-TTY, 3-FB expand, 4-Video regs, 5-Video render, 6-Keyboard, 7-ROM layout, 8-Firmware, 9-GDB stub.
- **Spec B:** 0-Constants, 1-IRQ, 2-TTY, 3-FB expand, 4-ROM move, 5-Video regs, 6-Keyboard, 7-Font texture, 8-Screen render, 9-Cursor, 10-Bus refactor, 11-Firmware, 12-Makefile.
- **Original Spec C:** 0-Constants, 1-IRQ, 2-TTY, 3-FB expand, 4-Video regs, 5-Keyboard, 6-ROM split+linker, 7-GDB stub, 8-Firmware source, 9-Playground, 10-E2E.

**Decision: Revised ordering (hybrid of A and C):**
0. Constants header
1. IRQ migration + device router skeleton
2. TTY migration (plug into router)
3. Frame buffer expansion (separate backing store)
4. Video control registers (plug into router)
5. Keyboard registers (plug into router)
6. ROM layout migration
7. GDB stub update
8. Firmware source migration
9. Playground migration
10. End-to-end validation

Rationale:
- Phases 0-5 are consistent across all three specs (same logical order).
- Spec C's approach of doing ROM layout *after* all device registers (Phase 6) is better than Spec B's approach of doing it early (Phase 4), because the ROM region `$D000-$DFFF` overlaps with device space. Moving ROM after devices are wired up means the dev bank at `$D000-$D7FF` is already understood as writable RAM, and device decode at `$D800-$DFFF` is in place. This avoids confusion about what `$D000-$DFFF` is during intermediate phases.
- Spec A's approach of doing ROM layout at Phase 7 (after keyboard) matches this reasoning.
- The rendering pipeline (Spec A Phase 5, Spec B Phases 7-9) is explicitly excluded from this spec. This spec covers bus architecture, device registers, firmware migration, and testing. The rendering pipeline is Spec B's domain and will be implemented separately after the bus migration is complete.
- GDB stub update immediately follows ROM layout (Phase 7) since they are tightly coupled.
- Firmware source migration (Phase 8) and playground migration (Phase 9) are last because firmware must use all new addresses simultaneously.
- E2E validation (Phase 10) is the final gate.

Adopted from: Spec C (ROM after devices, separate firmware/playground phases, E2E gate). Spec A (router introduced early in Phase 1).

### 5. GDB Stub Changes

- **Spec A** (Phase 9) updates memory map XML and notes that `gdb_read_mem`/`gdb_write_mem` should return `mem[addr]` for device registers to avoid side effects.
- **Original Spec C** (Phase 7) updates XML and adds tests for GDB reading device/FB regions.

**Decision: Align with Spec A's approach for device register reads, and Spec C's test coverage.**

For the frame buffer, GDB callbacks redirect to `frame_buffer[]` (done in Phase 3). For device registers at `$D800-$DFFF`, GDB callbacks continue to read `mem[addr]` which is safe because the device handlers write to `mem[]` for IRQ_FLAGS and the other devices have their own state. Reading `mem[addr]` for device registers via GDB returns the last raw bus value without triggering side effects, which is the correct behavior for a debugger.

Adopted from: Spec A (device register GDB read strategy). Spec C (test IDs T180-T184).

### 6. cc65 Linker Configuration

- **Spec A** (Phase 8): Single `ROM` memory region at `$E000`, size `$2000`. Simple.
- **Spec B** (Phase 11): Same single ROM region.
- **Original Spec C** (Phase 6): Four separate memory regions (`ROM_MON`, `ROM_IMPL`, `ROM_ENTRY`, `ROM_VEC`) with corresponding segments (`MONITOR`, `ENTRY`, etc.).

**Decision: Use a single `ROM` region at `$E000` with size `$2000` for now. Add fine-grained segments later when the firmware actually needs them.**

Rationale:
- The current firmware is ~1KB. Splitting into 4 ROM regions with 4+ segments adds complexity for no immediate benefit.
- The cc65 linker will place segments contiguously within a single ROM region. The VECTORS segment uses `start = $FFFA` which pins the vectors regardless of ROM region structure.
- When the firmware grows and needs explicit separation (e.g., a separate MONITOR segment for a future stdlib), the linker config can be refined. That is a firmware architecture decision, not a bus migration concern.
- Spec C's original multi-region config had a subtle issue: `ROM_VEC` at `$FFE0` with size `$0020` (32 bytes) matches the hardware spec's "Vectors + Bank Switch" region, but the VECTORS segment is pinned to `$FFFA` (6 bytes for NMI/RESET/IRQ). The remaining 26 bytes at `$FFE0-$FFF9` would be unusable padding. This works but is unnecessarily complex.

Adopted from: Spec A and Spec B (single ROM region, simplicity).

### 7. RAM Start Address

- **Spec A** leaves RAM start implicit.
- **Spec B** changes RAM start from `$200` to `$400` (matching the proposed memory map's "Reserved" region at `$0200-$03FF`).
- **Original Spec C** also changes to `$400`.

**Decision: Change RAM start to `$0400` in the linker config.**

The `$0200-$03FF` region is marked "Reserved" in the proposed memory map. While the emulator does not enforce this (it is all `mem[]`), the linker config should match the spec to prevent the cc65 runtime from placing variables in the reserved region.

Adopted from: Specs B and C.

### 8. Test ID Consolidation

All three specs propose tests. To avoid duplicates and ensure comprehensive coverage:

- **Spec A** test IDs: `T_IRQ_01-03`, `T_TTY_MIG_01`, `T_FB_01-04`, `T_VID_01-08`, `T_KBD_01-11`, `T_ROM_01-04`. (Descriptive names, ~30 tests.)
- **Spec B** test IDs: `T_VID_01-32`, `T_KBD_01-09`, `T_BUS_01-06`, `T_CUR_01-06`. (Prefix-based, ~50 tests.)
- **Original Spec C** test IDs: `T110-T184`, `E2E-01-10`. (Sequential numeric, 52 + 10 tests.)

**Decision: Use Spec C's sequential numeric scheme (T110+) for consistency with the existing test suite (T01-T101a). Incorporate the best test ideas from all three specs.**

The existing codebase uses `Tnn` or `Tnna` format. Continuing with `T110+` avoids collisions and is easy to reference. Descriptive names from Specs A and B are used in the test descriptions but the ID is numeric.

Test deduplication:
- Spec A's `T_FB_03` ("frame buffer does NOT write to mem[]") and Spec B's `T_BUS_01` ("Write to $C500 does not appear in mem[$C500]") are the same test. Consolidated as T121.
- Spec A's `T_FB_04` ("emulator_reset clears frame buffer") is a good test. Added as T122.
- Spec B's `fb_dirty` flag tests are included as T123.
- Spec A's scroll buffer overflow guard (clamp stride*height to FB_SIZE) is an important safety check. Added as T146.
- Spec B's ROM write protection test (`T_BUS_02`) is added as T175 since we add optional ROM write protection in Phase 6.

### 9. Rendering Pipeline Scope

- **Spec A** covers rendering (Phase 5: OpenGL texture, pixel buffer, ImGui window).
- **Spec B** covers rendering in detail (Phases 7-9: font atlas, screen texture, cursor compositing).
- **Original Spec C** did not cover rendering at all.

**Decision: Rendering remains out of scope for this spec.**

This spec covers bus architecture, device registers, firmware migration, and testing. The rendering pipeline (font loading, texture creation, pixel rasterization, cursor compositing, ImGui display window) is the domain of the graphics pipeline specialist (Spec B). It will be implemented as a separate effort after the bus migration lands, using the `frame_buffer[]` and `video_get_*()` APIs established here.

### 10. ROM Write Protection

- **Spec A** explicitly defers ROM write protection.
- **Spec B** (Phase 10) adds it: writes to `$E000-$FFFF` are silently ignored.
- **Original Spec C** did not address it.

**Decision: Add optional ROM write protection in Phase 6 (ROM layout migration), disabled by default.**

Rationale: The current emulator has no write protection. Some tests write directly to `mem[]` in the ROM region (e.g., `EmulatorFixture::load_at(0xD000, ...)`). After the ROM moves to `$E000`, tests that load code at `$E000` via `mem[]` writes need this to work. ROM write protection should be enforced only on the CPU bus (during `emulator_step()`), not on direct `mem[]` array access. This is the natural behavior since bus decode only runs during CPU ticks.

A compile-time or runtime flag (`N8_ROM_WRITE_PROTECT`) can be added later. For now, the bus decode in `emulator_step()` will silently ignore CPU writes to `$E000-$FFFF`. Direct `mem[]` array writes (used by tests and `emulator_loadrom()`) bypass this.

Adopted from: Spec B (ROM write protection in bus decode).

### 11. kbd_tick() vs Event-Driven

- **Spec A** says keyboard does not need a tick (event-driven from SDL).
- **Spec B** agrees (no `kbd_tick()`).
- **Original Spec C** included `kbd_tick()` to reassert the IRQ bit each tick.

**Decision: Remove `kbd_tick()`. The keyboard IRQ is asserted on `kbd_inject_key()` and deasserted on ACK. No per-tick polling needed.**

Rationale: The IRQ mechanism works by `IRQ_CLR()` zeroing the flags register every tick, then devices *reassert*. The TTY needs a tick because it checks its ring buffer every tick. The keyboard does not need a tick because:
- The IRQ flag is stored in `mem[N8_IRQ_FLAGS]`.
- `IRQ_CLR()` zeros it every tick.
- But `emu_set_irq(N8_IRQ_BIT_KBD)` is called from `kbd_inject_key()`, which runs from the SDL event loop *between* frames, not during a tick.
- After `IRQ_CLR()` runs, the keyboard IRQ bit is lost unless reasserted.

Wait -- this means keyboard IRQ *does* need per-tick reassertion, or the IRQ mechanism must change for keyboard. Reconsidering.

**Revised decision: Include `kbd_tick()` to reassert IRQ bit when `kbd_data_avail && kbd_ctrl & IRQ_EN`.** This matches the TTY pattern and the IRQ clear-reassert architecture. Spec C was correct.

Adopted from: Original Spec C (`kbd_tick()` for IRQ reassertion).

---

## Table of Contents

1. [Migration Overview](#1-migration-overview)
2. [Phase 0 -- Constants Header](#2-phase-0----constants-header)
3. [Phase 1 -- IRQ Flag Register Migration](#3-phase-1----irq-flag-register-migration)
4. [Phase 2 -- TTY Device Migration](#4-phase-2----tty-device-migration)
5. [Phase 3 -- Frame Buffer Expansion](#5-phase-3----frame-buffer-expansion)
6. [Phase 4 -- Video Control Registers](#6-phase-4----video-control-registers)
7. [Phase 5 -- Keyboard Registers](#7-phase-5----keyboard-registers)
8. [Phase 6 -- ROM Layout Migration](#8-phase-6----rom-layout-migration)
9. [Phase 7 -- GDB Stub Update](#9-phase-7----gdb-stub-update)
10. [Phase 8 -- Firmware Source Migration](#10-phase-8----firmware-source-migration)
11. [Phase 9 -- Playground Migration](#11-phase-9----playground-migration)
12. [Phase 10 -- End-to-End Validation](#12-phase-10----end-to-end-validation)
13. [Test Case Registry](#13-test-case-registry)
14. [Risk Matrix](#14-risk-matrix)
15. [Rollback Procedures](#15-rollback-procedures)

---

## 1. Migration Overview

### Current Memory Map (relevant regions)

| Address       | Region                | Size    |
|--------------:|:----------------------|--------:|
| `$0000-$00FF` | Zero Page (ZP_IRQ=$FF)| 256     |
| `$0100-$01FF` | Hardware Stack        | 256     |
| `$0200-$BEFF` | RAM                   | 48,384  |
| `$C000-$C0FF` | Frame Buffer (256B)   | 256     |
| `$C100-$C10F` | TTY Device            | 16      |
| `$D000-$FFFF` | ROM (12KB)            | 12,288  |

### Target Memory Map

| Address       | Region                | Size    |
|--------------:|:----------------------|--------:|
| `$0000-$00FF` | Zero Page             | 256     |
| `$0100-$01FF` | Hardware Stack        | 256     |
| `$0200-$03FF` | Reserved              | 512     |
| `$0400-$BFFF` | Program RAM           | 47 KB   |
| `$C000-$CFFF` | Frame Buffer (4KB)    | 4,096   |
| `$D000-$D7FF` | Dev Bank              | 2 KB    |
| `$D800-$D81F` | System/IRQ (slot 0)   | 32      |
| `$D820-$D83F` | TTY (slot 1)          | 32      |
| `$D840-$D85F` | Video Control (slot 2)| 32      |
| `$D860-$D87F` | Keyboard (slot 3)     | 32      |
| `$D880-$DFFF` | Future Devices        | varies  |
| `$E000-$EFFF` | Monitor/stdlib        | 4 KB    |
| `$F000-$FDFF` | Kernel Implementation | 3.5 KB  |
| `$FE00-$FFDF` | Kernel Entry          | 480     |
| `$FFE0-$FFFF` | Vectors + Bank Switch | 32      |

### Migration Principles

1. **One device at a time.** Each phase moves or adds exactly one
   hardware entity. All prior tests must still pass.
2. **Test-first.** New test cases are written and committed before the
   code that makes them pass.
3. **Firmware follows emulator.** The emulator bus decode moves first;
   firmware addresses update in a later phase.
4. **Each phase is a commit.** Rollback is `git revert`. At the end of
   every phase, `make test` passes.

---

## 2. Phase 0 -- Constants Header

### Objective

Create a single source of truth for all hardware addresses. Eliminate
magic numbers in `emulator.cpp`, `emu_tty.cpp`, and test code. Provide
a parallel `.inc` file for cc65 assembly firmware.

### Entry Criteria

Current `make test` passes (221 tests).

### Exit Criteria

New header exists. `make` and `make test` pass unchanged. No magic
numbers replaced yet (that happens in Phase 1+).

### Deliverables

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
#define N8_RAM_START       0x0400
#define N8_RAM_END         0xBFFF

// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4 KB target (current: 0x0100)
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

// --- Legacy addresses (removed after full migration) ---
#define N8_LEGACY_IRQ_ADDR   0x00FF
#define N8_LEGACY_TTY_BASE   0xC100
#define N8_LEGACY_FB_SIZE    0x0100
#define N8_LEGACY_ROM_BASE   0xD000
#define N8_LEGACY_ROM_SIZE   0x3000
```

**New file: `firmware/n8_memory_map.inc`** (assembly-language parallel)

```asm
; n8_memory_map.inc -- device register addresses
; Mirror of src/n8_memory_map.h for cc65 assembly

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

### Code Changes

- Add `#include "n8_memory_map.h"` to `test/test_helpers.h`.
- No behavior changes. No magic number replacements yet.

### Tests

None. This phase only adds files.

### Phase Gate

- `make` compiles cleanly with the new header present but not yet included
  by production code.
- `make test` still produces: **221 passed, 0 failed**.

---

## 3. Phase 1 -- IRQ Flag Register Migration

### Objective

Move the IRQ flags register from `$00FF` (zero page) to `$D800` (device
space). Introduce the device register router skeleton.

### Entry Criteria

Phase 0 complete, all tests pass.

### Exit Criteria

IRQ flags live at `$D800`. Old address `$00FF` is ordinary RAM.
Device router handles slot 0. All tests updated and passing.

### Emulator Changes

**`src/emulator.cpp`:**

1. `#include "n8_memory_map.h"`.
2. Replace IRQ macros:
   ```cpp
   #define IRQ_CLR() mem[N8_IRQ_FLAGS] = 0x00;
   #define IRQ_SET(bit) mem[N8_IRQ_FLAGS] = (mem[N8_IRQ_FLAGS] | (0x01 << bit))
   ```
3. Replace `emu_set_irq()` and `emu_clr_irq()` to use `N8_IRQ_FLAGS`.
4. Replace `if(mem[0x00FF] == 0)` with `if(mem[N8_IRQ_FLAGS] == 0)`.
5. Update `emulator_show_status_window()` reference to `mem[0x00FF]`.
6. Add the device register router:

```cpp
// Device register space: $D800-$DFFF
// Runs AFTER generic mem[] read/write (device decode overrides data bus)
if (addr >= N8_DEV_BASE && addr < (N8_DEV_BASE + N8_DEV_SIZE)) {
    uint16_t dev_offset = addr - N8_DEV_BASE;
    uint8_t  slot = dev_offset >> 5;    // 0-63
    uint8_t  reg  = dev_offset & 0x1F;  // 0-31

    switch (slot) {
        case N8_IRQ_SLOT:  // $D800: System / IRQ
            // IRQ_FLAGS is handled via mem[] directly.
            // Bus decode ensures reads return current IRQ state.
            if (pins & M6502_RW) {
                M6502_SET_DATA(pins, mem[N8_IRQ_FLAGS]);
            }
            // Writes already landed in mem[] via generic handler.
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

Note: The IRQ device handler for reads explicitly sets the data bus to
`mem[N8_IRQ_FLAGS]`. This overrides whatever the generic `mem[]` read
returned (which is the same value since `$D800` is in `mem[]`). This
is consistent with how `tty_decode()` already works -- device decode
runs after the generic handler and overwrites the data bus.

For writes, the generic handler already wrote `mem[addr]`. Since
`N8_IRQ_FLAGS` == `addr` == `$D800`, the value is in `mem[]` where
`IRQ_CLR()` and the IRQ check will find it. No additional work needed.

### Test Changes

**Modified: `test/test_utils.cpp`:**

Update T69, T69a, T69b, T70, T70a -- all references to `mem[0x00FF]`
become `mem[N8_IRQ_FLAGS]`.

**Modified: `test/test_tty.cpp`:**

All lines that set `mem[0x00FF] = 0` become `mem[N8_IRQ_FLAGS] = 0`.
T76 check changes from `mem[0x00FF]` to `mem[N8_IRQ_FLAGS]`.

**New tests in `test/test_bus.cpp`:**

| Test ID | Description |
|---------|-------------|
| T110 | Write `$D800` stores in `mem[N8_IRQ_FLAGS]`, read returns value |
| T111 | `IRQ_CLR()` clears `mem[N8_IRQ_FLAGS]` every tick |
| T112 | IRQ line asserted when `mem[N8_IRQ_FLAGS]` non-zero after tick |
| T113 | Old address `$00FF` no longer acts as IRQ register |

### Phase Gate

- All 221 existing tests pass (with updated addresses).
- T110-T113 (4 new tests) pass.
- **Total: 225 tests.**

### Rollback

Revert `emulator.cpp` IRQ macros and helpers to use `0x00FF`. Revert
test address constants. Remove device router block.

---

## 4. Phase 2 -- TTY Device Migration

### Objective

Move TTY device registers from `$C100-$C10F` to `$D820-$D83F`.
Integrate TTY into the device router as slot 1.

### Entry Criteria

Phase 1 complete, all tests pass.

### Exit Criteria

TTY registers decode at `$D820-$D823`. Old address `$C100` is ordinary
RAM. TTY uses the device router. All tests updated and passing.

### Emulator Changes

**`src/emulator.cpp`:**

1. Remove the standalone TTY bus decode:
   ```cpp
   // REMOVE:
   BUS_DECODE(addr, 0xC100, 0xFFF0) {
       const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
       tty_decode(pins, dev_reg);
   }
   ```

2. Add TTY to the device router switch:
   ```cpp
   case N8_TTY_SLOT:  // $D820: TTY
       tty_decode(pins, reg);
       break;
   ```

The `tty_decode()` function signature is unchanged. It already accepts
a register offset (0-3). The offset is now computed as
`(addr - N8_DEV_BASE) & 0x1F` instead of `(addr - 0xC100) & 0xFF`.
Since registers 0-3 are the same in both schemes, no logic changes.

**`src/emu_tty.cpp`:**

No changes. The TTY module is address-agnostic.

### Test Changes

**Modified: `test/test_tty.cpp`:**

All `make_read_pins(0xC1xx)` and `make_write_pins(0xC1xx, ...)` calls
update to use `N8_TTY_OUT_STATUS` / `N8_TTY_OUT_DATA` / etc.

T78a (phantom reg test) range changes to offsets 4-31 in the 32-byte
device slot.

**Modified: `test/test_gdb_callbacks.cpp`:**

`mem[0xC100]` references become `mem[N8_TTY_OUT_STATUS]`.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T114 | TTY read at old `$C100` does NOT trigger `tty_decode` |
| T115 | TTY read at `$D820` returns OUT_STATUS (0x00) |
| T116 | TTY write at `$D821` sends character (putchar side effect) |

### Phase Gate

- All prior tests pass (with updated addresses).
- T114-T116 pass.
- **Total: 228 tests.**

### Rollback

Revert device router to remove TTY case. Restore standalone
`BUS_DECODE` block. Revert test addresses.

---

## 5. Phase 3 -- Frame Buffer Expansion

### Objective

Expand frame buffer from 256 bytes (`$C000-$C0FF`) to 4 KB
(`$C000-$CFFF`). Introduce a separate `frame_buffer[]` backing store
as specified in `hardware.md`. Update GDB memory callbacks.

### Entry Criteria

Phase 2 complete, all tests pass.

### Exit Criteria

Frame buffer is 4 KB, backed by `frame_buffer[4096]`. Bus decode
intercepts `$C000-$CFFF` reads/writes and routes to the backing store.
`mem[$C000-$CFFF]` is not used by the frame buffer. GDB callbacks
handle the new backing store. All tests updated and passing.

### Design Decision

The hardware spec states: "CPU reads/writes are intercepted by bus
decode and routed to a separate backing store (not backed by main RAM)."
We implement this now, not later, because:
- All subsequent phases (video scroll operations, future rendering
  pipeline) can assume `frame_buffer[]` exists.
- The GDB callback fix is small and contained.
- Tests are updated mechanically (`mem[0xC0xx]` -> `frame_buffer[xx]`).

### Emulator Changes

**`src/emulator.cpp`:**

1. Add backing store:
   ```cpp
   uint8_t frame_buffer[N8_FB_SIZE] = { };
   bool fb_dirty = true;
   ```

2. Add bus decode for frame buffer. This must run **before** the
   generic `mem[]` read/write to prevent `mem[]` from being
   touched for `$C000-$CFFF`:

   ```cpp
   // Frame buffer intercept: $C000-$CFFF
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

   This is a structural change to `emulator_step()`. The current code
   unconditionally reads/writes `mem[addr]`, then device decode
   overrides. With a separate backing store, we must skip `mem[]` for
   frame buffer addresses. The intercept-first approach is the cleanest
   way to achieve this.

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

### Test Changes

**Modified: `test/test_bus.cpp`:**

- T64: `CHECK(mem[0xC000] == 0x41)` -> `CHECK(frame_buffer[0] == 0x41)`
- T65: `mem[0xC000] = 0x42` -> `frame_buffer[0] = 0x42`
- T66: `CHECK(mem[0xC0FF] == 0x7E)` -> `CHECK(frame_buffer[0xFF] == 0x7E)`
- T67: `CHECK(mem[0xC005] == 0x33)` -> `CHECK(frame_buffer[5] == 0x33)`

**Modified: `test/test_integration.cpp`:**

- T99: `CHECK(mem[0xC000] == 0x48)` -> `CHECK(frame_buffer[0] == 0x48)`
- T99: `CHECK(mem[0xC001] == 0x69)` -> `CHECK(frame_buffer[1] == 0x69)`

**New tests:**

| Test ID | Description |
|---------|-------------|
| T117 | Write to `$C100` stores in `frame_buffer[0x100]` (no longer TTY) |
| T118 | Write to `$C7CF` (end of 80x25 active area) stores correctly |
| T119 | Write to `$CFFF` (last byte of 4KB FB) stores correctly |
| T120 | Read from `$C500` returns previously written value (round-trip) |
| T121 | Frame buffer write does NOT appear in `mem[]` (`mem[$C000]` stays 0) |
| T122 | `emulator_reset()` clears frame buffer to all zeros |
| T123 | `fb_dirty` flag set after write, clearable |

### Phase Gate

- All prior tests pass (with updated references).
- T117-T123 (7 new tests) pass.
- **Total: 235 tests.**

### Rollback

Remove `frame_buffer[]` declaration. Restore generic `mem[]` access
for all addresses. Revert GDB callbacks. Revert test references.

---

## 6. Phase 4 -- Video Control Registers

### Objective

Implement the video control register block at `$D840-$D85F` as
specified in `hardware.md`. Plug into device router as slot 2.

### Entry Criteria

Phase 3 complete, all tests pass.

### Exit Criteria

Video registers at `$D840-$D847` are bus-accessible. Mode write
auto-sets defaults. Scroll operations execute on `frame_buffer[]`.
All tests passing.

### Emulator Changes

**New file: `src/emu_video.h`**

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

**New file: `src/emu_video.cpp`**

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

static void video_scroll_up() {
    uint8_t w = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    uint16_t active = (uint16_t)w * h;
    if (active > N8_FB_SIZE) return;  // Safety clamp
    memmove(frame_buffer, frame_buffer + w, w * (h - 1));
    memset(frame_buffer + w * (h - 1), 0x00, w);
}

static void video_scroll_down() {
    uint8_t w = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    uint16_t active = (uint16_t)w * h;
    if (active > N8_FB_SIZE) return;
    memmove(frame_buffer + w, frame_buffer, w * (h - 1));
    memset(frame_buffer, 0x00, w);
}

static void video_scroll_left() {
    uint8_t w = vid_regs[N8_VID_WIDTH];
    uint8_t s = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    uint16_t active = (uint16_t)s * h;
    if (active > N8_FB_SIZE) return;
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x00;
    }
}

static void video_scroll_right() {
    uint8_t w = vid_regs[N8_VID_WIDTH];
    uint8_t s = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    uint16_t active = (uint16_t)s * h;
    if (active > N8_FB_SIZE) return;
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

**`src/emulator.cpp`:**

Add to device router:
```cpp
#include "emu_video.h"

case N8_VID_SLOT:  // $D840: Video Control
    video_decode(pins, reg);
    break;
```

Call `video_init()` from `emulator_init()`.
Call `video_reset()` from `emulator_reset()`.

**`Makefile`:**

Add `$(SRC_DIR)/emu_video.cpp` to `SOURCES`.
Add `$(BUILD_DIR)/emu_video.o` to `TEST_SRC_OBJS`.

### Test Changes

**New file: `test/test_video.cpp`**

| Test ID | Description |
|---------|-------------|
| T130 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | Read VID_MODE (`$D840`) returns 0x00 after reset |
| T132 | Write VID_MODE=0x00 auto-sets width=80, height=25, stride=80 |
| T133 | Write VID_MODE=0x01 retains current dimension values |
| T134 | Write/read VID_WIDTH |
| T135 | Write/read VID_HEIGHT |
| T136 | Write/read VID_STRIDE |
| T137 | VID_OPER=0x00 scroll up: row 0 gets row 1 data, last row zeroed |
| T138 | VID_OPER=0x01 scroll down: row 1 gets row 0 data, first row zeroed |
| T139 | Read VID_OPER returns 0 (write-only) |
| T140 | Write/read VID_CURSOR |
| T141 | Write/read VID_CURCOL |
| T142 | Write/read VID_CURROW |
| T143 | Phantom registers ($D848-$D85F) read 0x00, write no-op |
| T144 | VID_OPER=0x02 scroll left: each row shifts left 1, rightmost cleared |
| T145 | VID_OPER=0x03 scroll right: each row shifts right 1, leftmost cleared |
| T146 | Scroll with stride*height > N8_FB_SIZE does not corrupt memory |

Tests T137, T138, T144, T145 use `EmulatorFixture`. Pre-fill
`frame_buffer[]` with known patterns, trigger scroll via program
(STA to `$D844`), verify `frame_buffer[]` contents.

### Phase Gate

- All prior 235 tests pass.
- T130-T146 (17 new tests) pass.
- **Total: 252 tests.**

### Rollback

Remove `emu_video.cpp`, `emu_video.h`. Remove router case. Remove
`test_video.cpp`. Revert Makefile.

---

## 7. Phase 5 -- Keyboard Registers

### Objective

Implement the keyboard register block at `$D860-$D87F` as specified
in `hardware.md`. Plug into device router as slot 3.

### Entry Criteria

Phase 4 complete, all tests pass.

### Exit Criteria

Keyboard registers at `$D860-$D862` are bus-accessible. IRQ bit 2
fires on keypress when enabled. `kbd_tick()` reasserts IRQ each tick.
All tests passing.

### Emulator Changes

**New file: `src/emu_kbd.h`**

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

**New file: `src/emu_kbd.cpp`**

```cpp
#include "emu_kbd.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"

static uint8_t kbd_data     = 0x00;
static uint8_t kbd_status   = 0x00;   // Bits: [5:0] per hardware.md
static uint8_t kbd_ctrl     = 0x00;   // Bit 0 = IRQ enable

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

uint8_t kbd_get_data()       { return kbd_data; }
uint8_t kbd_get_status()     { return kbd_status; }
bool kbd_data_available()    { return (kbd_status & N8_KBD_STAT_AVAIL) != 0; }
```

**`src/emulator.cpp`:**

Add to device router:
```cpp
#include "emu_kbd.h"

case N8_KBD_SLOT:  // $D860: Keyboard
    kbd_decode(pins, reg);
    break;
```

Add `kbd_tick()` call in `emulator_step()` next to `tty_tick(pins)`.
Add `kbd_init()` to `emulator_init()`, `kbd_reset()` to
`emulator_reset()`.

**`src/main.cpp`:**

In the SDL event loop, map `SDL_KEYDOWN` events to `kbd_inject_key()`:

```cpp
#include "emu_kbd.h"

if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard
    && event.key.repeat == 0) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods   = sdl_to_n8_modifiers(SDL_GetModState());
    if (n8_key != 0) {
        kbd_inject_key(n8_key, mods);
    }
}
```

The `sdl_to_n8_keycode()` and `sdl_to_n8_modifiers()` translation
functions are defined as static helpers in `main.cpp`. They map SDL
keycodes to the N8 keycode scheme per `keycodes.md`. The implementation
follows Spec B's version which includes shifted symbol mappings
(!, @, #, etc.) in addition to the base ASCII and extended key ranges.

Key design points:
- `event.key.repeat == 0` filters SDL auto-repeat. The N8 keyboard is
  single-shot; firmware handles repeat if desired.
- `!io.WantCaptureKeyboard` prevents ImGui input fields from leaking
  keys to the emulated keyboard.

**`Makefile`:**

Add `$(SRC_DIR)/emu_kbd.cpp` to `SOURCES`.
Add `$(BUILD_DIR)/emu_kbd.o` to `TEST_SRC_OBJS`.

### Test Changes

**New file: `test/test_kbd.cpp`**

| Test ID | Description |
|---------|-------------|
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
| T160 | Modifier bits update even when DATA_AVAIL=0 |
| T161 | Reserved registers ($D863-$D867) read 0, write no-op |
| T162 | Extended key code ($80 = Up Arrow) stored correctly |
| T163 | Function key ($90 = F1) stored correctly |
| T164 | Bus decode: program writes KBD_ACK via STA, clears status |
| T165 | `kbd_tick()` reasserts IRQ bit when data avail + IRQ enabled |
| T166 | `kbd_tick()` does NOT assert IRQ when IRQ disabled |

### Phase Gate

- All prior 252 tests pass.
- T150-T166 (17 new tests) pass.
- **Total: 269 tests.**

### Rollback

Remove `emu_kbd.cpp`, `emu_kbd.h`. Remove router case and tick call.
Remove `test_kbd.cpp`. Remove SDL key mapping from `main.cpp`.
Revert Makefile.

---

## 8. Phase 6 -- ROM Layout Migration

### Objective

Move ROM from `$D000-$FFFF` (12 KB) to `$E000-$FFFF` (8 KB). The
`$D000-$D7FF` dev bank becomes writable RAM. The `$D800-$DFFF` device
register space is already handled by the device router. Add ROM write
protection on the CPU bus.

### Entry Criteria

Phase 5 complete, all tests pass.

### Exit Criteria

ROM loads at `$E000`. Code executes from all ROM sub-regions. Dev bank
is writable. CPU writes to `$E000-$FFFF` are silently ignored. All
tests pass.

### Emulator Changes

**`src/emulator.cpp` -- `emulator_loadrom()`:**

Support both old (12 KB) and new (8 KB) firmware binaries:

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
        load_addr = N8_ROM_BASE;  // $E000 (new 8KB layout)
    } else {
        load_addr = N8_LEGACY_ROM_BASE;  // $D000 (legacy 12KB)
    }

    printf("Loading ROM at $%04X (%ld bytes)\r\n", load_addr, size);
    fflush(stdout);

    fread(&mem[load_addr], 1, size, fp);
    fclose(fp);
}
```

**`src/emulator.cpp` -- ROM write protection:**

In `emulator_step()`, add ROM write protection in the bus decode.
The frame buffer intercept already skips `mem[]` for `$C000-$CFFF`.
For ROM addresses, we silently drop writes:

```cpp
// In the generic write path:
if (!fb_access) {
    if (BUS_READ) {
        M6502_SET_DATA(pins, mem[addr]);
    } else {
        // ROM write protection: $E000-$FFFF
        if (addr < N8_ROM_BASE) {
            mem[addr] = M6502_GET_DATA(pins);
        }
        // else: silently ignore writes to ROM
    }
}
```

Note: Direct `mem[]` writes (from tests, `emulator_loadrom()`, GDB
`write_mem`) are not affected. Only CPU bus writes are blocked.

### cc65 Linker Configuration

**`firmware/n8.cfg`:**

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

Key changes: ROM start `$E000` (was `$D000`), size `$2000` (was
`$3000`). RAM start `$400` (was `$200`), size `$BC00` (was `$BD00`).

### Test Changes

Tests that load code at `$D000` via `EmulatorFixture::load_at()` do
not need to change. `$D000` is now dev bank (writable RAM). The CPU
executes from RAM the same as ROM. Test programs work at any address
as long as the reset vector points there.

However, we add tests for the new ROM regions and dev bank:

| Test ID | Description |
|---------|-------------|
| T170 | `emulator_loadrom()` loads 8KB binary at `$E000` |
| T171 | Code at `$E000` is executable (NOP sled, PC advances) |
| T172 | Reset vector at `$FFFC-$FFFD` works from ROM region |
| T173 | RAM at `$0400` is writable |
| T174 | Legacy loadrom: >8KB binary loads at `$D000` |
| T175 | CPU write to `$E000` is silently ignored (ROM protection) |
| T176 | Dev bank `$D000-$D7FF` is writable RAM |

### Phase Gate

- All prior 269 tests pass.
- T170-T176 (7 new tests) pass.
- `make firmware` succeeds with new `n8.cfg`.
- **Total: 276 tests.**

### Rollback

Restore original `n8.cfg`. Revert `emulator_loadrom()`. Remove ROM
write protection. Revert test changes.

---

## 9. Phase 7 -- GDB Stub Update

### Objective

Update the GDB memory map XML to reflect the new layout. Verify GDB
memory access to device registers and frame buffer.

### Entry Criteria

Phase 6 complete, all tests pass.

### Exit Criteria

GDB memory map XML matches new layout. n8gdb can inspect all memory
regions. All GDB tests pass.

### Code Changes

**`src/gdb_stub.cpp` -- `memory_map_xml[]`:**

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

Regions:
- `$0000-$BFFF`: RAM (48 KB)
- `$C000-$CFFF`: RAM (frame buffer -- GDB reads/writes go to
  `frame_buffer[]` via the callback fix in Phase 3)
- `$D000-$D7FF`: RAM (dev bank)
- `$D800-$DFFF`: RAM (device registers -- GDB reads `mem[addr]` which
  avoids device side effects; this is correct for a debugger)
- `$E000-$FFFF`: ROM (8 KB)

**`bin/n8gdb/n8gdb.mjs`:**

No changes required. The n8gdb client has no hardcoded device
addresses. It reads the reset vector from `$FFFC` which has not moved.

### Test Changes

**Modified: `test/test_gdb_protocol.cpp`:**

T58 (memory map XML test): Update expected ROM address from `0xD000` to
`0xE000`.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T180 | Memory map XML contains `0xC000` RAM region (4KB FB) |
| T181 | Memory map XML contains `0xD800` RAM region (device space) |
| T182 | Memory map XML ROM at `0xE000` with length `0x2000` |
| T183 | GDB memory read at `$D840` returns `mem[]` value (no side effects) |
| T184 | GDB memory write at `$C000` writes to `frame_buffer[]` |

### Phase Gate

- All prior 276 tests pass (T58 updated).
- T180-T184 (5 new tests) pass.
- **Total: 281 tests.**

### Rollback

Revert `memory_map_xml[]`. Revert T58.

---

## 10. Phase 8 -- Firmware Source Migration

### Objective

Update all firmware assembly source files to use new device addresses
from `n8_memory_map.inc`.

### Entry Criteria

Phase 7 complete, all tests pass.

### Exit Criteria

Firmware builds with `make firmware`. Boot banner displays. Echo loop
works. All automated tests still pass.

### Firmware Source Changes

**`firmware/devices.inc`** (complete replacement):

```asm
; devices.inc -- Device register addresses (new memory map)
; See also: src/n8_memory_map.h, docs/memory_map/hardware.md

.include "n8_memory_map.inc"

; Legacy aliases for transitional compatibility
.define TXT_CTRL         N8_FB_BASE
.define TXT_BUFF         N8_FB_BASE+1

.define TTY_OUT_CTRL     N8_TTY_OUT_STATUS
.define TTY_OUT_DATA     N8_TTY_OUT_DATA
.define TTY_IN_CTRL      N8_TTY_IN_STATUS
.define TTY_IN_DATA      N8_TTY_IN_DATA
```

**`firmware/devices.h`** (complete replacement):

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

**`firmware/zp.inc`:**

Remove `ZP_IRQ $FF`. IRQ flags are now at `$D800`, not in zero page.

```asm
; zp.inc -- Zero Page locations

.define ZP_A_PTR    $E0
.define ZP_B_PTR    $E2
.define ZP_C_PTR    $E4
.define ZP_D_PTR    $E6

.define BYTE_0      $F0
.define BYTE_1      $F1
.define BYTE_2      $F2
.define BYTE_3      $F3
; ZP_IRQ removed -- IRQ flags now at $D800 (N8_IRQ_FLAGS)
```

**Other firmware files** (`interrupt.s`, `tty.s`, `main.s`, `init.s`,
`vectors.s`): These use symbolic names from `devices.inc`. The aliases
ensure no source changes are needed in these files. When legacy aliases
are removed (Phase 10), these files must switch to `N8_*` names.

### Build Verification

```bash
make clean
make firmware     # should build with new n8.cfg and devices.inc
make              # should build emulator
make test         # 281 tests pass
```

### Phase Gate

- `make firmware` produces `N8firmware` without errors.
- `make test`: **281 passed, 0 failed**.
- Manual: `./n8` boots, banner prints via TTY, echo loop works.

### Rollback

Restore original `devices.inc`, `devices.h`, `zp.inc`.

---

## 11. Phase 9 -- Playground Migration

### Objective

Update all playground and gdb_playground programs to use new addresses
and linker configs.

### Entry Criteria

Phase 8 complete, firmware builds and runs.

### Exit Criteria

All playground programs build. GDB playground tests pass via n8gdb.

### Files Requiring Changes

| File | Changes |
|------|---------|
| `firmware/playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/playground/mon1.s` | TTY equates -> `$D820-$D823` |
| `firmware/playground/mon2.s` | TTY equates -> `$D820-$D823` |
| `firmware/playground/mon3.s` | TTY + TXT_BASE equates updated |
| `firmware/gdb_playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/gdb_playground/test_tty.s` | TTY equates -> `$D820-$D821` |

**Playground linker config:**

```
MEMORY {
    ZP:  start =   $00, size =  $100, type = rw, define = yes;
    RAM: start =  $400, size = $BC00, define = yes;
    ROM: start = $E000, size = $2000, file = %O;
}

SEGMENTS {
    ZEROPAGE: load = ZP,  type = zp,  optional = yes;
    BSS:      load = RAM, type = bss, optional = yes, define = yes;
    DATA:     load = ROM, type = rw,  run = RAM, optional = yes, define = yes;
    CODE:     load = ROM, type = ro;
    RODATA:   load = ROM, type = ro,  optional = yes;
    VECTORS:  load = ROM, type = ro,  start = $FFFA;
}
```

### Build Verification

```bash
make -C firmware/playground clean && make -C firmware/playground
make -C firmware/gdb_playground clean && make -C firmware/gdb_playground
```

### Phase Gate

- All playground programs build.
- `make test`: **281 passed, 0 failed**.
- Manual: `n8gdb repl` with `test_tty` sends output correctly.

### Rollback

Restore original equates and `playground.cfg` files.

---

## 12. Phase 10 -- End-to-End Validation

### Objective

Final validation. Remove legacy compatibility code and transition
aliases.

### Cleanup Tasks

1. Remove `N8_LEGACY_*` defines from `n8_memory_map.h`.
2. Remove legacy loadrom path from `emulator_loadrom()` (always load
   at `$E000`).
3. Remove legacy alias defines from `firmware/devices.inc` (replace
   with direct `N8_*` symbols throughout firmware source).
4. Update `CLAUDE.md` memory map documentation.

### End-to-End Test Scenarios

| Test ID | Scenario | Pass Criteria |
|---------|----------|---------------|
| E2E-01 | Boot firmware, TTY echo loop | Characters echoed to stdout |
| E2E-02 | GDB connect, `regs`, `step` | Registers read correctly |
| E2E-03 | GDB `load` playground program | Program executes from `$E000` |
| E2E-04 | GDB breakpoint at label | Execution halts at correct PC |
| E2E-05 | GDB `read $D800 20` | Device register space readable |
| E2E-06 | GDB `read $C000 100` | Frame buffer readable |
| E2E-07 | Video scroll up via program | `frame_buffer[]` shifted correctly |
| E2E-08 | Keyboard inject + IRQ | CPU vectors to IRQ handler |
| E2E-09 | Reset via GDB | CPU restarts at `$FFFC` vector |
| E2E-10 | Full mon3 session | All commands work with new TTY addresses |

### Final Test Count

| Phase | New Tests | Running Total |
|------:|----------:|--------------:|
| Baseline | -- | 221 |
| Phase 0 (constants) | 0 | 221 |
| Phase 1 (IRQ move) | 4 | 225 |
| Phase 2 (TTY move) | 3 | 228 |
| Phase 3 (FB expand) | 7 | 235 |
| Phase 4 (Video regs) | 17 | 252 |
| Phase 5 (Keyboard) | 17 | 269 |
| Phase 6 (ROM layout) | 7 | 276 |
| Phase 7 (GDB stub) | 5 | 281 |
| Phase 8 (Firmware) | 0 (build test) | 281 |
| Phase 9 (Playground) | 0 (build test) | 281 |
| Phase 10 (E2E) | 10 (manual) | 281 + 10 manual |

### Phase Gate

- `make clean && make && make firmware && make test` all succeed.
- **281 automated tests, 0 failures.**
- All 10 E2E scenarios pass manually.
- No legacy addresses remain in the codebase.
- Tag: `git tag memory-map-v2`

---

## 13. Test Case Registry

### Naming Convention

- `Tnnn` -- automated unit/integration tests (doctest).
- `E2E-nn` -- manual end-to-end validation scenarios.
- Test IDs are sequential within phases and do not overlap with existing
  test IDs (existing range: T01-T101a).

### New Test IDs by File

**`test/test_bus.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T110 | 1 | Write/read `$D800` (IRQ_FLAGS device space) |
| T111 | 1 | IRQ_FLAGS cleared every tick (`IRQ_CLR()`) |
| T112 | 1 | IRQ line asserted when flags non-zero after tick |
| T113 | 1 | Old `$00FF` no longer acts as IRQ register |
| T114 | 2 | TTY read at old `$C100` does NOT trigger tty_decode |
| T115 | 2 | TTY read at `$D820` returns OUT_STATUS |
| T116 | 2 | TTY write at `$D821` sends character |
| T117 | 3 | Write to `$C100` stores in `frame_buffer[0x100]` |
| T118 | 3 | Write to `$C7CF` stores correctly in frame buffer |
| T119 | 3 | Write to `$CFFF` stores correctly in frame buffer |
| T120 | 3 | Read from `$C500` returns written value (round-trip) |
| T121 | 3 | Write to FB does NOT appear in `mem[]` |
| T122 | 3 | `emulator_reset()` clears frame buffer to zeros |
| T123 | 3 | `fb_dirty` set on write, clearable |

**`test/test_video.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T130 | 4 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | 4 | Read VID_MODE returns 0x00 after reset |
| T132 | 4 | Write VID_MODE=0x00 auto-sets 80/25/80 |
| T133 | 4 | Write VID_MODE=0x01 retains current dims |
| T134 | 4 | Write/read VID_WIDTH |
| T135 | 4 | Write/read VID_HEIGHT |
| T136 | 4 | Write/read VID_STRIDE |
| T137 | 4 | VID_OPER=0x00 scroll up |
| T138 | 4 | VID_OPER=0x01 scroll down |
| T139 | 4 | Read VID_OPER returns 0 (write-only) |
| T140 | 4 | Write/read VID_CURSOR |
| T141 | 4 | Write/read VID_CURCOL |
| T142 | 4 | Write/read VID_CURROW |
| T143 | 4 | Phantom registers ($D848-$D85F) read 0 |
| T144 | 4 | VID_OPER=0x02 scroll left |
| T145 | 4 | VID_OPER=0x03 scroll right |
| T146 | 4 | Scroll with oversized stride*height is safe |

**`test/test_kbd.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T150 | 5 | `kbd_reset()` clears state |
| T151 | 5 | `kbd_inject_key` sets DATA_AVAIL |
| T152 | 5 | Read KBD_DATA returns key code |
| T153 | 5 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | 5 | Write KBD_ACK clears flags |
| T155 | 5 | Second inject sets OVERFLOW |
| T156 | 5 | IRQ enable + inject sets IRQ bit 2 |
| T157 | 5 | IRQ disable + inject does NOT set IRQ |
| T158 | 5 | ACK deasserts IRQ bit 2 |
| T159 | 5 | Modifier bits reflect in status |
| T160 | 5 | Modifiers update without DATA_AVAIL |
| T161 | 5 | Reserved regs read 0, write no-op |
| T162 | 5 | Extended key code ($80) stored |
| T163 | 5 | Function key ($90) stored |
| T164 | 5 | Bus decode: program writes KBD_ACK |
| T165 | 5 | `kbd_tick()` reasserts IRQ when data avail + IRQ enabled |
| T166 | 5 | `kbd_tick()` does not assert IRQ when disabled |

**`test/test_integration.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T170 | 6 | `emulator_loadrom()` places 8KB at `$E000` |
| T171 | 6 | Code at `$E000` is executable |
| T172 | 6 | Reset vector in ROM region works |
| T173 | 6 | RAM at `$0400` writable |
| T174 | 6 | Legacy loadrom (>8KB at `$D000`) |
| T175 | 6 | CPU write to `$E000` silently ignored (ROM protection) |
| T176 | 6 | Dev bank `$D000-$D7FF` is writable RAM |

**`test/test_gdb_protocol.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T180 | 7 | Memory map has `$C000` RAM (4KB FB) |
| T181 | 7 | Memory map has `$D800` RAM (device) |
| T182 | 7 | Memory map ROM at `$E000` length `$2000` |
| T183 | 7 | GDB read at `$D840` returns `mem[]` value |
| T184 | 7 | GDB write at `$C000` writes to `frame_buffer[]` |

### Existing Test Modifications

Tests modified in place (same ID, updated addresses):

| ID | Phase | Change |
|----|-------|--------|
| T64-T67 | 3 | `mem[0xC0xx]` -> `frame_buffer[xx]` |
| T69, T69a, T69b, T70, T70a | 1 | `mem[0x00FF]` -> `mem[N8_IRQ_FLAGS]` |
| T71-T78a | 2 | `0xC1xx` pin addresses -> `0xD82x` |
| T76 | 1 | `mem[0x00FF]` -> `mem[N8_IRQ_FLAGS]` |
| T58 | 7 | Memory map XML check for `0xE000` |
| T99 | 3 | `mem[0xC000]`/`mem[0xC001]` -> `frame_buffer[0]`/`frame_buffer[1]` |

---

## 14. Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Bus decode mask error causes phantom addresses | Medium | High | Boundary tests at mask edges per phase |
| Structural change to `emulator_step()` (Phase 3 frame buffer intercept) | Medium | High | Thorough test coverage of all bus regions. Existing tests T62-T67 serve as regression checks. |
| cc65 segment overflow in 8KB ROM | Low | Medium | Current firmware is ~1KB. 8KB is ample. Monitor `.map` output. |
| IRQ timing regression ($D800 vs $00FF) | Low | High | Tick-level tests T111, T112. The IRQ mechanism is address-agnostic. |
| Keyboard IRQ lost due to `IRQ_CLR()` every tick | Medium | Medium | `kbd_tick()` reasserts IRQ each tick (same pattern as TTY). Tests T165-T166 verify. |
| Video scroll corrupts frame buffer | Medium | Medium | Safety clamp: `stride*height <= N8_FB_SIZE`. Test T146. |
| GDB reads of device registers trigger side effects | Low | Medium | GDB callbacks read `mem[addr]` directly, bypassing device decode. Frame buffer redirected to `frame_buffer[]` in Phase 3. |
| Firmware won't build with new linker config | Low | High | `make firmware` is a phase gate. Verified before proceeding. |
| Playground programs too large for 8KB ROM | Very Low | Low | Largest program (mon3.s) is ~1KB. |
| Legacy loadrom dual-path is fragile | Medium | Low | Temporary. Removed in Phase 10. Tested by T174. |

---

## 15. Rollback Procedures

### Per-Phase Rollback

Each phase is a separate commit. Rollback is `git revert`.

**General procedure:**

1. `git stash` any uncommitted work.
2. `git log --oneline -10` to find last known-good commit.
3. `git revert <bad-commit>`.
4. `make clean && make && make test` to verify.

### Phase-Specific Notes

| Phase | Complexity | Notes |
|-------|-----------|-------|
| 0 | Trivial | Delete new files. No behavior change. |
| 1 | Simple | Revert IRQ macros + device router. Revert ~8 test updates. |
| 2 | Simple | Revert router case. Restore `BUS_DECODE` block. |
| 3 | Moderate | Remove `frame_buffer[]`. Restore generic `mem[]` access. Revert GDB callbacks. Revert ~10 test refs. |
| 4 | Moderate | Remove source files + Makefile + test file. Remove router case. |
| 5 | Moderate | Same as Phase 4 for keyboard files. |
| 6 | Complex | Revert linker config + loadrom + ROM protection + 7 tests. Firmware must be rebuilt with old config. |
| 7 | Simple | Revert XML string + modified test. |
| 8 | Complex | Revert firmware source files. Rebuild with old config. |
| 9 | Moderate | Revert equates in 4 playground files + 2 config files. |
| 10 | Simple | Re-add legacy compatibility code. |

### Emergency Rollback

Tag the repo before starting Phase 1:

```bash
git tag pre-memory-map-migration
```

If a deep regression is found:

```bash
git checkout pre-memory-map-migration
make clean && make && make firmware && make test
```

---

## Appendix A: Device Router Reference

### Slot-Based Router (introduced Phase 1)

```cpp
if (addr >= N8_DEV_BASE && addr < (N8_DEV_BASE + N8_DEV_SIZE)) {
    uint16_t dev_offset = addr - N8_DEV_BASE;
    uint8_t  slot = dev_offset >> 5;    // 0-63
    uint8_t  reg  = dev_offset & 0x1F;  // 0-31

    switch (slot) {
        case 0:  dev_irq_decode(pins, reg);   break;  // Phase 1
        case 1:  tty_decode(pins, reg);        break;  // Phase 2
        case 2:  video_decode(pins, reg);      break;  // Phase 4
        case 3:  kbd_decode(pins, reg);        break;  // Phase 5
        default:
            if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
            break;
    }
}
```

Advantages over per-device `BUS_DECODE` blocks:
- Single range check for all device I/O.
- Adding a device is one `case` line.
- Slot number is self-documenting (matches hardware.md device table).
- Clean separation between routing (emulator.cpp) and device logic
  (emu_tty.cpp, emu_video.cpp, emu_kbd.cpp).

### Mask Reference

| Region | Base | Size | Mask (for BUS_DECODE) |
|--------|------|------|-----------------------|
| Device space | `$D800` | 2 KB | `$F800` (top 5 bits) |
| Single device slot | varies | 32 B | `$FFE0` (top 11 bits) |
| Frame buffer | `$C000` | 4 KB | `$F000` (top 4 bits) |

The device router uses an explicit range check (`addr >= base && addr < end`)
rather than `BUS_DECODE` because it needs the slot and register values,
not just a match/no-match.

---

## Appendix B: Bus Decode Order in `emulator_step()`

After all phases, the bus decode in `emulator_step()` has this structure:

```
1. m6502_tick() -- CPU produces address + R/W
2. Breakpoint/watchpoint checks
3. IRQ tick: IRQ_CLR(), tty_tick(), kbd_tick(), check flags, set/clear pin
4. Bus decode:
   a. Frame buffer ($C000-$CFFF): read/write frame_buffer[], skip mem[]
   b. Generic mem[] read/write (for all other addresses)
   c. Device router ($D800-$DFFF): slot dispatch overrides data bus
   d. ROM write protection ($E000-$FFFF): drops CPU writes
   e. ZP monitor, Vector monitor (logging only)
5. tick_count++
```

Key ordering constraints:
- Frame buffer intercept (4a) must run BEFORE generic mem[] (4b) to
  prevent `mem[$C000-$CFFF]` from being touched.
- Device router (4c) runs AFTER generic mem[] (4b). Device decode
  overrides the data bus for reads. For writes, `mem[addr]` is already
  written (used by IRQ_FLAGS at `$D800`).
- ROM write protection (4d) can be part of the generic write path
  (skip write if `addr >= N8_ROM_BASE`).

---

## Appendix C: cc65 Segment Size Budget

| Segment | Region | Available | Current Est. |
|---------|--------|----------:|-------------:|
| All (CODE, RODATA, STARTUP, etc.) | ROM ($E000-$FFF9) | 8,186 B | ~1,000 B |
| VECTORS | ROM ($FFFA-$FFFF) | 6 B | 6 B |
| BSS + DATA (runtime) | RAM ($0400-$BFFF) | 48,128 B | ~100 B |
| ZEROPAGE | ZP ($0000-$00FF) | 256 B | ~20 B |

Current firmware total ROM usage is ~1KB. The 8KB ROM region provides
comfortable growth margin. If fine-grained ROM segmentation (Monitor,
Kernel, Entry) is needed later, the linker config can be split without
changing the emulator.

---

## Appendix D: File Change Summary

### New Files

| File | Phase | Purpose |
|------|-------|---------|
| `src/n8_memory_map.h` | 0 | Hardware address constants (C++) |
| `firmware/n8_memory_map.inc` | 0 | Hardware address constants (asm) |
| `src/emu_video.h` | 4 | Video control register header |
| `src/emu_video.cpp` | 4 | Video control register implementation |
| `src/emu_kbd.h` | 5 | Keyboard register header |
| `src/emu_kbd.cpp` | 5 | Keyboard register implementation |
| `test/test_video.cpp` | 4 | Video register tests |
| `test/test_kbd.cpp` | 5 | Keyboard register tests |

### Modified Files

| File | Phases | Changes |
|------|--------|---------|
| `src/emulator.cpp` | 1,2,3,4,5,6 | IRQ macros, device router, FB intercept, ROM loader, ROM protection |
| `src/emulator.h` | 3 | `extern frame_buffer[]`, `extern fb_dirty` |
| `src/main.cpp` | 3,5 | GDB callbacks (FB redirect), SDL key mapping |
| `src/gdb_stub.cpp` | 7 | Memory map XML |
| `Makefile` | 4,5 | New source files |
| `firmware/n8.cfg` | 6 | ROM at $E000, RAM at $0400 |
| `firmware/devices.inc` | 8 | Include n8_memory_map.inc, legacy aliases |
| `firmware/devices.h` | 8 | New addresses |
| `firmware/zp.inc` | 8 | Remove ZP_IRQ |
| `firmware/playground/playground.cfg` | 9 | ROM at $E000 |
| `firmware/playground/mon1.s` | 9 | TTY equates |
| `firmware/playground/mon2.s` | 9 | TTY equates |
| `firmware/playground/mon3.s` | 9 | TTY + TXT_BASE equates |
| `firmware/gdb_playground/playground.cfg` | 9 | ROM at $E000 |
| `firmware/gdb_playground/test_tty.s` | 9 | TTY equates |
| `test/test_helpers.h` | 0 | Add n8_memory_map.h include |
| `test/test_utils.cpp` | 1 | IRQ register address |
| `test/test_tty.cpp` | 1,2 | IRQ + TTY addresses |
| `test/test_bus.cpp` | 1,2,3 | New bus decode tests, FB references |
| `test/test_gdb_callbacks.cpp` | 1,2 | Address updates |
| `test/test_gdb_protocol.cpp` | 7 | Memory map XML test |
| `test/test_integration.cpp` | 3,6 | FB references, ROM tests |

### Unchanged Files

| File | Reason |
|------|--------|
| `src/m6502.h` | Vendored; never edit |
| `src/emu_tty.cpp` | Address-agnostic (works on relative register offsets) |
| `src/emu_dis6502.cpp` | No address dependencies |
| `src/emu_labels.cpp` | No address dependencies |
| `src/gui_console.cpp` | No address dependencies |
| `src/utils.cpp` | No address dependencies |
| `bin/n8gdb/rsp.mjs` | Protocol-level; no addresses |
| `bin/n8gdb/n8gdb.mjs` | No hardcoded device addresses |
| `firmware/init.s` | Uses linker symbols |
| `firmware/interrupt.s` | Uses devices.inc aliases |
| `firmware/tty.s` | Uses devices.inc aliases |
| `firmware/main.s` | Uses devices.inc aliases |
| `firmware/vectors.s` | Uses linker segment |
| `test/test_cpu.cpp` | Uses CpuFixture; address-independent |
| `test/test_disasm.cpp` | No device addresses |
| `test/test_labels.cpp` | No device addresses |
| `test/doctest.h` | Vendored framework |

---

## Appendix E: Dependency Graph

```
Phase 0 --- Constants Header (n8_memory_map.h + .inc)
   |
   v
Phase 1 --- IRQ Migration ($00FF -> $D800) + Device Router Skeleton
   |
   v
Phase 2 --- TTY Migration ($C100 -> $D820) + Plug into Router
   |
   v
Phase 3 --- Frame Buffer Expansion (256B -> 4KB) + Separate Backing Store
   |         + GDB Callback Fix
   v
Phase 4 --- Video Control Registers ($D840) + Scroll Ops
   |
   v
Phase 5 --- Keyboard Registers ($D860) + SDL Key Mapping
   |
   v
Phase 6 --- ROM Layout ($D000 -> $E000) + Linker Config + ROM Protection
   |
   v
Phase 7 --- GDB Stub Memory Map XML
   |
   v
Phase 8 --- Firmware Source Migration (devices.inc, zp.inc)
   |
   v
Phase 9 --- Playground Migration (configs + equates)
   |
   v
Phase 10 -- End-to-End Validation + Legacy Cleanup
```

Phases 4 and 5 are independent of each other (both depend on Phase 3).
They could be parallelized but sequential is recommended for clean test
progression.

Phase 6 depends on all device phases (1-5) being complete so the dev
bank and device register space are fully understood.

Phase 8 depends on ALL emulator-side phases (0-7) because firmware
must use all new addresses simultaneously.
