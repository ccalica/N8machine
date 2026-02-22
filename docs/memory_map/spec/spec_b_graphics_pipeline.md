# Spec B: Graphics Pipeline, Keyboard Input, and Memory Map Migration

Version: 1.0
Date: 2026-02-22

## Scope

This specification covers two tasks executed as a single incremental migration:

1. **Migrate emulator to the proposed memory map** (bus decode, IRQ, firmware, tests, GDB stub)
2. **Implement video rendering pipeline and keyboard input** (font, textures, cursor, scroll, SDL2 key events)

Both tasks share the same bus decode layer and are sequenced so each phase produces a testable, runnable emulator.

---

## Current State Summary

### Bus Decode (emulator.cpp)

```
$0000-$00FF  Zero page (IRQ flag at $00FF)
$0100-$01FF  Hardware stack
$0200-$BEFF  RAM
$C000-$C0FF  Frame buffer (256 bytes, plain mem[] -- no rendering)
$C100-$C10F  TTY device (4 registers, decoded via BUS_DECODE(addr, 0xC100, 0xFFF0))
$D000-$FFF9  ROM (firmware binary loaded from file)
$FFFA-$FFFF  Vectors (NMI/RESET/IRQ)
```

- Frame buffer is 256 bytes at `$C000`. No video hardware -- bytes sit in `mem[]` and nothing reads them.
- IRQ flag register is at `$00FF` in zero page. Cleared every tick; devices reassert.
- TTY is the only I/O device. It reads host stdin via `select()`/`read()` in `tty_tick()`.
- There is no keyboard device. There is no video controller.
- The main loop (`main.cpp`) renders only ImGui debug windows. No emulated display window exists.
- Font data exists in `docs/charset/n8_font.h` (256 chars, 8x16, 1-bit-per-pixel, MSB=left).
- SDL2 + OpenGL3 context is already initialized. ImGui docking and multi-viewport enabled.

### Firmware (cc65)

- Device addresses hardcoded in `firmware/devices.inc` and `firmware/devices.h`.
- Linker config `firmware/n8.cfg`: ROM at `$D000`, RAM at `$0200`, ZP at `$0000`.
- IRQ handler in `interrupt.s` references `TTY_IN_CTRL` (`$C102`) and `TTY_IN_DATA` (`$C103`).
- `zp.inc` defines `ZP_IRQ` at `$FF`.

### Tests (doctest)

- `test_tty.cpp`: Tests TTY registers at offsets 0-3 via `tty_decode(pins, reg)`.
- `test_bus.cpp`: Tests frame buffer R/W at `$C000-$C0FF`.
- `test_integration.cpp`: Tests boot, breakpoints, IRQ via TTY char injection.
- `test_helpers.h`: `EmulatorFixture` zeros all of `mem[]`, resets CPU. `CpuFixture` is isolated.

---

## Target Memory Map

From `docs/memory_map/README.md` and `docs/memory_map/hardware.md`:

```
$0000-$00FF  Zero page (NO device registers here anymore)
$0100-$01FF  Hardware stack
$0200-$03FF  Reserved
$0400-$BFFF  Program RAM (47 KB)
$C000-$CFFF  Frame buffer (4 KB -- active region depends on mode)
$D000-$D7FF  Dev bank (2 KB)
$D800-$D800  IRQ_FLAGS (replaces $00FF)
$D820-$D823  TTY (4 regs, replaces $C100-$C103)
$D840-$D847  Video Control (8 regs -- NEW)
$D860-$D867  Keyboard (3 regs + 5 reserved -- NEW)
$D880-$DFFF  Reserved device space
$E000-$EFFF  Monitor / stdlib
$F000-$FDFF  Kernel implementation
$FE00-$FFDF  Kernel entry
$FFE0-$FFFF  Vectors + bank switch
```

Key changes from current state:

| What | Old | New | Impact |
|------|-----|-----|--------|
| IRQ flags | `$00FF` (ZP) | `$D800` | Bus decode, firmware, tests |
| TTY registers | `$C100-$C103` | `$D820-$D823` | Bus decode, firmware, tests |
| Frame buffer | `$C000-$C0FF` (256 B) | `$C000-$CFFF` (4 KB) | Bus decode, rendering, tests |
| Video control | (none) | `$D840-$D847` | New device module |
| Keyboard | (none) | `$D860-$D862` | New device module |
| ROM base | `$D000` | `$E000` (or `$F000`) | Linker config, loader |

**Note on ROM base:** The proposed map puts "Monitor / stdlib" at `$E000` and "Kernel Implementation" at `$F000`. The current 12 KB ROM at `$D000-$FFFF` overlaps the new device space at `$D800-$DFFF`. The ROM must move. The simplest approach: ROM loads at `$E000` (8 KB: `$E000-$FFFF`), which matches the new map's kernel regions. The `$D000-$D7FF` dev bank and `$D800-$DFFF` device registers become bus-decoded I/O space rather than ROM. Firmware linker config changes from `ROM start=$D000 size=$3000` to `ROM start=$E000 size=$2000`.

---

## Phase Breakdown

### Phase 0: Extract Constants into `machine.h`

**Goal:** Centralize all memory map constants so the migration is a constants-only change in one file.

**Changes:**

`src/machine.h` -- replace the current single-line content with:

```cpp
#pragma once
#include <cstdint>

// ---- Memory Map Constants ----

// Frame buffer
static const uint16_t FB_BASE       = 0xC000;
static const uint16_t FB_SIZE       = 0x1000;  // 4 KB
static const uint16_t FB_END        = FB_BASE + FB_SIZE - 1;  // $CFFF

// Default text mode
static const int      VID_COLS      = 80;
static const int      VID_ROWS      = 25;
static const int      VID_STRIDE    = 80;
static const int      FB_ACTIVE     = VID_COLS * VID_ROWS;  // 2000

// Device space
static const uint16_t DEV_BASE      = 0xD800;
static const uint16_t DEV_END       = 0xDFFF;
static const uint16_t DEV_MASK      = 0xF800;  // Top 5 bits: $D800-$DFFF

// IRQ flags
static const uint16_t IRQ_FLAGS     = 0xD800;

// TTY device
static const uint16_t TTY_BASE      = 0xD820;
static const uint16_t TTY_OUT_STATUS= 0xD820;
static const uint16_t TTY_OUT_DATA  = 0xD821;
static const uint16_t TTY_IN_STATUS = 0xD822;
static const uint16_t TTY_IN_DATA   = 0xD823;

// Video control
static const uint16_t VID_BASE      = 0xD840;
static const uint16_t VID_MODE      = 0xD840;
static const uint16_t VID_WIDTH     = 0xD841;
static const uint16_t VID_HEIGHT    = 0xD842;
static const uint16_t VID_STRIDE_R  = 0xD843;
static const uint16_t VID_OPER      = 0xD844;
static const uint16_t VID_CURSOR    = 0xD845;
static const uint16_t VID_CURCOL    = 0xD846;
static const uint16_t VID_CURROW    = 0xD847;

// Keyboard
static const uint16_t KBD_BASE     = 0xD860;
static const uint16_t KBD_DATA     = 0xD860;
static const uint16_t KBD_STATUS   = 0xD861;
static const uint16_t KBD_ACK      = 0xD861;  // Write alias
static const uint16_t KBD_CTRL     = 0xD862;

// IRQ bits
static const uint8_t  IRQ_BIT_TTY  = 1;
static const uint8_t  IRQ_BIT_KBD  = 2;

// ROM
static const uint16_t ROM_BASE     = 0xE000;
static const uint16_t ROM_SIZE     = 0x2000;  // 8 KB

// Font
static const int      FONT_CHARS   = 256;
static const int      FONT_WIDTH   = 8;
static const int      FONT_HEIGHT  = 16;
```

**Test:** Existing tests compile and pass unchanged (constants not yet used by old code paths).

---

### Phase 1: IRQ Flag Register Migration (`$00FF` to `$D800`)

**Goal:** Move the IRQ flag register from zero page `$00FF` to device space `$D800`. This is the smallest, safest change and affects the most code paths.

**Emulator changes (`emulator.cpp`):**

1. Replace `#define IRQ_CLR() mem[0x00FF] = 0x00` with `mem[IRQ_FLAGS] = 0x00`.
2. Replace `#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)` with `mem[IRQ_FLAGS] = (mem[IRQ_FLAGS] | (0x01 << bit))`.
3. In `emulator_step()`, replace `if(mem[0x00FF] == 0)` with `if(mem[IRQ_FLAGS] == 0)`.
4. In `emu_clr_irq()`, replace `mem[0x00FF]` references with `mem[IRQ_FLAGS]`.
5. Add bus decode for `$D800`:
   ```cpp
   BUS_DECODE(addr, IRQ_FLAGS, 0xFFFF) {
       // IRQ_FLAGS is R/W at $D800
       // Read: return current flags. Write: allow firmware to clear bits.
       // Already handled by the generic mem[] read/write above.
   }
   ```

**Firmware changes:**

- `firmware/zp.inc`: Remove `ZP_IRQ $FF` or keep as legacy alias. Add new device address constant.
- `firmware/devices.inc`: Add `IRQ_FLAGS $D800`.
- `firmware/interrupt.s`: If it reads `$00FF` for IRQ status, update to `$D800`. (Current code does not read `$00FF` directly -- it checks TTY status register instead. But verify.)

**Test changes (`test_tty.cpp`, `test_integration.cpp`):**

- Replace `mem[0x00FF]` references with `mem[IRQ_FLAGS]` or `mem[0xD800]`.
- T76 ("clears IRQ bit 1"): `CHECK((mem[0xD800] & 0x02) == 0)`.
- T101 ("IRQ triggers vector"): The firmware-level test may not reference `$00FF` directly, but ensure the IRQ mechanism still fires.

**Verification:**
```bash
make test   # All existing tests pass
```

---

### Phase 2: TTY Address Migration (`$C100` to `$D820`)

**Goal:** Move the TTY device from `$C100-$C103` to `$D820-$D823`.

**Emulator changes (`emulator.cpp`):**

1. Change bus decode from:
   ```cpp
   BUS_DECODE(addr, 0xC100, 0xFFF0) {
       const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
       tty_decode(pins, dev_reg);
   }
   ```
   to:
   ```cpp
   BUS_DECODE(addr, TTY_BASE, 0xFFE0) {  // $D820-$D83F (32-byte slot)
       const uint8_t dev_reg = addr - TTY_BASE;
       tty_decode(pins, dev_reg);
   }
   ```
   The mask `0xFFE0` decodes the top 11 bits, giving a 32-byte window at `$D820`. The TTY only uses offsets 0-3; offsets 4-31 return `$00` on read (already handled by `tty_decode` default case).

**Firmware changes:**

- `firmware/devices.inc`:
  ```
  .define TTY_OUT_CTRL  $D820
  .define TTY_OUT_DATA  $D821
  .define TTY_IN_CTRL   $D822
  .define TTY_IN_DATA   $D823
  ```
- `firmware/devices.h`: Same updates for C defines.
- Rebuild firmware: `make firmware`.

**Test changes:**

- `test_tty.cpp`: Update pin address comments. The tests use `tty_decode(p, reg)` with the register offset, not the absolute address. The `make_read_pins(0xC100)` calls are cosmetic (the address in the pins is not used by `tty_decode`). However, for documentation clarity, update to `make_read_pins(0xD820)`.
- `test_bus.cpp`: The bus decode tests at `$C100` are implicit (no explicit TTY bus tests exist in `test_bus.cpp`). No change needed.
- If any integration test sends CPU instructions to `$C100`, update those addresses.

**Verification:**
```bash
make test       # All tests pass
make firmware   # Firmware builds
make            # Emulator builds
```

---

### Phase 3: Frame Buffer Expansion (`$C000-$C0FF` to `$C000-$CFFF`)

**Goal:** Expand the frame buffer from 256 bytes to 4 KB. Introduce a separate `frame_buffer[]` backing store so the frame buffer is not just `mem[]` aliases.

**Rationale for separate backing store:** The hardware spec says "CPU reads/writes are intercepted by bus decode and routed to a separate backing store (not backed by main RAM)." This enables future bank-switching of the `$C000-$CFFF` region without affecting the frame buffer. It also makes dirty tracking simpler.

**Emulator changes:**

1. Add to `emulator.cpp` (or a new `emu_video.cpp`):
   ```cpp
   static uint8_t frame_buffer[FB_SIZE];  // 4096 bytes
   static bool    fb_dirty = true;        // Set on any write to $C000-$CFFF
   ```

2. Modify bus decode in `emulator_step()`. Currently, the generic `mem[]` read/write handles everything. Add frame buffer interception **before** the generic handler:
   ```cpp
   // Frame buffer decode: $C000-$CFFF
   if (addr >= FB_BASE && addr <= FB_END) {
       if (BUS_READ) {
           M6502_SET_DATA(pins, frame_buffer[addr - FB_BASE]);
       } else {
           uint8_t val = M6502_GET_DATA(pins);
           if (frame_buffer[addr - FB_BASE] != val) {
               frame_buffer[addr - FB_BASE] = val;
               fb_dirty = true;
           }
       }
       // Skip generic mem[] handler for this address
       goto bus_done;
   }
   ```

   Alternatively, keep writing to both `mem[]` and `frame_buffer[]` if refactoring the control flow is too invasive. The cleaner approach is to skip `mem[]` for the `$C000-$CFFF` range entirely.

3. Expose frame buffer to rendering code:
   ```cpp
   // In emulator.h
   extern uint8_t* emulator_get_framebuffer();
   extern bool     emulator_fb_dirty();
   extern void     emulator_fb_clear_dirty();
   ```

**Test changes (`test_bus.cpp`):**

- Update T64-T67 to verify writes go to frame buffer, not just `mem[]`:
  ```cpp
  // T64 updated: write to $C000 lands in frame buffer
  CHECK(emulator_get_framebuffer()[0] == 0x41);
  ```
- Add new tests:
  - **T64a:** Write to `$C7CF` (last active byte in 80x25) succeeds.
  - **T64b:** Write to `$CFFF` (last byte of 4 KB buffer) succeeds.
  - **T64c:** Read from `$C000` after write returns correct value (round-trip).
  - **T64d:** `fb_dirty` flag is set after write, cleared after `emulator_fb_clear_dirty()`.

**Verification:**
```bash
make test   # All bus tests pass with frame buffer backing store
```

---

### Phase 4: ROM Base Migration (`$D000` to `$E000`)

**Goal:** Move ROM from `$D000-$FFFF` (12 KB) to `$E000-$FFFF` (8 KB), freeing `$D000-$DFFF` for dev bank and device registers.

**Emulator changes (`emulator.cpp`):**

1. Change `emulator_loadrom()`:
   ```cpp
   uint16_t rom_ptr = ROM_BASE;  // $E000 instead of $D000
   ```

2. The generic `mem[]` handler already covers the ROM region. No bus decode changes needed for ROM reads.

3. Optionally, add write protection for the ROM region:
   ```cpp
   // In the write path:
   if (addr >= ROM_BASE) {
       // ROM is read-only; silently ignore writes
       goto bus_done;
   }
   ```

**Firmware changes (`firmware/n8.cfg`):**

```
MEMORY {
    ZP:   start =    $0, size =  $100, type = rw, define = yes;
    RAM:  start =  $400, size = $BC00, define = yes;
    ROM:  start = $E000, size = $2000, file = %O;
}
```

Note: RAM start changes from `$200` to `$400` to match the proposed map (the `$200-$3FF` region is marked "????" / reserved). If firmware needs the `$200-$3FF` range, keep `RAM start=$200 size=$BE00`. The conservative choice is to match the spec exactly.

**GDB stub changes (`gdb_stub.cpp`):**

Update `memory_map_xml`:
```cpp
static const char memory_map_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE memory-map SYSTEM \"gdb-memory-map.dtd\">\n"
    "<memory-map>\n"
    "  <memory type=\"ram\"  start=\"0x0000\" length=\"0xC000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC000\" length=\"0x1000\"/>\n"  // Frame buffer
    "  <memory type=\"ram\"  start=\"0xD000\" length=\"0x1000\"/>\n"  // Dev bank + device regs
    "  <memory type=\"rom\"  start=\"0xE000\" length=\"0x2000\"/>\n"
    "</memory-map>\n";
```

**Test changes:**

- `test_integration.cpp`: All tests load code at `$D000` and set reset vector to `$D000`. These tests use `EmulatorFixture` which writes directly to `mem[]`. This still works even after the ROM move because tests bypass `emulator_loadrom()`. The reset vector tests at `$FFFC-$FFFD` are unchanged.
- **However**, any test that relies on `$D000` being ROM-like (write-protected) will need adjustment if we add ROM write protection. For now, keep tests loading at `$D000` since it is now RAM/dev bank space and writes are valid.
- Add test: **T95a:** Load code at `$E000`, set reset vector, verify PC lands at `$E000`.

**Verification:**
```bash
make test
make firmware   # Firmware builds to 8 KB binary at $E000
make            # Emulator loads from new ROM base
```

---

### Phase 5: Video Control Registers (`$D840-$D847`)

**Goal:** Implement the VID_MODE, VID_WIDTH, VID_HEIGHT, VID_STRIDE, VID_OPER, VID_CURSOR, VID_CURCOL, VID_CURROW registers.

**New file: `src/emu_video.h`**

```cpp
#pragma once
#include <cstdint>

// Video state (readable by renderer)
struct video_state_t {
    uint8_t  mode;       // $00 = Text Default, $01 = Text Custom
    uint8_t  width;      // columns (default 80)
    uint8_t  height;     // rows (default 25)
    uint8_t  stride;     // row stride (default 80, same as width)
    uint8_t  cursor;     // VID_CURSOR register value
    uint8_t  cur_col;    // cursor column
    uint8_t  cur_row;    // cursor row
};

void video_init();
void video_reset();
void video_decode(uint64_t& pins, uint8_t dev_reg);
const video_state_t& video_get_state();

// Frame buffer access
uint8_t* video_get_framebuffer();
bool     video_fb_dirty();
void     video_fb_clear_dirty();

// Scroll operations (called from video_decode on VID_OPER write)
void video_scroll_up();
void video_scroll_down();
void video_scroll_left();
void video_scroll_right();
```

**New file: `src/emu_video.cpp`**

```cpp
#include "emu_video.h"
#include "machine.h"
#include "m6502.h"
#include <cstring>

static video_state_t state;
static uint8_t frame_buffer[FB_SIZE];
static bool    fb_dirty = true;

void video_init() {
    video_reset();
}

void video_reset() {
    state.mode    = 0x00;  // Text Default
    state.width   = VID_COLS;
    state.height  = VID_ROWS;
    state.stride  = VID_STRIDE;
    state.cursor  = 0x00;  // Cursor off
    state.cur_col = 0;
    state.cur_row = 0;
    memset(frame_buffer, 0, FB_SIZE);
    fb_dirty = true;
}

void video_decode(uint64_t& pins, uint8_t dev_reg) {
    if (pins & M6502_RW) {  // Read
        uint8_t data = 0;
        switch (dev_reg) {
            case 0: data = state.mode;    break;
            case 1: data = state.width;   break;
            case 2: data = state.height;  break;
            case 3: data = state.stride;  break;
            case 4: data = 0;             break;  // VID_OPER is write-only
            case 5: data = state.cursor;  break;
            case 6: data = state.cur_col; break;
            case 7: data = state.cur_row; break;
            default: data = 0;
        }
        M6502_SET_DATA(pins, data);
    } else {  // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (dev_reg) {
            case 0:  // VID_MODE
                state.mode = val;
                if (val == 0x00) {
                    state.width  = VID_COLS;
                    state.height = VID_ROWS;
                    state.stride = VID_STRIDE;
                }
                break;
            case 1: state.width  = val; break;
            case 2: state.height = val; break;
            case 3: state.stride = val; break;
            case 4:  // VID_OPER -- write-once scroll trigger
                switch (val) {
                    case 0x00: video_scroll_up();    break;
                    case 0x01: video_scroll_down();  break;
                    case 0x02: video_scroll_left();  break;
                    case 0x03: video_scroll_right(); break;
                }
                break;
            case 5: state.cursor  = val; break;
            case 6: state.cur_col = val; break;
            case 7: state.cur_row = val; break;
        }
    }
}

// ---- Scroll operations ----
// All operate on the active framebuffer region defined by width/height/stride.

void video_scroll_up() {
    int w = state.width;
    int h = state.height;
    int s = state.stride;
    // Move rows 1..h-1 up by one row
    for (int row = 0; row < h - 1; row++) {
        memcpy(&frame_buffer[row * s], &frame_buffer[(row + 1) * s], w);
    }
    // Clear bottom row
    memset(&frame_buffer[(h - 1) * s], 0x00, w);
    fb_dirty = true;
}

void video_scroll_down() {
    int w = state.width;
    int h = state.height;
    int s = state.stride;
    // Move rows h-2..0 down by one row (iterate backwards)
    for (int row = h - 1; row > 0; row--) {
        memcpy(&frame_buffer[row * s], &frame_buffer[(row - 1) * s], w);
    }
    // Clear top row
    memset(&frame_buffer[0], 0x00, w);
    fb_dirty = true;
}

void video_scroll_left() {
    int w = state.width;
    int h = state.height;
    int s = state.stride;
    for (int row = 0; row < h; row++) {
        uint8_t* line = &frame_buffer[row * s];
        memmove(line, line + 1, w - 1);
        line[w - 1] = 0x00;
    }
    fb_dirty = true;
}

void video_scroll_right() {
    int w = state.width;
    int h = state.height;
    int s = state.stride;
    for (int row = 0; row < h; row++) {
        uint8_t* line = &frame_buffer[row * s];
        memmove(line + 1, line, w - 1);
        line[0] = 0x00;
    }
    fb_dirty = true;
}

const video_state_t& video_get_state() { return state; }
uint8_t* video_get_framebuffer()       { return frame_buffer; }
bool     video_fb_dirty()              { return fb_dirty; }
void     video_fb_clear_dirty()        { fb_dirty = false; }
```

**Bus decode integration (`emulator.cpp`):**

```cpp
#include "emu_video.h"

// In emulator_step(), add:

// Frame buffer: $C000-$CFFF
if (addr >= FB_BASE && addr <= FB_END) {
    uint8_t* fb = video_get_framebuffer();
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, fb[addr - FB_BASE]);
    } else {
        uint8_t val = M6502_GET_DATA(pins);
        fb[addr - FB_BASE] = val;
        // fb_dirty set implicitly (or call a setter)
    }
    // Do NOT fall through to generic mem[] handler
}

// Video control: $D840-$D85F (32-byte slot)
BUS_DECODE(addr, VID_BASE, 0xFFE0) {
    video_decode(pins, addr - VID_BASE);
}
```

**Tests (`test/test_video.cpp` -- new file):**

```
TEST_SUITE("video") {

    T_VID_01: video_init sets mode=$00, width=80, height=25, stride=80
    T_VID_02: video_reset clears framebuffer to all zeros
    T_VID_03: Read VID_MODE returns $00 after init
    T_VID_04: Write VID_MODE=$00 sets width=80, height=25, stride=80
    T_VID_05: Write VID_WIDTH=$40 reads back $40
    T_VID_06: Read VID_OPER returns $00 (write-only, doesn't latch)

    T_VID_10: Scroll up -- row 0 gets row 1 content, bottom row cleared
    T_VID_11: Scroll down -- row 1 gets row 0 content, top row cleared
    T_VID_12: Scroll left -- each row shifts left, rightmost column cleared
    T_VID_13: Scroll right -- each row shifts right, leftmost column cleared
    T_VID_14: Scroll up via VID_OPER write $00 triggers scroll_up

    T_VID_20: Write VID_CURSOR=$05 reads back $05
    T_VID_21: Write VID_CURCOL=$4F, VID_CURROW=$18 reads back correctly
    T_VID_22: video_reset sets cursor off, col=0, row=0

    T_VID_30: Frame buffer write at $C000 via bus decode reaches video_get_framebuffer()[0]
    T_VID_31: Frame buffer read at $C7CF returns last active byte for 80x25
    T_VID_32: Frame buffer write at $CFFF (beyond active) still accessible
}
```

**Verification:**
```bash
make test   # New video tests pass, existing tests unaffected
```

---

### Phase 6: Keyboard Device (`$D860-$D862`)

**Goal:** Implement the keyboard input device with SDL2 key event capture.

**New file: `src/emu_keyboard.h`**

```cpp
#pragma once
#include <cstdint>

void kbd_init();
void kbd_reset();
void kbd_decode(uint64_t& pins, uint8_t dev_reg);

// Called from SDL2 event loop (main.cpp)
void kbd_key_press(uint8_t keycode, uint8_t modifiers);
void kbd_update_modifiers(uint8_t modifiers);
```

**New file: `src/emu_keyboard.cpp`**

```cpp
#include "emu_keyboard.h"
#include "machine.h"
#include "emulator.h"
#include "m6502.h"

static uint8_t kbd_data     = 0;    // KBD_DATA register
static uint8_t kbd_status   = 0;    // KBD_STATUS register (read-only bits)
static uint8_t kbd_ctrl     = 0;    // KBD_CTRL register
static uint8_t kbd_modifier = 0;    // Live modifier state (bits 2-5 of status)

void kbd_init()  { kbd_reset(); }

void kbd_reset() {
    kbd_data     = 0;
    kbd_status   = 0;
    kbd_ctrl     = 0;
    kbd_modifier = 0;
}

void kbd_decode(uint64_t& pins, uint8_t dev_reg) {
    if (pins & M6502_RW) {  // Read
        uint8_t data = 0;
        switch (dev_reg) {
            case 0:  // KBD_DATA
                data = kbd_data;
                break;
            case 1:  // KBD_STATUS
                data = (kbd_status & 0x03) | (kbd_modifier & 0x3C);
                break;
            case 2:  // KBD_CTRL
                data = kbd_ctrl;
                break;
            default:
                data = 0;
        }
        M6502_SET_DATA(pins, data);
    } else {  // Write
        uint8_t val = M6502_GET_DATA(pins);
        switch (dev_reg) {
            case 1:  // KBD_ACK -- write any value clears DATA_AVAIL + OVERFLOW + IRQ
                kbd_status &= ~0x03;  // Clear DATA_AVAIL and OVERFLOW
                emu_clr_irq(IRQ_BIT_KBD);
                break;
            case 2:  // KBD_CTRL
                kbd_ctrl = val;
                break;
            default:
                break;  // KBD_DATA is read-only; writes ignored
        }
    }
}

void kbd_key_press(uint8_t keycode, uint8_t modifiers) {
    if (kbd_status & 0x01) {
        // DATA_AVAIL already set -- overflow (most-recent-key-wins)
        kbd_status |= 0x02;  // Set OVERFLOW
    }
    kbd_data = keycode;
    kbd_status |= 0x01;  // Set DATA_AVAIL
    kbd_modifier = modifiers & 0x3C;  // Bits 2-5

    if (kbd_ctrl & 0x01) {  // IRQ enabled
        emu_set_irq(IRQ_BIT_KBD);
    }
}

void kbd_update_modifiers(uint8_t modifiers) {
    kbd_modifier = modifiers & 0x3C;
}
```

**SDL2 Key Conversion (`main.cpp`):**

In the SDL event loop, intercept `SDL_KEYDOWN` events when the emulated display has focus (not when ImGui wants keyboard input):

```cpp
// In the SDL_PollEvent loop:
if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
    uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
    uint8_t mods   = sdl_to_n8_modifiers(event.key.keysym.mod);
    if (n8_key != 0) {
        kbd_key_press(n8_key, mods);
    }
    kbd_update_modifiers(mods);
}
```

**Key conversion function:**

```cpp
static uint8_t sdl_to_n8_keycode(SDL_Keysym keysym) {
    SDL_Keycode k = keysym.sym;
    uint16_t mod = keysym.mod;

    // Ctrl+letter → $01-$1A
    if ((mod & KMOD_CTRL) && k >= SDLK_a && k <= SDLK_z) {
        return (uint8_t)(k - SDLK_a + 1);
    }

    // ASCII printable range
    if (k >= SDLK_SPACE && k <= SDLK_z) {
        // SDL keycodes for printable ASCII match the ASCII values
        char c = (char)k;
        // Handle shift for letters
        if (k >= SDLK_a && k <= SDLK_z && (mod & KMOD_SHIFT)) {
            c = c - 32;  // lowercase to uppercase
        }
        // Handle shift for number row and symbols
        // (SDL provides the unshifted keycode; we must map shifted variants)
        if (mod & KMOD_SHIFT) {
            switch (k) {
                case SDLK_1: return '!';
                case SDLK_2: return '@';
                case SDLK_3: return '#';
                case SDLK_4: return '$';
                case SDLK_5: return '%';
                case SDLK_6: return '^';
                case SDLK_7: return '&';
                case SDLK_8: return '*';
                case SDLK_9: return '(';
                case SDLK_0: return ')';
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

        // Extended keys ($80+)
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

        // Function keys
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

        default: return 0;  // Unknown key -- ignore
    }
}

static uint8_t sdl_to_n8_modifiers(uint16_t sdl_mod) {
    uint8_t m = 0;
    if (sdl_mod & KMOD_SHIFT) m |= 0x04;  // Bit 2
    if (sdl_mod & KMOD_CTRL)  m |= 0x08;  // Bit 3
    if (sdl_mod & KMOD_ALT)   m |= 0x10;  // Bit 4
    if (sdl_mod & KMOD_CAPS)  m |= 0x20;  // Bit 5
    return m;
}
```

**Bus decode integration (`emulator.cpp`):**

```cpp
#include "emu_keyboard.h"

// In emulator_step():
BUS_DECODE(addr, KBD_BASE, 0xFFE0) {  // $D860-$D87F
    kbd_decode(pins, addr - KBD_BASE);
}
```

**Tests (`test/test_keyboard.cpp` -- new file):**

```
TEST_SUITE("keyboard") {
    T_KBD_01: kbd_init -- KBD_DATA=0, KBD_STATUS=0, KBD_CTRL=0
    T_KBD_02: kbd_key_press sets DATA_AVAIL, read KBD_DATA returns keycode
    T_KBD_03: KBD_ACK write clears DATA_AVAIL and OVERFLOW
    T_KBD_04: Two key presses without ACK sets OVERFLOW, data=second key
    T_KBD_05: KBD_CTRL bit 0 enables IRQ; kbd_key_press asserts IRQ bit 2
    T_KBD_06: KBD_ACK deasserts IRQ bit 2
    T_KBD_07: Modifier bits reflect live state independent of DATA_AVAIL
    T_KBD_08: kbd_reset clears all state
    T_KBD_09: Read from reserved registers ($D863-$D867) returns 0
}
```

**Verification:**
```bash
make test   # Keyboard tests pass
```

---

### Phase 7: Font Loading and Texture Creation

**Goal:** Load the N8 font into an OpenGL texture atlas. This is the foundation of the rendering pipeline.

**Architecture Decision: Single Texture Atlas**

The 256-character font is baked into a single OpenGL texture at startup. Layout: 16 columns x 16 rows of 8x16 cells = 128 x 256 pixel texture. Each pixel is 1 byte (GL_RED or GL_LUMINANCE). The shader or rendering code maps this to the desired phosphor color.

```
Texture atlas layout (128 x 256 pixels):
  Col:  0    1    2    ...  15
  Row 0: $00  $01  $02  ...  $0F    (each cell is 8x16 px)
  Row 1: $10  $11  $12  ...  $1F
  ...
  Row 15: $F0  $F1  $F2  ...  $FF
```

**UV calculation for character `c`:**

```
col = c & 0x0F
row = (c >> 4) & 0x0F
u0 = col * 8.0 / 128.0
v0 = row * 16.0 / 256.0
u1 = u0 + 8.0 / 128.0
v1 = v0 + 16.0 / 256.0
```

**Implementation (`src/emu_video.cpp` or `src/video_renderer.cpp`):**

```cpp
#include "n8_font.h"  // Copy from docs/charset/ to src/
#include <SDL_opengl.h>

static GLuint font_texture = 0;

void video_create_font_texture() {
    // Build monochrome atlas: 128 x 256 pixels, 1 byte per pixel
    uint8_t atlas[256 * 128];
    memset(atlas, 0, sizeof(atlas));

    for (int ch = 0; ch < 256; ch++) {
        int col = ch & 0x0F;
        int row = (ch >> 4) & 0x0F;
        int base_x = col * FONT_WIDTH;    // 0..120
        int base_y = row * FONT_HEIGHT;   // 0..240

        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t bits = n8_font[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                bool lit = (bits >> (7 - x)) & 1;
                atlas[(base_y + y) * 128 + base_x + x] = lit ? 0xFF : 0x00;
            }
        }
    }

    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 128, 256, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas);
}

void video_destroy_font_texture() {
    if (font_texture) {
        glDeleteTextures(1, &font_texture);
        font_texture = 0;
    }
}
```

**Why GL_NEAREST:** The N8 font is pixel art. Any interpolation (GL_LINEAR) would blur the sharp edges. GL_NEAREST preserves the crisp 1-pixel-is-1-pixel aesthetic at any zoom level.

**Why GL_RED:** The font is monochrome. Using a single-channel texture saves GPU memory (32 KB vs 128 KB for RGBA) and lets the fragment shader apply any phosphor color. If GL_RED is not available (GLES2), fall back to GL_LUMINANCE.

**Integration point:** Call `video_create_font_texture()` once after OpenGL context creation, before the main loop. Call `video_destroy_font_texture()` during cleanup.

**Test:** This is a GPU operation and cannot be tested in the headless test binary. Verification is visual: add an ImGui debug window that displays the font atlas texture.

---

### Phase 8: Screen Texture and Rendering Pipeline

**Goal:** Render the frame buffer contents to an OpenGL texture every frame, then display it in an ImGui window.

**Architecture: CPU-side Rasterization to Screen Texture**

The rendering pipeline is:

```
frame_buffer[2000]  --->  screen_pixels[640x400]  --->  GL texture  --->  ImGui::Image()
     (CPU)                    (CPU rasterize)           (GPU upload)      (GPU draw)
```

**Why CPU-side rasterization (not GPU glyph rendering):**

1. **Simplicity.** Compositing 2000 textured quads per frame on the GPU requires a vertex buffer, shader program, and draw calls. CPU rasterization is a tight nested loop.
2. **Dirty tracking.** Only re-rasterize when `fb_dirty` is set. Most frames, the frame buffer does not change, so the texture upload is skipped entirely.
3. **Performance.** 640x400 pixels = 256 KB. Filling this from the font atlas is a memcpy-speed operation on modern CPUs (~0.1 ms). The GPU upload via `glTexSubImage2D` is also fast for this size.
4. **Cursor compositing.** The cursor overlay is trivially applied during CPU rasterization. No separate draw pass needed.

**Screen texture dimensions:**

- 80 columns x 8 pixels/char = 640 pixels wide
- 25 rows x 16 pixels/char = 400 pixels tall
- Format: RGBA (4 bytes/pixel) for ImGui compatibility
- Total: 640 x 400 x 4 = 1,024,000 bytes (1 MB)

**Rasterization function:**

```cpp
static GLuint  screen_texture = 0;
static uint32_t screen_pixels[640 * 400];  // RGBA

// Phosphor color presets (RGBA, little-endian on x86)
static const uint32_t COLOR_GREEN  = 0xFF33FF33;  // #33FF33
static const uint32_t COLOR_AMBER  = 0xFF00BBFF;  // #FFBB00 (ABGR)
static const uint32_t COLOR_WHITE  = 0xFFCCCCCC;  // #CCCCCC
static const uint32_t COLOR_BG     = 0xFF000000;  // Black

static uint32_t fg_color = COLOR_GREEN;

void video_rasterize_screen() {
    const uint8_t* fb = video_get_framebuffer();
    const video_state_t& vs = video_get_state();
    int cols = vs.width;
    int rows = vs.height;
    int stride = vs.stride;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            uint8_t ch = fb[row * stride + col];
            const uint8_t* glyph = n8_font[ch];

            int px = col * FONT_WIDTH;
            int py = row * FONT_HEIGHT;

            for (int gy = 0; gy < FONT_HEIGHT; gy++) {
                uint8_t bits = glyph[gy];
                uint32_t* dest = &screen_pixels[(py + gy) * 640 + px];
                for (int gx = 0; gx < FONT_WIDTH; gx++) {
                    bool lit = (bits >> (7 - gx)) & 1;
                    dest[gx] = lit ? fg_color : COLOR_BG;
                }
            }
        }
    }
}

void video_create_screen_texture() {
    glGenTextures(1, &screen_texture);
    glBindTexture(GL_TEXTURE_2D, screen_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 400, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void video_upload_screen_texture() {
    glBindTexture(GL_TEXTURE_2D, screen_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 400,
                    GL_RGBA, GL_UNSIGNED_BYTE, screen_pixels);
}
```

**ImGui display window (`main.cpp`):**

```cpp
static bool show_display_window = true;

// In the ImGui rendering section:
if (show_display_window) {
    ImGui::Begin("N8 Display", &show_display_window);

    if (video_fb_dirty()) {
        video_rasterize_screen();
        video_upload_screen_texture();
        video_fb_clear_dirty();
    }

    // Display at 1x, 2x, or fit-to-window
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale_x = avail.x / 640.0f;
    float scale_y = avail.y / 400.0f;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1.0f) scale = 1.0f;
    ImVec2 size(640.0f * scale, 400.0f * scale);

    ImGui::Image((ImTextureID)(intptr_t)screen_texture, size);
    ImGui::End();
}
```

**Performance notes:**

- At 60 FPS with the emulator running, `fb_dirty` will typically be set only when firmware writes to the frame buffer. During idle (no writes), the rasterize+upload path is skipped entirely.
- If firmware does rapid writes (e.g., scrolling text), the dirty flag coalesces all writes within a single frame into one rasterize pass. This is optimal -- we never rasterize more than once per frame.
- The 1 MB screen pixel buffer is stack-unfriendly. Allocate as static or heap.

**Verification:** Visual. Write a test firmware that fills `$C000-$C7CF` with sequential character codes and verify all 256 glyphs render correctly.

---

### Phase 9: Cursor Rendering

**Goal:** Render the text cursor as specified by VID_CURSOR, VID_CURCOL, VID_CURROW.

**Cursor state machine:**

```
VID_CURSOR bits:
  [1:0] MODE:  00=off, 01=steady, 10=flash, 11=reserved
  [2]   SHAPE: 0=underline, 1=block
  [4:3] RATE:  00=off, 01=slow(1Hz), 10=medium(2Hz), 11=fast(4Hz)
```

**Implementation:**

The cursor is composited during `video_rasterize_screen()`, after the character glyphs. This avoids a separate draw pass.

```cpp
static uint32_t cursor_frame_counter = 0;

void video_rasterize_cursor() {
    const video_state_t& vs = video_get_state();
    uint8_t mode  = vs.cursor & 0x03;
    bool    block = (vs.cursor >> 2) & 1;
    uint8_t rate  = (vs.cursor >> 3) & 0x03;
    uint8_t col   = vs.cur_col;
    uint8_t row   = vs.cur_row;

    if (mode == 0) return;  // Cursor off

    // Flash logic
    if (mode == 2) {  // Flash mode
        uint32_t period;
        switch (rate) {
            case 0:  return;          // Rate off = no flash = cursor off
            case 1:  period = 60; break;  // 1 Hz at 60 FPS: 30 on, 30 off
            case 2:  period = 30; break;  // 2 Hz
            case 3:  period = 15; break;  // 4 Hz
            default: period = 60;
        }
        if ((cursor_frame_counter % period) >= (period / 2)) {
            return;  // Cursor in "off" half of flash cycle
        }
    }

    // Bounds check
    if (col >= vs.width || row >= vs.height) return;

    int px = col * FONT_WIDTH;
    int py = row * FONT_HEIGHT;

    if (block) {
        // Block cursor: invert entire cell
        for (int gy = 0; gy < FONT_HEIGHT; gy++) {
            uint32_t* dest = &screen_pixels[(py + gy) * 640 + px];
            for (int gx = 0; gx < FONT_WIDTH; gx++) {
                dest[gx] = (dest[gx] == fg_color) ? COLOR_BG : fg_color;
            }
        }
    } else {
        // Underline cursor: fill bottom 2 rows of cell with fg color
        for (int gy = FONT_HEIGHT - 2; gy < FONT_HEIGHT; gy++) {
            uint32_t* dest = &screen_pixels[(py + gy) * 640 + px];
            for (int gx = 0; gx < FONT_WIDTH; gx++) {
                dest[gx] = fg_color;
            }
        }
    }
}
```

**Integration in rasterize path:**

```cpp
void video_rasterize_frame() {
    video_rasterize_screen();
    video_rasterize_cursor();
    cursor_frame_counter++;
}
```

**Dirty flag interaction:** The cursor flash requires re-rasterization even when `fb_dirty` is false. When the cursor is in flash mode, force rasterization every frame:

```cpp
bool need_rasterize = video_fb_dirty();
const video_state_t& vs = video_get_state();
uint8_t cursor_mode = vs.cursor & 0x03;
if (cursor_mode == 2) need_rasterize = true;  // Flash mode always re-renders
if (cursor_mode == 1) need_rasterize = true;  // Steady cursor needs initial render

if (need_rasterize) {
    video_rasterize_frame();
    video_upload_screen_texture();
    video_fb_clear_dirty();
}
```

**Optimization:** If the cursor is steady (mode=1) and `fb_dirty` is false, skip rasterization after the first frame. Track `cursor_dirty` separately for cursor position/style changes.

**Tests:**

Unit tests for cursor logic can verify the pixel buffer contents:

```
T_CUR_01: Cursor off (mode=00) -- no pixels modified
T_CUR_02: Cursor steady underline -- bottom 2 rows of cell are fg_color
T_CUR_03: Cursor steady block -- all cell pixels inverted
T_CUR_04: Cursor flash slow -- alternates every 30 frames
T_CUR_05: Cursor position out of bounds -- no crash, no pixels modified
T_CUR_06: Cursor position update via VID_CURCOL/VID_CURROW moves cursor
```

---

### Phase 10: Complete Bus Decode Refactor

**Goal:** Clean up `emulator_step()` to handle the full new memory map with proper decode priority.

**Decode order in `emulator_step()`:**

```cpp
// 1. Tick CPU
pins = m6502_tick(&cpu, pins);
const uint16_t addr = M6502_GET_ADDR(pins);

// 2. Breakpoint/watchpoint checks (unchanged)

// 3. IRQ tick
mem[IRQ_FLAGS] = 0x00;  // Clear flags; devices reassert below
tty_tick(pins);
// kbd does not need a tick -- it's event-driven from SDL

// 4. IRQ line
if (mem[IRQ_FLAGS] == 0) {
    pins = pins & ~M6502_IRQ;
} else {
    pins = pins | M6502_IRQ;
}

// 5. Bus decode (order matters: devices before generic RAM/ROM)
bool handled = false;

// Frame buffer: $C000-$CFFF
if (addr >= FB_BASE && addr <= FB_END) {
    uint8_t* fb = video_get_framebuffer();
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, fb[addr - FB_BASE]);
    } else {
        fb[addr - FB_BASE] = M6502_GET_DATA(pins);
        fb_dirty_flag = true;
    }
    handled = true;
}

// Device registers: $D800-$DFFF
if (!handled && addr >= DEV_BASE && addr <= DEV_END) {
    uint16_t dev_off = addr - DEV_BASE;
    if (dev_off < 0x20) {
        // $D800-$D81F: System/IRQ
        // IRQ_FLAGS at $D800 -- just use mem[]
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, mem[addr]);
        } else {
            mem[addr] = M6502_GET_DATA(pins);
        }
    } else if (dev_off < 0x40) {
        // $D820-$D83F: TTY
        tty_decode(pins, addr - TTY_BASE);
    } else if (dev_off < 0x60) {
        // $D840-$D85F: Video control
        video_decode(pins, addr - VID_BASE);
    } else if (dev_off < 0x80) {
        // $D860-$D87F: Keyboard
        kbd_decode(pins, addr - KBD_BASE);
    } else {
        // $D880-$DFFF: Reserved -- read returns $00, write ignored
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, 0x00);
        }
    }
    handled = true;
}

// ROM write protection: $E000-$FFFF
if (!handled && addr >= ROM_BASE && !(pins & M6502_RW)) {
    // Silently ignore writes to ROM
    handled = true;
}

// Generic RAM/ROM read/write
if (!handled) {
    if (pins & M6502_RW) {
        M6502_SET_DATA(pins, mem[addr]);
    } else {
        mem[addr] = M6502_GET_DATA(pins);
    }
}

tick_count++;
```

**Key design decisions:**

1. **Frame buffer is NOT backed by `mem[]`.** Reads/writes go to the separate `frame_buffer[]` array. This is critical for future bank switching.
2. **Device registers are NOT backed by `mem[]`** (except IRQ_FLAGS for simplicity). Reads/writes go through device decode functions.
3. **ROM is write-protected.** Writes to `$E000-$FFFF` are silently ignored. This prevents firmware bugs from corrupting ROM.
4. **Reserved device space** reads as `$00`. No crash, no side effects.

**Tests:**

Update all existing bus tests to verify the new decode behavior. Add:

```
T_BUS_01: Write to $C500 (frame buffer) does not appear in mem[$C500]
T_BUS_02: Write to $E000 (ROM) is silently ignored
T_BUS_03: Read from $D880 (reserved device) returns $00
T_BUS_04: Write to $D800 (IRQ_FLAGS) is writable by CPU
T_BUS_05: Read from $D840 (VID_MODE) after init returns $00
T_BUS_06: Write to $D860 (KBD_DATA) is ignored (read-only register)
```

---

### Phase 11: Firmware Update

**Goal:** Update all firmware source to use new device addresses and ROM base.

**Changes:**

1. **`firmware/devices.inc`:**
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

2. **`firmware/devices.h`:** Mirror the above as C `#define` macros.

3. **`firmware/n8.cfg`:**
   ```
   MEMORY {
       ZP:   start =    $0, size =  $100, type = rw, define = yes;
       RAM:  start =  $400, size = $BC00, define = yes;
       ROM:  start = $E000, size = $2000, file = %O;
   }
   ```

4. **`firmware/zp.inc`:** Remove `ZP_IRQ $FF`.

5. **`firmware/interrupt.s`:** Update `TTY_IN_CTRL` and `TTY_IN_DATA` references (already handled by `devices.inc` defines).

6. **`firmware/main.s`:** Update `TXT_BUFF` references to use `FB_BASE` (or keep `TXT_BUFF` as a define pointing to `$C000`). The writes `STA TXT_BUFF,X` remain valid since the frame buffer base is still `$C000`.

7. **Playground programs:** Update `playground.cfg` and `gdb_playground/playground.cfg` to use `ROM start=$E000 size=$2000`.

**Verification:**
```bash
make firmware       # Builds to 8 KB binary
make                # Emulator loads from $E000
# Boot emulator, verify "Welcome banner" still prints via TTY
```

---

### Phase 12: Makefile Updates

**Build system changes:**

1. Add new source files:
   ```makefile
   SOURCES += $(SRC_DIR)/emu_video.cpp $(SRC_DIR)/emu_keyboard.cpp
   ```

2. Add test source files:
   ```makefile
   # test/test_video.cpp and test/test_keyboard.cpp are picked up by wildcard
   ```

3. Update test production objects:
   ```makefile
   TEST_SRC_OBJS = $(BUILD_DIR)/emulator.o $(BUILD_DIR)/emu_tty.o \
                   $(BUILD_DIR)/emu_video.o $(BUILD_DIR)/emu_keyboard.o \
                   $(BUILD_DIR)/emu_dis6502.o $(BUILD_DIR)/emu_labels.o \
                   $(BUILD_DIR)/utils.o $(TEST_BUILD_DIR)/gdb_stub.o
   ```

4. Copy `docs/charset/n8_font.h` to `src/n8_font.h` (or add include path). Preference: copy to `src/` so the emulator build does not depend on `docs/`.

---

## Data Flow Diagrams

### Video Rendering Pipeline

```
                    ┌──────────────────────────────────────┐
                    │           CPU (m6502_tick)            │
                    │                                      │
                    │  STA $C000  ──> bus decode            │
                    │  STA $D844  ──> video_decode          │
                    └──────┬──────────────┬────────────────┘
                           │              │
                           v              v
                  ┌────────────┐  ┌───────────────┐
                  │ frame_     │  │ video_state_t  │
                  │ buffer[]   │  │ (mode, cursor, │
                  │ (4096 B)   │  │  width, etc.)  │
                  └─────┬──────┘  └───────┬────────┘
                        │                 │
                        v                 v
              ┌─────────────────────────────────────┐
              │       video_rasterize_frame()        │
              │                                     │
              │  for each cell:                     │
              │    ch = fb[row * stride + col]       │
              │    glyph = n8_font[ch]              │
              │    blit 8x16 pixels → screen_pixels │
              │                                     │
              │  composite cursor overlay            │
              └──────────────┬──────────────────────┘
                             │
                             v
                  ┌──────────────────┐
                  │  screen_pixels[] │
                  │  (640x400 RGBA)  │
                  └────────┬─────────┘
                           │
                           v  glTexSubImage2D()
                  ┌──────────────────┐
                  │  screen_texture  │
                  │  (GL_TEXTURE_2D) │
                  └────────┬─────────┘
                           │
                           v  ImGui::Image()
                  ┌──────────────────┐
                  │   ImGui Window   │
                  │  "N8 Display"    │
                  └──────────────────┘
```

### Keyboard Input Pipeline

```
  ┌─────────────────────────────────┐
  │  Physical Keyboard (USB HID)    │
  └──────────────┬──────────────────┘
                 │
                 v  OS → SDL2
  ┌─────────────────────────────────┐
  │  SDL_KEYDOWN event              │
  │  keysym.sym + keysym.mod        │
  └──────────────┬──────────────────┘
                 │
                 v  sdl_to_n8_keycode()
  ┌─────────────────────────────────┐
  │  N8 Key Code ($00-$FF)          │
  │  + modifier byte (bits 2-5)     │
  └──────────────┬──────────────────┘
                 │
                 v  kbd_key_press()
  ┌─────────────────────────────────┐
  │  Keyboard Registers             │
  │  KBD_DATA   = keycode           │
  │  KBD_STATUS |= DATA_AVAIL      │
  │  (if CTRL.0) IRQ bit 2 asserted │
  └──────────────┬──────────────────┘
                 │
                 v  CPU reads $D860
  ┌─────────────────────────────────┐
  │  6502 Firmware                  │
  │  LDA $D860  → key code         │
  │  STA $D861  → acknowledge      │
  └─────────────────────────────────┘
```

---

## File Inventory

### New Files

| File | Purpose |
|------|---------|
| `src/emu_video.h` | Video control and frame buffer API |
| `src/emu_video.cpp` | Video registers, scroll ops, rasterizer |
| `src/emu_keyboard.h` | Keyboard device API |
| `src/emu_keyboard.cpp` | Keyboard registers, IRQ |
| `src/n8_font.h` | Font data (copy from `docs/charset/n8_font.h`) |
| `test/test_video.cpp` | Video device and rendering tests |
| `test/test_keyboard.cpp` | Keyboard device tests |

### Modified Files

| File | Changes |
|------|---------|
| `src/machine.h` | Replace with full memory map constants |
| `src/emulator.cpp` | Bus decode refactor, frame buffer separation, new device routing |
| `src/emulator.h` | Add frame buffer / video accessors |
| `src/emu_tty.cpp` | No code change (register offsets are relative) |
| `src/main.cpp` | Add display window, keyboard input, font/texture init |
| `src/gdb_stub.cpp` | Update `memory_map_xml` |
| `firmware/devices.inc` | New device addresses |
| `firmware/devices.h` | New device addresses |
| `firmware/n8.cfg` | ROM at `$E000`, RAM at `$0400` |
| `firmware/zp.inc` | Remove `ZP_IRQ` |
| `firmware/interrupt.s` | References update via includes |
| `Makefile` | Add new source files to build |
| `test/test_helpers.h` | Update constants if hardcoded |
| `test/test_bus.cpp` | Update frame buffer tests |
| `test/test_tty.cpp` | Update address references |
| `test/test_integration.cpp` | Update IRQ flag references, add ROM-at-$E000 test |

---

## Performance Budget

| Operation | Per Frame | Target |
|-----------|-----------|--------|
| CPU ticks (~13ms slice @ 1 MHz) | ~13,000 ticks | Existing, unchanged |
| Frame buffer rasterize (when dirty) | 640x400 px = 256K writes | < 0.5 ms |
| Cursor composite | 8x16 px = 128 writes | < 0.01 ms |
| Texture upload (glTexSubImage2D) | 1 MB | < 0.3 ms |
| ImGui::Image draw | 1 quad | < 0.01 ms |
| SDL2 key event processing | ~0 per frame (event-driven) | < 0.01 ms |
| **Total rendering overhead** | | **< 1 ms** |

The rendering overhead is negligible compared to the 13 ms frame budget. The emulator is CPU-bound by the 6502 tick loop, not the display.

---

## Open Questions and Deferred Items

1. **Text Custom mode ($01):** Width/height constraints, maximum framebuffer usage, and stride behavior are "TBD" in the spec. Defer to future work. The register infrastructure is in place.

2. **Phosphor color selection:** The spec mentions amber, green, and white phosphor aesthetics. Implement as a simple UI dropdown in the Emulator Control window. Not part of the hardware spec.

3. **Font ROM bank switching:** The spec says "Swappable font ROM deferred to future work (likely via bank switching into a Dev Bank)." The `$D000-$D7FF` dev bank is reserved for this. The current implementation bakes the font into the emulator binary.

4. **Frame buffer bank switching:** The `$C000-$CFFF` region may be bank-switchable in the future. The separate `frame_buffer[]` backing store already supports this architecture.

5. **TTY interaction with keyboard:** The existing TTY device reads from host stdin via `select()`/`read()`. With the new keyboard device, there are two input paths. Decision: keep both. The keyboard feeds the emulated keyboard registers; the TTY remains a host-side debug channel. Firmware can choose which to use.

6. **SDL2 `SDL_TEXTINPUT` vs `SDL_KEYDOWN`:** `SDL_TEXTINPUT` gives properly shifted/composed characters but does not fire for non-printable keys. `SDL_KEYDOWN` fires for all keys but requires manual shift mapping. The implementation above uses `SDL_KEYDOWN` with manual shift mapping, which matches the "Teensy 4.1 handles USB HID-to-ASCII conversion" hardware model. Consider switching to `SDL_TEXTINPUT` for printable characters plus `SDL_KEYDOWN` for extended keys in a future refinement.

7. **Playground/GDB playground linker configs:** These also use `ROM start=$D000`. Update to `$E000` in Phase 11.

8. **ImGui keyboard focus:** The `!io.WantCaptureKeyboard` check prevents keyboard events from reaching the emulated keyboard when an ImGui text field is focused. This is correct for the debugger UI. For a "full screen" display mode, a toggle to capture all keyboard input (disabling ImGui keyboard nav) may be needed.
