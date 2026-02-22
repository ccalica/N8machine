# Spec A (Revised): Bus Architecture Migration & Device Implementation

> Migration from current memory map to proposed layout, plus video and keyboard device implementation.
> Incremental phases -- each independently testable.

---

## Reconciliation Notes

This revision incorporates insights from all three specs (A, B, C) and resolves conflicts between them. Key changes from the original Spec A:

### frame_buffer[] vs mem[] (the biggest conflict)

**Original Spec A** proposed reintroducing a separate `frame_buffer[]` array in Phase 3. **Spec B** also wanted a separate backing store with `fb_dirty` tracking. **Spec C** argued for deferring separation and keeping `mem[]` as the backing store.

**Decision: Keep using `mem[]` for Phase 3. Introduce separate backing store later (Phase 5, alongside rendering pipeline).**

Rationale: The user explicitly said "remove frame_buffer[] and just use mem[]." The hardware.md spec says "routed to a separate backing store (not backed by main RAM)" but that is a target-state aspiration, not a Phase 3 requirement. The immediate goal of Phase 3 is address range expansion ($C000-$CFFF), which works fine with `mem[]` since the 64KB array already covers the range. Separating the backing store is only needed when the rendering pipeline arrives (Phase 5) -- at that point, dirty tracking and bank-switching isolation become relevant. This matches Spec C's pragmatism while preserving Spec A/B's architectural intent for the right phase.

### Constants header naming

- **Spec A** proposed `src/n8_memory_map.h` (new file, `#define` macros with `N8_` prefix)
- **Spec B** proposed expanding the existing `src/machine.h` (C++ `static const` variables, no prefix)
- **Spec C** proposed `src/hw_regs.h` (new file, `#define` macros with `HW_` prefix) plus a parallel `firmware/hw_regs.inc`

**Decision: Expand `src/machine.h` (Spec B's approach) but use `#define` macros (Spec A/C's approach).**

Rationale: `machine.h` already exists in the codebase and is already `#include`d in `emulator.cpp`. Expanding it avoids adding a new file and a new `#include`. However, `#define` macros are preferred over `static const` because: (a) they work in preprocessor expressions and `switch` labels, (b) they match the existing `#define BUS_DECODE` pattern in `emulator.cpp`, and (c) `static const uint16_t` in a header generates per-TU storage on some compilers. Use `N8_` prefix for clarity (Spec A) since the codebase may eventually include third-party headers. Also adopt Spec C's idea of a parallel `firmware/hw_regs.inc` for cc65 assembly.

### Phase ordering

- **Spec A** had 10 phases: Constants, IRQ, TTY, FB expand, Video regs, Rendering, Keyboard, ROM layout, Firmware, GDB.
- **Spec B** had 12 phases, interleaving ROM migration (Phase 4) before video/keyboard.
- **Spec C** had 11 phases (10 + E2E), with ROM split at Phase 6, firmware at Phase 8-9.

**Decision: Adopt a hybrid ordering based on Spec C's risk-minimization principle with Spec B's earlier ROM migration.**

The key insight from Spec B is that ROM migration (moving to $E000) should happen *before* video/keyboard implementation, because the device register space at $D800-$DFFF overlaps the current ROM region ($D000-$FFFF). Until ROM is moved, loading firmware writes padding bytes into the device register address range, which is confusing and could mask bus decode bugs. Moving ROM early (Phase 4) makes the device space cleanly available for Phases 5-6.

Revised order: Constants (0) -> IRQ move (1) -> TTY move (2) -> FB expand (3) -> ROM migration (4) -> Video regs (5) -> Keyboard (6) -> Rendering pipeline (7) -> Firmware migration (8) -> GDB + Playground (9).

### Device router architecture

**Spec A** proposed a slot-based dispatch router with `switch (slot)`. **Spec B** used individual `BUS_DECODE` macros per device. **Spec C** also used individual `BUS_DECODE` macros.

**Decision: Use individual `BUS_DECODE` macros (Spec B/C approach), not a router.**

Rationale: The `BUS_DECODE` macro is the established pattern in the codebase. A router adds indirection (compute slot index, switch on it) for negligible benefit -- there are only 4 devices. The `BUS_DECODE` pattern is also more readable: each device's address is visible at the decode site. If we later have 10+ devices, a router can be introduced as a refactoring.

However, Spec A's insight about a unified device space check is valuable for the "reserved space returns $00" behavior. Adopt Spec B's Phase 10 approach: wrap all device decodes in a single `if (addr >= DEV_BASE && addr <= DEV_END)` guard, with individual `BUS_DECODE` or offset checks inside.

### Test count and strategy

- **Spec A** had ~30 new test cases across all phases, loosely numbered.
- **Spec B** had ~35 test case descriptions, referenced by T_XXX_NN naming.
- **Spec C** had 52 new automated tests with a precise running total (221 -> 273), specific test IDs (T110-T184), and a full test registry.

**Decision: Adopt Spec C's test registry approach with precise IDs and running totals.** Spec C's discipline of counting tests per phase and tracking the running total is the right way to ensure nothing falls through the cracks. Adopted the T110+ numbering scheme to avoid collisions with existing tests (T01-T101a).

### ROM layout -- split vs. flat

- **Spec A** proposed a flat 8KB ROM at $E000 for Phase 7, with the cc65 linker handling segment placement.
- **Spec C** proposed a split ROM with four MEMORY regions (ROM_MON, ROM_IMPL, ROM_ENTRY, ROM_VEC) in the cc65 linker config.

**Decision: Keep a single 8KB ROM region in the linker config (Spec A/B approach).**

Rationale: The cc65 SEGMENTS directive controls code placement within a MEMORY region. Having one `ROM: start=$E000, size=$2000` region with multiple segments (STARTUP, CODE, RODATA, VECTORS) is simpler and achieves the same layout. Spec C's four-region split adds complexity to the linker config without benefit -- the 6502 does not have MMU protection, so there is no hardware reason to separate the regions. The logical separation (Monitor, Kernel, Vectors) is handled by segment placement within the single ROM region.

### fb_dirty flag

**Spec B** introduced `fb_dirty` tracking on frame buffer writes. **Spec A** did not have this in the bus decode phase.

**Decision: Introduce `fb_dirty` in Phase 7 (rendering pipeline), not in Phase 3.**

The dirty flag is a rendering optimization. It has no meaning without a rendering pipeline. Adding it in Phase 3 (bus decode) would be premature -- it adds code with no consumer. When the rendering pipeline arrives in Phase 7, the backing store separation and dirty tracking are introduced together.

### Legacy compatibility in ROM loader

**Spec C** proposed a dual-path ROM loader (detect 8KB vs 12KB binary, load at appropriate address). **Spec A** proposed always loading at $D000.

**Decision: Adopt Spec C's dual-path approach for the transition period.**

The emulator must remain functional between Phase 4 (ROM migration) and Phase 8 (firmware rebuild). During this window, the firmware binary is still 12KB. A size-based heuristic (>8KB -> load at $D000, <=8KB -> load at $E000) ensures backward compatibility. The legacy path is removed in the final cleanup phase.

### Scroll fill character

**Spec A** filled cleared rows/columns with `0x20` (space). **Spec B** filled with `0x00`. The hardware.md spec does not specify.

**Decision: Fill with `0x00` (Spec B).** In a character-mapped display, `0x00` is the null/blank character in the font. Using `0x20` (ASCII space) assumes ASCII encoding, which may not hold for all 256 characters in the N8 font. `0x00` is the safer default.

### GDB callbacks for frame buffer

**Spec A** proposed updating `gdb_read_mem()`/`gdb_write_mem()` in Phase 3 to redirect $C000-$CFFF to `frame_buffer[]`. Since we are deferring the separate backing store, this is not needed in Phase 3. When the backing store is introduced (Phase 7), the GDB callbacks will be updated then.

### Spec C's test-first principle

Spec C advocates writing tests before the code that makes them pass. This is good discipline but impractical for specs (tests need to compile against the code structure). Adopted the principle as: **each phase must define all test cases before implementation begins**, and tests are committed alongside (or before) the production code.

### Spec C's parallel firmware constants file

Spec C proposed `firmware/hw_regs.inc` as a parallel to the C++ header, so firmware assembly can use the same symbolic names. This is a strong idea -- it ensures firmware and emulator stay in sync. **Adopted for Phase 0.**

### Spec B's shifted-symbol key translation

Spec B included a complete shifted-symbol lookup table for the keyboard (!, @, #, etc.). Spec A acknowledged this as "simplified" and deferred it. **Adopted Spec B's complete table** for the keyboard phase -- it is straightforward and avoids a known-incomplete implementation.

### Spec B's font texture atlas

Spec B proposed a 128x256 monochrome texture atlas (GL_RED) rather than Spec A's per-frame CPU rasterization to a 640x400 RGBA buffer. Both approaches work. **Adopted Spec A's CPU-side rasterization** because it avoids shader programming and works with ImGui's texture model directly. The performance difference is negligible at these sizes.

### ROM write protection

Spec B proposed adding write protection for ROM ($E000+). Spec A deferred it. **Deferred.** The current codebase has no ROM protection, and adding it is a separate concern. Tests that load code directly into `mem[]` at ROM addresses would need updating. Not worth the risk for this migration.

---

## Table of Contents

- [Current State Summary](#current-state-summary)
- [Target State Summary](#target-state-summary)
- [Phase 0: Machine Constants Header](#phase-0-machine-constants-header)
- [Phase 1: IRQ Register Migration ($00FF to $D800)](#phase-1-irq-register-migration-00ff-to-d800)
- [Phase 2: TTY Device Migration ($C100 to $D820)](#phase-2-tty-device-migration-c100-to-d820)
- [Phase 3: Frame Buffer Expansion (256B to 4KB)](#phase-3-frame-buffer-expansion-256b-to-4kb)
- [Phase 4: ROM Base Migration ($D000 to $E000)](#phase-4-rom-base-migration-d000-to-e000)
- [Phase 5: Video Control Registers ($D840)](#phase-5-video-control-registers-d840)
- [Phase 6: Keyboard Device ($D860)](#phase-6-keyboard-device-d860)
- [Phase 7: Video Rendering Pipeline](#phase-7-video-rendering-pipeline)
- [Phase 8: Firmware Migration](#phase-8-firmware-migration)
- [Phase 9: GDB Stub, Playground, and Cleanup](#phase-9-gdb-stub-playground-and-cleanup)
- [Risk Assessment & Rollback Strategy](#risk-assessment--rollback-strategy)
- [Dependency Graph](#dependency-graph)
- [Test Case Registry](#test-case-registry)

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

### Bus Decode Logic (`emulator.cpp:140-153`)

```cpp
BUS_DECODE(addr, 0x0000, 0xFF00) { /* ZP monitor */ }
BUS_DECODE(addr, 0xFFF0, 0xFFF0) { /* Vector access logging */ }
BUS_DECODE(addr, 0xC100, 0xFFF0) { /* TTY: 16-byte window */ }
```

The frame buffer at `$C000` has **no bus decode logic** -- reads/writes go directly to `mem[]`. There is no read-only enforcement on ROM.

### IRQ Mechanism (`emulator.cpp:23-24, 110-128`)

```cpp
#define IRQ_CLR() mem[0x00FF] = 0x00;
#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)
```

Every tick: `IRQ_CLR()` -> devices reassert -> check `mem[0x00FF]` -> set/clear M6502_IRQ pin. Firmware-readable via `LDA $FF` (zero page addressing).

### TTY Device (`emu_tty.cpp`)

Four registers decoded at offsets 0-3 from base `$C100`. Uses IRQ bit 1. Input buffered in `std::queue<uint8_t>`.

### Current Test Coverage

| Suite | File | Tests |
|-------|------|-------|
| bus | `test_bus.cpp` | T62-T67 (RAM, frame buffer read/write) |
| tty | `test_tty.cpp` | T71-T79 (register read/write, IRQ, reset) |
| utils | `test_utils.cpp` | T69-T70a (IRQ set/clear) |
| integration | `test_integration.cpp` | T95-T101a (boot, BP, IRQ) |
| gdb | `test_gdb_protocol.cpp`, `test_gdb_callbacks.cpp` | GDB RSP protocol + emulator callbacks |

**Baseline:** 221 tests passing, 0 failures.

---

## Target State Summary

### Memory Map (proposed)

```
$FFE0-$FFFF  Vectors + Bank Switch (32 bytes)
$FE00-$FFDF  Kernel Entry (480 bytes)
$F000-$FDFF  Kernel Implementation (3.5 KB)
$E000-$EFFF  Monitor / stdlib (4 KB)
$D800-$DFFF  Device Registers (2 KB, 32-byte device slots)
$D000-$D7FF  Dev Bank (2 KB)
$C000-$CFFF  Frame Buffer (4 KB)
$0400-$BFFF  Program RAM (47 KB)
$0200-$03FF  Reserved (512 bytes)
$0100-$01FF  Hardware Stack
$0000-$00FF  Zero Page (IRQ flags REMOVED from here)
```

### Device Register Slots at `$D800-$DFFF`

| Base | Device | Registers |
|------|--------|-----------|
| `$D800` | System/IRQ | 1 (IRQ_FLAGS) |
| `$D820` | TTY | 4 (OUT_STATUS, OUT_DATA, IN_STATUS, IN_DATA) |
| `$D840` | Video Control | 8 (MODE, WIDTH, HEIGHT, STRIDE, OPER, CURSOR, CURCOL, CURROW) |
| `$D860` | Keyboard | 3 (KBD_DATA, KBD_STATUS/ACK, KBD_CTRL) |

---

## Phase 0: Machine Constants Header

**Goal:** Centralize all memory map addresses and bus decode masks, eliminating magic numbers scattered across source files. Every subsequent phase uses these constants.

**Entry criteria:** Current `make test` passes (221 tests, 0 failures).
**Exit criteria:** All tests still pass. No behavior changes. New constants files exist and compile cleanly.

### Expand existing file: `src/machine.h`

The file currently contains only `const int total_memory = 65536;`. Expand it:

```cpp
//#pragma once

//////////////////////////////////////
//
// Machine config
const int total_memory = 65536;

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
#define N8_ROM_BASE        0xD000   // Phase 0: current location
#define N8_ROM_SIZE        0x3000   // 12KB

// --- Vectors ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE
```

### New file: `firmware/hw_regs.inc` (assembly-language parallel)

```asm
; hw_regs.inc -- Device register addresses for cc65 firmware
; Mirror of src/machine.h constants
; Used by firmware source files after Phase 8 migration

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

**`src/emulator.cpp`:**
- Already includes `machine.h`. No new `#include` needed.
- Replace `mem[0x00FF]` with `mem[N8_IRQ_ADDR]` in IRQ macros and `emu_set_irq()`/`emu_clr_irq()`
- Replace `0xD000` in `emulator_loadrom()` with `N8_ROM_BASE`
- Replace `0xC100` / `0xFFF0` in `BUS_DECODE` with `N8_TTY_BASE` / `N8_TTY_MASK`
- Replace hardcoded `0xC100` in TTY register offset calc with `N8_TTY_BASE`
- Replace `mem[0x00FF]` in `emulator_show_status_window()` with `mem[N8_IRQ_ADDR]`

**`src/emu_tty.cpp`:**
- `#include "machine.h"`
- Replace `emu_set_irq(1)` with `emu_set_irq(N8_IRQ_BIT_TTY)`
- Replace `emu_clr_irq(1)` with `emu_clr_irq(N8_IRQ_BIT_TTY)`

**`test/test_helpers.h`:**
- `#include "machine.h"` (for constants in test assertions)

**`test/test_utils.cpp`:**
- Replace `mem[0x00FF]` references with `mem[N8_IRQ_ADDR]`

**`test/test_tty.cpp`:**
- Replace `mem[0x00FF]` references with `mem[N8_IRQ_ADDR]`
- Replace `0xC100`-`0xC103` pin addresses with `N8_TTY_BASE + offset`

**`test/test_bus.cpp`:**
- Replace `0xC000` / `0xC0FF` / `0xC005` with `N8_FB_BASE + offset`
- Replace `0xD000` with `N8_ROM_BASE`

**`test/test_integration.cpp`:**
- Replace `0xD000` with `N8_ROM_BASE`

### Tests

No new tests -- this is a pure refactor. `make test` must still produce 221 passed, 0 failed.

### Verification

```bash
# After refactor, no raw hex addresses should remain for mapped regions:
grep -rn '0x00FF\|0xC100' src/emulator.cpp src/emu_tty.cpp
# Should return zero hits (except comments)
```

### Phase Gate

- `make` compiles cleanly.
- `make test` still produces: **221 passed, 0 failed**.

---

## Phase 1: IRQ Register Migration ($00FF to $D800)

**Goal:** Move IRQ flags from zero page `$00FF` to device register space at `$D800`. Firmware-visible change: `LDA $FF` becomes `LDA $D800`.

**Entry criteria:** Phase 0 complete, all tests pass.
**Exit criteria:** IRQ flags live at `$D800`. Old address `$00FF` is ordinary RAM. All tests updated and passing.

### Constants Update: `src/machine.h`

```cpp
// --- Device Register Space ---
#define N8_DEV_BASE        0xD800
#define N8_DEV_END         0xDFFF
#define N8_DEV_SIZE        0x0800   // 2KB total

// --- System / IRQ ---
#define N8_IRQ_ADDR        0xD800   // Replaces 0x00FF
```

### Bus Decode Design

Add a `BUS_DECODE` block for `$D800` in `emulator_step()`. Since IRQ_FLAGS at `$D800` is already backed by `mem[]` (the generic RAM handler writes to `mem[$D800]` first, then the device decode can override reads), the IRQ device decode only needs to ensure reads return the correct value.

In the current code architecture, `$D800` falls inside the ROM region (`$D000-$FFFF`). The generic handler reads/writes `mem[addr]` first, then device decode overrides the data bus for reads. This means the `IRQ_CLR()` and `IRQ_SET()` macros writing to `mem[$D800]` work naturally -- the value lands in `mem[]` which is the same backing store.

```cpp
// In emulator_step(), after existing BUS_DECODE blocks:
BUS_DECODE(addr, N8_IRQ_ADDR, 0xFFFF) {
    // IRQ_FLAGS at $D800: R/W passthrough to mem[]
    // Already handled by generic mem[] read/write above.
    // This decode block exists for documentation and future
    // side-effect logic.
}
```

### Code Changes

**`src/emulator.cpp`:**

1. Update IRQ macros (already using `N8_IRQ_ADDR` from Phase 0, value changes to `0xD800`):
   ```cpp
   #define IRQ_CLR() mem[N8_IRQ_ADDR] = 0x00;
   #define IRQ_SET(bit) mem[N8_IRQ_ADDR] = (mem[N8_IRQ_ADDR] | (0x01 << bit))
   ```

2. `emu_clr_irq()` already uses `N8_IRQ_ADDR`.

3. `emulator_step()`: `if(mem[N8_IRQ_ADDR] == 0)` -- already uses constant.

4. `emulator_show_status_window()`: already updated in Phase 0.

5. Add `BUS_DECODE` block for `$D800`.

**`src/machine.h`:** Change `N8_IRQ_ADDR` from `0x00FF` to `0xD800`.

### Test Changes

**`test/test_utils.cpp`:**
- T69, T69a, T69b, T70, T70a: all references to `mem[N8_IRQ_ADDR]` now point to `$D800` (no code change, constant changed).

**`test/test_tty.cpp`:**
- T76 checks `(mem[N8_IRQ_ADDR] & 0x02) == 0` -- works at new address.
- All `mem[N8_IRQ_ADDR] = 0` setup lines work at new address.

**New tests in `test/test_bus.cpp`:**

| Test ID | Description |
|---------|-------------|
| T110 | Write to `$D800` stores value, read returns it |
| T111 | IRQ_FLAGS cleared every tick (`IRQ_CLR()` behavior) |
| T112 | IRQ line asserted when IRQ_FLAGS non-zero after tick |
| T113 | Old address `$00FF` no longer acts as IRQ register |

### Phase Gate

- All 221 existing tests pass (with updated address constant).
- T110-T113 (4 new tests) pass.
- **Total: 225 tests.**

### Risks

- **Firmware breakage:** The real firmware references `ZP_IRQ $FF`. After this phase, firmware reads/writes the wrong address. Acceptable -- firmware migration is Phase 8, and during Phases 1-7 tests use inline programs.
- **GDB stub memory map XML** still lists old layout. Updated in Phase 9.

---

## Phase 2: TTY Device Migration ($C100 to $D820)

**Goal:** Move TTY device registers from `$C100` to `$D820`.

**Entry criteria:** Phase 1 complete, all tests pass.
**Exit criteria:** TTY registers decode at `$D820-$D83F`. Old address `$C100` is ordinary RAM. All tests updated and passing.

### Constants Update: `src/machine.h`

```cpp
// --- TTY Device ---
#define N8_TTY_BASE        0xD820   // Was 0xC100
#define N8_TTY_MASK        0xFFE0   // 32-byte decode window (was 0xFFF0)
```

### Code Changes

**`src/emulator.cpp`:**

1. Replace the TTY `BUS_DECODE` block:
   ```cpp
   // Old:
   BUS_DECODE(addr, 0xC100, 0xFFF0) {
       const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
       tty_decode(pins, dev_reg);
   }
   // New:
   BUS_DECODE(addr, N8_TTY_BASE, N8_TTY_MASK) {
       const uint8_t dev_reg = (addr - N8_TTY_BASE) & 0x1F;
       tty_decode(pins, dev_reg);
   }
   ```

   The mask `0xFFE0` decodes the top 11 bits, giving a 32-byte window at `$D820`. TTY uses offsets 0-3; offsets 4-31 return `$00` on read (already handled by `tty_decode` default case).

**`src/emu_tty.cpp`:** No changes. `tty_decode()` takes a relative register offset (0-3) and is address-agnostic.

### Test Changes

**`test/test_tty.cpp`:**
- All `make_read_pins(0xC1xx)` / `make_write_pins(0xC1xx, ...)` update to use `N8_TTY_BASE + offset`.
- T78a (phantom register test): adjust range to offsets 4-31 (32-byte window).

**`test/test_gdb_callbacks.cpp`:**
- `mem[0xC100]` references become `mem[N8_TTY_BASE]`.
- `make_read_pins(0xC103)` becomes `make_read_pins(N8_TTY_BASE + 3)`.

**New tests in `test/test_bus.cpp`:**

| Test ID | Description |
|---------|-------------|
| T114 | TTY read at old address `$C100` does NOT trigger tty_decode |
| T115 | TTY read at `$D820` returns OUT_STATUS ($00) |
| T116 | TTY write at `$D821` sends character (putchar side effect) |

### Phase Gate

- All prior 225 tests pass (with updated addresses).
- T114-T116 (3 new tests) pass.
- **Total: 228 tests.**

### Risks

- Low risk. TTY decode is self-contained and address-agnostic. Only the bus decode routing changes.

---

## Phase 3: Frame Buffer Expansion (256B to 4KB)

**Goal:** Expand frame buffer from 256 bytes at `$C000-$C0FF` to 4KB at `$C000-$CFFF`. Continue using `mem[]` as the backing store.

**Entry criteria:** Phase 2 complete, all tests pass.
**Exit criteria:** Frame buffer addresses `$C000-$CFFF` are all accessible. Old TTY address `$C100` is now frame buffer space. All tests updated and passing.

### Design Decision: Keep using mem[]

The hardware.md spec says "routed to a separate backing store (not backed by main RAM)." However, the user explicitly directed removing `frame_buffer[]` in favor of `mem[]`. A separate backing store will be introduced later in Phase 7 when the rendering pipeline needs dirty tracking and the bus decode structure is refactored for that purpose.

For this phase, the expansion is purely a matter of ensuring no other bus decode claims `$C100-$CFFF`. Since TTY moved to `$D820` in Phase 2, the old `$C100` decode is gone. The generic `mem[]` handler covers the entire `$C000-$CFFF` range naturally since `mem[]` is 64KB.

### Constants Update: `src/machine.h`

```cpp
// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4KB (was 0x0100)
#define N8_FB_END          0xCFFF
```

### Code Changes

**No emulator code changes required for this phase.** The generic `mem[]` handler already covers `$C000-$CFFF`. The TTY decode at `$C100` was removed in Phase 2, so there is no conflict.

### Test Changes

**`test/test_bus.cpp`:** Existing tests (T64-T67) continue to work since they access `$C000-$C0FF` which is still valid.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T117 | Write to `$C100` stores in mem (no longer TTY) |
| T118 | Write to `$C7CF` (end of 80x25 active area) stores correctly |
| T119 | Write to `$CFFF` (last byte of 4KB FB) stores correctly |
| T120 | Read from `$C500` returns previously written value |

### Phase Gate

- All prior 228 tests pass.
- T117-T120 (4 new tests) pass.
- **Total: 232 tests.**

### Risks

- Very low. No emulator code changed. Only new tests added to validate the expanded address range.

---

## Phase 4: ROM Base Migration ($D000 to $E000)

**Goal:** Move ROM from `$D000-$FFFF` (12KB) to `$E000-$FFFF` (8KB), freeing `$D000-$DFFF` for dev bank and device registers.

**Entry criteria:** Phase 3 complete, all tests pass.
**Exit criteria:** ROM loads at `$E000`. Dev bank `$D000-$D7FF` is writable RAM. All tests passing.

**Rationale for doing this before video/keyboard:** The device register space at `$D800-$DFFF` overlaps the current ROM region. Until ROM is moved, loading firmware writes padding bytes into the device register address range. Moving ROM early makes the device space cleanly available for subsequent phases.

### Constants Update: `src/machine.h`

```cpp
// --- ROM ---
#define N8_ROM_BASE        0xE000   // Was 0xD000
#define N8_ROM_SIZE        0x2000   // 8KB (was 0x3000)
#define N8_ROM_END         0xFFFF

// --- Dev Bank ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_END     0xD7FF
#define N8_DEVBANK_SIZE    0x0800   // 2KB
```

### Code Changes

**`src/emulator.cpp` -- `emulator_loadrom()`:**

Dual-path loader for backward compatibility during the transition:

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

    // Determine load address based on binary size
    uint16_t load_addr;
    if (size > N8_ROM_SIZE) {
        load_addr = 0xD000;  // Legacy 12KB layout
    } else {
        load_addr = N8_ROM_BASE;  // New 8KB layout at $E000
    }

    printf("Loading ROM at $%04X (%ld bytes)\r\n", load_addr, size);
    fflush(stdout);

    uint16_t rom_ptr = load_addr;
    while (1) {
        uint8_t c = fgetc(fp);
        if (feof(fp)) break;
        mem[rom_ptr] = c;
        rom_ptr++;
    }
    fclose(fp);
}
```

### Test Changes

Existing tests that `load_at(0xD000, ...)` via EmulatorFixture still work because `$D000` is now dev bank (writable RAM) and the CPU executes from it identically. No existing test changes needed.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T170 | Code at `$E000` is executable (NOP sled, PC advances) |
| T171 | Reset vector at `$FFFC-$FFFD` still works |
| T172 | RAM at `$0400` is writable |
| T173 | Dev Bank `$D000-$D7FF` is writable (LDA #$AA; STA $D000 round-trips) |
| T174 | Legacy loadrom: >8KB binary loads at `$D000` (transition support) |

### Phase Gate

- All prior 232 tests pass.
- T170-T174 (5 new tests) pass.
- **Total: 237 tests.**

### Risks

- **Firmware breakage:** The existing 12KB firmware binary continues to load via the legacy path. Until `make firmware` is updated (Phase 8), the emulator auto-detects the old binary.
- **Dev Bank vs ROM:** `$D000-$D7FF` is loaded from the ROM file but treated as writable RAM. Harmless -- those bytes are padding/zeros from the linker.

---

## Phase 5: Video Control Registers ($D840)

**Goal:** Implement the video control register block at `$D840-$D847`. No rendering yet -- just registers as readable/writable device state, plus scroll operations on `mem[]` frame buffer.

**Entry criteria:** Phase 4 complete, all tests pass.
**Exit criteria:** Video registers at `$D840-$D847` are bus-accessible. Mode write auto-sets defaults. Scroll operations execute on `mem[]` frame buffer. All tests passing.

### Constants Update: `src/machine.h`

```cpp
// --- Video Control ---
#define N8_VID_BASE        0xD840
#define N8_VID_REG_MODE    0x00     // Register offsets within device
#define N8_VID_REG_WIDTH   0x01
#define N8_VID_REG_HEIGHT  0x02
#define N8_VID_REG_STRIDE  0x03
#define N8_VID_REG_OPER    0x04
#define N8_VID_REG_CURSOR  0x05
#define N8_VID_REG_CURCOL  0x06
#define N8_VID_REG_CURROW  0x07

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
#define N8_VID_DEFAULT_STRIDE   80

// Font
#define N8_FONT_WIDTH   8
#define N8_FONT_HEIGHT  16
#define N8_FONT_CHARS   256
```

### New File: `src/emu_video.h`

```cpp
#pragma once
#include <cstdint>

void video_init();
void video_reset();
void video_decode(uint64_t &pins, uint8_t reg);

// State accessors for rendering pipeline (Phase 7)
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
```

### New File: `src/emu_video.cpp`

Internal state: an 8-byte register file. Scroll operations work directly on `mem[N8_FB_BASE..]`.

```cpp
#include "emu_video.h"
#include "emulator.h"
#include "machine.h"
#include "m6502.h"
#include <cstring>

extern uint8_t mem[];

static uint8_t vid_regs[8] = { 0 };

void video_init() {
    video_reset();
}

void video_reset() {
    memset(vid_regs, 0, sizeof(vid_regs));
    vid_regs[N8_VID_REG_MODE]   = N8_VIDMODE_TEXT_DEFAULT;
    vid_regs[N8_VID_REG_WIDTH]  = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_REG_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
    vid_regs[N8_VID_REG_STRIDE] = N8_VID_DEFAULT_STRIDE;
    vid_regs[N8_VID_REG_CURSOR] = 0x00;  // Cursor off
    vid_regs[N8_VID_REG_CURCOL] = 0;
    vid_regs[N8_VID_REG_CURROW] = 0;
}

static void video_apply_mode(uint8_t mode) {
    if (mode == N8_VIDMODE_TEXT_DEFAULT) {
        vid_regs[N8_VID_REG_WIDTH]  = N8_VID_DEFAULT_WIDTH;
        vid_regs[N8_VID_REG_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
        vid_regs[N8_VID_REG_STRIDE] = N8_VID_DEFAULT_STRIDE;
    }
    // N8_VIDMODE_TEXT_CUSTOM: don't auto-set, user provides dimensions
}

// Guard: clamp stride*height to FB_SIZE to prevent overrun
static int safe_rows() {
    int s = vid_regs[N8_VID_REG_STRIDE];
    int h = vid_regs[N8_VID_REG_HEIGHT];
    if (s == 0) return 0;
    int max_rows = N8_FB_SIZE / s;
    return (h < max_rows) ? h : max_rows;
}

static void video_scroll_up() {
    int w = vid_regs[N8_VID_REG_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    uint8_t *fb = &mem[N8_FB_BASE];
    for (int row = 0; row < h - 1; row++) {
        memcpy(fb + row * w, fb + (row + 1) * w, w);
    }
    memset(fb + (h - 1) * w, 0x00, w);
}

static void video_scroll_down() {
    int w = vid_regs[N8_VID_REG_STRIDE];
    int h = safe_rows();
    if (h < 2 || w == 0) return;
    uint8_t *fb = &mem[N8_FB_BASE];
    for (int row = h - 1; row > 0; row--) {
        memcpy(fb + row * w, fb + (row - 1) * w, w);
    }
    memset(fb, 0x00, w);
}

static void video_scroll_left() {
    int w = vid_regs[N8_VID_REG_WIDTH];
    int s = vid_regs[N8_VID_REG_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0) return;
    uint8_t *fb = &mem[N8_FB_BASE];
    for (int row = 0; row < h; row++) {
        uint8_t *line = fb + row * s;
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x00;
    }
}

static void video_scroll_right() {
    int w = vid_regs[N8_VID_REG_WIDTH];
    int s = vid_regs[N8_VID_REG_STRIDE];
    int h = safe_rows();
    if (w < 2 || s == 0) return;
    uint8_t *fb = &mem[N8_FB_BASE];
    for (int row = 0; row < h; row++) {
        uint8_t *line = fb + row * s;
        memmove(line + 1, line, w - 1);
        line[0] = 0x00;
    }
}

void video_decode(uint64_t &pins, uint8_t reg) {
    if (reg > 7) {
        // Phantom registers: read 0, write no-op
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, 0x00);
        }
        return;
    }

    if (pins & M6502_RW) {
        // Read
        if (reg == N8_VID_REG_OPER) {
            M6502_SET_DATA(pins, 0x00);  // Write-only, reads 0
        } else {
            M6502_SET_DATA(pins, vid_regs[reg]);
        }
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_VID_REG_MODE:
                vid_regs[reg] = val;
                video_apply_mode(val);
                break;
            case N8_VID_REG_OPER:
                // Write-once trigger, does not latch
                switch (val) {
                    case N8_VIDOP_SCROLL_UP:    video_scroll_up(); break;
                    case N8_VIDOP_SCROLL_DOWN:  video_scroll_down(); break;
                    case N8_VIDOP_SCROLL_LEFT:  video_scroll_left(); break;
                    case N8_VIDOP_SCROLL_RIGHT: video_scroll_right(); break;
                }
                break;
            default:
                vid_regs[reg] = val;
                break;
        }
    }
}

uint8_t video_get_mode()         { return vid_regs[N8_VID_REG_MODE]; }
uint8_t video_get_width()        { return vid_regs[N8_VID_REG_WIDTH]; }
uint8_t video_get_height()       { return vid_regs[N8_VID_REG_HEIGHT]; }
uint8_t video_get_stride()       { return vid_regs[N8_VID_REG_STRIDE]; }
uint8_t video_get_cursor_style() { return vid_regs[N8_VID_REG_CURSOR]; }
uint8_t video_get_cursor_col()   { return vid_regs[N8_VID_REG_CURCOL]; }
uint8_t video_get_cursor_row()   { return vid_regs[N8_VID_REG_CURROW]; }
```

### Integration: `src/emulator.cpp`

```cpp
#include "emu_video.h"

// In emulator_step(), add BUS_DECODE:
BUS_DECODE(addr, N8_VID_BASE, 0xFFE0) {
    const uint8_t dev_reg = (addr - N8_VID_BASE) & 0x1F;
    video_decode(pins, dev_reg);
}
```

Add `video_init()` to `emulator_init()`. Add `video_reset()` to `emulator_reset()`.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_video.cpp` to `SOURCES`. Add `$(BUILD_DIR)/emu_video.o` to `TEST_SRC_OBJS`.

### Test Cases

**New file: `test/test_video.cpp`**

| Test ID | Description |
|---------|-------------|
| T130 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | Read VID_MODE (`$D840`) returns 0x00 after reset |
| T132 | Write VID_MODE=0x00 auto-sets width=80, height=25, stride=80 |
| T133 | Write VID_MODE=0x01, registers retain prior values |
| T134 | Write/read VID_WIDTH |
| T135 | Write/read VID_HEIGHT |
| T136 | Write/read VID_STRIDE |
| T137 | VID_OPER=0x00 (scroll up): row 0 gets row 1 data, last row zeroed |
| T138 | VID_OPER=0x01 (scroll down): row 1 gets row 0 data, first row zeroed |
| T139 | Read VID_OPER returns 0 (write-only) |
| T140 | Write/read VID_CURSOR |
| T141 | Write/read VID_CURCOL |
| T142 | Write/read VID_CURROW |
| T143 | Phantom registers ($D848-$D85F) read 0x00, write no-op |
| T144 | VID_OPER=0x02 (scroll left): each row shifts left 1 |
| T145 | VID_OPER=0x03 (scroll right): each row shifts right 1 |

Tests T137-T138, T144-T145 use `EmulatorFixture`. Pre-fill `mem[N8_FB_BASE..]` with known patterns, trigger scroll via program (STA to $D844), verify `mem[]` contents.

### Phase Gate

- All prior 237 tests pass.
- T130-T145 (16 new tests) pass.
- **Total: 253 tests.**

### Risks

- **Scroll overrun:** `stride * height` could exceed `N8_FB_SIZE`. The `safe_rows()` guard clamps to buffer size.
- **Stride vs Width:** Scroll functions use stride for row spacing (vertical) and width for data movement (horizontal). This matches the hardware.md spec.

---

## Phase 6: Keyboard Device ($D860)

**Goal:** Implement keyboard input: SDL2 key events -> ASCII/extended code translation -> device registers at `$D860` -> optional IRQ bit 2.

**Entry criteria:** Phase 5 complete, all tests pass.
**Exit criteria:** Keyboard input works via `$D860-$D862` registers. IRQ bit 2 fires on keypress when enabled. All tests passing.

### Constants Update: `src/machine.h`

```cpp
// --- Keyboard ---
#define N8_KBD_BASE        0xD860
#define N8_KBD_REG_DATA    0x00     // Register offsets
#define N8_KBD_REG_STATUS  0x01
#define N8_KBD_REG_ACK     0x01     // Same address, write = ACK
#define N8_KBD_REG_CTRL    0x02

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

### New File: `src/emu_keyboard.h`

```cpp
#pragma once
#include <cstdint>

void keyboard_init();
void keyboard_reset();
void keyboard_decode(uint64_t &pins, uint8_t reg);

// Called from SDL event loop (main.cpp)
void keyboard_key_down(uint8_t keycode, uint8_t modifiers);
void keyboard_update_modifiers(uint8_t modifiers);

// For testing
void keyboard_inject_key(uint8_t keycode, uint8_t modifiers);
bool keyboard_data_available();
```

### New File: `src/emu_keyboard.cpp`

```cpp
#include "emu_keyboard.h"
#include "emulator.h"
#include "machine.h"
#include "m6502.h"

static uint8_t kbd_data = 0x00;
static uint8_t kbd_status = 0x00;   // Bits: CAPS|ALT|CTRL|SHIFT|OVERFLOW|AVAIL
static uint8_t kbd_ctrl = 0x00;     // Bit 0 = IRQ enable

void keyboard_init() {
    keyboard_reset();
}

void keyboard_reset() {
    kbd_data = 0x00;
    kbd_status = 0x00;
    kbd_ctrl = 0x00;
    emu_clr_irq(N8_IRQ_BIT_KBD);
}

void keyboard_key_down(uint8_t keycode, uint8_t modifiers) {
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

void keyboard_update_modifiers(uint8_t modifiers) {
    kbd_status = (kbd_status & 0x03) | (modifiers & 0x3C);
}

void keyboard_inject_key(uint8_t keycode, uint8_t modifiers) {
    keyboard_key_down(keycode, modifiers);
}

bool keyboard_data_available() {
    return (kbd_status & N8_KBD_STAT_AVAIL) != 0;
}

void keyboard_decode(uint64_t &pins, uint8_t reg) {
    if (pins & M6502_RW) {
        // Read
        uint8_t val = 0x00;
        switch (reg) {
            case N8_KBD_REG_DATA:   val = kbd_data; break;
            case N8_KBD_REG_STATUS: val = kbd_status; break;
            case N8_KBD_REG_CTRL:   val = kbd_ctrl; break;
            default:                val = 0x00; break;
        }
        M6502_SET_DATA(pins, val);
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_KBD_REG_ACK:
                kbd_status &= ~(N8_KBD_STAT_AVAIL | N8_KBD_STAT_OVERFLOW);
                emu_clr_irq(N8_IRQ_BIT_KBD);
                break;
            case N8_KBD_REG_CTRL:
                kbd_ctrl = val & N8_KBD_CTRL_IRQ_EN;
                break;
            default:
                break;  // KBD_DATA is read-only
        }
    }
}
```

### SDL2 Key Translation (in `main.cpp`)

```cpp
#include "emu_keyboard.h"

// In SDL event polling loop:
if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods = sdl_to_n8_modifiers(SDL_GetModState());
    if (n8_key != 0) {
        keyboard_key_down(n8_key, mods);
    }
    keyboard_update_modifiers(mods);
}
```

Key translation function includes complete shifted-symbol table (adopted from Spec B):

```cpp
static uint8_t sdl_to_n8_keycode(SDL_Keysym keysym) {
    SDL_Keycode k = keysym.sym;
    uint16_t mod = keysym.mod;

    // Ctrl+letter -> control character ($01-$1A)
    if ((mod & KMOD_CTRL) && k >= SDLK_a && k <= SDLK_z) {
        return (uint8_t)(k - SDLK_a + 1);
    }

    // Printable ASCII range
    if (k >= SDLK_SPACE && k <= SDLK_z) {
        char c = (char)k;
        if (k >= SDLK_a && k <= SDLK_z && (mod & KMOD_SHIFT)) {
            c -= 32;  // a->A
        }
        // Shifted number row and symbols
        if (mod & KMOD_SHIFT) {
            switch (k) {
                case SDLK_1: return '!';   case SDLK_2: return '@';
                case SDLK_3: return '#';   case SDLK_4: return '$';
                case SDLK_5: return '%';   case SDLK_6: return '^';
                case SDLK_7: return '&';   case SDLK_8: return '*';
                case SDLK_9: return '(';   case SDLK_0: return ')';
                case SDLK_MINUS:      return '_';
                case SDLK_EQUALS:     return '+';
                case SDLK_LEFTBRACKET:  return '{';
                case SDLK_RIGHTBRACKET: return '}';
                case SDLK_BACKSLASH:  return '|';
                case SDLK_SEMICOLON:  return ':';
                case SDLK_QUOTE:      return '"';
                case SDLK_COMMA:      return '<';
                case SDLK_PERIOD:     return '>';
                case SDLK_SLASH:      return '?';
                case SDLK_BACKQUOTE:  return '~';
                default: break;
            }
        }
        return (uint8_t)c;
    }

    // Special keys
    switch (k) {
        case SDLK_RETURN:    return 0x0D;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB:       return 0x09;
        case SDLK_ESCAPE:    return 0x1B;

        // Extended range ($80+)
        case SDLK_UP:    return 0x80;  case SDLK_DOWN:  return 0x81;
        case SDLK_LEFT:  return 0x82;  case SDLK_RIGHT: return 0x83;
        case SDLK_HOME:  return 0x84;  case SDLK_END:   return 0x85;
        case SDLK_PAGEUP: return 0x86; case SDLK_PAGEDOWN: return 0x87;
        case SDLK_INSERT: return 0x88; case SDLK_DELETE: return 0x89;
        case SDLK_PRINTSCREEN: return 0x8A;
        case SDLK_PAUSE: return 0x8B;

        case SDLK_F1:  return 0x90;  case SDLK_F2:  return 0x91;
        case SDLK_F3:  return 0x92;  case SDLK_F4:  return 0x93;
        case SDLK_F5:  return 0x94;  case SDLK_F6:  return 0x95;
        case SDLK_F7:  return 0x96;  case SDLK_F8:  return 0x97;
        case SDLK_F9:  return 0x98;  case SDLK_F10: return 0x99;
        case SDLK_F11: return 0x9A;  case SDLK_F12: return 0x9B;

        default: return 0;
    }
}

static uint8_t sdl_to_n8_modifiers(uint16_t sdl_mod) {
    uint8_t m = 0;
    if (sdl_mod & KMOD_SHIFT) m |= N8_KBD_STAT_SHIFT;
    if (sdl_mod & KMOD_CTRL)  m |= N8_KBD_STAT_CTRL;
    if (sdl_mod & KMOD_ALT)   m |= N8_KBD_STAT_ALT;
    if (sdl_mod & KMOD_CAPS)  m |= N8_KBD_STAT_CAPS;
    return m;
}
```

### Integration: `src/emulator.cpp`

```cpp
#include "emu_keyboard.h"

// In emulator_step(), add BUS_DECODE:
BUS_DECODE(addr, N8_KBD_BASE, 0xFFE0) {
    const uint8_t dev_reg = (addr - N8_KBD_BASE) & 0x1F;
    keyboard_decode(pins, dev_reg);
}
```

Add `keyboard_init()` to `emulator_init()`. Add `keyboard_reset()` to `emulator_reset()`.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_keyboard.cpp` to `SOURCES` and `TEST_SRC_OBJS`.

### Test Cases

**New file: `test/test_keyboard.cpp`**

| Test ID | Description |
|---------|-------------|
| T150 | `keyboard_reset()` clears data, status, ctrl |
| T151 | `keyboard_inject_key(0x41, 0)` sets DATA_AVAIL in status |
| T152 | Read KBD_DATA returns injected key code |
| T153 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | Write KBD_ACK clears DATA_AVAIL and OVERFLOW |
| T155 | Second inject before ACK sets OVERFLOW, data=second key |
| T156 | KBD_CTRL bit 0 enables IRQ; inject_key sets IRQ bit 2 |
| T157 | KBD_CTRL bit 0 clear: inject_key does NOT set IRQ |
| T158 | ACK deasserts IRQ bit 2 |
| T159 | Modifier bits (SHIFT=bit2) reflect in KBD_STATUS |
| T160 | Modifiers update even when DATA_AVAIL=0 |
| T161 | Reserved registers ($D863-$D867) read 0, write no-op |
| T162 | Extended key code ($80 = Up Arrow) stored correctly |
| T163 | Function key ($90 = F1) stored correctly |
| T164 | Bus decode: program writes KBD_ACK, clears status |

### Phase Gate

- All prior 253 tests pass.
- T150-T164 (15 new tests) pass.
- **Total: 268 tests.**

### Risks

- **ImGui keyboard conflict:** `!io.WantCaptureKeyboard` guard prevents forwarding keystrokes meant for ImGui input fields. May need a dedicated "keyboard capture" toggle.
- **Key repeat:** SDL2 sends repeat events. Filter `event.key.repeat != 0` to ignore repeats, or let firmware handle it.

---

## Phase 7: Video Rendering Pipeline

**Goal:** Render the frame buffer to an OpenGL texture using the N8 font, displayed in an ImGui window. Introduce separate backing store (`frame_buffer[]`) with dirty tracking. Replace the current "we just look at mem[]" approach with a proper rendering pipeline.

**Entry criteria:** Phase 6 complete, all tests pass.
**Exit criteria:** An ImGui window displays the 80x25 text framebuffer using the N8 font. Cursor rendering works. Frame buffer backed by separate array. No test regressions.

### Architecture

```
CPU writes to $C000-$CFFF  ->  bus decode  ->  frame_buffer[4096] + fb_dirty
                                                        |
video_get_state() (mode, cursor, etc.) ----+            |
                                           v            v
                                    video_rasterize_frame()
                                           |
                                           v
                                    screen_pixels[640x400 RGBA]
                                           |
                                           v  glTexSubImage2D()
                                    screen_texture (GL_TEXTURE_2D)
                                           |
                                           v  ImGui::Image()
                                    "N8 Display" window
```

### Backing Store Separation

This phase introduces `frame_buffer[]` as a separate array and modifies the bus decode in `emulator_step()` to intercept `$C000-$CFFF`:

```cpp
// In emu_video.cpp (or emulator.cpp):
static uint8_t frame_buffer[N8_FB_SIZE];  // 4096 bytes
static bool    fb_dirty = true;

// In emulator_step(), BEFORE the generic mem[] handler:
if (addr >= N8_FB_BASE && addr <= N8_FB_END) {
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, frame_buffer[addr - N8_FB_BASE]);
    } else {
        uint8_t val = M6502_GET_DATA(pins);
        frame_buffer[addr - N8_FB_BASE] = val;
        fb_dirty = true;
    }
    // Skip generic mem[] handler
    goto bus_done;  // or use handled flag per Spec B's Phase 10 pattern
}
```

This is a structural change to `emulator_step()`. Adopt Spec B's `handled` flag pattern to avoid `goto`:

```cpp
bool handled = false;

// Frame buffer: $C000-$CFFF
if (addr >= N8_FB_BASE && addr <= N8_FB_END) {
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, frame_buffer[addr - N8_FB_BASE]);
    } else {
        frame_buffer[addr - N8_FB_BASE] = M6502_GET_DATA(pins);
        fb_dirty = true;
    }
    handled = true;
}

// Device registers: $D800-$DFFF
if (!handled && addr >= N8_DEV_BASE && addr <= N8_DEV_END) {
    // Existing BUS_DECODE blocks for IRQ, TTY, Video, Keyboard
    // ...
    handled = true;
}

// Generic RAM/ROM
if (!handled) {
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, mem[addr]);
    } else {
        mem[addr] = M6502_GET_DATA(pins);
    }
}
```

### Scroll Operations Update

The scroll functions in `emu_video.cpp` must now operate on `frame_buffer[]` instead of `mem[N8_FB_BASE..]`. Update all references from `&mem[N8_FB_BASE]` to `frame_buffer`.

### GDB Callback Update

**`src/main.cpp`:**

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

### Rendering: CPU-side Rasterization

**Approach:** Render all 80x25 characters into a CPU-side `uint32_t pixel_buf[640*400]` each frame (when dirty), then upload via `glTexSubImage2D`. Simple, portable, no shader complexity. At 640x400x4 = 1MB/frame, the bandwidth is trivial.

**Why OpenGL directly (not SDL_Renderer):** The emulator already uses `SDL_GL_CreateContext()` for ImGui. `SDL_CreateTexture()` requires an `SDL_Renderer` which conflicts with the OpenGL context. Use `glGenTextures` / `glTexSubImage2D` directly.

### New File: `src/emu_display.h`

```cpp
#pragma once

// Initialize display (creates GL texture, must call after GL context)
void display_init();

// Render frame buffer to pixel buffer and upload texture (call each frame)
void display_render_if_dirty();

// Get GL texture ID for ImGui::Image()
unsigned int display_get_gl_texture();

// Pixel dimensions for ImGui sizing
int display_get_pixel_width();   // 640
int display_get_pixel_height();  // 400

// Cleanup
void display_shutdown();
```

### New File: `src/emu_display.cpp`

CPU-side rasterization with cursor compositing. Cursor flash uses frame counter (not SDL_GetTicks) for deterministic behavior per Spec B's approach.

The rendering code:
1. Iterates over `width * height` character cells
2. Looks up each character in `n8_font[]`
3. Blits 8x16 pixels per character into `pixel_buf[]`
4. Composites cursor overlay (underline or block, steady or flash)
5. Uploads via `glTexSubImage2D`

Dirty flag optimization: skip rasterization+upload when `fb_dirty` is false and cursor is not flashing.

### Font File

Copy `docs/charset/n8_font.h` to `src/n8_font.h`. The font is `static const` so it compiles into the display module only.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_display.cpp` to `SOURCES`. Do NOT add to test build -- display depends on OpenGL which is unavailable in the test environment.

### main.cpp Integration

1. Call `display_init()` after OpenGL context creation.
2. In render loop, after `emulator_step()` time slice, call `display_render_if_dirty()`.
3. Add ImGui window:
   ```cpp
   static bool show_display_window = true;
   if (show_display_window) {
       ImGui::Begin("N8 Display", &show_display_window);
       ImTextureID tex_id = (ImTextureID)(intptr_t)display_get_gl_texture();
       ImVec2 avail = ImGui::GetContentRegionAvail();
       float scale = fmin(avail.x / 640.0f, avail.y / 400.0f);
       if (scale < 1.0f) scale = 1.0f;
       ImGui::Image(tex_id, ImVec2(640.0f * scale, 400.0f * scale));
       ImGui::End();
   }
   ```
4. Call `display_shutdown()` in cleanup.

### Test Cases

**`test/test_bus.cpp`** updates:

| Test ID | Description |
|---------|-------------|
| T190 | Write to $C000 lands in frame_buffer[0], NOT in mem[$C000] |
| T191 | Read from $C000 returns frame_buffer[0] value |
| T192 | Write to $CFFF lands in frame_buffer[0xFFF] |
| T193 | emulator_reset clears frame_buffer |

**`test/test_video.cpp`** updates:
- T137-T138, T144-T145: Scroll tests now verify `frame_buffer[]` instead of `mem[N8_FB_BASE..]`

**`test/test_integration.cpp`** updates:
- T99 (if exists): `CHECK(mem[0xC000] == 0x48)` -> `CHECK(frame_buffer[0] == 0x48)`

No unit tests for the rendering pipeline itself (visual output). Verification is manual: run emulator, load firmware, verify characters display correctly.

### Phase Gate

- All prior 268 tests pass (with updated frame buffer references).
- T190-T193 (4 new tests) pass.
- **Total: 272 tests.**

### Risks

- **Structural change to emulator_step():** The `handled` flag pattern changes the control flow. All existing bus decode blocks must be verified.
- **OpenGL context state:** Texture upload must not interfere with ImGui's OpenGL state. Call before `ImGui::Render()`.
- **Performance:** 640x400 RGBA = 1MB upload. At 60fps = 60MB/s. Well within modern GPU bandwidth.

---

## Phase 8: Firmware Migration

**Goal:** Update cc65 linker configs and firmware source files to use new device register addresses and ROM layout.

**Entry criteria:** Phase 7 complete, emulator tests pass.
**Exit criteria:** `make firmware` succeeds with new addresses. Firmware runs correctly on updated emulator.

### Linker Config: `firmware/n8.cfg`

```
MEMORY {
    ZP:   start =    $0, size =  $100, type   = rw, define = yes;
    RAM:  start =  $400, size = $BC00, define = yes;
    ROM:  start = $E000, size = $2000, file   = %O;
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

### Firmware Source Changes

**`firmware/devices.inc`** -- include `hw_regs.inc` and provide legacy aliases:

```asm
.include "hw_regs.inc"

; Legacy aliases for transitional compatibility
.define TXT_CTRL         N8_FB_BASE
.define TXT_BUFF         N8_FB_BASE+1

.define TTY_OUT_CTRL     N8_TTY_OUT_STATUS
.define TTY_OUT_DATA     N8_TTY_OUT_DATA
.define TTY_IN_CTRL      N8_TTY_IN_STATUS
.define TTY_IN_DATA      N8_TTY_IN_DATA
```

**`firmware/devices.h`:**

```c
#ifndef __DEVICES__
#define __DEVICES__

#define   IRQ_FLAGS    $D800

#define   TTY_OUT_CTRL $D820
#define   TTY_OUT_DATA $D821
#define   TTY_IN_CTRL  $D822
#define   TTY_IN_DATA  $D823

#define   VID_MODE     $D840
#define   VID_WIDTH    $D841
#define   VID_HEIGHT   $D842
#define   VID_STRIDE   $D843
#define   VID_OPER     $D844
#define   VID_CURSOR   $D845
#define   VID_CURCOL   $D846
#define   VID_CURROW   $D847

#define   KBD_DATA     $D860
#define   KBD_STATUS   $D861
#define   KBD_ACK      $D861
#define   KBD_CTRL     $D862

#define   FB_BASE      $C000
#define   TXT_CTRL     $C000
#define   TXT_BUFF     $C001

#endif
```

**`firmware/zp.inc`:** Remove `ZP_IRQ $FF`.

**`firmware/interrupt.s`, `firmware/tty.s`, `firmware/main.s`:** Use symbolic names from `devices.inc`. No source changes needed -- only the definitions change via the updated includes.

### Playground Linker Configs

**`firmware/gdb_playground/playground.cfg` and `firmware/playground/playground.cfg`:** Change ROM to `start=$E000, size=$2000`.

### Playground Source Changes

**`firmware/playground/mon1.s`, `mon2.s`, `mon3.s`:** Update local TTY equates from `$C100-$C103` to `$D820-$D823`.

**`firmware/gdb_playground/test_tty.s`:** Update TTY equates from `$C100-$C101` to `$D820-$D821`.

### emulator_loadrom() Cleanup

Remove the legacy dual-path loader. Always load at `N8_ROM_BASE` ($E000):

```cpp
void emulator_loadrom() {
    uint16_t rom_ptr = N8_ROM_BASE;
    printf("Loading ROM at $%04X\r\n", rom_ptr);
    fflush(stdout);
    FILE *fp = fopen(rom_file, "r");
    if (!fp) {
        printf("ERROR: Cannot open ROM file '%s'\r\n", rom_file);
        return;
    }
    while (1) {
        uint8_t c = fgetc(fp);
        if (feof(fp)) break;
        mem[rom_ptr] = c;
        rom_ptr++;
    }
    fclose(fp);
}
```

### Phase Gate

- `make firmware` produces `N8firmware` without errors.
- `make -C firmware/gdb_playground` succeeds.
- `make -C firmware/playground` succeeds.
- `make test` still **272 passed, 0 failed**.
- Manual: `./n8` boots, banner prints, echo works.

### Risks

- **Breaking change:** Old firmware binaries will not work with the new emulator. Both sides must be updated together in this phase.
- **All firmware must be rebuilt:** Old binaries use wrong addresses. This is intentional -- Phase 8 is the coordinated cutover.

---

## Phase 9: GDB Stub, Playground, and Cleanup

**Goal:** Update GDB stub memory map XML. Remove legacy compatibility code. Final validation.

**Entry criteria:** Phase 8 complete, firmware builds and runs.
**Exit criteria:** GDB memory map matches new layout. No legacy addresses remain. All tests pass.

### GDB Stub Changes

**`src/gdb_stub.cpp` -- `memory_map_xml`:**

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

Device registers at `$D800-$DFFF` are mapped as RAM for GDB purposes. GDB reads return `mem[addr]` (the last CPU-written value) without triggering device side effects. This is the safe default per Spec A's original analysis.

### Cleanup

1. Remove legacy constants from `machine.h` (if any `LEGACY_*` defines were added).
2. Remove legacy dual-path in `emulator_loadrom()` (done in Phase 8).
3. Remove legacy alias defines from `firmware/devices.inc` if all firmware sources now use `hw_regs.inc` directly.

### Test Changes

**`test/test_gdb_protocol.cpp`:**

T58 (memory map XML test): update expected values to check for `0xE000` ROM region.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T180 | Memory map XML contains `0xC000` RAM region (4KB FB) |
| T181 | Memory map XML contains `0xD800` RAM region (devices) |
| T182 | Memory map XML ROM at `0xE000` with length `0x2000` |
| T183 | GDB memory read at `$D840` (VID_MODE) returns value |

### End-to-End Validation (Manual)

| Test ID | Scenario | Pass Criteria |
|---------|----------|---------------|
| E2E-01 | Boot firmware, TTY echo loop | Characters echoed to stdout |
| E2E-02 | GDB connect, `regs`, `step` | Registers read correctly |
| E2E-03 | GDB `load` playground program | Program executes from `$E000` |
| E2E-04 | GDB breakpoint at label | Execution halts at correct PC |
| E2E-05 | GDB `read $D800 20` | Device register space readable |
| E2E-06 | GDB `read $C000 100` | Frame buffer readable |
| E2E-07 | Video scroll up via program | Frame buffer shifted correctly |
| E2E-08 | Keyboard inject + IRQ | CPU vectors to IRQ handler |
| E2E-09 | Reset via GDB | CPU restarts at `$FFFC` vector |
| E2E-10 | Full mon3 session | All commands work with new TTY addresses |

### Phase Gate

- `make clean && make && make firmware && make test` all succeed.
- T58 updated, T180-T183 (4 new tests) pass.
- **Total: 276 automated tests, 0 failures.**
- All 10 E2E scenarios pass manually.
- No legacy addresses remain in the codebase.

---

## Risk Assessment & Rollback Strategy

### Overall Risk Profile

| Phase | Risk | Impact | Mitigation |
|-------|------|--------|------------|
| 0 | Very Low | None (pure refactor) | `make test` before/after |
| 1 | Low | IRQ mechanism could break | Tests cover IRQ path; firmware not yet updated |
| 2 | Low | TTY device addressing | Self-contained; tests comprehensive |
| 3 | Very Low | No code change, just tests | Only tests added |
| 4 | Low | ROM loader change | Dual-path auto-detect for transition |
| 5 | Low | New code, isolated module | Unit tests for all register behaviors |
| 6 | Low | New device, well-understood pattern | Unit tests; SDL translation isolatable |
| 7 | **Medium** | Structural change to emulator_step() + separate backing store | Thorough test coverage; `handled` flag pattern |
| 8 | **Medium** | Firmware source changes across many files | `make firmware` as gate; playground programs |
| 9 | Very Low | Static XML update | GDB test suite |

### Rollback Strategy

Each phase is a separate commit (or small commit series). Rollback = `git revert` of that phase's commits.

**Critical invariant:** At the end of every phase, `make test` passes. If tests fail, the phase is not complete.

**Branch strategy:** Work on a feature branch. Tag the repo before starting Phase 1:
```bash
git tag pre-migration-v1
```

**Emergency rollback:**
```bash
git checkout pre-migration-v1
make clean && make && make firmware && make test
```

---

## Dependency Graph

```
Phase 0 --- Machine Constants Header + firmware/hw_regs.inc
   |
   v
Phase 1 --- IRQ Migration ($00FF -> $D800)
   |
   v
Phase 2 --- TTY Migration ($C100 -> $D820)
   |
   v
Phase 3 --- Frame Buffer Expansion (256B -> 4KB, still mem[])
   |
   v
Phase 4 --- ROM Base Migration ($D000 -> $E000)
   |         + Dev Bank recognition
   |         + Dual-path loader (transition)
   v
Phase 5 --- Video Control Registers ($D840)
   |         + Scroll operations on mem[]
   v
Phase 6 --- Keyboard Device ($D860)
   |         + SDL2 key translation
   |         + IRQ bit 2
   v
Phase 7 --- Video Rendering Pipeline
   |         + Separate frame_buffer[] + fb_dirty
   |         + OpenGL texture + ImGui display window
   |         + N8 font integration
   |         + Bus decode refactor (handled flag)
   |         + GDB callback update
   v
Phase 8 --- Firmware Migration
   |         + cc65 linker configs ($E000)
   |         + Assembly source address updates
   |         + Playground programs
   |         + Remove dual-path loader
   v
Phase 9 --- GDB Stub + Cleanup + E2E Validation
```

Phases 5 and 6 are independent of each other but both depend on Phase 4 (ROM moved so device space is clean). They could be parallelized if needed.

Phase 7 depends on Phases 5 and 6 (video registers and keyboard must exist before the rendering pipeline and bus decode refactor).

Phase 8 depends on ALL prior phases -- firmware must use all new addresses simultaneously.

---

## Test Case Registry

### Final Test Count

| Phase | New Tests | Running Total |
|------:|----------:|--------------:|
| Baseline | -- | 221 |
| Phase 0 (constants) | 0 | 221 |
| Phase 1 (IRQ move) | 4 | 225 |
| Phase 2 (TTY move) | 3 | 228 |
| Phase 3 (FB expand) | 4 | 232 |
| Phase 4 (ROM move) | 5 | 237 |
| Phase 5 (Video regs) | 16 | 253 |
| Phase 6 (Keyboard) | 15 | 268 |
| Phase 7 (Rendering) | 4 | 272 |
| Phase 8 (Firmware) | 0 (build test) | 272 |
| Phase 9 (GDB + cleanup) | 4 | 276 |
| **Total** | **55 new** | **276 automated + 10 manual** |

### New Test IDs by File

**`test/test_bus.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T110 | 1 | Write/read `$D800` (IRQ_FLAGS) |
| T111 | 1 | IRQ_FLAGS cleared every tick |
| T112 | 1 | IRQ line asserted when flags non-zero |
| T113 | 1 | Old `$00FF` no longer acts as IRQ register |
| T114 | 2 | TTY read at old `$C100` does NOT trigger tty_decode |
| T115 | 2 | TTY read at `$D820` returns OUT_STATUS |
| T116 | 2 | TTY write at `$D821` sends character |
| T117 | 3 | Write to `$C100` stores in mem (no longer TTY) |
| T118 | 3 | Write to `$C7CF` (end of active FB) stores correctly |
| T119 | 3 | Write to `$CFFF` (end of 4KB FB) stores correctly |
| T120 | 3 | Read from `$C500` returns written value |
| T190 | 7 | Write to $C000 lands in frame_buffer[0], NOT in mem[$C000] |
| T191 | 7 | Read from $C000 returns frame_buffer[0] value |
| T192 | 7 | Write to $CFFF lands in frame_buffer[0xFFF] |
| T193 | 7 | emulator_reset clears frame_buffer |

**`test/test_video.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T130 | 5 | video_reset() sets default mode/dims |
| T131 | 5 | Read VID_MODE returns 0x00 after reset |
| T132 | 5 | Write VID_MODE=0x00 auto-sets 80/25/80 |
| T133 | 5 | Write VID_MODE=0x01 retains current dims |
| T134 | 5 | Write/read VID_WIDTH |
| T135 | 5 | Write/read VID_HEIGHT |
| T136 | 5 | Write/read VID_STRIDE |
| T137 | 5 | VID_OPER=0x00 scroll up |
| T138 | 5 | VID_OPER=0x01 scroll down |
| T139 | 5 | Read VID_OPER returns 0 (write-only) |
| T140 | 5 | Write/read VID_CURSOR |
| T141 | 5 | Write/read VID_CURCOL |
| T142 | 5 | Write/read VID_CURROW |
| T143 | 5 | Phantom registers read 0 |
| T144 | 5 | VID_OPER=0x02 scroll left |
| T145 | 5 | VID_OPER=0x03 scroll right |

**`test/test_keyboard.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T150 | 6 | keyboard_reset() clears state |
| T151 | 6 | inject_key sets DATA_AVAIL |
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
| T162 | 6 | Extended key code ($80) stored |
| T163 | 6 | Function key ($90) stored |
| T164 | 6 | Bus decode: program writes KBD_ACK |

**`test/test_integration.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T170 | 4 | Code at $E000 executable |
| T171 | 4 | Reset vector in ROM works |
| T172 | 4 | RAM at $0400 writable |
| T173 | 4 | Dev Bank $D000 writable |
| T174 | 4 | Legacy loadrom (>8KB at $D000) |

**`test/test_gdb_protocol.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T180 | 9 | Memory map has $C000 RAM (4KB FB) |
| T181 | 9 | Memory map has $D800 RAM (devices) |
| T182 | 9 | Memory map ROM at $E000 length $2000 |
| T183 | 9 | GDB read at $D840 returns value |

### Existing Test Modifications

| ID | Phase | Change |
|----|-------|--------|
| T69, T69a, T69b, T70, T70a | 0+1 | `mem[0x00FF]` -> `mem[N8_IRQ_ADDR]` |
| T71-T78a | 0+2 | `0xC1xx` addresses -> `N8_TTY_BASE + offset` |
| T76 | 0+1 | `mem[0x00FF]` -> `mem[N8_IRQ_ADDR]` |
| T58 | 9 | Memory map XML checks for `0xE000` |
| T64-T67 | 7 | `mem[0xC0xx]` -> `frame_buffer[offset]` |

---

## Summary of Files Modified Per Phase

| Phase | Modified Files | New Files |
|-------|---------------|-----------|
| 0 | `machine.h`, `emulator.cpp`, `emu_tty.cpp`, `test_helpers.h`, `test_utils.cpp`, `test_tty.cpp`, `test_bus.cpp`, `test_integration.cpp` | `firmware/hw_regs.inc` |
| 1 | `machine.h`, `test_bus.cpp` | -- |
| 2 | `emulator.cpp`, `machine.h`, `test_tty.cpp`, `test_gdb_callbacks.cpp`, `test_bus.cpp` | -- |
| 3 | `machine.h`, `test_bus.cpp` | -- |
| 4 | `emulator.cpp`, `machine.h`, `test_integration.cpp` | -- |
| 5 | `emulator.cpp`, `machine.h`, `Makefile` | `src/emu_video.h`, `src/emu_video.cpp`, `test/test_video.cpp` |
| 6 | `emulator.cpp`, `main.cpp`, `machine.h`, `Makefile` | `src/emu_keyboard.h`, `src/emu_keyboard.cpp`, `test/test_keyboard.cpp` |
| 7 | `emulator.cpp`, `emulator.h`, `emu_video.cpp`, `main.cpp`, `Makefile`, `test_bus.cpp`, `test_video.cpp`, `test_integration.cpp` | `src/emu_display.h`, `src/emu_display.cpp`, `src/n8_font.h` (copy) |
| 8 | `emulator.cpp`, `firmware/n8.cfg`, `firmware/devices.inc`, `firmware/devices.h`, `firmware/zp.inc`, `firmware/gdb_playground/playground.cfg`, `firmware/playground/playground.cfg`, `firmware/playground/mon1.s`, `firmware/playground/mon2.s`, `firmware/playground/mon3.s`, `firmware/gdb_playground/test_tty.s` | -- |
| 9 | `gdb_stub.cpp`, `test_gdb_protocol.cpp` | -- |

**Total new files:** 8 (hw_regs.inc, emu_video.h/cpp, emu_display.h/cpp, emu_keyboard.h/cpp, n8_font.h copy, test_video.cpp, test_keyboard.cpp)

**Total modified files:** ~25 unique files across all phases

---

## Appendix: Bus Decode Reference

### Current bus decode

```
addr     mask     device
$0000    $FF00    Zero Page monitor (logging only)
$FFF0    $FFF0    Vector monitor (logging only)
$C100    $FFF0    TTY (16 addresses: $C100-$C10F)
```

Everything else falls through to generic `mem[]` read/write.

### Target bus decode (after all phases)

```
addr     mask       device          handler
$C000    range      Frame Buffer    frame_buffer[] (handled flag)
$D800    $FFFF      System/IRQ      mem[] passthrough (or dedicated handler)
$D820    $FFE0      TTY             tty_decode(pins, reg)
$D840    $FFE0      Video Control   video_decode(pins, reg)
$D860    $FFE0      Keyboard        keyboard_decode(pins, reg)
$D880+   range      Reserved        read $00, write ignored
$0000    $FF00      ZP monitor      logging only
$FFF0    $FFF0      Vector monitor  logging only
```

The `$FFE0` mask for 32-byte device blocks: `addr & $FFE0 == base` is true for addresses `base` through `base+$1F`.
