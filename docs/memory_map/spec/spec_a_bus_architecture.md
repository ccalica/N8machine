# Spec A: Bus Architecture Migration & Device Implementation

> Migration from current memory map to proposed layout, plus video and keyboard device implementation.
> Incremental phases — each independently testable.

## Table of Contents

- [Current State Summary](#current-state-summary)
- [Target State Summary](#target-state-summary)
- [Phase 0: Machine Constants Header](#phase-0-machine-constants-header)
- [Phase 1: IRQ Register Migration ($00FF to $D800)](#phase-1-irq-register-migration-00ff-to-d800)
- [Phase 2: TTY Device Migration ($C100 to $D820)](#phase-2-tty-device-migration-c100-to-d820)
- [Phase 3: Frame Buffer Expansion (256B to 4KB)](#phase-3-frame-buffer-expansion-256b-to-4kb)
- [Phase 4: Video Control Registers ($D840)](#phase-4-video-control-registers-d840)
- [Phase 5: Video Rendering Pipeline](#phase-5-video-rendering-pipeline)
- [Phase 6: Keyboard Device ($D860)](#phase-6-keyboard-device-d860)
- [Phase 7: ROM Layout Migration](#phase-7-rom-layout-migration)
- [Phase 8: Firmware Migration](#phase-8-firmware-migration)
- [Phase 9: GDB Stub Memory Map Update](#phase-9-gdb-stub-memory-map-update)
- [Risk Assessment & Rollback Strategy](#risk-assessment--rollback-strategy)
- [Dependency Graph](#dependency-graph)

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

The frame buffer at `$C000` has **no bus decode logic** — it reads/writes directly to `mem[]`. There is no read-only enforcement on ROM.

### IRQ Mechanism (`emulator.cpp:23-24, 110-128`)

```cpp
#define IRQ_CLR() mem[0x00FF] = 0x00;
#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)
```

Every tick: `IRQ_CLR()` → devices reassert → check `mem[0x00FF]` → set/clear M6502_IRQ pin. This is a zero-page memory location, readable/writable by firmware via `LDA $FF` / `STA $FF`. Firmware references it as `ZP_IRQ $FF` in `zp.inc`.

### TTY Device (`emu_tty.cpp`)

Four registers decoded at offsets 0-3 from base `$C100`. Uses IRQ bit 1. Input buffered in `std::queue<uint8_t>`.

### Current Test Coverage

| Suite | File | Tests |
|-------|------|-------|
| bus | `test_bus.cpp` | T62-T67 (RAM, frame buffer read/write) |
| tty | `test_tty.cpp` | T71-T79 (register read/write, IRQ, reset) |
| integration | `test_integration.cpp` | T95-T101a (boot, BP, IRQ) |
| gdb | `test_gdb_protocol.cpp`, `test_gdb_callbacks.cpp` | GDB RSP protocol + emulator callbacks |

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

**Goal:** Centralize all memory map addresses and bus decode masks in a single header, eliminating magic numbers scattered across source files. Every subsequent phase uses these constants.

**Entry criteria:** Current `make test` passes.
**Exit criteria:** All tests still pass. No magic numbers remain in emulator.cpp, emu_tty.cpp, or test files for memory-mapped addresses.

### New File: `src/n8_memory_map.h`

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
#define N8_ROM_SIZE        0x3000   // 12KB

// --- Vectors ---
#define N8_VEC_NMI         0xFFFA
#define N8_VEC_RESET       0xFFFC
#define N8_VEC_IRQ         0xFFFE
```

### Code Changes

**`src/emulator.cpp`:**
- `#include "n8_memory_map.h"`
- Replace `mem[0x00FF]` with `mem[N8_IRQ_ADDR]`
- Replace `0xD000` in `emulator_loadrom()` with `N8_ROM_BASE`
- Replace `0xC100` / `0xFFF0` in `BUS_DECODE` with `N8_TTY_BASE` / `N8_TTY_MASK`
- Replace hardcoded `0xC100` in TTY register offset calc with `N8_TTY_BASE`
- Replace `IRQ_CLR()` and `IRQ_SET()` macros to use `N8_IRQ_ADDR`

**`src/emu_tty.cpp`:**
- Replace `emu_set_irq(1)` with `emu_set_irq(N8_IRQ_BIT_TTY)` (include `n8_memory_map.h`)
- Replace `emu_clr_irq(1)` with `emu_clr_irq(N8_IRQ_BIT_TTY)`

**`test/test_helpers.h`:**
- `#include "n8_memory_map.h"` (for use in test assertions)

**`test/test_tty.cpp`:**
- Replace `mem[0x00FF]` references with `mem[N8_IRQ_ADDR]`
- Replace `0xC100`-`0xC103` pin addresses with `N8_TTY_BASE + offset`

**`test/test_bus.cpp`:**
- Replace `0xC000` / `0xC0FF` / `0xC005` with `N8_FB_BASE + offset`
- Replace `0xD000` with `N8_ROM_BASE`

**`test/test_integration.cpp`:**
- Replace `0xD000` / `0xD100` with `N8_ROM_BASE` / `(N8_ROM_BASE + 0x100)`

### Tests

No new tests — this is a pure refactor. Run `make test` to verify zero regressions.

### Verification

```bash
# After refactor, no raw hex addresses should remain for mapped regions:
grep -rn '0x00FF\|0xC000\|0xC100\|0xD000' src/emulator.cpp src/emu_tty.cpp
# Should return zero hits (except comments)
```

---

## Phase 1: IRQ Register Migration ($00FF to $D800)

**Goal:** Move IRQ flags from zero page `$00FF` to device register space at `$D800`. Add bus decode for the `$D800` region. Firmware-visible change: `LDA $FF` becomes `LDA $D800`.

**Entry criteria:** Phase 0 complete, all tests pass.
**Exit criteria:** IRQ flags live at `$D800`. Old address `$00FF` is ordinary RAM. All tests updated and passing.

### Constants Update: `src/n8_memory_map.h`

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

### Bus Decode Design: Device Register Router

The `$D800-$DFFF` region needs a unified bus decode. Rather than adding separate `BUS_DECODE` blocks for each device, introduce a **device router** that:

1. Matches any address in `$D800-$DFFF`
2. Computes the device slot: `(addr - 0xD800) >> 5` (0-63)
3. Computes the register offset: `(addr - 0xD800) & 0x1F` (0-31)
4. Dispatches to device-specific handler

This is more extensible than per-device `BUS_DECODE` blocks and gives clean separation between routing and device logic.

```cpp
// In emulator.cpp — after RAM read/write

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
            break;
    }
}
```

### IRQ Device Handler

**New function in `emulator.cpp`:**

```cpp
static void dev_irq_decode(uint64_t &pins, uint8_t reg) {
    if (reg != 0) return;  // Only register 0 defined

    if (pins & M6502_RW) {
        // Read: return current IRQ flags
        M6502_SET_DATA(pins, mem[N8_IRQ_ADDR]);
    } else {
        // Write: firmware can write to clear/set individual bits
        mem[N8_IRQ_ADDR] = M6502_GET_DATA(pins);
    }
}
```

### IRQ Mechanism Changes

The existing IRQ mechanism has a critical property: `IRQ_CLR()` zeroes the register **every tick**, then devices reassert. This means `mem[N8_IRQ_ADDR]` at address `$D800` will behave identically — the clear+reassert cycle is address-agnostic.

However, since `$D800` is in the device region (not zero page), the CPU will read/write it via `LDA $D800` (absolute addressing, 4 cycles) instead of `LDA $FF` (zero page, 3 cycles). This is an acceptable performance trade-off — IRQ polling is infrequent.

**Key constraint:** The device decode for `$D800` must override the ROM/RAM read that already happened in the main memory access block. This is the existing pattern — `tty_decode()` already calls `M6502_SET_DATA()` after the generic `mem[addr]` read. The device decode **wins** because it runs after the RAM read and overwrites the data bus.

Wait — there is a subtlety. In the current code, `$D800` falls inside the ROM region (`$D000-$FFFF`). The current code does:

```cpp
if (BUS_READ) {
    M6502_SET_DATA(pins, mem[addr]);  // reads mem[$D800]
} else {
    mem[addr] = M6502_GET_DATA(pins); // writes mem[$D800]
}
```

Then later, device decode runs and overwrites the data bus for reads. For writes, `mem[$D800]` is already written by the generic RAM handler, and the device decode can use that value directly. This means **the device handler for reads must always set the data bus**, which it does.

**One issue:** ROM write protection. Currently there is **no ROM write protection** — the CPU can write to `$D000-$FFFF` and it sticks in `mem[]`. This is actually convenient for Phase 1: `IRQ_CLR()` writing to `mem[$D800]` works naturally. ROM protection is a separate concern (not in scope for this spec, but noted).

### Code Changes

**`src/emulator.cpp`:**

1. Change `IRQ_CLR()` and `IRQ_SET()` macros:
   ```cpp
   #define IRQ_CLR() mem[N8_IRQ_ADDR] = 0x00;
   #define IRQ_SET(bit) mem[N8_IRQ_ADDR] = (mem[N8_IRQ_ADDR] | (0x01 << bit))
   ```
   (Already use `N8_IRQ_ADDR` from Phase 0, so just the address value changes.)

2. Add `dev_irq_decode()` function.

3. Add device router block after the existing bus decode section.

4. In `emu_set_irq()` and `emu_clr_irq()`, already use `N8_IRQ_ADDR`.

**`src/emu_tty.cpp`:** No changes (already uses `emu_set_irq(N8_IRQ_BIT_TTY)`).

**`src/n8_memory_map.h`:** Update `N8_IRQ_ADDR` from `0x00FF` to `0xD800`.

### Test Changes

**`test/test_tty.cpp`:**

- All tests that do `mem[0x00FF] = 0` become `mem[N8_IRQ_ADDR] = 0` (already done in Phase 0)
- T76 checks `(mem[N8_IRQ_ADDR] & 0x02) == 0` — works at new address

**`test/test_bus.cpp`:** Add new test cases:

```
T_IRQ_01: Write $D800 sets IRQ flags in mem[N8_IRQ_ADDR]
  - EmulatorFixture, load program: LDA #$02; STA $D800
  - After execution, check mem[N8_IRQ_ADDR] == 0x02

T_IRQ_02: Read $D800 returns IRQ flags
  - EmulatorFixture, set mem[N8_IRQ_ADDR] = 0x04
  - Load program: LDA $D800; STA $0200
  - After execution, check mem[0x0200] == 0x04

T_IRQ_03: Old address $00FF is now ordinary RAM
  - EmulatorFixture, write 0xAA to mem[0x00FF]
  - Load program: LDA $00FF; STA $0200
  - After execution, check mem[0x0200] == 0xAA
  - Verify mem[N8_IRQ_ADDR] is NOT 0xAA (independent)
```

**`test/test_integration.cpp`:** T101 (IRQ triggers vector) — the IRQ mechanism is internal to the emulator; the test injects via `tty_inject_char()` and checks the CPU jumped to the handler. This test does not reference `$00FF` directly (the firmware IRQ handler does, but that firmware is loaded from inline bytes, not the real firmware). The test should continue passing because:
- `tty_inject_char()` calls `emu_set_irq(1)` which writes `mem[N8_IRQ_ADDR]`
- `emulator_step()` checks `mem[N8_IRQ_ADDR]` for IRQ pin assertion
- The CPU takes the IRQ and jumps to the handler

### Risks

- **Firmware breakage:** The real firmware (`firmware/zp.inc`) defines `ZP_IRQ $FF`. After this phase, the firmware will read/write the wrong address. This is acceptable because firmware migration is Phase 8, and during Phases 1-7 the emulator can run without firmware (tests use inline programs).
- **GDB stub memory map XML** still lists old layout. Updated in Phase 9.

---

## Phase 2: TTY Device Migration ($C100 to $D820)

**Goal:** Move TTY device registers from `$C100` to `$D820`. Integrate TTY into the device register router.

**Entry criteria:** Phase 1 complete, all tests pass.
**Exit criteria:** TTY registers decode at `$D820-$D823`. Old address `$C100` is ordinary RAM. All tests updated and passing.

### Constants Update: `src/n8_memory_map.h`

```cpp
// --- TTY Device ---
#define N8_TTY_BASE        0xD820   // Was 0xC100
#define N8_TTY_MASK        0xFFE0   // 32-byte decode window (was 0xFFF0)
#define N8_TTY_SLOT        1        // Device slot index for router
```

### Code Changes

**`src/emulator.cpp`:**

1. Remove the standalone TTY `BUS_DECODE` block:
   ```cpp
   // REMOVE:
   BUS_DECODE(addr, 0xC100, 0xFFF0) {
       const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
       tty_decode(pins, dev_reg);
   }
   ```

2. Add TTY to the device router:
   ```cpp
   case 1:  // $D820: TTY
       tty_decode(pins, reg);
       break;
   ```

3. The `tty_decode()` function signature and implementation remain unchanged. It already accepts a register offset (0-3) and handles reads/writes. The only change is that `reg` is now computed from `(addr - N8_DEV_BASE) & 0x1F` instead of `(addr - 0xC100) & 0xFF`. Since the register offsets are 0-3 in both schemes, no logic changes needed.

**`src/emu_tty.cpp`:** No changes.

### Test Changes

**`test/test_tty.cpp`:**

Update pin construction addresses:
- `make_read_pins(0xC100)` → `make_read_pins(N8_TTY_BASE + 0)`
- `make_read_pins(0xC101)` → `make_read_pins(N8_TTY_BASE + 1)`
- etc.
- T78a (phantom addresses): loop `reg = 4..15` → `reg = 4..31` (32-byte window)

New test:
```
T_TTY_MIG_01: Old address $C100 is now ordinary RAM
  - tty_reset(), write 0xBB to mem[0xC100]
  - Read mem[0xC100] == 0xBB (not intercepted by TTY)
```

**`test/test_bus.cpp`:** No changes (frame buffer tests at $C000 unaffected).

**`test/test_integration.cpp`:** T101 (IRQ via TTY) still works — it uses `tty_inject_char()` which is address-independent.

### Risks

- Low risk. TTY decode is self-contained. Address change is purely in routing.

---

## Phase 3: Frame Buffer Expansion (256B to 4KB)

**Goal:** Expand frame buffer from 256 bytes at `$C000-$C0FF` to 4KB at `$C000-$CFFF`. Introduce a separate backing store array (not backed by `mem[]`) to cleanly separate video memory from main RAM.

**Entry criteria:** Phase 2 complete, all tests pass.
**Exit criteria:** Frame buffer is 4KB, backed by `frame_buffer[]` array. Bus decode intercepts reads/writes to `$C000-$CFFF`. All tests updated and passing.

### Design Decision: Separate Backing Store

The current frame buffer uses `mem[]` directly. This works but has problems:
1. No separation between CPU memory and video memory
2. The video rendering pipeline (Phase 5) needs to detect changes efficiently
3. Future bank switching of `$C000-$CFFF` requires the frame buffer to be independent

Introduce `uint8_t frame_buffer[N8_FB_SIZE]` as a separate array. Bus decode intercepts `$C000-$CFFF` and redirects reads/writes to this array.

### Constants Update: `src/n8_memory_map.h`

```cpp
// --- Frame Buffer ---
#define N8_FB_BASE         0xC000
#define N8_FB_SIZE         0x1000   // 4KB (was 0x0100)
#define N8_FB_END          0xCFFF
```

### Code Changes

**`src/emulator.cpp`:**

1. Add backing store:
   ```cpp
   uint8_t frame_buffer[N8_FB_SIZE] = { };
   ```

2. Declare in header:
   ```cpp
   extern uint8_t frame_buffer[];
   ```

3. Add bus decode for frame buffer **before** the generic RAM read/write:
   ```cpp
   // Frame buffer intercept — BEFORE generic mem[] access
   bool fb_access = (addr >= N8_FB_BASE && addr <= N8_FB_END);
   if (fb_access) {
       uint16_t fb_offset = addr - N8_FB_BASE;
       if (pins & M6502_RW) {
           M6502_SET_DATA(pins, frame_buffer[fb_offset]);
       } else {
           frame_buffer[fb_offset] = M6502_GET_DATA(pins);
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

   This is a structural change to `emulator_step()`. The current code unconditionally reads/writes `mem[addr]`, then device decode overrides. With a separate backing store, we must **skip** the `mem[]` access for frame buffer addresses.

   **Alternative:** Keep the current structure (always access `mem[]` first, then override). But this means `mem[$C000-$CFFF]` contains stale data, and writes go to `mem[]` then get copied to `frame_buffer[]`. This is wasteful and confusing. The intercept-first approach is cleaner.

4. Add `emulator_reset()` clearing:
   ```cpp
   memset(frame_buffer, 0, N8_FB_SIZE);
   ```

**`src/emulator.h`:**
```cpp
extern uint8_t frame_buffer[];
```

### Test Changes

**`test/test_bus.cpp`:**

Update existing tests:
- T64: `CHECK(mem[0xC000] == 0x41)` → `CHECK(frame_buffer[0] == 0x41)`
- T65: `mem[0xC000] = 0x42` → `frame_buffer[0] = 0x42`
- T66: `CHECK(mem[0xC0FF] == 0x7E)` → `CHECK(frame_buffer[0xFF] == 0x7E)`
- T67: `CHECK(mem[0xC005] == 0x33)` → `CHECK(frame_buffer[5] == 0x33)`

New tests:
```
T_FB_01: Write to $C800 (middle of expanded buffer) stores in frame_buffer[0x800]
  - LDA #$AA; STA $C800
  - CHECK(frame_buffer[0x800] == 0xAA)

T_FB_02: Read from $CFFF returns frame_buffer[0xFFF]
  - frame_buffer[0xFFF] = 0x55
  - LDA $CFFF; STA $0200
  - CHECK(mem[0x0200] == 0x55)

T_FB_03: Frame buffer does NOT write to mem[]
  - LDA #$AA; STA $C000
  - CHECK(mem[0xC000] == 0x00)  // mem[] untouched
  - CHECK(frame_buffer[0] == 0xAA)

T_FB_04: emulator_reset clears frame buffer
  - frame_buffer[0] = 0xFF
  - emulator_reset()  // or direct memset
  - CHECK(frame_buffer[0] == 0x00)
```

**`test/test_integration.cpp`:**
- T99: `CHECK(mem[0xC000] == 0x48)` → `CHECK(frame_buffer[0] == 0x48)`
- T99: `CHECK(mem[0xC001] == 0x69)` → `CHECK(frame_buffer[1] == 0x69)`

### Risks

- **Structural change to emulator_step():** The intercept-before-RAM pattern changes the control flow. All existing bus decode blocks (ZP monitor, vectors, device router) must be verified to still work.
- **GDB stub `read_mem` / `write_mem`:** The GDB callbacks read `mem[addr]` directly. After this change, `mem[$C000-$CFFF]` is no longer the frame buffer. GDB reads of the frame buffer region will return stale/zero data. **Fix:** Update `gdb_read_mem()` and `gdb_write_mem()` in `main.cpp` to redirect `$C000-$CFFF` to `frame_buffer[]`. This is required for correctness but is a small, testable change.

### GDB Callback Fix (in this phase)

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

---

## Phase 4: Video Control Registers ($D840)

**Goal:** Implement the video control register block at `$D840-$D847`. No rendering yet — just the registers as readable/writable device state.

**Entry criteria:** Phase 3 complete, all tests pass.
**Exit criteria:** Video registers at `$D840-$D847` are bus-accessible. Mode write auto-sets defaults. Scroll operations execute on framebuffer. All tests passing.

### Constants Update: `src/n8_memory_map.h`

```cpp
// --- Video Control ---
#define N8_VID_BASE        0xD840
#define N8_VID_SLOT        2        // Device slot index
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

### New File: `src/emu_video.h`

```cpp
#pragma once
#include <stdint.h>

void video_init();
void video_reset();
void video_decode(uint64_t &pins, uint8_t reg);

// State accessors for rendering pipeline (Phase 5)
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
```

### New File: `src/emu_video.cpp`

Internal state: an 8-byte register file.

```cpp
#include "emu_video.h"
#include "emulator.h"
#include "n8_memory_map.h"
#include "m6502.h"
#include <cstring>

extern uint8_t frame_buffer[];

static uint8_t vid_regs[8] = { 0 };

void video_init() {
    video_reset();
}

void video_reset() {
    memset(vid_regs, 0, sizeof(vid_regs));
    vid_regs[N8_VID_MODE]   = N8_VIDMODE_TEXT_DEFAULT;
    vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
    vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
    vid_regs[N8_VID_CURSOR] = 0x00;  // Cursor off
    vid_regs[N8_VID_CURCOL] = 0;
    vid_regs[N8_VID_CURROW] = 0;
}

static void video_apply_mode(uint8_t mode) {
    switch (mode) {
        case N8_VIDMODE_TEXT_DEFAULT:
            vid_regs[N8_VID_WIDTH]  = N8_VID_DEFAULT_WIDTH;
            vid_regs[N8_VID_HEIGHT] = N8_VID_DEFAULT_HEIGHT;
            vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;
            break;
        case N8_VIDMODE_TEXT_CUSTOM:
            // Don't auto-set — user provides dimensions
            break;
    }
}

static void video_scroll_up() {
    uint8_t w = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    memmove(frame_buffer, frame_buffer + w, w * (h - 1));
    memset(frame_buffer + w * (h - 1), 0x20, w);  // Fill with spaces
}

static void video_scroll_down() {
    uint8_t w = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    memmove(frame_buffer + w, frame_buffer, w * (h - 1));
    memset(frame_buffer, 0x20, w);
}

static void video_scroll_left() {
    uint8_t w = vid_regs[N8_VID_WIDTH];
    uint8_t s = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x20;
    }
}

static void video_scroll_right() {
    uint8_t w = vid_regs[N8_VID_WIDTH];
    uint8_t s = vid_regs[N8_VID_STRIDE];
    uint8_t h = vid_regs[N8_VID_HEIGHT];
    for (int row = 0; row < h; row++) {
        uint8_t *line = frame_buffer + row * s;
        memmove(line + 1, line, w - 1);
        line[0] = 0x20;
    }
}

void video_decode(uint64_t &pins, uint8_t reg) {
    if (reg > 7) return;

    if (pins & M6502_RW) {
        // Read
        if (reg == N8_VID_OPER) {
            // VID_OPER is write-only; reads return 0
            M6502_SET_DATA(pins, 0x00);
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

uint8_t video_get_mode()         { return vid_regs[N8_VID_MODE]; }
uint8_t video_get_width()        { return vid_regs[N8_VID_WIDTH]; }
uint8_t video_get_height()       { return vid_regs[N8_VID_HEIGHT]; }
uint8_t video_get_stride()       { return vid_regs[N8_VID_STRIDE]; }
uint8_t video_get_cursor_style() { return vid_regs[N8_VID_CURSOR]; }
uint8_t video_get_cursor_col()   { return vid_regs[N8_VID_CURCOL]; }
uint8_t video_get_cursor_row()   { return vid_regs[N8_VID_CURROW]; }
```

### Integration with Device Router

**`src/emulator.cpp`:**

```cpp
#include "emu_video.h"

// In device router:
case 2:  // $D840: Video Control
    video_decode(pins, reg);
    break;
```

Add `video_init()` to `emulator_init()` and `video_reset()` to `emulator_reset()`.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_video.cpp` to `SOURCES`. Add `$(BUILD_DIR)/emu_video.o` to `TEST_SRC_OBJS`.

### Test Cases

**New file: `test/test_video.cpp`**

```
T_VID_01: Reset state — VID_MODE=0, WIDTH=80, HEIGHT=25, STRIDE=80
  - video_reset()
  - Check all register accessors

T_VID_02: Read VID_MODE returns 0x00 after reset
  - Read pins at $D840
  - CHECK data == 0x00

T_VID_03: Write VID_MODE=0x00 auto-sets WIDTH=80, HEIGHT=25
  - Write 0x00 to $D840
  - Read $D841 == 80, $D842 == 25, $D843 == 80

T_VID_04: Write VID_OPER=0x00 triggers scroll up
  - Fill frame_buffer row 0 with 'A', row 1 with 'B'
  - Write 0x00 to $D844
  - Check frame_buffer[0..79] == 'B' (row 1 moved to row 0)
  - Check frame_buffer[80*24..80*24+79] == 0x20 (last row cleared)

T_VID_05: VID_OPER read returns 0 (write-only)
  - Write 0x01 to $D844
  - Read $D844 == 0x00

T_VID_06: Cursor position read/write
  - Write 40 to $D846 (CURCOL), 12 to $D847 (CURROW)
  - Read back: $D846 == 40, $D847 == 12

T_VID_07: Scroll down shifts rows down, top row filled with spaces
  - Fill row 0 with 'X'
  - Write 0x01 to VID_OPER
  - frame_buffer[0..79] == 0x20
  - frame_buffer[80..159] == 'X'

T_VID_08: Registers beyond offset 7 are no-ops
  - Read/write $D848..D85F — no crash, reads return 0
```

### Risks

- **Scroll operations modify frame_buffer:** Must ensure frame_buffer is properly sized and scroll doesn't overrun. The `memmove` sizes depend on `vid_regs[STRIDE]` and `vid_regs[HEIGHT]` which are 8-bit. Max product = 255*255 = 65025, but frame_buffer is 4096 bytes. **Guard:** clamp `stride * height` to `N8_FB_SIZE` in scroll functions.
- **Stride vs Width:** Stride can differ from Width (e.g., stride=128 for aligned access). Scroll functions must use stride for row spacing but width for data movement. The implementation above uses stride for vertical scrolls (correct) and width+stride for horizontal (correct).

---

## Phase 5: Video Rendering Pipeline

**Goal:** Render the frame buffer to an SDL2 texture using the N8 font, displayed in an ImGui window. Replace the current "we just look at mem[]" approach with a proper rendering pipeline.

**Entry criteria:** Phase 4 complete, all tests pass.
**Exit criteria:** An ImGui window displays the 80x25 text framebuffer using the N8 font. Cursor rendering works. No test regressions.

### Architecture

```
frame_buffer[4096]  →  render_text_display()  →  SDL_Texture  →  ImGui::Image()
     ↑                       ↑                       ↑
   CPU writes          n8_font[256][16]         SDL_Renderer
                       video_regs (mode,         (created once)
                        cursor, etc.)
```

### Rendering Approach

**Option A: CPU-side pixel buffer → SDL_UpdateTexture**

Render all 80x25 characters (640x400 pixels) into a CPU-side `uint32_t` pixel buffer each frame, then `SDL_UpdateTexture()` to upload to GPU. Simple, portable, no shader complexity.

**Option B: OpenGL texture per frame**

Use `glTexImage2D` directly. The emulator already uses OpenGL 3+ for ImGui.

**Recommendation: Option A** — simpler, and at 640x400 the bandwidth is trivial (~1MB/frame at 32bpp). The emulator already runs at ~60fps with ImGui; adding a 1MB texture upload is negligible.

### New File: `src/emu_display.h`

```cpp
#pragma once

#include <SDL.h>

// Initialize display subsystem (creates SDL texture)
void display_init(SDL_Renderer* renderer);

// Render frame buffer to texture, call each frame
void display_render();

// Get the SDL texture for ImGui::Image()
SDL_Texture* display_get_texture();

// Get pixel dimensions for ImGui sizing
int display_get_pixel_width();   // 640
int display_get_pixel_height();  // 400

// Cleanup
void display_shutdown();
```

### New File: `src/emu_display.cpp`

```cpp
#include "emu_display.h"
#include "emu_video.h"
#include "n8_memory_map.h"
#include "n8_font.h"   // from docs/charset/ — copy to src/

#include <cstring>

extern uint8_t frame_buffer[];

// Font: 8 pixels wide, 16 pixels tall
#define FONT_W 8
#define FONT_H 16

// Display dimensions in pixels
#define DISPLAY_W (N8_VID_DEFAULT_WIDTH * FONT_W)   // 640
#define DISPLAY_H (N8_VID_DEFAULT_HEIGHT * FONT_H)  // 400

static SDL_Renderer* sdl_renderer = nullptr;
static SDL_Texture*  sdl_texture = nullptr;
static uint32_t      pixel_buf[DISPLAY_W * DISPLAY_H];

// Phosphor color (green by default, configurable later)
static const uint32_t COLOR_FG = 0xFF33FF33;  // ARGB green phosphor
static const uint32_t COLOR_BG = 0xFF000000;  // ARGB black

void display_init(SDL_Renderer* renderer) {
    sdl_renderer = renderer;
    sdl_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_W, DISPLAY_H
    );
    memset(pixel_buf, 0, sizeof(pixel_buf));
}

void display_render() {
    uint8_t width  = video_get_width();
    uint8_t height = video_get_height();
    uint8_t stride = video_get_stride();

    // Render each character cell
    for (int row = 0; row < height && row < N8_VID_DEFAULT_HEIGHT; row++) {
        for (int col = 0; col < width && col < N8_VID_DEFAULT_WIDTH; col++) {
            uint8_t ch = frame_buffer[row * stride + col];
            const uint8_t* glyph = n8_font[ch];

            int px_x = col * FONT_W;
            int px_y = row * FONT_H;

            for (int gy = 0; gy < FONT_H; gy++) {
                uint8_t glyph_row = glyph[gy];
                for (int gx = 0; gx < FONT_W; gx++) {
                    bool pixel_on = (glyph_row >> (7 - gx)) & 1;
                    pixel_buf[(px_y + gy) * DISPLAY_W + px_x + gx] =
                        pixel_on ? COLOR_FG : COLOR_BG;
                }
            }
        }
    }

    // Cursor rendering
    uint8_t cursor_style = video_get_cursor_style();
    uint8_t cursor_mode = cursor_style & 0x03;
    if (cursor_mode == 0x01 || cursor_mode == 0x02) {
        uint8_t ccol = video_get_cursor_col();
        uint8_t crow = video_get_cursor_row();

        if (ccol < width && crow < height) {
            bool cursor_visible = true;

            // Flash logic for mode 0x02
            if (cursor_mode == 0x02) {
                uint8_t rate = (cursor_style >> 3) & 0x03;
                uint32_t ticks = SDL_GetTicks();
                uint32_t period;
                switch (rate) {
                    case 0: cursor_visible = false; break; // off
                    case 1: period = 1000; break;  // slow
                    case 2: period = 500;  break;  // medium
                    case 3: period = 250;  break;  // fast
                }
                if (rate > 0) {
                    cursor_visible = ((ticks / period) % 2) == 0;
                }
            }

            if (cursor_visible) {
                bool block = (cursor_style >> 2) & 1;
                int px_x = ccol * FONT_W;
                int px_y = crow * FONT_H;
                int start_y = block ? 0 : (FONT_H - 2);  // block or underline
                int end_y   = FONT_H;

                for (int gy = start_y; gy < end_y; gy++) {
                    for (int gx = 0; gx < FONT_W; gx++) {
                        // XOR with current pixel for visibility
                        int idx = (px_y + gy) * DISPLAY_W + px_x + gx;
                        pixel_buf[idx] ^= (COLOR_FG ^ COLOR_BG);
                    }
                }
            }
        }
    }

    // Upload to GPU
    SDL_UpdateTexture(sdl_texture, NULL, pixel_buf, DISPLAY_W * sizeof(uint32_t));
}

SDL_Texture* display_get_texture() { return sdl_texture; }
int display_get_pixel_width()  { return DISPLAY_W; }
int display_get_pixel_height() { return DISPLAY_H; }

void display_shutdown() {
    if (sdl_texture) {
        SDL_DestroyTexture(sdl_texture);
        sdl_texture = nullptr;
    }
}
```

### Integration: `src/main.cpp`

**Problem:** The current code uses `SDL_GL_CreateContext()` (OpenGL) but `SDL_CreateTexture()` requires an `SDL_Renderer`. These two approaches conflict — you can't use both an OpenGL context (for ImGui) and an SDL_Renderer on the same window.

**Solution:** Use OpenGL directly to upload the texture.

Replace `SDL_CreateTexture` / `SDL_UpdateTexture` with:

```cpp
// In display_init():
glGenTextures(1, &gl_texture);
glBindTexture(GL_TEXTURE_2D, gl_texture);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DISPLAY_W, DISPLAY_H,
             0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

// In display_render() — upload:
glBindTexture(GL_TEXTURE_2D, gl_texture);
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_W, DISPLAY_H,
                GL_BGRA, GL_UNSIGNED_BYTE, pixel_buf);

// In main.cpp — ImGui display window:
ImGui::Begin("Display");
ImTextureID tex_id = (ImTextureID)(intptr_t)gl_texture;
ImGui::Image(tex_id, ImVec2(DISPLAY_W, DISPLAY_H));
ImGui::End();
```

This avoids the SDL_Renderer conflict entirely. ImGui natively supports OpenGL textures as `ImTextureID`.

**Modified `emu_display.h` / `emu_display.cpp`:** Use `GLuint gl_texture` instead of `SDL_Texture*`. Init with `display_init()` (no SDL_Renderer needed). Return `GLuint` for ImGui.

### main.cpp Changes

1. After `emulator_init()`, call `display_init()`.
2. In the render loop, after `emulator_step()` time slice, call `display_render()`.
3. Add new ImGui window:
   ```cpp
   static bool show_display_window = true;
   if (show_display_window) {
       ImGui::Begin("N8 Display", &show_display_window);
       ImTextureID tex_id = (ImTextureID)(intptr_t)display_get_gl_texture();
       ImVec2 sz(display_get_pixel_width(), display_get_pixel_height());
       ImGui::Image(tex_id, sz);
       ImGui::End();
   }
   ```
4. Add checkbox for display window in Emulator Control panel.
5. Call `display_shutdown()` in cleanup.

### Font File

Copy `docs/charset/n8_font.h` to `src/n8_font.h`. The font is `static const` so it will be compiled into the display module only.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_display.cpp` to `SOURCES`.

For tests, `emu_display.cpp` depends on OpenGL which is not available in the test build. Either:
- Conditionally compile display code (not needed in tests)
- Add display stubs to `test/test_stubs.cpp`

**Recommendation:** Guard `emu_display.cpp` with a compile flag or simply don't link it into the test binary. The display is pure output — no bus decode logic lives there. Tests interact with `frame_buffer[]` and `video_decode()` directly.

### Test Cases

No new unit tests for the rendering pipeline itself (it's visual output). Test verification:

1. Manual: Run emulator, load firmware, verify characters display correctly.
2. Automated smoke test: Write known characters to frame_buffer, call display_render(), verify pixel_buf contains expected pixel patterns (optional, can be added later).

### Risks

- **OpenGL context state:** ImGui manages the OpenGL state. Ensure `glBindTexture` / `glTexSubImage2D` calls don't interfere with ImGui's state. Call texture upload before `ImGui::Render()` or ensure state is saved/restored.
- **Performance:** 640x400 pixels * 4 bytes = 1MB per frame. At 60fps = 60MB/s texture upload. This is well within modern GPU bandwidth.

---

## Phase 6: Keyboard Device ($D860)

**Goal:** Implement keyboard input: SDL2 key events → ASCII/extended code translation → device registers at `$D860` → optional IRQ bit 2.

**Entry criteria:** Phase 5 complete (display working), all tests pass.
**Exit criteria:** Keyboard input works via `$D860-$D862` registers. IRQ bit 2 fires on keypress when enabled. All tests passing.

### Constants Update: `src/n8_memory_map.h`

```cpp
// --- Keyboard ---
#define N8_KBD_BASE        0xD860
#define N8_KBD_SLOT        3        // Device slot index
#define N8_KBD_DATA        0x00     // Register offsets
#define N8_KBD_STATUS      0x01
#define N8_KBD_ACK         0x01     // Same address, write = ACK
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

### New File: `src/emu_keyboard.h`

```cpp
#pragma once
#include <stdint.h>

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
#include "n8_memory_map.h"
#include "m6502.h"

// Internal state
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
        // Previous key not yet acknowledged — overflow
        kbd_status |= N8_KBD_STAT_OVERFLOW;
    }

    kbd_data = keycode;
    kbd_status = (kbd_status & ~0x3C) | (modifiers & 0x3C);  // Update modifier bits
    kbd_status |= N8_KBD_STAT_AVAIL;

    if (kbd_ctrl & N8_KBD_CTRL_IRQ_EN) {
        emu_set_irq(N8_IRQ_BIT_KBD);
    }
}

void keyboard_update_modifiers(uint8_t modifiers) {
    // Update live modifier bits without affecting AVAIL/OVERFLOW
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
            case N8_KBD_DATA:
                val = kbd_data;
                break;
            case N8_KBD_STATUS:
                val = kbd_status;
                break;
            case N8_KBD_CTRL:
                val = kbd_ctrl;
                break;
            default:
                val = 0x00;
                break;
        }
        M6502_SET_DATA(pins, val);
    } else {
        // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_KBD_ACK:   // $D861 write = acknowledge
                kbd_status &= ~(N8_KBD_STAT_AVAIL | N8_KBD_STAT_OVERFLOW);
                emu_clr_irq(N8_IRQ_BIT_KBD);
                break;
            case N8_KBD_CTRL:
                kbd_ctrl = val & N8_KBD_CTRL_IRQ_EN;  // Mask to valid bits
                break;
            default:
                break;  // KBD_DATA is read-only
        }
    }
}
```

### SDL2 Key Translation (in `main.cpp`)

SDL2 provides `SDL_KEYDOWN` events with `SDL_Keysym`. Translation to N8 keycodes:

```cpp
#include "emu_keyboard.h"

// In SDL event polling loop:
if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods = sdl_to_n8_modifiers(SDL_GetModState());
    if (n8_key != 0) {
        keyboard_key_down(n8_key, mods);
    }
}
```

**Translation function:**

```cpp
static uint8_t sdl_to_n8_keycode(SDL_Keysym keysym) {
    SDL_Keycode k = keysym.sym;
    uint16_t mod = keysym.mod;

    // Printable ASCII range
    if (k >= SDLK_SPACE && k <= SDLK_z) {
        uint8_t c = (uint8_t)(k & 0x7F);
        // Apply shift for letters
        if (k >= SDLK_a && k <= SDLK_z && (mod & KMOD_SHIFT)) {
            c -= 32;  // a→A
        }
        // Ctrl+letter → control character
        if (k >= SDLK_a && k <= SDLK_z && (mod & KMOD_CTRL)) {
            c = (k - SDLK_a) + 1;  // Ctrl+A=1, Ctrl+C=3, etc.
        }
        return c;
    }

    // Special keys
    switch (k) {
        case SDLK_RETURN:    return 0x0D;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB:       return 0x09;
        case SDLK_ESCAPE:    return 0x1B;

        // Extended range ($80+)
        case SDLK_UP:        return 0x80;
        case SDLK_DOWN:      return 0x81;
        case SDLK_LEFT:      return 0x82;
        case SDLK_RIGHT:     return 0x83;
        case SDLK_HOME:      return 0x84;
        case SDLK_END:       return 0x85;
        case SDLK_PAGEUP:    return 0x86;
        case SDLK_PAGEDOWN:  return 0x87;
        case SDLK_INSERT:    return 0x88;
        case SDLK_DELETE:    return 0x89;
        case SDLK_PRINTSCREEN: return 0x8A;
        case SDLK_PAUSE:     return 0x8B;

        case SDLK_F1:  return 0x90;
        case SDLK_F2:  return 0x91;
        case SDLK_F3:  return 0x92;
        case SDLK_F4:  return 0x93;
        case SDLK_F5:  return 0x94;
        case SDLK_F6:  return 0x95;
        case SDLK_F7:  return 0x96;
        case SDLK_F8:  return 0x97;
        case SDLK_F9:  return 0x98;
        case SDLK_F10: return 0x99;
        case SDLK_F11: return 0x9A;
        case SDLK_F12: return 0x9B;

        default: return 0;  // Unknown key, ignore
    }
}

static uint8_t sdl_to_n8_modifiers(uint16_t sdl_mod) {
    uint8_t mods = 0;
    if (sdl_mod & KMOD_SHIFT) mods |= N8_KBD_STAT_SHIFT;
    if (sdl_mod & KMOD_CTRL)  mods |= N8_KBD_STAT_CTRL;
    if (sdl_mod & KMOD_ALT)   mods |= N8_KBD_STAT_ALT;
    if (sdl_mod & KMOD_CAPS)  mods |= N8_KBD_STAT_CAPS;
    return mods;
}
```

**Important:** The `!io.WantCaptureKeyboard` guard ensures keystrokes meant for ImGui input fields (e.g., breakpoint input, memory range input) are NOT forwarded to the emulated keyboard. The N8 display window should be focused/active for keyboard input. This may need refinement — consider a "keyboard capture" toggle.

### Integration with Device Router

**`src/emulator.cpp`:**

```cpp
#include "emu_keyboard.h"

// In device router:
case 3:  // $D860: Keyboard
    keyboard_decode(pins, reg);
    break;
```

Add `keyboard_init()` to `emulator_init()`, `keyboard_reset()` to `emulator_reset()`.

### Build Changes

**`Makefile`:** Add `$(SRC_DIR)/emu_keyboard.cpp` to `SOURCES` and `TEST_SRC_OBJS`.

### Test Cases

**New file: `test/test_keyboard.cpp`**

```
T_KBD_01: After reset, KBD_STATUS DATA_AVAIL is 0
  - keyboard_reset()
  - Read $D861 → 0x00

T_KBD_02: keyboard_inject_key sets DATA_AVAIL
  - keyboard_inject_key('A', 0)
  - Read $D861 → bit 0 set
  - Read $D860 → 0x41

T_KBD_03: Write to KBD_ACK ($D861) clears DATA_AVAIL
  - keyboard_inject_key('B', 0)
  - Write any value to $D861
  - Read $D861 → bit 0 clear

T_KBD_04: Second key before ACK sets OVERFLOW
  - keyboard_inject_key('A', 0)
  - keyboard_inject_key('B', 0)
  - Read $D861 → bits 0 AND 1 set
  - Read $D860 → 0x42 (most recent wins)

T_KBD_05: ACK clears both AVAIL and OVERFLOW
  - keyboard_inject_key('A', 0)
  - keyboard_inject_key('B', 0)
  - Write $D861
  - Read $D861 → 0x00

T_KBD_06: Modifier bits reflect live state
  - keyboard_update_modifiers(N8_KBD_STAT_SHIFT | N8_KBD_STAT_CTRL)
  - Read $D861 → bits 2,3 set, bit 0 clear (no key data)

T_KBD_07: IRQ fires when KBD_CTRL bit 0 enabled
  - Write 0x01 to $D862 (enable IRQ)
  - keyboard_inject_key('X', 0)
  - Check mem[N8_IRQ_ADDR] & (1 << N8_IRQ_BIT_KBD) != 0

T_KBD_08: IRQ does NOT fire when KBD_CTRL bit 0 disabled (default)
  - keyboard_inject_key('X', 0)
  - Check mem[N8_IRQ_ADDR] & (1 << N8_IRQ_BIT_KBD) == 0

T_KBD_09: ACK deasserts IRQ
  - Write 0x01 to $D862
  - keyboard_inject_key('Y', 0)
  - Write $D861 (ACK)
  - Check mem[N8_IRQ_ADDR] & (1 << N8_IRQ_BIT_KBD) == 0

T_KBD_10: Extended keycodes ($80-$9B) round-trip through registers
  - keyboard_inject_key(0x80, 0)  // Up arrow
  - Read $D860 → 0x80

T_KBD_11: keyboard_reset clears all state
  - keyboard_inject_key('Z', N8_KBD_STAT_SHIFT)
  - keyboard_reset()
  - Read $D861 → 0x00
  - Read $D860 → 0x00
```

### Risks

- **ImGui keyboard conflict:** SDL2 key events go to both ImGui and our keyboard handler. The `io.WantCaptureKeyboard` check is essential. May need a dedicated "focus mode" for the display window.
- **Shift/symbol translation:** The initial translation is simplified. Shifted number keys (!, @, # etc.) need a lookup table. This can be added incrementally.
- **Key repeat:** SDL2 sends repeat events. The N8 keyboard is single-shot (no auto-repeat at hardware level). Filter `event.key.repeat != 0` to ignore repeats, or let firmware handle it.

---

## Phase 7: ROM Layout Migration

**Goal:** Update the ROM layout from a flat 12KB at `$D000` to the segmented layout: Kernel Entry ($FE00), Kernel Implementation ($F000-$FDFF), Monitor/stdlib ($E000-$EFFF). Also establish the Dev Bank region at `$D000-$D7FF`.

**Entry criteria:** Phase 6 complete, all tests pass.
**Exit criteria:** ROM loading supports the new layout. Tests verify code execution from all ROM segments. Dev Bank at `$D000-$D7FF` is recognized as separate from ROM.

### Understanding the Change

Currently `emulator_loadrom()` does:
```cpp
uint16_t rom_ptr = 0xD000;
// Load sequential bytes from file
```

The firmware binary is a flat 12KB blob starting at `$D000`. The cc65 linker places code anywhere in `$D000-$FFF9` and the vectors at `$FFFA-$FFFF`.

The new layout divides this space into regions with different purposes, but the **binary loading mechanism is unchanged** — the cc65 linker still outputs a flat binary. The linker config determines where segments land within the binary. The emulator still loads the file starting at `$D000` and fills through `$FFFF`.

**Key insight:** The emulator's `emulator_loadrom()` does not need to change for the basic case. The ROM binary is still loaded at `$D000` and fills `$D000-$FFFF`. What changes is:

1. The cc65 linker config (Phase 8) — segments land in new regions
2. The emulator's bus decode — `$D000-$D7FF` should not be ROM (it's Dev Bank = RAM/device expansion)
3. The emulator's bus decode — `$D800-$DFFF` is device registers (not ROM)

### Constants Update: `src/n8_memory_map.h`

```cpp
// --- ROM Regions ---
#define N8_ROM_BASE        0xE000   // Start of ROM (was 0xD000)
#define N8_ROM_END         0xFFFF
#define N8_ROM_SIZE        0x2000   // 8KB total (Monitor + Kernel)

#define N8_ROM_MONITOR     0xE000   // Monitor / stdlib
#define N8_ROM_KERNEL_IMPL 0xF000   // Kernel Implementation
#define N8_ROM_KERNEL_ENTRY 0xFE00  // Kernel Entry

// --- Dev Bank (RAM-like, for future bank switching) ---
#define N8_DEVBANK_BASE    0xD000
#define N8_DEVBANK_SIZE    0x0800   // 2KB
#define N8_DEVBANK_END     0xD7FF
```

### Code Changes

**`src/emulator.cpp`:**

1. `emulator_loadrom()`: The binary is still loaded at `$D000` for now. But we need to decide: does the firmware binary include the `$D000-$D7FF` dev bank region and `$D800-$DFFF` device region? If so, those bytes are loaded into `mem[]` but get overridden by device decode.

   **Decision:** Keep loading from `$D000` for backward compatibility. The cc65 linker config (Phase 8) will place no code in `$D000-$DFFF`. The emulator loads the binary at `$D000`, and bytes at `$D000-$DFFF` are just padding. ROM execution only happens at `$E000+`.

   However, for a clean design, **load the binary at the ROM base**:
   ```cpp
   void emulator_loadrom() {
       // Load binary, detect size, place at appropriate address
       FILE *fp = fopen(rom_file, "r");
       if (!fp) { printf("ROM not found\n"); return; }

       fseek(fp, 0, SEEK_END);
       long size = ftell(fp);
       fseek(fp, 0, SEEK_SET);

       // Determine load address based on binary size
       // Old format: 12KB → load at $D000
       // New format: 8KB → load at $E000
       uint16_t load_addr;
       if (size <= 0x2000) {
           load_addr = N8_ROM_BASE;  // $E000 (new 8KB layout)
       } else {
           load_addr = 0xD000;       // Legacy 12KB layout
       }

       fread(&mem[load_addr], 1, size, fp);
       fclose(fp);
   }
   ```

   **Or simpler:** Just always load at `$D000` with the full size. Let the linker config deal with placement. The `$D000-$D7FF` dev bank just happens to contain ROM padding bytes, which is harmless since dev bank is writable RAM anyway.

   **Recommendation:** Keep loading at `$D000` (no change). This is simpler and backward-compatible. The dev bank bytes are just zeros/padding from the linker.

2. **Bus decode for Dev Bank ($D000-$D7FF):** This region is RAM-like. The current code treats it as ROM (no write protection anyway). No change needed — `mem[$D000-$D7FF]` is readable/writable, which is correct for a dev bank.

3. **ROM read-only enforcement (optional, deferred):** Not implementing ROM write protection in this phase. It's a separate concern and the current codebase doesn't enforce it.

### Test Cases

```
T_ROM_01: Code executes from $E000 (Monitor region)
  - EmulatorFixture, load NOP sled at $E000
  - set_reset_vector($E000)
  - step_n(20)
  - CHECK(PC >= $E000 && PC < $E010)

T_ROM_02: Code executes from $F000 (Kernel Implementation)
  - Load LDA #$42; STA $0200 at $F000
  - set_reset_vector($F000)
  - step_n(20)
  - CHECK(mem[0x0200] == 0x42)

T_ROM_03: Code executes from $FE00 (Kernel Entry)
  - Load NOP sled at $FE00
  - set_reset_vector($FE00)
  - step_n(20)
  - CHECK(PC >= $FE00)

T_ROM_04: Dev Bank $D000-$D7FF is writable RAM
  - LDA #$AA; STA $D000
  - CHECK(mem[0xD000] == 0xAA)
  - LDA $D000; STA $0200
  - CHECK(mem[0x0200] == 0xAA)
```

### Risks

- **Backward compatibility:** Old firmware binaries are 12KB at `$D000`. The emulator still loads them correctly. New firmware will be 8KB or smaller, loaded at `$D000` with code only in `$E000+`.
- **Dev Bank vs ROM ambiguity:** `$D000-$D7FF` is loaded from the ROM file but treated as writable RAM. This is intentional — it's a transition step toward proper bank switching.

---

## Phase 8: Firmware Migration

**Goal:** Update cc65 linker configs and firmware source files to use new device register addresses and ROM layout.

**Entry criteria:** Phase 7 complete, emulator tests pass with new memory map.
**Exit criteria:** Firmware builds with new addresses. `make firmware` succeeds. Firmware runs correctly on updated emulator.

### Linker Config Changes

**`firmware/n8.cfg`:**

```
MEMORY {
    ZP:        start =    $0, size =  $100, type   = rw, define = yes;
    RAM:       start =  $200, size = $BDFF, define = yes;
    ROM:       start = $E000, size = $2000, file   = %O;
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

Key changes:
- ROM starts at `$E000` (was `$D000`), size is `$2000` (8KB, was `$3000`)
- RAM end extended to `$BDFF` (was `$BD00`) — covers up to `$BFFF` when combined with stack

**`firmware/gdb_playground/playground.cfg` and `firmware/playground/playground.cfg`:**

Same change: ROM start = `$E000`, size = `$2000`.

### Firmware Source Changes

**`firmware/devices.h`:**

```c
#ifndef __DEVICES__
#define __DEVICES__

/* Device registers in $D800-$DFFF region */
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

/* Frame buffer */
#define   FB_BASE      $C000

/* Legacy aliases (removed) */
/* TXT_CTRL, TXT_BUFF — replaced by FB_BASE + VID registers */

#endif
```

**`firmware/devices.inc`:**

```asm
; Device registers in $D800-$DFFF region
.define   IRQ_FLAGS    $D800

.define   TTY_OUT_CTRL $D820
.define   TTY_OUT_DATA $D821
.define   TTY_IN_CTRL  $D822
.define   TTY_IN_DATA  $D823

.define   VID_MODE     $D840
.define   VID_WIDTH    $D841
.define   VID_HEIGHT   $D842
.define   VID_STRIDE   $D843
.define   VID_OPER     $D844
.define   VID_CURSOR   $D845
.define   VID_CURCOL   $D846
.define   VID_CURROW   $D847

.define   KBD_DATA     $D860
.define   KBD_STATUS   $D861
.define   KBD_ACK      $D861
.define   KBD_CTRL     $D862

.define   FB_BASE      $C000
```

**`firmware/zp.inc`:**

Remove `ZP_IRQ $FF` — IRQ flags are no longer in zero page.

```asm
; Zero Page locations

.define ZP_A_PTR    $E0
.define ZP_B_PTR    $E2
.define ZP_C_PTR    $E4
.define ZP_D_PTR    $E6

.define BYTE_0      $F0
.define BYTE_1      $F1
.define BYTE_2      $F2
.define BYTE_3      $F3

; IRQ_FLAGS is now at $D800 (devices.inc), NOT in zero page
```

**`firmware/tty.s`:** Update register references:
- `TTY_OUT_CTRL` → already defined in devices.inc (just the value changes)
- `TTY_OUT_DATA` → same
- `TTY_IN_CTRL` → now `TTY_IN_CTRL` (was `TTY_IN_CTRL`)
- `TTY_IN_DATA` → same

No source changes needed — the assembly uses symbolic names from `devices.inc`. Only the definitions change.

**`firmware/interrupt.s`:** Same — uses `TTY_IN_CTRL` and `TTY_IN_DATA` from `devices.inc`. Also uses `TXT_BUFF` which needs to change:
- `STA TXT_BUFF` → `STA FB_BASE` (for debug display in frame buffer)
- `STA TXT_BUFF+1` → `STA FB_BASE+1`

**`firmware/main.s`:** Uses `TXT_BUFF` — change to `FB_BASE`:
- `STA TXT_BUFF,X` → `STA FB_BASE,X`

### emulator_loadrom() Adjustment

The new firmware binary starts at `$E000`. But `emulator_loadrom()` currently loads at `$D000`. Two options:

1. **Pad the binary:** The cc65 linker outputs `$E000-$FFFF` as an 8KB file. Load at `$E000`:
   ```cpp
   uint16_t rom_ptr = N8_ROM_BASE;  // $E000
   ```

2. **Auto-detect:** Check file size. 12KB = old layout (load at `$D000`). 8KB = new layout (load at `$E000`).

**Recommendation:** Option 1, with a constant. Clean and simple. Update `emulator_loadrom()`:

```cpp
void emulator_loadrom() {
    uint16_t rom_ptr = N8_ROM_BASE;  // $E000
    printf("Loading ROM at $%04X\r\n", rom_ptr);
    // ... rest unchanged
}
```

### Test Cases

Firmware tests are integration tests — build firmware, run emulator, verify behavior. The existing `make firmware` target should build successfully with the new config.

```
Manual: make firmware && ./n8
  - Verify welcome banner prints on TTY
  - Verify keyboard echo works
  - Verify no crash on startup

Automated (if gdb_playground tests use new config):
  - make -C firmware/gdb_playground
  - Load test binaries, verify via n8gdb
```

### Risks

- **All firmware must be rebuilt:** Old binaries will not work with the new emulator (addresses have moved). This is a breaking change. Both sides must be updated together.
- **Playground programs:** All playground `.cfg` files need updating. There are 3 configs total: `firmware/n8.cfg`, `firmware/gdb_playground/playground.cfg`, `firmware/playground/playground.cfg`.

---

## Phase 9: GDB Stub Memory Map Update

**Goal:** Update the GDB stub's embedded XML memory map to reflect the new layout. Update GDB memory read/write callbacks to handle device registers.

**Entry criteria:** Phase 8 complete, firmware builds and runs.
**Exit criteria:** GDB memory map XML matches new layout. n8gdb can inspect all memory regions. All GDB tests pass.

### Code Changes

**`src/gdb_stub.cpp`:**

Update `memory_map_xml`:

```cpp
static const char memory_map_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE memory-map SYSTEM \"gdb-memory-map.dtd\">\n"
    "<memory-map>\n"
    "  <memory type=\"ram\"  start=\"0x0000\" length=\"0xC000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC000\" length=\"0x1000\"/>\n"  // Frame buffer
    "  <memory type=\"ram\"  start=\"0xD000\" length=\"0x0800\"/>\n"  // Dev Bank
    "  <memory type=\"ram\"  start=\"0xD800\" length=\"0x0800\"/>\n"  // Device regs
    "  <memory type=\"rom\"  start=\"0xE000\" length=\"0x2000\"/>\n"  // ROM
    "</memory-map>\n";
```

**`src/main.cpp`:**

Update `gdb_read_mem` / `gdb_write_mem` to also handle device register reads via the device decode functions. Currently, GDB reads `mem[addr]` directly, which works for RAM/ROM but not for device registers (which may have side effects or return computed values).

For Phase 9, the simplest correct approach:

```cpp
static uint8_t gdb_read_mem(uint16_t addr) {
    if (addr >= N8_FB_BASE && addr <= N8_FB_END)
        return frame_buffer[addr - N8_FB_BASE];
    // Device registers: return mem[addr] which holds the last-written value
    // This is "good enough" for GDB — avoids side effects from device decode
    return mem[addr];
}
```

Device register reads via GDB should NOT trigger side effects (e.g., reading KBD_DATA via GDB should not clear DATA_AVAIL). Returning `mem[addr]` is the safe default — it shows whatever value was last written by the CPU or the device, without invoking device decode logic.

### Test Cases

Update GDB protocol tests that check memory map XML response:
- `test/test_gdb_protocol.cpp`: Any test checking `qXfer:memory-map:read` response needs updated expected values.

### Risks

- Low risk. XML is a static string. GDB callbacks are minimal changes.

---

## Risk Assessment & Rollback Strategy

### Overall Risk Profile

| Phase | Risk | Impact | Mitigation |
|-------|------|--------|------------|
| 0 | Very Low | None (pure refactor) | `make test` before/after |
| 1 | Low | IRQ mechanism could break | Tests cover IRQ path; firmware not yet updated |
| 2 | Low | TTY device addressing | Self-contained; tests comprehensive |
| 3 | **Medium** | Structural change to emulator_step() | Thorough test coverage of all bus regions |
| 4 | Low | New code, isolated module | Unit tests for all register behaviors |
| 5 | **Medium** | OpenGL integration, SDL2 interaction | Manual visual verification; no test regressions |
| 6 | Low | New device, well-understood pattern | Unit tests; SDL translation is isolatable |
| 7 | Low | ROM layout, backward compat | Auto-detect or fixed constant |
| 8 | **Medium** | Firmware source changes across many files | `make firmware` as gate; playground test programs |
| 9 | Very Low | Static XML update | GDB test suite |

### Rollback Strategy

Each phase is a separate commit (or small commit series). Rollback = `git revert` of that phase's commits.

**Critical invariant:** At the end of every phase, `make test` passes. If tests fail, the phase is not complete.

**Branch strategy:** Work on a feature branch. Each phase is a PR or merge point. Main branch always has passing tests.

### Testing Pyramid

```
                    ┌──────────────┐
                    │  Manual/E2E  │  Run emulator + firmware
                    ├──────────────┤
                    │ Integration  │  test_integration.cpp, test_gdb_callbacks.cpp
                    ├──────────────┤
              ┌─────┴──────────────┴─────┐
              │        Unit Tests        │  test_bus, test_tty, test_video, test_keyboard
              └──────────────────────────┘
```

---

## Dependency Graph

```
Phase 0 ─── Machine Constants Header
   │
   ▼
Phase 1 ─── IRQ Migration ($00FF → $D800)
   │         + Device register router skeleton
   ▼
Phase 2 ─── TTY Migration ($C100 → $D820)
   │         + Plug into device router
   ▼
Phase 3 ─── Frame Buffer Expansion (256B → 4KB)
   │         + Separate backing store
   │         + GDB callback update
   ▼
Phase 4 ─── Video Control Registers ($D840)
   │         + Scroll operations
   ▼
Phase 5 ─── Video Rendering Pipeline
   │         + N8 font integration
   │         + OpenGL texture + ImGui display window
   ▼
Phase 6 ─── Keyboard Device ($D860)
   │         + SDL2 key translation
   │         + IRQ bit 2
   ▼
Phase 7 ─── ROM Layout Migration
   │         + Dev Bank recognition
   ▼
Phase 8 ─── Firmware Migration
   │         + cc65 linker configs
   │         + Assembly source address updates
   ▼
Phase 9 ─── GDB Stub Memory Map Update
```

Phases 4, 5, and 6 have a dependency on Phase 3 (frame buffer) but are otherwise independent of each other. They could be parallelized if needed, though sequential is recommended for clean testing.

Phase 7 can technically be done earlier (even after Phase 1), but doing it late means all device register tests are settled before touching ROM layout.

Phase 8 depends on ALL prior phases being complete — firmware must use all new addresses simultaneously.

---

## Summary of Files Modified Per Phase

| Phase | Modified Files | New Files |
|-------|---------------|-----------|
| 0 | `emulator.cpp`, `emulator.h`, `emu_tty.cpp`, `test_helpers.h`, `test_tty.cpp`, `test_bus.cpp`, `test_integration.cpp` | `src/n8_memory_map.h` |
| 1 | `emulator.cpp`, `n8_memory_map.h`, `test_bus.cpp` | — |
| 2 | `emulator.cpp`, `n8_memory_map.h`, `test_tty.cpp` | — |
| 3 | `emulator.cpp`, `emulator.h`, `main.cpp`, `n8_memory_map.h`, `test_bus.cpp`, `test_integration.cpp` | — |
| 4 | `emulator.cpp`, `n8_memory_map.h`, `Makefile` | `src/emu_video.h`, `src/emu_video.cpp`, `test/test_video.cpp` |
| 5 | `main.cpp`, `Makefile` | `src/emu_display.h`, `src/emu_display.cpp`, `src/n8_font.h` (copy) |
| 6 | `emulator.cpp`, `main.cpp`, `n8_memory_map.h`, `Makefile` | `src/emu_keyboard.h`, `src/emu_keyboard.cpp`, `test/test_keyboard.cpp` |
| 7 | `emulator.cpp`, `n8_memory_map.h`, `test_bus.cpp` | — |
| 8 | `firmware/n8.cfg`, `firmware/devices.h`, `firmware/devices.inc`, `firmware/zp.inc`, `firmware/interrupt.s`, `firmware/main.s`, `firmware/gdb_playground/playground.cfg`, `firmware/playground/playground.cfg`, `emulator.cpp` | — |
| 9 | `gdb_stub.cpp`, `test_gdb_protocol.cpp` | — |

**Total new files:** 7 (n8_memory_map.h, emu_video.h/cpp, emu_display.h/cpp, emu_keyboard.h/cpp, n8_font.h copy, test_video.cpp, test_keyboard.cpp)

**Total modified files:** ~20 unique files across all phases
