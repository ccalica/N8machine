# N8 Machine — Hardware Registers

Device registers at `$D800–$DFFF` (2 KB), 32-byte spacing per device.
Footnotes show current (pre-move) locations in the emulator.

## Device Register Map (`$D800–$DFFF`)

| Base    | Device           | Registers Used | Notes                              |
| -------:| ---------------- | --------------:| ---------------------------------- |
| `$D800` | System / IRQ     | 1              | IRQ flags, system status           |
| `$D820` | TTY              | 4              | Serial I/O                         |
| `$D840` | Video Control    | 5              | Mode, Width, Height, Stride, Banks |
| `$D860` | Keyboard         | 3              | ASCII + extended, IRQ, Phase 1     |
| `$D880` | Math Coprocessor | TBD            | RPN stack window                   |
| `$D8A0` | Storage          | TBD            |                                    |
| `$D8C0` | MMU              | TBD            | PID register, bank select          |
| `$D8E0` | TCP UART         | TBD            |                                    |
| `$D900` | Sound            | TBD            | Maybe                              |

## System / IRQ — `$D800`

| Address | R/W | Name      | Description                |
| -------:| --- | --------- | -------------------------- |
| `$D800` | R/W | IRQ_FLAGS | Bit-mapped IRQ status [^1] |

**Bits:**

| Bit | Source | Description                        |
| ---:| ------ | ---------------------------------- |
| 0   | —      | Reserved                           |
| 1   | TTY    | Data available in TTY input buffer |
| 2–7 | —      | Reserved                           |

Cleared every CPU tick. Devices reassert their bits as needed.
When non-zero, the IRQ line is pulled high.

## TTY — `$D820`

4 registers. Maps host stdin/stdout. Input is buffered (ring buffer)
and asserts IRQ bit 1 when data is available.

| Address | R/W | Name           | Description                                                          |
| -------:| --- | -------------- | -------------------------------------------------------------------- |
| `$D820` | R   | TTY_OUT_STATUS | Always `$00` (ready to transmit) [^2]                                |
| `$D820` | W   | —              | No-op                                                                |
| `$D821` | R   | TTY_OUT_DATA   | Returns `$FF` (invalid; write-only in practice) [^3]                 |
| `$D821` | W   | TTY_OUT_DATA   | Write byte to stdout                                                 |
| `$D822` | R   | TTY_IN_STATUS  | `$00` = empty, `$01` = data available [^4]                           |
| `$D822` | W   | —              | No-op                                                                |
| `$D823` | R   | TTY_IN_DATA    | Read byte from input buffer (pops; clears IRQ bit 1 when empty) [^5] |
| `$D823` | W   | —              | No-op                                                                |

## Video Control — `$D840`

| Address | R/W | Name       | Description                               |
| -------:| --- | ---------- | ----------------------------------------- |
| `$D840` | R/W | VID_MODE   | `$00` = Text Default, `$01` = Text Custom |
| `$D841` | R/W | VID_WIDTH  | Display width (columns)                   |
| `$D842` | R/W | VID_HEIGHT | Display height (rows)                     |
| `$D843` | R/W | VID_STRIDE | Row stride                                |
| `$D844` | R/W | VID_BANKS  | Video bank select                         |

**VID_MODE values:**

| Value | Mode | Description |
|------:|------|-------------|
| `$00` | Text Default | 80x25, 2000-byte framebuffer at `$C000` |
| `$01` | Text Custom | User-defined width/height |

Writing VID_MODE auto-sets VID_WIDTH, VID_HEIGHT, and VID_STRIDE
to the mode's defaults. VID_STRIDE defaults to same as VID_WIDTH.

## Keyboard — `$D860` (Phase 1)

8-byte block. Teensy 4.1 handles USB HID-to-ASCII conversion.
Apple II-style: poll or IRQ, read data, acknowledge.

| Address         | R/W | Name       | Description                                                 |
| ---------------:| --- | ---------- | ----------------------------------------------------------- |
| `$D860`         | R   | KBD_DATA   | Key code. `$00–$7F` = ASCII, `$80–$FF` = extended [^7]      |
| `$D861`         | R   | KBD_STATUS | Flags + live modifier state (see bits below)                |
| `$D861`         | W   | KBD_ACK    | Write any value: clears DATA_AVAIL, OVERFLOW, deasserts IRQ |
| `$D862`         | R/W | KBD_CTRL   | Bit 0 = IRQ enable (default 0 = polling only)               |
| `$D863`–`$D867` | —   | —          | Reserved (Phase 2)                                          |

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

4 KB buffer for text/graphics display data. CPU reads/writes are
intercepted by bus decode and routed to a separate backing store
(not backed by main RAM).

| Address         | R/W | Name    | Description              |
| ---------------:| --- | ------- | ------------------------ |
| `$C000`–`$CFFF` | R/W | FB_DATA | 4 KB display buffer [^6] |

## Footnotes — Current Locations

[^1]: IRQ_FLAGS is currently at **`$00FF`** (zero page).
[^2]: TTY_OUT_STATUS is currently at **`$C100`**.
[^3]: TTY_OUT_DATA is currently at **`$C101`**.
[^4]: TTY_IN_STATUS is currently at **`$C102`**.
[^5]: TTY_IN_DATA is currently at **`$C103`**.
[^6]: Frame buffer is currently **256 bytes** at **`$C000–$C0FF`**.
[^7]: Notion spec used placeholder **`$FF00`**. See: *N8Machine Keyboard Register Interface Spec — Phase 1*.
