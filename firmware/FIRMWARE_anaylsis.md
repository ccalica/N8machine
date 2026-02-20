# N8 Firmware Analysis

Reverse-engineered description of the N8machine firmware — a 6502 assembly program built with cc65, targeting a custom memory-mapped hardware platform emulated by the N8machine emulator.

## Build System

- **Toolchain**: cc65 (cl65 driver), version 2.19
- **Target**: `none` (bare-metal, no OS platform)
- **CPU**: 6502
- **Linker config**: `n8.cfg`
- **Runtime library**: `n8.lib` (custom cc65 library, see below)
- **Output**: `N8firmware` raw binary, plus `.map` and `.sym` files
- **Source files**: `main.s init.s tty.s interrupt.s vectors.s`

## Memory Map (from n8.cfg)

```
$0000-$00FF  ZP       Zero Page (rw) — cc65 runtime + firmware pointers
$0100-$01FF  (stack)  6502 hardware stack (implicit, not in config)
$0200-$BEFF  RAM      General purpose RAM (48,384 bytes)
                       DATA segment runs here (loaded from ROM, copied at init)
                       BSS segment here
                       HEAP segment here (optional)
$C000-$C0FF  I/O      Text display device (memory-mapped)
$C100-$C10F  I/O      TTY device (memory-mapped)
$D000-$FCFF  ROM      Firmware code + read-only data (12,288 bytes)
                       STARTUP, ONCE, CODE, RODATA segments
$FFFA-$FFFF  ROM      6502 interrupt vectors (NMI, RESET, IRQ/BRK)
```

### I/O Device Registers

| Address | Name | Direction | Description |
|---------|------|-----------|-------------|
| `$C000` | `TXT_CTRL` | R/W | Text display control register |
| `$C001`-`$C0FF` | `TXT_BUFF` | R/W | Text display buffer (255 bytes) |
| `$C100` | `TTY_OUT_CTRL` | R | Bit 0: 1=busy, 0=ready to accept char |
| `$C101` | `TTY_OUT_DATA` | W | Write a character to TTY output |
| `$C102` | `TTY_IN_CTRL` | R | Bit 0: 1=char available, 0=empty |
| `$C103` | `TTY_IN_DATA` | R | Read next character from TTY input buffer |

### Zero Page Allocations (firmware-defined, zp.inc)

| Address | Name | Size | Purpose |
|---------|------|------|---------|
| `$E0-$E1` | `ZP_A_PTR` | 2 | General pointer A (used by tty_puts) |
| `$E2-$E3` | `ZP_B_PTR` | 2 | General pointer B |
| `$E4-$E5` | `ZP_C_PTR` | 2 | General pointer C |
| `$E6-$E7` | `ZP_D_PTR` | 2 | General pointer D |
| `$F0-$F3` | `BYTE_0..3` | 4 | Scratch bytes |
| `$FF` | `ZP_IRQ` | 1 | IRQ-related flag |

cc65 runtime owns the low zero page (`sp`, `sreg`, `ptr1`-`ptr4`, `tmp1`-`tmp4`, `regsave`, `regbank`).

## Boot Sequence (init.s)

```
RESET vector → _init
  1. LDX #$FF; TXS     — set hardware stack to $01FF
  2. CLD                — clear decimal mode
  3. Set cc65 sp        — software stack at top of RAM
  4. JSR zerobss        — zero BSS segment
  5. JSR copydata       — copy DATA segment from ROM to RAM
  6. JSR initlib        — run cc65 constructor table
  7. CLI                — enable IRQs
  8. JSR _main          — enter main program
  9. JSR donelib        — run cc65 destructor table
 10. spin: JMP spin     — halt (infinite loop)
```

## Main Program (main.s)

1. Writes three bytes (`$02, $05, $08`) to `TXT_BUFF[0..2]` — likely a test/init of the text display device.
2. Prints "Welcome banner.  Enjoy this world.\r\n" via `tty_puts`.
3. Prints "PLAY?\r\n" via `tty_puts`.
4. Enters an echo loop (`rb_test`):
   - `tty_getc` — blocks until ring buffer has a char
   - `tty_putc` — echoes it back
   - On `\r` (0x0D), also sends `\n` (0x0A)
   - Loops forever

## TTY Driver (tty.s)

### Output

- **`_tty_putc`** — Busy-waits on `TTY_OUT_CTRL` bit 0 (busy flag), then writes A to `TTY_OUT_DATA`.
- **`_tty_puts`** — Takes string pointer in A (low) / X (high). Walks null-terminated string, calling `_tty_putc` per char.

### Input (Ring Buffer)

A 32-byte circular buffer in RAM:

| Symbol | Description |
|--------|-------------|
| `rb_base` | 32-byte buffer storage |
| `rb_len` | Buffer capacity (32) |
| `rb_start` | Read index (consumer) |
| `rb_end` | Write index (producer) |

- **`_tty_getc`** — Returns next char from ring buffer in A (0 if empty). Advances `rb_start`, wraps at `rb_len`.
- **`_tty_peekc`** — Returns count of available chars in A (`rb_end - rb_start`, wrapping handled).
- **`tty_recv`** — Called from IRQ handler. Pushes char (in A) into ring buffer at `rb_end`. Discards if buffer full (checks for `rb_end` about to collide with `rb_start`). Wraps index at `rb_len`.

### Ring Buffer Full Detection (tty_recv)

The overflow guard compares `rb_end - rb_start`. If the difference is 0 (common empty case) it writes normally. If -1 (`$FF`) or -31 (`$E1`, i.e., buffer full with wrap), it discards the incoming char. This is a somewhat fragile check — it handles the two boundary cases but doesn't generalize for arbitrary buffer sizes.

## Interrupt Handling (interrupt.s)

### IRQ Handler (`_irq_int`)

1. Saves A, X, Y to stack.
2. Checks the B flag in the stacked status register (`$0104,X`) to distinguish hardware IRQ from BRK instruction.
3. If BRK: infinite loop at `brken` (unrecoverable).
4. If hardware IRQ:
   - Reads `TTY_IN_CTRL` — loops while bit 0 is set (char available).
   - Reads `TTY_IN_DATA` into A, calls `tty_recv` to buffer it.
   - Stores debug info to `TXT_BUFF[0..1]`.
   - Loops back to check for more chars (drains the input queue).
5. Restores Y, X, A and RTI.

### NMI Handler (`_nmi_int`)

Immediate `RTI` — NMI is unused.

### Vector Table (vectors.s)

| Vector | Address | Handler |
|--------|---------|---------|
| NMI | `$FFFA` | `_nmi_int` |
| RESET | `$FFFC` | `_init` |
| IRQ/BRK | `$FFFE` | `_irq_int` |

## Emulator-Side Device Implementation

### Text Display (`emulator.cpp:127-137`)

- Decoded at `$C000-$C0FF` (mask `$FF00`).
- Backed by `frame_buffer[]` array.
- Read: returns `frame_buffer[addr - $C000]`.
- Write: stores to `frame_buffer[addr - $C000]`.
- The firmware writes test values here but doesn't use the display further (the echo loop uses TTY).

### TTY Device (`emulator.cpp:142-145`, `emu_tty.cpp`)

- Decoded at `$C100-$C10F` (mask `$FFF0`).
- Delegates to `tty_decode(pins, dev_reg)`.

**Register behavior (emu_tty.cpp:66-117):**

| Reg | Read | Write |
|-----|------|-------|
| `$00` (OUT_CTRL) | Always `0x00` (never busy) | No-op |
| `$01` (OUT_DATA) | `0xFF` (shouldn't happen) | `putchar(c)` to host terminal |
| `$02` (IN_CTRL) | `0x01` if input queued, else `0x00` | No-op |
| `$03` (IN_DATA) | Dequeues from `tty_buff` | No-op |

**Input path (`emu_tty.cpp:51-64`):**

`tty_tick()` is called each emulator cycle:
1. If `tty_buff` is non-empty, asserts IRQ line 1.
2. Polls host stdin (`select()` + `read()`).
3. New keystrokes pushed to `tty_buff` queue, IRQ asserted.
4. IRQ cleared when `tty_buff` drained via IN_DATA reads.

Host terminal is set to raw mode (`cfmakeraw`) at init, restored on exit via `atexit()`.

**Additional functions:**
- `tty_inject_char(c)` — programmatic char injection (used by test harness / debugger).
- `tty_buff_count()` — returns queue depth.
- `tty_reset()` — flushes queue, clears IRQ.

## n8.lib Contents

A cc65 `none`-target runtime library (v2.19). Contains ~400 object modules. Key modules used by this firmware:

**Runtime bootstrap (linked by init.s):**
- `zerobss.o` — zeros the BSS segment
- `copydata.o` — copies initialized DATA from ROM to RAM
- `condes.o` — constructor/destructor table support (`initlib`/`donelib`)
- `callirq.o` — IRQ dispatch infrastructure
- `callmain.o` — main() calling convention support
- `crt0.o` — default C runtime startup (overridden by init.s)
- `zeropage.o` — defines cc65 zero page variables (`sp`, `sreg`, `ptr1`-`ptr4`, `tmp1`-`tmp4`, `regsave`, `regbank`)

**Also available but likely not linked** (no C code in this firmware):
- Full C standard library: `printf`, `malloc`/`free`, `string.h`, `stdio.h` family
- Math helpers: `mul`, `div`, `imul*`, `umul*`, shift helpers
- Extended modules: TGI (graphics), joystick, mouse, serial, extended memory kernels
- Debug support: `dbg*.o`

Since the firmware is pure assembly and only imports `zerobss`, `copydata`, `initlib`, `donelib`, and the `sp` zero page symbol, only a handful of modules are actually linked into the final binary.

## Data Flow Summary

```
Host Keyboard → select()/read() → tty_buff queue → IRQ asserted
    → 6502 IRQ handler → reads TTY_IN_DATA → tty_recv → ring buffer
    → main loop: tty_getc → tty_putc → TTY_OUT_DATA
    → emulator putchar() → Host Terminal
```

## Notes

- The firmware is essentially a minimal TTY echo program — a "hello world" for validating the N8 hardware/emulator.
- The text display device (`$C000`) is written to during init but otherwise unused — suggests it's a framebuffer for a future display peripheral.
- The ring buffer overflow detection in `tty_recv` is hardcoded to buffer size 32 (checks for `-1` and `-31`). Would break if `rb_len` changes.
- `_tty_peekc` doesn't set carry before `SBC`, so the subtraction result can be off by one depending on carry state from the caller.
- The `RTS` after the `JMP rb_test` loop in main.s is dead code (unreachable).
