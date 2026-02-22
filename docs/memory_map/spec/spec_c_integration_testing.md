# Spec C: Integration, Testing & Firmware Migration

> Covers the full incremental migration from the current memory map to the
> proposed layout (see `docs/memory_map/README.md` and `hardware.md`), plus
> implementation of the revised video and keyboard subsystems.

**Baseline:** 221 tests passing (349 assertions), 0 failures.

---

## Table of Contents

1. [Migration Overview](#1-migration-overview)
2. [Phase 0 — Preparation & Constants Header](#2-phase-0--preparation--constants-header)
3. [Phase 1 — IRQ Flag Register Move](#3-phase-1--irq-flag-register-move)
4. [Phase 2 — TTY Device Move](#4-phase-2--tty-device-move)
5. [Phase 3 — Frame Buffer Expansion](#5-phase-3--frame-buffer-expansion)
6. [Phase 4 — Video Control Registers](#6-phase-4--video-control-registers)
7. [Phase 5 — Keyboard Registers](#7-phase-5--keyboard-registers)
8. [Phase 6 — ROM Split & Firmware Linker Migration](#8-phase-6--rom-split--firmware-linker-migration)
9. [Phase 7 — GDB Stub & n8gdb Client Updates](#9-phase-7--gdb-stub--n8gdb-client-updates)
10. [Phase 8 — Firmware Source Migration](#10-phase-8--firmware-source-migration)
11. [Phase 9 — Playground & GDB Playground Migration](#11-phase-9--playground--gdb-playground-migration)
12. [Phase 10 — End-to-End Validation](#12-phase-10--end-to-end-validation)
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
| `$D800-$D81F` | System/IRQ            | 32      |
| `$D820-$D83F` | TTY                   | 32      |
| `$D840-$D85F` | Video Control         | 32      |
| `$D860-$D87F` | Keyboard              | 32      |
| `$D880-$DFFF` | Future Devices        | varies  |
| `$E000-$EFFF` | Monitor/stdlib        | 4 KB    |
| `$F000-$FDFF` | Kernel Implementation | 3.5 KB  |
| `$FE00-$FFDF` | Kernel Entry          | 480     |
| `$FFE0-$FFFF` | Vectors + Bank Switch | 32      |

### Migration Principles

1. **One device at a time.** Each phase moves or adds exactly one
   hardware entity. All prior tests must still pass.
2. **Feature flags during transition.** Where possible, new bus decode
   code coexists with old (compile-time or runtime switch) until old
   code is removed.
3. **Test-first.** New test cases are written and committed before the
   code that makes them pass.
4. **Firmware follows emulator.** The emulator bus decode moves first;
   firmware addresses update in a later phase.

---

## 2. Phase 0 — Preparation & Constants Header

### Objective

Create a single source of truth for all hardware addresses, used by both
emulator C++ and (via a parallel `.inc` file) firmware assembly. Eliminate
magic numbers in emulator.cpp, emu_tty.cpp, and test code.

### Deliverables

**New file: `src/hw_regs.h`**

```cpp
#pragma once

// ---- System / IRQ ----
#define HW_IRQ_FLAGS      0xD800    // was $00FF

// ---- TTY ----
#define HW_TTY_BASE       0xD820    // was $C100
#define HW_TTY_OUT_STATUS 0xD820    // was $C100
#define HW_TTY_OUT_DATA   0xD821    // was $C101
#define HW_TTY_IN_STATUS  0xD822    // was $C102
#define HW_TTY_IN_DATA    0xD823    // was $C103
#define HW_TTY_SIZE       0x20

// ---- Video Control ----
#define HW_VID_BASE       0xD840
#define HW_VID_MODE       0xD840
#define HW_VID_WIDTH      0xD841
#define HW_VID_HEIGHT     0xD842
#define HW_VID_STRIDE     0xD843
#define HW_VID_OPER       0xD844
#define HW_VID_CURSOR     0xD845
#define HW_VID_CURCOL     0xD846
#define HW_VID_CURROW     0xD847
#define HW_VID_SIZE       0x20

// ---- Keyboard ----
#define HW_KBD_BASE       0xD860
#define HW_KBD_DATA       0xD860
#define HW_KBD_STATUS     0xD861   // read
#define HW_KBD_ACK        0xD861   // write
#define HW_KBD_CTRL       0xD862
#define HW_KBD_SIZE       0x20

// ---- Frame Buffer ----
#define HW_FB_BASE        0xC000
#define HW_FB_SIZE        0x1000   // 4 KB  (was 0x0100)

// ---- Device Space (full range) ----
#define HW_DEV_BASE       0xD800
#define HW_DEV_END        0xDFFF

// ---- ROM regions ----
#define HW_ROM_MONITOR    0xE000   // 4 KB
#define HW_ROM_IMPL       0xF000   // 3.5 KB
#define HW_ROM_ENTRY      0xFE00   // 480 bytes
#define HW_ROM_VECTORS    0xFFE0   // 32 bytes (old: $FFFA, 6 bytes)

// ---- Legacy addresses (removed after full migration) ----
#define LEGACY_IRQ_FLAGS  0x00FF
#define LEGACY_TTY_BASE   0xC100
#define LEGACY_FB_SIZE    0x0100
#define LEGACY_ROM_BASE   0xD000
#define LEGACY_ROM_SIZE   0x3000
```

**New file: `firmware/hw_regs.inc`** (assembly-language parallel)

```asm
; hw_regs.inc — device register addresses
; Mirror of src/hw_regs.h for cc65 assembly

.define HW_IRQ_FLAGS      $D800

.define HW_TTY_OUT_STATUS $D820
.define HW_TTY_OUT_DATA   $D821
.define HW_TTY_IN_STATUS  $D822
.define HW_TTY_IN_DATA    $D823

.define HW_VID_MODE       $D840
.define HW_VID_WIDTH      $D841
.define HW_VID_HEIGHT     $D842
.define HW_VID_STRIDE     $D843
.define HW_VID_OPER       $D844
.define HW_VID_CURSOR     $D845
.define HW_VID_CURCOL     $D846
.define HW_VID_CURROW     $D847

.define HW_KBD_DATA       $D860
.define HW_KBD_STATUS     $D861
.define HW_KBD_ACK        $D861
.define HW_KBD_CTRL       $D862

.define HW_FB_BASE        $C000
```

### Test changes

None yet. This phase only adds files; no behavior changes.

### Phase Gate

- `make` compiles cleanly with the new header present but not yet included.
- `make test` still produces: **221 passed, 0 failed**.

---

## 3. Phase 1 — IRQ Flag Register Move

### Objective

Move the IRQ flags register from `$00FF` (zero page) to `$D800` (device
space). This is the simplest device register and touches the most code paths,
making it an ideal first migration target.

### Emulator Changes

**`src/emulator.cpp`:**

1. Include `hw_regs.h`.
2. Replace `#define IRQ_CLR() mem[0x00FF] = 0x00;` with
   `#define IRQ_CLR() mem[HW_IRQ_FLAGS] = 0x00;`
3. Replace `#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)`
   with `#define IRQ_SET(bit) mem[HW_IRQ_FLAGS] = (mem[HW_IRQ_FLAGS] | 0x01 << bit)`
4. `emu_set_irq()` — change `mem[0x00FF]` references to `mem[HW_IRQ_FLAGS]`.
5. `emu_clr_irq()` — same.
6. `emulator_step()` — change `if(mem[0x00FF] == 0)` to
   `if(mem[HW_IRQ_FLAGS] == 0)`.
7. Add bus decode for `$D800` region (device space). Initially just
   read/write passthrough to `mem[]` (no special behavior).

**`src/emulator_show_status_window()`:**
- Change `mem[0x00FF]` reference in the IRQ display line.

### Test Changes

**Modified: `test/test_utils.cpp`:**

Update T69, T69a, T69b, T70, T70a — all references to `mem[0x00FF]`
become `mem[HW_IRQ_FLAGS]` (`0xD800`).

```cpp
// T69: emu_set_irq(1) sets bit 1 in mem[HW_IRQ_FLAGS]
TEST_CASE("T69: emu_set_irq(1) sets bit 1 at IRQ_FLAGS register") {
    mem[HW_IRQ_FLAGS] = 0;
    emu_set_irq(1);
    CHECK((mem[HW_IRQ_FLAGS] & 0x02) != 0);
}
```

**Modified: `test/test_tty.cpp`:**

All lines that set `mem[0x00FF] = 0;` change to `mem[HW_IRQ_FLAGS] = 0;`.
T76 check changes from `mem[0x00FF]` to `mem[HW_IRQ_FLAGS]`.

**New tests in `test/test_bus.cpp`:**

| Test ID | Description |
|---------|-------------|
| T110 | Write to `$D800` stores in mem[], read returns value |
| T111 | IRQ_FLAGS cleared every tick (`IRQ_CLR()` behavior) |
| T112 | IRQ line asserted when IRQ_FLAGS non-zero after tick |
| T113 | Old address `$00FF` no longer acts as IRQ register |

### Phase Gate

- All 221 existing tests pass (with updated addresses).
- T110-T113 (4 new tests) also pass.
- **Total: 225 tests.**

### Rollback

Revert `emulator.cpp` IRQ macros and helpers to use `0x00FF`. Revert
test address constants.

---

## 4. Phase 2 — TTY Device Move

### Objective

Move TTY device registers from `$C100-$C10F` to `$D820-$D83F`.

### Emulator Changes

**`src/emulator.cpp`:**

1. Change TTY bus decode from:
   ```cpp
   BUS_DECODE(addr, 0xC100, 0xFFF0) {
       const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
       tty_decode(pins, dev_reg);
   }
   ```
   to:
   ```cpp
   BUS_DECODE(addr, HW_TTY_BASE, 0xFFE0) {
       const uint8_t dev_reg = (addr - HW_TTY_BASE) & 0x1F;
       tty_decode(pins, dev_reg);
   }
   ```
   The mask `0xFFE0` matches any address in `$D820-$D83F` (32-byte block).

**`src/emu_tty.cpp`:**

No changes required. `tty_decode()` takes a relative register offset
(0-3), which is computed by the caller. The TTY module is address-agnostic.

### Test Changes

**Modified: `test/test_tty.cpp`:**

All `make_read_pins(0xC1xx)` and `make_write_pins(0xC1xx, ...)` calls
update to use `HW_TTY_*` addresses:

| Old Address | New Address | Register |
|-------------|-------------|----------|
| `0xC100`    | `0xD820`    | OUT_STATUS |
| `0xC101`    | `0xD821`    | OUT_DATA |
| `0xC102`    | `0xD822`    | IN_STATUS |
| `0xC103`    | `0xD823`    | IN_DATA |

T78a (phantom reg test) changes to `0xD824`-`0xD82F` (regs 4-15 in new
32-byte block).

**Modified: `test/test_gdb_callbacks.cpp`:**

- `mem[0xC100]` references become `mem[HW_TTY_OUT_STATUS]`.
- `make_read_pins(0xC103)` becomes `make_read_pins(HW_TTY_IN_DATA)`.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T114 | TTY read at old address `$C100` does NOT trigger tty_decode |
| T115 | TTY read at `$D820` returns OUT_STATUS (0x00) |
| T116 | TTY write at `$D821` sends character (putchar side effect) |

### Phase Gate

- All prior tests pass (with updated addresses).
- T114-T116 pass.
- **Total: 228 tests.**

### Rollback

Revert bus decode mask in `emulator.cpp` to `0xC100, 0xFFF0`. Revert
test addresses.

---

## 5. Phase 3 — Frame Buffer Expansion

### Objective

Expand frame buffer from 256 bytes (`$C000-$C0FF`) to 4 KB
(`$C000-$CFFF`). The bus decode mask must change, and a separate
backing store (not part of main `mem[]`) is introduced per the
hardware.md spec.

### Design Decision

The spec says "CPU reads/writes are intercepted by bus decode and
routed to a separate backing store (not backed by main RAM)."

**However**, for Phase 3 we keep using `mem[]` as the backing store.
Separating it into `frame_buffer[4096]` is deferred because:

- The existing tests write to `mem[0xC000]` and read back from
  `mem[0xC000]`. A separate backing store would break these tests
  immediately.
- The GDB stub reads `mem[]` directly. Routing would need additional
  callbacks.
- The immediate need is address range expansion, not storage isolation.

A future phase (after video rendering integration) can introduce
`frame_buffer[]` with a compatibility layer.

### Emulator Changes

**`src/emulator.cpp`:**

Currently there is no explicit frame buffer bus decode in `emulator_step()`.
The `$C000` region falls through to the generic `mem[]` read/write. This
continues to work for the expanded 4 KB region because `mem[]` is 64 KB.

The only code change needed is ensuring no other bus decode claims
`$C100-$CFFF`. Since TTY moved to `$D820` in Phase 2, the old
`$C100` decode line is gone. No emulator code change is actually
required for this phase.

**`src/machine.h`:**

Add a constant for reference:

```cpp
const int frame_buffer_size = 4096;  // $C000-$CFFF
```

### Test Changes

**Modified: `test/test_bus.cpp`:**

T66 tested `$C0FF` as the frame buffer end boundary. Add tests for the
new range.

**New tests:**

| Test ID | Description |
|---------|-------------|
| T117 | Write to `$C100` stores in mem[0xC100] (no longer TTY) |
| T118 | Write to `$C7CF` (end of 80x25 active area) stores correctly |
| T119 | Write to `$CFFF` (last byte of 4KB FB) stores correctly |
| T120 | Read from `$C500` returns previously written value |

### Phase Gate

- All prior tests pass.
- T117-T120 pass.
- **Total: 232 tests.**

### Rollback

No emulator code was changed. Only tests added. Revert test file.

---

## 6. Phase 4 — Video Control Registers

### Objective

Implement the video control register block at `$D840-$D85F` as specified
in `hardware.md`.

### Emulator Changes

**New file: `src/emu_video.h`**

```cpp
#pragma once
#include <cstdint>

void video_init();
void video_reset();
void video_decode(uint64_t& pins, uint8_t dev_reg);

// Accessors for GUI rendering
uint8_t video_get_mode();
uint8_t video_get_width();
uint8_t video_get_height();
uint8_t video_get_stride();
uint8_t video_get_cursor_style();
uint8_t video_get_cursor_col();
uint8_t video_get_cursor_row();
```

**New file: `src/emu_video.cpp`**

State variables:
```cpp
static uint8_t vid_mode   = 0x00;
static uint8_t vid_width  = 80;
static uint8_t vid_height = 25;
static uint8_t vid_stride = 80;
static uint8_t vid_cursor = 0x00;
static uint8_t vid_curcol = 0;
static uint8_t vid_currow = 0;
```

Register decode (relative offset 0-7):
- Reg 0 (`VID_MODE`): R/W. On write, auto-set width/height/stride.
  Mode `$00` -> 80/25/80. Mode `$01` -> retain current.
- Reg 1 (`VID_WIDTH`): R/W.
- Reg 2 (`VID_HEIGHT`): R/W.
- Reg 3 (`VID_STRIDE`): R/W.
- Reg 4 (`VID_OPER`): W-only trigger. Read returns 0. On write,
  perform scroll operation on `mem[$C000..]` (or backing store).
- Reg 5 (`VID_CURSOR`): R/W.
- Reg 6 (`VID_CURCOL`): R/W.
- Reg 7 (`VID_CURROW`): R/W.

Scroll operations (VID_OPER):
```
$00 = scroll up:   memmove row 0 <- row 1, clear last row
$01 = scroll down: memmove row 1 <- row 0, clear first row
$02 = scroll left:  shift each row left 1, clear rightmost column
$03 = scroll right: shift each row right 1, clear leftmost column
```

`video_reset()`:
```cpp
vid_mode = 0x00; vid_width = 80; vid_height = 25; vid_stride = 80;
vid_cursor = 0x00; vid_curcol = 0; vid_currow = 0;
memset(&mem[HW_FB_BASE], 0x00, vid_height * vid_stride);
```

**`src/emulator.cpp`:**

Add bus decode in `emulator_step()`:
```cpp
BUS_DECODE(addr, HW_VID_BASE, 0xFFE0) {
    const uint8_t dev_reg = (addr - HW_VID_BASE) & 0x1F;
    video_decode(pins, dev_reg);
}
```

Call `video_init()` from `emulator_init()`.
Call `video_reset()` from `emulator_reset()`.

**Makefile:**

Add `$(SRC_DIR)/emu_video.cpp` to `SOURCES`.
Add `$(BUILD_DIR)/emu_video.o` to `TEST_SRC_OBJS`.

### Test Changes

**New file: `test/test_video.cpp`**

| Test ID | Description |
|---------|-------------|
| T130 | `video_reset()` sets mode=0, width=80, height=25, stride=80 |
| T131 | Read VID_MODE (`$D840`) returns 0x00 after reset |
| T132 | Write VID_MODE=0x00, read back, width/height/stride = 80/25/80 |
| T133 | Write VID_MODE=0x01, read back, registers retain prior values |
| T134 | Write VID_WIDTH=40, read back returns 40 |
| T135 | Write VID_HEIGHT=24, read back returns 24 |
| T136 | Write VID_STRIDE=128, read back returns 128 |
| T137 | Write VID_OPER=0x00 (scroll up): row 0 gets row 1 data, last row zeroed |
| T138 | Write VID_OPER=0x01 (scroll down): row 1 gets row 0 data, first row zeroed |
| T139 | Read VID_OPER returns 0 (write-only) |
| T140 | Write VID_CURSOR=0x05, read back returns 0x05 |
| T141 | Write VID_CURCOL=39, read back returns 39 |
| T142 | Write VID_CURROW=12, read back returns 12 |
| T143 | Phantom registers ($D848-$D85F) read 0x00, write no-op |
| T144 | VID_OPER=0x02 (scroll left): each row shifts left 1 |
| T145 | VID_OPER=0x03 (scroll right): each row shifts right 1 |

Tests T137-T138 use `EmulatorFixture`. Pre-fill `mem[$C000..]` with
known patterns, trigger scroll via program (STA to $D844), verify
`mem[]` contents.

### Phase Gate

- All prior 232 tests pass.
- T130-T145 (16 new tests) pass.
- **Total: 248 tests.**

### Rollback

Remove `emu_video.cpp`, `emu_video.h`. Remove bus decode line from
`emulator.cpp`. Remove `test_video.cpp`. Revert Makefile.

---

## 7. Phase 5 — Keyboard Registers

### Objective

Implement the keyboard register block at `$D860-$D87F` as specified
in `hardware.md` and `keycodes.md`.

### Emulator Changes

**New file: `src/emu_kbd.h`**

```cpp
#pragma once
#include <cstdint>

void kbd_init();
void kbd_reset();
void kbd_decode(uint64_t& pins, uint8_t dev_reg);
void kbd_tick(uint64_t& pins);

// Host-side injection (from SDL event loop)
void kbd_inject_key(uint8_t keycode, uint8_t modifiers);

// Test helpers
uint8_t kbd_get_data();
uint8_t kbd_get_status();
bool kbd_data_available();
```

**New file: `src/emu_kbd.cpp`**

State:
```cpp
static uint8_t kbd_data = 0x00;
static uint8_t kbd_status = 0x00;     // bits: [5:0] per spec
static uint8_t kbd_ctrl = 0x00;       // bit 0 = IRQ enable
static bool    kbd_data_avail = false;
static bool    kbd_overflow = false;
```

`kbd_inject_key(keycode, modifiers)`:
- If `kbd_data_avail` is already true, set OVERFLOW bit, replace data
  (most-recent-key-wins).
- Set `kbd_data = keycode`, `kbd_data_avail = true`.
- Update modifier bits (2-5) from `modifiers` parameter.
- If `kbd_ctrl & 0x01` (IRQ enabled), call `emu_set_irq(2)`.

`kbd_decode(pins, dev_reg)`:
- Reg 0 read: return `kbd_data`.
- Reg 1 read: return `kbd_status` (DATA_AVAIL, OVERFLOW, modifier bits).
- Reg 1 write: acknowledge — clear DATA_AVAIL, OVERFLOW, deassert IRQ
  bit 2 via `emu_clr_irq(2)`.
- Reg 2 R/W: `kbd_ctrl`.
- Regs 3-31: reserved. Read 0, write no-op.

`kbd_tick()`:
- If `kbd_data_avail` and `kbd_ctrl & 0x01`: `emu_set_irq(2)`.
- Update live modifier bits in `kbd_status` regardless of DATA_AVAIL.

`kbd_reset()`:
- Zero all state. Clear IRQ bit 2.

**`src/emulator.cpp`:**

Add bus decode:
```cpp
BUS_DECODE(addr, HW_KBD_BASE, 0xFFE0) {
    const uint8_t dev_reg = (addr - HW_KBD_BASE) & 0x1F;
    kbd_decode(pins, dev_reg);
}
```

Add `kbd_tick(pins)` call in `emulator_step()` next to `tty_tick(pins)`.

**`src/main.cpp`:**

In the SDL event loop (`SDL_PollEvent`), map `SDL_KEYDOWN` events to
`kbd_inject_key()`. Mapping logic:
- Printable ASCII: use SDL's text input event.
- Navigation/function keys: map SDL scancodes to extended codes per
  `keycodes.md`.
- Modifier state: read SDL modifier flags (`SDL_GetModState()`).

### Test Changes

**New file: `test/test_kbd.cpp`**

| Test ID | Description |
|---------|-------------|
| T150 | `kbd_reset()` clears data, status, ctrl |
| T151 | `kbd_inject_key(0x41, 0)` sets DATA_AVAIL in status |
| T152 | Read KBD_DATA returns injected key code |
| T153 | Read KBD_STATUS shows DATA_AVAIL=1 |
| T154 | Write KBD_ACK clears DATA_AVAIL and OVERFLOW |
| T155 | Second inject before ACK sets OVERFLOW, replaces data |
| T156 | KBD_CTRL bit 0 enables IRQ; inject_key sets IRQ bit 2 |
| T157 | KBD_CTRL bit 0 clear: inject_key does NOT set IRQ |
| T158 | ACK deasserts IRQ bit 2 |
| T159 | Modifier bits (SHIFT=bit2) reflect in KBD_STATUS |
| T160 | Modifier bits update even when DATA_AVAIL=0 |
| T161 | Reserved registers ($D863-$D867) read 0, write no-op |
| T162 | Extended key code ($80 = Up Arrow) stored correctly |
| T163 | Function key ($90 = F1) stored correctly |
| T164 | Bus decode: program at ROM writes to KBD_ACK, clears status |

### Phase Gate

- All prior 248 tests pass.
- T150-T164 (15 new tests) pass.
- **Total: 263 tests.**

### Rollback

Remove `emu_kbd.cpp`, `emu_kbd.h`. Remove bus decode and tick from
`emulator.cpp`. Remove SDL key mapping from `main.cpp`. Remove
`test_kbd.cpp`. Revert Makefile.

---

## 8. Phase 6 — ROM Split & Firmware Linker Migration

### Objective

Split the single 12 KB ROM region (`$D000-$FFFF`) into the four target
regions. The emulator ROM loader and cc65 linker config must both change.

### cc65 Linker Configuration

**New `firmware/n8.cfg`:**

```
# n8.cfg - ld65 linker configuration for n8 (new memory map)

MEMORY {
    ZP:         start =    $0, size =  $100, type   = rw, define = yes;
    RAM:        start =  $400, size = $BC00, define = yes;
    DEVBANK:    start = $D000, size = $0800, type   = rw, define = yes;
    ROM_MON:    start = $E000, size = $1000, file   = %O;
    ROM_IMPL:   start = $F000, size = $0E00, file   = %O;
    ROM_ENTRY:  start = $FE00, size = $00E0, file   = %O;
    ROM_VEC:    start = $FFE0, size = $0020, file   = %O;
}

SEGMENTS {
    ZEROPAGE:  load = ZP,        type = zp,  define   = yes;
    DATA:      load = ROM_MON,   type = rw,  define   = yes, run = RAM;
    BSS:       load = RAM,       type = bss, define   = yes;
    HEAP:      load = RAM,       type = bss, optional = yes;
    STARTUP:   load = ROM_ENTRY, type = ro;
    ONCE:      load = ROM_MON,   type = ro,  optional = yes;
    CODE:      load = ROM_IMPL,  type = ro;
    RODATA:    load = ROM_MON,   type = ro;
    MONITOR:   load = ROM_MON,   type = ro,  optional = yes;
    ENTRY:     load = ROM_ENTRY, type = ro;
    VECTORS:   load = ROM_VEC,   type = ro,  start    = $FFFA;
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

**Key changes from current `n8.cfg`:**

| Aspect | Current | New |
|--------|---------|-----|
| RAM start | `$200` | `$400` |
| RAM size | `$BD00` | `$BC00` |
| ROM | Single `$D000`, `$3000` | Four regions |
| VECTORS start | `$FFFA` | `$FFFA` (inside ROM_VEC) |
| New segments | — | `MONITOR`, `ENTRY` |

Note: VECTORS segment `start = $FFFA` is still within `ROM_VEC`
(`$FFE0-$FFFF`), so the 6-byte NMI/RESET/IRQ vectors land at
`$FFFA-$FFFF` as the 6502 requires.

### Emulator ROM Loader Changes

**`src/emulator.cpp` — `emulator_loadrom()`:**

The current loader reads the firmware binary starting at `$D000` and
fills contiguously. The new layout has gaps (`$D800-$DFFF` is device
space, not ROM). The cc65 output file is contiguous from the first
`file = %O` region to the last.

**Approach:** The cc65 binary will be a contiguous image from `$E000`
to `$FFFF` (8 KB). ROM_MON, ROM_IMPL, ROM_ENTRY, and ROM_VEC all
have `file = %O`, so they are concatenated in order.

```cpp
void emulator_loadrom() {
    uint16_t rom_ptr = HW_ROM_MONITOR;  // 0xE000
    printf("Loading ROM at $%04X\r\n", rom_ptr);
    fflush(stdout);
    FILE *fp = fopen(rom_file, "r");
    if (!fp) {
        printf("ERROR: Cannot open ROM file '%s'\r\n", rom_file);
        fflush(stdout);
        return;
    }
    while(1) {
        uint8_t c = fgetc(fp);
        if(feof(fp)) break;
        if (rom_ptr >= 0xE000 && rom_ptr <= 0xFFFF) {
            mem[rom_ptr] = c;
        }
        rom_ptr++;
    }
    fclose(fp);
}
```

During the transition period (before firmware is rebuilt), the emulator
must support loading the old 12 KB binary at `$D000`. Use a runtime
check: if file size <= 8 KB, load at `$E000`; if > 8 KB, load at `$D000`
(legacy mode).

### Playground & GDB Playground Linker Config

**New `firmware/gdb_playground/playground.cfg`:**

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

Same change for `firmware/playground/playground.cfg`.

### Test Changes

**Modified: `test/test_bus.cpp` and `test/test_integration.cpp`:**

All tests that `load_at(0xD000, ...)` must be updated. Since `$D000` is
now Dev Bank (RAM-like), loading test programs there still works for
CPU-only tests (CpuFixture). But for EmulatorFixture tests where the
bus decode matters, programs should be loaded at `$E000` instead.

**Analysis of affected tests:**

Tests that load ROM-like code at `$D000` via `EmulatorFixture`:
- T62, T63, T64, T65, T66, T67 (test_bus.cpp) — all use `f.load_at(0xD000, ...)`
  and `f.set_reset_vector(0xD000)`.
- T95-T101a (test_integration.cpp) — same pattern.
- test_gdb_callbacks.cpp — many tests load at `$D000`.

**Migration strategy:** These tests load machine code directly into
`mem[]` — they do not go through `emulator_loadrom()`. Since `$D000` in
the new map is Dev Bank (writable RAM), loading code there still works.
The CPU will execute from Dev Bank the same way it executes from ROM.
**No test changes are required for this phase** because the CPU does not
distinguish ROM from RAM at the bus level.

However, we add new tests to verify the ROM regions:

| Test ID | Description |
|---------|-------------|
| T170 | `emulator_loadrom()` loads 8KB binary starting at `$E000` |
| T171 | Code at `$E000` is executable (NOP sled, PC advances) |
| T172 | Reset vector at `$FFFC-$FFFD` still works (within ROM_VEC) |
| T173 | RAM starts at `$0400` (write to `$0400` succeeds) |
| T174 | Legacy loadrom: >8KB binary loads at `$D000` (transition) |

Note: T170 and T174 require a test firmware binary. Create a minimal
test binary (NOP sled + vectors) using a helper function that writes a
temp file, or use a static array in the test.

### Phase Gate

- All prior 263 tests pass.
- T170-T174 (5 new tests) pass.
- `make firmware` succeeds with new `n8.cfg`.
- `make -C firmware/gdb_playground` succeeds with new config.
- **Total: 268 tests.**

### Rollback

Restore original `n8.cfg`, `playground.cfg` files. Revert
`emulator_loadrom()`. Revert test changes.

---

## 9. Phase 7 — GDB Stub & n8gdb Client Updates

### Objective

Update the GDB memory map XML, and ensure the n8gdb client works with
the new layout.

### GDB Stub Changes

**`src/gdb_stub.cpp` — `memory_map_xml[]`:**

Replace:
```cpp
static const char memory_map_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE memory-map SYSTEM \"gdb-memory-map.dtd\">\n"
    "<memory-map>\n"
    "  <memory type=\"ram\"  start=\"0x0000\" length=\"0xC000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC000\" length=\"0x0100\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC100\" length=\"0x0010\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC110\" length=\"0x0EF0\"/>\n"
    "  <memory type=\"rom\"  start=\"0xD000\" length=\"0x3000\"/>\n"
    "</memory-map>\n";
```

With:
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
- `$C000-$CFFF`: RAM (frame buffer, 4 KB — mapped as RAM for GDB read/write)
- `$D000-$D7FF`: RAM (Dev Bank, 2 KB)
- `$D800-$DFFF`: RAM (device registers — mapped as RAM for GDB read/write;
  device side effects happen on CPU bus only, not GDB mem read)
- `$E000-$FFFF`: ROM (8 KB)

### n8gdb Client Changes

**`bin/n8gdb/n8gdb.mjs`:**

The `cmdReset()` function reads the reset vector from `$FFFC`:
```js
const vec = await client.readMemory(0xFFFC, 2);
```
This address hasn't changed (6502 reset vector is always at `$FFFC`),
so **no change required**.

No hardcoded device addresses exist in the n8gdb client. It only uses
symbol files and user-provided addresses. **No changes required.**

### Test Changes

**Modified: `test/test_gdb_protocol.cpp`:**

T58 (memory map XML test):
```cpp
TEST_CASE("T58: memory map XML") {
    GdbProtocolFixture f;
    std::string result = gdb_stub_process_packet("qXfer:memory-map:read::0,fff");
    CHECK(result[0] == 'l');
    CHECK(result.find("memory-map") != std::string::npos);
    CHECK(result.find("0xE000") != std::string::npos);  // was 0xD000
    CHECK(result.find("rom") != std::string::npos);
}
```

**New tests:**

| Test ID | Description |
|---------|-------------|
| T180 | Memory map XML contains `0xC000` RAM region (4KB frame buffer) |
| T181 | Memory map XML contains `0xD800` RAM region (device space) |
| T182 | Memory map XML ROM starts at `0xE000` with length `0x2000` |
| T183 | GDB memory read at `$D840` (VID_MODE) returns mem[] value |
| T184 | GDB memory write at `$D860` (KBD_DATA) modifies mem[] |

### Phase Gate

- All prior 268 tests pass (T58 updated).
- T180-T184 (5 new tests) pass.
- **Total: 273 tests.**

### Rollback

Revert `memory_map_xml[]` in `gdb_stub.cpp`. Revert T58.

---

## 10. Phase 8 — Firmware Source Migration

### Objective

Update all firmware assembly source files to use new device addresses
from `hw_regs.inc`.

### Firmware Source File Inventory

#### Files requiring changes

| File | Changes Needed |
|------|---------------|
| `firmware/devices.inc` | Replace all addresses with new values |
| `firmware/devices.h` | Replace all addresses (C preprocessor) |
| `firmware/zp.inc` | Remove `ZP_IRQ $FF` (moved to `$D800`) |
| `firmware/interrupt.s` | `TTY_IN_CTRL` -> `HW_TTY_IN_STATUS`, `TTY_IN_DATA` -> `HW_TTY_IN_DATA`, `TXT_BUFF` -> `HW_FB_BASE+1` |
| `firmware/tty.s` | `TTY_OUT_CTRL` -> `HW_TTY_OUT_STATUS`, `TTY_OUT_DATA` -> `HW_TTY_OUT_DATA` |
| `firmware/main.s` | `TXT_BUFF` -> `HW_FB_BASE+1` |
| `firmware/init.s` | Includes devices.inc (inherits changes), RAM size update |
| `firmware/vectors.s` | No address changes (vectors are relocated by linker) |

#### Detailed changes per file

**`firmware/devices.inc`** (complete replacement):

```asm
; devices.inc — Device register addresses (new memory map)
; See also: src/hw_regs.h, docs/memory_map/hardware.md

.include "hw_regs.inc"

; Legacy aliases for transitional compatibility
.define TXT_CTRL         HW_FB_BASE
.define TXT_BUFF         HW_FB_BASE+1

.define TTY_OUT_CTRL     HW_TTY_OUT_STATUS
.define TTY_OUT_DATA     HW_TTY_OUT_DATA
.define TTY_IN_CTRL      HW_TTY_IN_STATUS
.define TTY_IN_DATA      HW_TTY_IN_DATA
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

Remove `ZP_IRQ $FF` line. IRQ flags are now at `$D800`, not in zero page.

```asm
; zp.inc — Zero Page locations

.define ZP_A_PTR    $E0
.define ZP_B_PTR    $E2
.define ZP_C_PTR    $E4
.define ZP_D_PTR    $E6

.define BYTE_0      $F0
.define BYTE_1      $F1
.define BYTE_2      $F2
.define BYTE_3      $F3
; ZP_IRQ removed — IRQ flags now at $D800 (HW_IRQ_FLAGS)
```

**`firmware/interrupt.s`:**

The IRQ handler reads `TTY_IN_CTRL` and `TTY_IN_DATA`. These are now
aliases pointing to `$D822` and `$D823`. References to `TXT_BUFF` (for
debug display) now point to `$C001`. No source changes needed beyond
what `devices.inc` provides via the aliases.

**`firmware/init.s`:**

Uses `__RAM_START__` and `__RAM_SIZE__` from linker. These change
automatically when `n8.cfg` changes. No source changes.

**`firmware/tty.s`:**

Uses `TTY_OUT_CTRL`, `TTY_OUT_DATA`, `TTY_IN_CTRL`, `TTY_IN_DATA`.
These are aliases. No source changes.

**`firmware/main.s`:**

Uses `TXT_BUFF`. This is an alias. No source changes.

### Build Verification

```bash
make clean
make firmware     # should build with new n8.cfg and devices.inc
make              # should build emulator (no firmware source changes needed)
```

### Test Changes

No new unit tests. The firmware build itself is the test. If `make firmware`
succeeds and produces a binary, and the emulator can load and run it
(verified manually or via the gdb_playground tests), the migration is
validated.

### Phase Gate

- `make firmware` produces `N8firmware` without errors or warnings.
- `make` (emulator) compiles clean.
- `make test` still produces **273 passed, 0 failed**.
- Manual smoke test: `./n8` boots, displays banner, echo loop works.

### Rollback

Restore original `devices.inc`, `devices.h`, `zp.inc`, and `n8.cfg`.

---

## 11. Phase 9 — Playground & GDB Playground Migration

### Objective

Update all playground and gdb_playground programs to use new addresses
and linker configs.

### Files Requiring Changes

| File | Changes |
|------|---------|
| `firmware/playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/playground/mon1.s` | TTY register equates -> `$D820-$D823` |
| `firmware/playground/mon2.s` | TTY register equates -> `$D820-$D823` |
| `firmware/playground/mon3.s` | TTY + TXT_BASE equates updated |
| `firmware/gdb_playground/playground.cfg` | ROM at `$E000`, RAM at `$400` |
| `firmware/gdb_playground/test_tty.s` | TTY equates -> `$D820-$D821` |

#### Detailed changes

**`mon1.s`, `mon2.s`, `mon3.s`:** Each file has local TTY equates:
```asm
; Current:
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101
TTY_IN_CTRL  = $C102
TTY_IN_DATA  = $C103

; New:
TTY_OUT_CTRL = $D820
TTY_OUT_DATA = $D821
TTY_IN_CTRL  = $D822
TTY_IN_DATA  = $D823
```

**`mon3.s`** also has `TXT_BASE = $C000` — this address is unchanged.

**`test_tty.s`:**
```asm
; Current:
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101

; New:
TTY_OUT_CTRL = $D820
TTY_OUT_DATA = $D821
```

**All other gdb_playground programs** (`test_regs.s`, `test_breakpoints.s`,
`test_memory.s`, `test_stack.s`, `test_counter.s`, `test_zeropage.s`)
do not reference device addresses. Only `test_tty.s` needs changes.

### Build Verification

```bash
make -C firmware/playground clean && make -C firmware/playground
make -C firmware/gdb_playground clean && make -C firmware/gdb_playground
```

### Phase Gate

- All playground programs build cleanly.
- All gdb_playground programs build cleanly.
- `make test` still **273 passed, 0 failed**.
- Manual: `n8gdb repl` with `test_tty` program sends output correctly.

### Rollback

Restore original equates and playground.cfg files.

---

## 12. Phase 10 — End-to-End Validation

### Objective

Final validation that the complete system works with the new memory map.
Remove legacy compatibility code and transition flags.

### Cleanup Tasks

1. Remove `LEGACY_*` defines from `hw_regs.h`.
2. Remove legacy loadrom path from `emulator_loadrom()`.
3. Remove legacy alias defines from `devices.inc` (replace with direct
   `hw_regs.inc` symbols throughout firmware).
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
| E2E-07 | Video scroll up via program | Frame buffer shifted correctly |
| E2E-08 | Keyboard inject + IRQ | CPU vectors to IRQ handler |
| E2E-09 | Reset via GDB | CPU restarts at `$FFFC` vector |
| E2E-10 | Full mon3 session | All commands work with new TTY addresses |

### Final Test Count

| Phase | New Tests | Running Total |
|------:|----------:|--------------:|
| Baseline | — | 221 |
| Phase 1 (IRQ move) | 4 | 225 |
| Phase 2 (TTY move) | 3 | 228 |
| Phase 3 (FB expand) | 4 | 232 |
| Phase 4 (Video regs) | 16 | 248 |
| Phase 5 (Keyboard) | 15 | 263 |
| Phase 6 (ROM split) | 5 | 268 |
| Phase 7 (GDB stub) | 5 | 273 |
| Phase 8 (Firmware) | 0 (build test) | 273 |
| Phase 9 (Playground) | 0 (build test) | 273 |
| Phase 10 (E2E) | 10 (manual) | 273 + 10 manual |

### Phase Gate

- `make clean && make && make firmware && make test` all succeed.
- **273 automated tests, 0 failures.**
- All 10 E2E scenarios pass manually.
- No legacy addresses remain in the codebase.

---

## 13. Test Case Registry

### Naming Convention

- `Tnnn` — automated unit/integration tests (doctest).
- `E2E-nn` — manual end-to-end validation scenarios.
- Test IDs are sequential within phases and do not overlap with existing
  test IDs (existing range: T01-T101a).

### New Test IDs by File

**`test/test_bus.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T110 | 1 | Write/read `$D800` (IRQ_FLAGS device space) |
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

**`test/test_video.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T130 | 4 | video_reset() sets default mode/dims |
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
| T143 | 4 | Phantom registers read 0 |
| T144 | 4 | VID_OPER=0x02 scroll left |
| T145 | 4 | VID_OPER=0x03 scroll right |

**`test/test_kbd.cpp` (new file):**

| ID | Phase | Description |
|----|-------|-------------|
| T150 | 5 | kbd_reset() clears state |
| T151 | 5 | inject_key sets DATA_AVAIL |
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

**`test/test_integration.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T170 | 6 | loadrom places 8KB at $E000 |
| T171 | 6 | Code at $E000 executable |
| T172 | 6 | Reset vector in ROM_VEC works |
| T173 | 6 | RAM at $0400 writable |
| T174 | 6 | Legacy loadrom (>8KB at $D000) |

**`test/test_gdb_protocol.cpp` additions:**

| ID | Phase | Description |
|----|-------|-------------|
| T180 | 7 | Memory map has $C000 RAM (4KB FB) |
| T181 | 7 | Memory map has $D800 RAM (device) |
| T182 | 7 | Memory map ROM at $E000 length $2000 |
| T183 | 7 | GDB read at $D840 returns mem value |
| T184 | 7 | GDB write at $D860 modifies mem |

### Existing Test Modifications

Tests modified in place (same ID, updated addresses):

| ID | Phase | Change |
|----|-------|--------|
| T69, T69a, T69b, T70, T70a | 1 | `mem[0x00FF]` -> `mem[HW_IRQ_FLAGS]` |
| T71-T78a | 2 | `0xC1xx` addresses -> `0xD8xx` addresses |
| T58 | 7 | Memory map XML checks for `0xE000` |
| T76 (tty) | 1 | `mem[0x00FF]` -> `mem[HW_IRQ_FLAGS]` |

### Test Fixture Modifications

**`test/test_helpers.h`:**

Add `#include "hw_regs.h"` at the top, so all test files have access to
the hardware constants.

`make_read_pins()` and `make_write_pins()` — no changes needed (they
take an address parameter).

`EmulatorFixture` — no structural changes. The `load_at()` and
`set_reset_vector()` methods work with any address.

`CpuFixture` — no changes. It uses its own `mem[]` array and is
address-independent.

---

## 14. Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Bus decode mask error causes phantom addresses | Medium | High | Each phase adds boundary tests at mask edges. Test at base, base+size-1, and base+size. |
| cc65 segment overflow (code doesn't fit in split ROM) | Medium | Medium | Monitor `*.map` file output sizes. IMPL is 3.5KB which is tight. Add Makefile size check. |
| IRQ timing regression (IRQ_FLAGS at $D800 vs $00FF) | Low | High | The CPU accesses $D800 via the same bus as $00FF. Tick-level IRQ tests (T111, T112) verify timing. |
| TTY ring buffer corruption during move | Low | Medium | TTY module is address-agnostic (works on relative regs). Only the bus decode address changes. |
| GDB memory map mismatch causes n8gdb confusion | Low | Low | n8gdb doesn't enforce the memory map — it's informational for GDB frontends. |
| Firmware won't fit in split ROM regions | Medium | High | Current firmware is ~2.5KB. Target CODE segment (IMPL) is 3.5KB. RODATA + DATA in MON is 4KB. Should fit. Build and check map file. |
| Playground programs too large for 8KB ROM | Low | Low | mon3.s is ~1KB. All fit easily in 8KB. |
| Video scroll corrupts frame buffer | Medium | Medium | Scroll tests pre-fill known patterns and verify exact byte positions after operation. |
| Keyboard IRQ conflicts with TTY IRQ | Low | Medium | They use different IRQ bits (1 for TTY, 2 for keyboard). Tested independently. |
| `emulator_loadrom()` dual-path (legacy/new) is fragile | Medium | Low | Legacy path is temporary. Remove in Phase 10. Tested in T174. |

---

## 15. Rollback Procedures

### Per-Phase Rollback

Each phase operates on a git branch. Rollback is `git checkout` to the
prior phase's commit.

**General rollback procedure:**

1. `git stash` any uncommitted work.
2. `git log --oneline -10` to find the last known-good commit.
3. `git checkout <commit>` or `git revert <bad-commit>`.
4. `make clean && make && make test` to verify restoration.

### Phase-Specific Notes

| Phase | Rollback Complexity | Notes |
|-------|-------------------|-------|
| 0 | Trivial | Delete new files. No behavior change. |
| 1 | Simple | Revert IRQ macros to `0x00FF`. Revert 5 test updates. |
| 2 | Simple | Revert bus decode mask. Revert ~10 test address changes. |
| 3 | Trivial | Only tests added. Revert test file. |
| 4 | Moderate | Remove new source files + Makefile changes + test file. |
| 5 | Moderate | Same as Phase 4. Remove source files + tests. |
| 6 | Complex | Revert 3 linker configs + emulator_loadrom() + 5 tests. Firmware must be rebuilt with old config. |
| 7 | Simple | Revert XML blob + 1 modified test. |
| 8 | Complex | Revert firmware source files (devices.inc, devices.h, zp.inc). Rebuild firmware with old config. |
| 9 | Moderate | Revert equates in 4 playground files + 2 config files. |
| 10 | Simple | Re-add legacy compatibility code. |

### Emergency Rollback (all phases)

If multiple phases have been applied and a deep regression is found:

```bash
git log --oneline --all    # find pre-migration tag
git checkout pre-migration # or the branch/tag name
make clean && make && make firmware && make test
```

**Recommendation:** Tag the repo before starting Phase 1:
```bash
git tag pre-migration-v1
```

---

## Appendix A: Bus Decode Reference

### Current bus decode in `emulator_step()`

```
addr     mask     device
$0000    $FF00    Zero Page monitor (logging only)
$FFF0    $FFF0    Vector monitor (logging only)
$C100    $FFF0    TTY (16 addresses: $C100-$C10F)
```

Everything else falls through to generic `mem[]` read/write.

### Target bus decode (after all phases)

```
addr     mask     device          handler
$D800    $FFE0    System/IRQ      (direct mem[] or dedicated handler)
$D820    $FFE0    TTY             tty_decode(pins, reg)
$D840    $FFE0    Video Control   video_decode(pins, reg)
$D860    $FFE0    Keyboard        kbd_decode(pins, reg)
$C000    $F000    Frame Buffer    (mem[] passthrough, 4KB)
```

The `$FFE0` mask for device blocks matches 32-byte aligned blocks.
`addr & $FFE0 == base` is true for addresses `base` through `base+$1F`.

### Mask Derivation

For a 32-byte device block at base `$D8xx`:
- The base address bit pattern:  `1101 1000 XXXX XXXX`
- The significant bits (top 11): `1101 1000 XXX0 0000`
- Mask = `$FFE0` (all bits significant except lower 5)

For the 4 KB frame buffer at `$C000`:
- Base: `1100 0000 0000 0000`
- Mask: `$F000` (top 4 bits)

---

## Appendix B: cc65 Segment Size Budget

### Firmware segments with new n8.cfg

| Segment | Memory Region | Available | Current Est. |
|---------|--------------|----------:|-----------:|
| STARTUP | ROM_ENTRY ($FE00-$FFDF) | 480 bytes | ~60 bytes |
| CODE | ROM_IMPL ($F000-$FDFF) | 3,584 bytes | ~800 bytes |
| RODATA | ROM_MON ($E000-$EFFF) | 4,096 bytes | ~100 bytes |
| DATA (load) | ROM_MON | (shared with RODATA) | ~40 bytes |
| VECTORS | ROM_VEC ($FFE0-$FFFF) at $FFFA | 6 bytes | 6 bytes |
| ENTRY | ROM_ENTRY | (shared with STARTUP) | — |
| MONITOR | ROM_MON | (shared with RODATA) | — |

Current firmware total ROM usage: ~1,000 bytes (from `.map` file).
Available: 8,160 bytes across all ROM regions. Comfortable margin.

### Playground segment budget

All playground code fits in a single 8KB ROM region (`$E000-$FFFF`).
Largest program (mon3.s) is ~1,000 bytes. No overflow risk.

---

## Appendix C: File Change Summary

### New Files

| File | Phase | Purpose |
|------|-------|---------|
| `src/hw_regs.h` | 0 | Hardware address constants (C++) |
| `firmware/hw_regs.inc` | 0 | Hardware address constants (asm) |
| `src/emu_video.h` | 4 | Video control register header |
| `src/emu_video.cpp` | 4 | Video control register implementation |
| `src/emu_kbd.h` | 5 | Keyboard register header |
| `src/emu_kbd.cpp` | 5 | Keyboard register implementation |
| `test/test_video.cpp` | 4 | Video register tests |
| `test/test_kbd.cpp` | 5 | Keyboard register tests |

### Modified Files

| File | Phases | Changes |
|------|--------|---------|
| `src/emulator.cpp` | 1,2,4,5,6 | IRQ macros, bus decode, ROM loader, tick calls |
| `src/emulator.h` | 4,5 | New device init/reset declarations (if needed) |
| `src/main.cpp` | 5 | SDL keyboard event mapping |
| `src/gdb_stub.cpp` | 7 | Memory map XML |
| `src/machine.h` | 3 | Frame buffer size constant |
| `Makefile` | 4,5 | New source files in SOURCES and TEST_SRC_OBJS |
| `firmware/n8.cfg` | 6 | Complete rewrite for split ROM |
| `firmware/devices.inc` | 8 | New addresses via hw_regs.inc |
| `firmware/devices.h` | 8 | New addresses |
| `firmware/zp.inc` | 8 | Remove ZP_IRQ |
| `firmware/playground/playground.cfg` | 9 | ROM at $E000 |
| `firmware/playground/mon1.s` | 9 | TTY equates |
| `firmware/playground/mon2.s` | 9 | TTY equates |
| `firmware/playground/mon3.s` | 9 | TTY + TXT_BASE equates |
| `firmware/gdb_playground/playground.cfg` | 9 | ROM at $E000 |
| `firmware/gdb_playground/test_tty.s` | 9 | TTY equates |
| `test/test_helpers.h` | 0 | Add hw_regs.h include |
| `test/test_utils.cpp` | 1 | IRQ register address |
| `test/test_tty.cpp` | 1,2 | IRQ + TTY addresses |
| `test/test_bus.cpp` | 1,2,3 | New bus decode tests |
| `test/test_gdb_callbacks.cpp` | 1,2 | Address updates |
| `test/test_gdb_protocol.cpp` | 7 | Memory map XML test |
| `test/test_integration.cpp` | 6 | ROM loader tests |

### Unchanged Files

| File | Reason |
|------|--------|
| `src/m6502.h` | Vendored; never edit |
| `src/emu_dis6502.cpp` | No address dependencies |
| `src/emu_labels.cpp` | No address dependencies |
| `src/gui_console.cpp` | No address dependencies |
| `src/utils.cpp` | No address dependencies |
| `bin/n8gdb/rsp.mjs` | Protocol-level; no addresses |
| `bin/n8gdb/n8gdb.mjs` | No hardcoded device addresses |
| `firmware/init.s` | Uses linker symbols, not hardcoded addresses |
| `firmware/interrupt.s` | Uses devices.inc aliases |
| `firmware/tty.s` | Uses devices.inc aliases |
| `firmware/main.s` | Uses devices.inc aliases |
| `firmware/vectors.s` | No addresses; uses linker segment |
| `firmware/gdb_playground/common_init.s` | No device addresses |
| `firmware/gdb_playground/common_vectors.s` | No device addresses |
| `firmware/gdb_playground/test_regs.s` | No device addresses |
| `firmware/gdb_playground/test_breakpoints.s` | No device addresses |
| `firmware/gdb_playground/test_memory.s` | No device addresses |
| `firmware/gdb_playground/test_stack.s` | No device addresses |
| `firmware/gdb_playground/test_counter.s` | No device addresses |
| `firmware/gdb_playground/test_zeropage.s` | No device addresses |
| `firmware/playground/common_init.s` | No device addresses |
| `firmware/playground/common_vectors.s` | No device addresses |
| `test/test_cpu.cpp` | Uses CpuFixture; address-independent |
| `test/test_disasm.cpp` | No device addresses |
| `test/test_labels.cpp` | No device addresses |
| `test/test_main.cpp` | doctest runner; no tests |
| `test/test_stubs.cpp` | Link stubs; no logic |
| `test/doctest.h` | Vendored framework |
