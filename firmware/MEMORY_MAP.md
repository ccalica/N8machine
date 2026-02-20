# N8 Memory Map

## Overview

64KB address space. RAM from $0200-$BEFF, memory-mapped I/O at $C000-$C1FF, ROM from $D000-$FFFF.

```
        +------------------+ $FFFF
        | Vectors (6 bytes)|
        +------------------+ $FFFA
        |                  |
        |   ROM (12KB)     |
        |   Code + Data    |
        |                  |
        +------------------+ $D000
        |                  |
        |   Unmapped       |
        |                  |
        +------------------+ $C200
        | TTY Device       |
        +------------------+ $C100
        | Text Display     |
        +------------------+ $C000
        |                  |
        |   Unmapped       |
        |                  |
        +------------------+ $BF00
        |                  |
        |                  |
        |   RAM (48KB)     |
        |   BSS / DATA     |
        |   HEAP           |
        |                  |
        |                  |
        +------------------+ $0200
        | Hardware Stack   |
        +------------------+ $0100
        | Zero Page        |
        +------------------+ $0000
```

## Regions

### $0000-$00FF — Zero Page

| Range       | Owner    | Symbol(s)               | Notes                          |
|-------------|----------|-------------------------|--------------------------------|
| `$00-$01`   | cc65     | `sp`                    | Software stack pointer         |
| `$02-$03`   | cc65     | `sreg`                  | Secondary register (32-bit)    |
| `$04-$07`   | cc65     | `regsave`               | Register save area             |
| `$08-$0F`   | cc65     | `ptr1`-`ptr4`           | Scratch pointers (2 bytes ea.) |
| `$10-$17`   | cc65     | `tmp1`-`tmp4`           | Scratch temporaries            |
| `$18-$1F`   | cc65     | `regbank`               | Register bank                  |
| `$20-$DF`   | cc65     | (available)             | Unused by runtime              |
| `$E0-$E1`   | firmware | `ZP_A_PTR`              | General pointer A              |
| `$E2-$E3`   | firmware | `ZP_B_PTR`              | General pointer B              |
| `$E4-$E5`   | firmware | `ZP_C_PTR`              | General pointer C              |
| `$E6-$E7`   | firmware | `ZP_D_PTR`              | General pointer D              |
| `$E8-$EF`   | —        | (free)                  |                                |
| `$F0`       | firmware | `BYTE_0`                | Scratch byte                   |
| `$F1`       | firmware | `BYTE_1`                | Scratch byte                   |
| `$F2`       | firmware | `BYTE_2`                | Scratch byte                   |
| `$F3`       | firmware | `BYTE_3`                | Scratch byte                   |
| `$F4-$FE`   | —        | (free)                  |                                |
| `$FF`       | firmware | `ZP_IRQ`                | IRQ flag                       |

### $0100-$01FF — Hardware Stack

6502 stack page. Initialized to `$01FF` at boot. Grows downward. Used by subroutine calls, interrupt context saves, and `tty_putc` (pushes/pops A).

### $0200-$BEFF — RAM (48,384 bytes)

```
        +------------------+ $BEFF
        |  cc65 software   |
        |  stack (grows ↓) |    __STACKSIZE__ = $0200 (512 bytes)
        +------------------+ $BD00 (approx)
        |                  |
        |  HEAP (optional) |    grows upward if malloc used
        |                  |
        +------------------+
        |  BSS             |    zero-initialized at boot (zerobss)
        +------------------+
        |  DATA            |    copied from ROM at boot (copydata)
        +------------------+ $0200
```

Segments placed by linker. DATA and BSS sizes depend on firmware content. Currently DATA holds:
- Welcome strings ("Welcome banner..." and "PLAY?")
- TTY ring buffer (`rb_base`, 32 bytes) + control bytes (`rb_len`, `rb_start`, `rb_end`)

### $C000-$C0FF — Text Display Device

Bus decode: address AND `$FF00` == `$C000`

| Address       | Register   | R/W | Description                          |
|---------------|------------|-----|--------------------------------------|
| `$C000`       | `TXT_CTRL` | R/W | Control register (function TBD)      |
| `$C001-$C0FF` | `TXT_BUFF` | R/W | 255-byte framebuffer window          |

**Emulator implementation** (`emulator.cpp:127-137`):
Backed by `frame_buffer[]` array. Direct read/write. Currently used only for debug — firmware writes `$02, $05, $08` at init and the IRQ handler stores TTY status bytes for inspection.

### $C100-$C10F — TTY Device

Bus decode: address AND `$FFF0` == `$C100`

| Address | Register       | Read                              | Write                    |
|---------|----------------|-----------------------------------|--------------------------|
| `$C100` | `TTY_OUT_CTRL` | Bit 0: 1=busy, 0=ready           | No-op                    |
| `$C101` | `TTY_OUT_DATA` | `$FF` (invalid)                   | Send char to terminal    |
| `$C102` | `TTY_IN_CTRL`  | Bit 0: 1=char available, 0=empty | No-op                    |
| `$C103` | `TTY_IN_DATA`  | Dequeue next input char           | No-op                    |

**Emulator implementation** (`emu_tty.cpp:66-117`):
- Output: `putchar()` to host stdout. OUT_CTRL always returns 0 (never busy).
- Input: Host stdin polled via `select()`/`read()` in raw mode. Chars queued in `tty_buff` (C++ `std::queue`). IRQ line 1 asserted when queue non-empty, cleared when drained.

**Data flow:**
```
Keyboard → host stdin → tty_buff → IRQ → TTY_IN_DATA read → firmware ring buffer
firmware tty_putc → TTY_OUT_DATA write → putchar() → host stdout → Terminal
```

### $C110-$CFFF — Unmapped I/O Space

3,824 bytes available for future peripherals. Not decoded by emulator — reads return open bus, writes are ignored.

### $D000-$FFF9 — ROM (12,282 bytes)

| Segment   | Contents                                               |
|-----------|--------------------------------------------------------|
| `STARTUP` | Boot code (`_init`), cc65 constructor/destructor tables |
| `ONCE`    | One-time initialization (optional, currently unused)    |
| `CODE`    | Main program, TTY driver, interrupt handlers            |
| `RODATA`  | Read-only data (string literals stored here in ROM, copied to DATA at boot) |

Note: DATA segment is *loaded* from ROM but *runs* in RAM — the linker places the initial values in ROM and `copydata` copies them to RAM at `$0200+` during boot.

### $FFFA-$FFFF — Interrupt Vectors

| Address | Vector | Handler      | Function                          |
|---------|--------|--------------|-----------------------------------|
| `$FFFA` | NMI    | `_nmi_int`   | Immediate RTI (unused)            |
| `$FFFC` | RESET  | `_init`      | Boot entry point                  |
| `$FFFE` | IRQ    | `_irq_int`   | TTY input + BRK detection         |

## Address Decode Summary

Emulator bus decode priority (checked per cycle in `emulator.cpp`):

1. `addr & $FF00 == $0000` → Zero page monitor (logging only)
2. `addr & $FF00 == $C000` → Text display (`frame_buffer[]`)
3. `addr & $FFF0 == $FFF0` → Vector access monitor (logging only)
4. `addr & $FFF0 == $C100` → TTY device (`tty_decode()`)
5. Everything else → main memory array (`mem[]`)
