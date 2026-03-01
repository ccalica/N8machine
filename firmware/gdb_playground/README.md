# gdb_playground

Test programs used to validate the GDB RSP stub and `n8gdb` client during development. For new firmware experiments, use [`../playground/`](../playground/) instead.

## Build

```
make          # build all test programs
make clean    # remove build artifacts
```

Requires `cc65` (cl65/ca65/ld65). No cc65 runtime dependency — programs use a minimal startup (`common_init.s`).

Each build produces:
- `test_<name>` — 8KB ROM binary (loads at $E000)
- `test_<name>.sym` — cc65 symbol file (for n8gdb `sym` command)
- `test_<name>.dbg` — debug info

## Usage with n8gdb

Start the N8machine emulator, then use the REPL to load a binary and interact:

```
n8gdb repl
n8> load test_regs 0xE000
n8> sym test_regs.sym
n8> reset
```

Or with `--sym` to pre-load symbols:

```
n8gdb repl --sym test_regs.sym
n8> load test_regs 0xE000
n8> reset
```

Breakpoints and watchpoints persist across connections, so individual CLI
commands can be chained (`n8gdb bp ...`, `n8gdb run`, `n8gdb read ...`).
REPL mode is available for interactive exploration.

## Test Programs

### test_regs — Register read/write

Loads known values into A, X, Y and manipulates flags. NOP padding between operations makes single-stepping easy to follow.

```
n8> load test_regs 0xE000
n8> sym test_regs.sym
n8> reset
n8> bp final_state
n8> run                   # stops at final_state
n8> step 4                # execute LDA/LDX/LDY + NOP
n8> regs                  # A=$DE X=$AD Y=$42
```

**Tests:** `regs`, `wreg`, `step`, `pc`

### test_breakpoints — Breakpoint set/clear

Four subroutines (`func_a`–`func_d`) called in a loop. Set and clear breakpoints to verify the stub handles multiple breakpoints and clearing correctly.

```
n8> load test_breakpoints 0xE000
n8> sym test_breakpoints.sym
n8> reset
n8> bp func_a
n8> bp func_c
n8> run                   # stops at func_a
n8> run                   # stops at func_c (skips func_b)
n8> bpc func_a            # clear func_a breakpoint
n8> run                   # stops at func_c again
```

**Tests:** `bp`, `bpc`, `run`, `goto`, `read` (BSS variable)

### test_memory — Memory read/write

Fills RAM regions with known patterns, then spins. Inspect and modify memory after the fill completes.

```
n8> load test_memory 0xE000
n8> sym test_memory.sym
n8> reset
n8> bp fill_done
n8> run                   # wait for all fills to complete
n8> read 0300 40          # ascending: 00 01 02 ... 3F
n8> read 0400 10          # all $AA
n8> read 0500 20          # checkerboard: 55 AA 55 AA ...
n8> read 0600 4           # markers: DE AD BE EF
n8> write 0300 DEADBEEF   # overwrite 4 bytes
n8> read 0300 10          # verify the write took effect
```

**Tests:** `read`, `write`, `load`, `bp`

### test_stack — Stack operations

Push/pop sequences and 3-deep nested JSR/RTS. Watch the stack pointer change and inspect the hardware stack page.

```
n8> load test_stack 0xE000
n8> sym test_stack.sym
n8> reset
n8> bp push_start
n8> run
n8> regs                  # SP=$FF
n8> step 6                # 3x LDA+PHA
n8> regs                  # SP=$FC
n8> read 01FD 3           # stack contents: 33 22 11 (LIFO)
n8> bp depth_3
n8> run                   # 3 JSR frames deep
n8> regs                  # SP=$F9 (6 bytes of return addresses)
```

**Tests:** `regs` (SP tracking), `read` on stack page ($01xx), `step` through JSR/RTS

### test_counter — Running loop / halt

A tight 16-bit counting loop. Let it run freely, interrupt with `halt`, inspect the counter in RAM.

```
n8> load test_counter 0xE000
n8> sym test_counter.sym
n8> reset
n8> run                   # let it run
n8> halt                  # interrupt after a moment
n8> regs                  # PC somewhere in count_loop
n8> read 0200 3           # counter: lo, hi, overflow
n8> step 5                # step a few iterations
n8> read 0200 3           # counter advanced
n8> reset
n8> bp overflow_hit
n8> run                   # run until 16-bit counter wraps (64K iterations)
n8> read 0200 3           # lo=00 hi=00 overflow about to increment
```

**Tests:** `run`, `halt`, `reset`, `read` (BSS variables)

### test_zeropage — Indirect addressing

Uses zero page pointers ($E0–$E3) with `(zp),Y` indirect indexed addressing to copy a data table. Modify ZP pointers via `write` to redirect where data goes.

```
n8> load test_zeropage 0xE000
n8> sym test_zeropage.sym
n8> reset
n8> bp zp_sum
n8> run                   # copy completes, stops before sum loop
n8> read 00E0 4           # ZP pointers: E0/E1=src ($D03B), E2/E3=dst ($0400)
n8> read 0400 8           # copied data: 10 20 30 40 50 60 70 80
n8> bpc zp_sum
n8> bp restart
n8> run                   # sum loop completes
n8> read 00F0 1           # zp_tmp = $10+$20+$30+$40 = $A0
```

**Tests:** `read`/`write` on zero page, `step` through indirect addressing, pointer manipulation

### test_tty — TTY device output

Sends three strings to the TTY device via memory-mapped I/O at $C100. Includes its own `tty_putc`/`tty_puts` routines (no firmware link needed).

```
n8> load test_tty 0xE000
n8> sym test_tty.sym
n8> reset
n8> bp send_done
n8> run                   # all 3 strings sent
n8> read C100 4           # TTY registers (last char in data reg)
```

Strings sent: `"Hello, world!"`, `"0123456789"`, `"=== N8machine GDB test ===\nTTY output working."`

**Tests:** `step` through I/O, `bp` on output routines, `read` on I/O region ($C100), `run`

### test_monitor — Keyboard + video terminal

Interactive terminal: polls keyboard, echoes to frame buffer. Tests keyboard polling, frame buffer writes, video cursor, scroll, printable ASCII, backspace, enter.

```
n8gdb load test_monitor 0xE000
n8gdb reset
n8gdb run
(type in emulator window — text appears on Screen)
```

**Tests:** `kbd_inject`, `console_text`, keyboard IRQ, video scroll

### test_benchmark — 6502 cycle benchmark

Measures CPU cycles/frame using two methods across 10 instruction groups. Results displayed on the frame buffer.

```
n8gdb load test_benchmark 0xE000
n8gdb reset
n8gdb run
n8gdb console_text            # results table on screen
```

**Tests:** `VID_VSYNC` timing, frame buffer direct writes, `console_text`

### test_viddata — VID_DATA / VID_CTRL / VID_STATUS validation

10-phase test exercising all new video registers and VID_OPER codes. Each phase stores results in zero page (`$00`–`$0F`) for GDB inspection at labeled breakpoints.

| Phase | Test                                 | Key Registers         |
|------:|--------------------------------------|-----------------------|
| 1     | VID_OPER CLEAR                       | VID_OPER, VID_STATUS  |
| 2     | VID_DATA write + ADVANCE             | VID_DATA, VID_CTRL    |
| 3     | ADVANCE + WRAP across row boundary   | VID_CTRL              |
| 4     | ADVANCE + WRAP + SCROLL at corner    | VID_CTRL, VID_DATA    |
| 5     | No-WRAP overflow + CTRL clear        | VID_STATUS            |
| 6     | VID_DATA read + ADVANCE              | VID_DATA (read path)  |
| 7     | Read never scrolls                   | VID_DATA, VID_STATUS  |
| 8     | Cursor movement ops + clamping       | VID_OPER (new codes)  |
| 9     | Stream write (fill row, OVERFLOW)    | VID_DATA, VID_STATUS  |
| 10    | Stream read (read row, OVERFLOW)     | VID_DATA, VID_STATUS  |

```
n8gdb --sym test_viddata.sym load test_viddata 0xE000
n8gdb reset
n8gdb --sym test_viddata.sym bp phase1_done
n8gdb --sym test_viddata.sym bp all_done
n8gdb run                     # stops at phase1_done
n8gdb read $00 6              # verify phase 1 results
n8gdb run                     # continue to all_done
n8gdb console_text            # "VID_DATA TEST: ALL 10 PHASES PASSED"
```

**Tests:** `VID_CTRL`, `VID_DATA` (read/write), `VID_STATUS`, `VID_OPER` (CLEAR, cursor movement, HOME), streaming patterns with OVERFLOW detection

## Memory Layout

All programs use the same memory map as the main firmware:

```
$0000-$00FF  Zero Page (ZP pointers at $E0-$E7, scratch at $F0-$F3)
$0100-$01FF  Hardware Stack
$0200-$BEFF  RAM (BSS variables)
$C000-$CFFF  Frame Buffer (4KB)
$D000-$D7FF  Dev Bank (2KB RAM)
$D800-$DFFF  Device Registers (slots 0-7)
$E000-$FFF9  ROM (test program code + data)
$FFFA-$FFFF  Vectors (NMI, RESET, IRQ)
```

## Adding a New Test

1. Create `test_newname.s` with `.export _main` and any labels you want visible in n8gdb
2. Add `test_newname` to the `TESTS` list in `Makefile`
3. `make test_newname`
