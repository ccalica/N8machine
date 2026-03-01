# N8 Machine — Hardware Registers

Device registers at `$D800–$DFFF` (2 KB), 32-byte spacing per device.

## Device Register Map (`$D800–$DFFF`)

| Base    | Device           | Registers Used | Notes                            |
| -------:| ---------------- | --------------:| -------------------------------- |
| `$D800` | System / IRQ     | 1              | IRQ flags, system status         |
| `$D820` | TTY              | 4              | Serial I/O                       |
| `$D840` | Video Control    | 12             | Mode, dimensions, cursor, scroll, VID_DATA streaming |
| `$D860` | Keyboard         | 3              | ASCII + extended, IRQ, Phase 1   |
| `$D880` | Math Coprocessor | TBD            | RPN stack window                 |
| `$D8A0` | Storage          | TBD            |                                  |
| `$D8C0` | MMU              | TBD            | PID register, bank select        |
| `$D8E0` | TCP UART         | TBD            |                                  |
| `$D900` | Sound            | TBD            | Maybe                            |

## System / IRQ — `$D800`

| Address | R/W | Name      | Description                |
| -------:| --- | --------- | -------------------------- |
| `$D800` | R/W | IRQ_FLAGS | Bit-mapped IRQ status |

**Bits:**

| Bit | Source   | Description                        |
| ---:| -------- | ---------------------------------- |
| 0   | —        | Reserved                           |
| 1   | TTY      | Data available in TTY input buffer |
| 2   | Keyboard | Keypress pending (DATA_AVAIL)      |
| 3–7 | —        | Reserved                           |

Cleared every CPU tick. Devices reassert their bits as needed.
When non-zero, the IRQ line is pulled high.

## TTY — `$D820`

4 registers. Maps host stdin/stdout. Input is buffered (ring buffer)
and asserts IRQ bit 1 when data is available.

| Address | R/W | Name           | Description                                                          |
| -------:| --- | -------------- | -------------------------------------------------------------------- |
| `$D820` | R   | TTY_OUT_STATUS | Always `$00` (ready to transmit)                                     |
| `$D820` | W   | —              | No-op                                                                |
| `$D821` | R   | TTY_OUT_DATA   | Returns `$FF` (invalid; write-only in practice)                      |
| `$D821` | W   | TTY_OUT_DATA   | Write byte to stdout                                                 |
| `$D822` | R   | TTY_IN_STATUS  | `$00` = empty, `$01` = data available                                |
| `$D822` | W   | —              | No-op                                                                |
| `$D823` | R   | TTY_IN_DATA    | Read byte from input buffer (pops; clears IRQ bit 1 when empty)      |
| `$D823` | W   | —              | No-op                                                                |

## Video Control — `$D840`

12 registers (offsets `$00`–`$0B`). Remaining 20 bytes (`$D84C`–`$D85F`)
are phantom registers (read `$00`, write no-op).

| Address | R/W | Name       | Description                               |
| -------:| --- | ---------- | ----------------------------------------- |
| `$D840` | R/W | VID_MODE   | `$00` = Text Default, `$01` = Text Custom |
| `$D841` | R/W | VID_WIDTH  | Display width (columns)                   |
| `$D842` | R/W | VID_HEIGHT | Display height (rows)                     |
| `$D843` | R/W | VID_STRIDE | Row stride                                |
| `$D844` | W   | VID_OPER   | Operation trigger (write-once, does not latch) |
| `$D845` | R/W | VID_CURSOR | Cursor style and flash rate               |
| `$D846` | R/W | VID_CURCOL | Cursor column (0-based)                   |
| `$D847` | R/W | VID_CURROW | Cursor row (0-based)                      |
| `$D848` | R   | VID_VSYNC  | Frame counter (increments each frame)     |
| `$D849` | R/W | VID_CTRL   | VID_DATA behavior control                 |
| `$D84A` | R/W | VID_DATA   | Character read/write at cursor            |
| `$D84B` | R   | VID_STATUS | Status flags                              |

**VID_MODE values:**

| Value | Mode         | Description                             |
| -----:| ------------ | --------------------------------------- |
| `$00` | Text Default | 80x25, 2000-byte framebuffer at `$C000` |
| `$01` | Text Custom  | User-defined width/height (TBD)         |

Writing VID_MODE auto-sets VID_WIDTH, VID_HEIGHT, and VID_STRIDE
to the mode's defaults. VID_STRIDE defaults to same as VID_WIDTH.

**VID_OPER values (write-only):**

Write triggers an immediate one-time operation. Register does not latch.

| Value | Operation    | Description                                          |
| -----:| ------------ | ---------------------------------------------------- |
| `$00` | NOP          | No operation                                         |
| `$01` | SCROLL_UP    | Scroll frame buffer up one row                       |
| `$02` | SCROLL_DOWN  | Scroll frame buffer down one row                     |
| `$03` | SCROLL_LEFT  | Scroll frame buffer left one column                  |
| `$04` | SCROLL_RIGHT | Scroll frame buffer right one column                 |
| `$05` | CLEAR        | Fill FB with `$20` (stride×height), cursor to (0,0), clear OVERFLOW |
| `$06` | CURSOR_UP    | row = max(0, row − 1), clear OVERFLOW                |
| `$07` | CURSOR_DOWN  | row = min(height − 1, row + 1), clear OVERFLOW       |
| `$08` | CURSOR_LEFT  | col = max(0, col − 1), clear OVERFLOW                |
| `$09` | CURSOR_RIGHT | col = min(width − 1, col + 1), clear OVERFLOW        |
| `$0A` | CURSOR_HOME  | col = 0, row = 0, clear OVERFLOW                     |

**VID_CURSOR bits:**

| Bits | Name  | Description                                                    |
| ----:| ----- | -------------------------------------------------------------- |
| 0–1  | MODE  | `00` = off, `01` = on (steady), `10` = flash, `11` = reserved |
| 2–3  | SHAPE | `00` = underline, `01` = block                                |
| 4–7  | RATE  | Frames per toggle (`0` = cursor not displayed)                 |

### VID_CTRL — `$D849`

Persistent bitmask controlling VID_DATA read/write behavior.

| Bit | Name    | Description                                         |
| ---:| ------- | --------------------------------------------------- |
| 0   | ADVANCE | Auto-increment column after VID_DATA access         |
| 1   | WRAP    | Auto-wrap to next row when column reaches width     |
| 2   | SCROLL  | Auto-scroll up when row reaches height (writes only)|
| 3–7 | —       | Reserved (read 0, writes ignored)                   |

Default on reset: `$07` (ADVANCE + WRAP + SCROLL). Writing VID_CTRL
clears OVERFLOW in VID_STATUS. Only bits 0–2 are stored; bits 3–7 are
masked off.

### VID_DATA — `$D84A`

Character at cursor position. Hardware-accelerated put_char / get_char.

**Write:** Stores byte at `frame_buffer[row × stride + col]`, sets
fb_dirty. Then applies cursor advance per VID_CTRL:

1. If ADVANCE: col++
2. If WRAP and col ≥ width: col = 0, row++
3. If !WRAP and col ≥ width: col = width − 1, set OVERFLOW
4. If SCROLL and row ≥ height: scroll_up(), row = height − 1
5. If !SCROLL and row ≥ height: row = height − 1, set OVERFLOW
6. Update VID_CURCOL and VID_CURROW

Bounds guard: if `row × stride + col ≥ 4096` (FB_SIZE), write is a
no-op and OVERFLOW is set.

**Read:** Returns `frame_buffer[row × stride + col]`. Then applies
the same advance logic **except SCROLL is always ignored** (reads never
scroll). Out-of-bounds reads return `$00` and set OVERFLOW.

### VID_STATUS — `$D84B`

Read-only status flags. Writes are ignored.

| Bit | Name     | Description                                    |
| ---:| -------- | ---------------------------------------------- |
| 0   | OVERFLOW | Cursor advance was clamped (boundary reached)  |
| 1–7 | —        | Reserved (read 0)                              |

OVERFLOW is set when VID_DATA advance hits a boundary (no-wrap column
clamp, no-scroll row clamp, or out-of-bounds access).

Cleared by: writing VID_CTRL, writing VID_CURCOL, writing VID_CURROW,
or any CURSOR_* / CLEAR VID_OPER code.

### Firmware Streaming Patterns

```asm
; Stream-write: fill row with 'A' (stop on OVERFLOW)
        LDA #$0A                ; CURSOR_HOME
        STA VID_OPER
        LDA #$01                ; ADVANCE only (no wrap/scroll)
        STA VID_CTRL
        LDA #'A'
@loop:  STA VID_DATA
        LDA VID_STATUS
        AND #$01
        BNE @done               ; OVERFLOW = hit col boundary
        LDA #'A'
        JMP @loop
@done:

; Stream-read: dump screen to buffer
        LDA #$0A                ; CURSOR_HOME
        STA VID_OPER
        LDA #$03                ; ADVANCE + WRAP (no scroll)
        STA VID_CTRL
        LDY #$00
@loop:  LDA VID_DATA            ; read + auto-advance
        STA (ptr),Y
        INY
        LDA VID_STATUS
        AND #$01
        BNE @done               ; hit bottom-right corner
        JMP @loop
@done:
```

**Reset state:** VID_MODE = `$00`, VID_WIDTH = 80, VID_HEIGHT = 25,
VID_STRIDE = 80, VID_CTRL = `$07`, VID_CURSOR = `$00` (cursor off),
VID_CURCOL = 0, VID_CURROW = 0, VID_STATUS = `$00`.

**Font:** Baked into the emulator from `docs/charset/`. Swappable font
ROM deferred to future work (likely via bank switching into a Dev Bank).

## Keyboard — `$D860` (Phase 1)

8-byte block. Teensy 4.1 handles USB HID-to-ASCII conversion.
Apple II-style: poll or IRQ, read data, acknowledge.

| Address         | R/W | Name       | Description                                                                             |
| ---------------:| --- | ---------- | --------------------------------------------------------------------------------------- |
| `$D860`         | R   | KBD_DATA   | Key code. `$00–$7F` = ASCII, `$80–$FF` = extended (see [keycodes.md](keycodes.md)) |
| `$D861`         | R   | KBD_STATUS | Flags + live modifier state (see bits below)                                            |
| `$D861`         | W   | KBD_ACK    | Write any value: clears DATA_AVAIL, OVERFLOW, deasserts IRQ                             |
| `$D862`         | R/W | KBD_CTRL   | Bit 0 = IRQ enable (default 0 = polling only)                                           |
| `$D863`–`$D867` | —   | —          | Reserved (Phase 2)                                                                      |

**KBD_STATUS bits (read-only):**

| Bit | Name       | Description                                         |
| ---:| ---------- | --------------------------------------------------- |
| 0   | DATA_AVAIL | 1 = key code waiting in KBD_DATA                    |
| 1   | OVERFLOW   | 1 = key arrived before previous was read (key lost) |
| 2   | SHIFT      | 1 = Shift held                                      |
| 3   | CTRL       | 1 = Ctrl held                                       |
| 4   | ALT        | 1 = Alt held                                        |
| 5   | CAPS_LOCK  | 1 = Caps Lock on                                    |
| 6–7 | —          | Reserved (reads 0)                                  |

Modifier bits (2–5) reflect live state, updated on every HID report
regardless of DATA_AVAIL. On overflow, new key replaces previous
(most-recent-key-wins).

## Frame Buffer — `$C000–$CFFF`

4 KB buffer. CPU reads/writes are intercepted by bus decode and routed
to a separate backing store (not backed by main RAM).

Each byte is a character index into the baked-in font. Monochrome.
In Text Default mode (80x25), the first 2000 bytes (`$C000–$C7CF`)
are active; remaining bytes are unused.

| Address         | R/W | Name    | Description              |
| ---------------:| --- | ------- | ------------------------ |
| `$C000`–`$CFFF` | R/W | FB_DATA | 4 KB display buffer |

## Open Issues

- **Text Custom mode:** Width/height constraints, stride behavior,
  and max framebuffer usage not yet defined.

