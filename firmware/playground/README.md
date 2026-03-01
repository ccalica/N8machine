# playground

Firmware experiments for the N8machine emulator. Each program is a standalone bare-metal 6502 binary, debuggable with `n8gdb`.

## Programs

| Program | Description | Status |
|---------|-------------|--------|
| mon1 | Line parser — reads input, splits command + remainder, echoes parse | Working |
| mon2 | Memory monitor — hex dump, write, fill, go, help | Working |
| mon3 | Extended monitor — mon2 + copy/cmp/search/calc/cls/ascii | **Buggy** |

## Quick Start

1. Start the N8machine emulator
2. Build and load a program:

```
make mon2
n8gdb load mon2 0xE000
n8gdb reset
n8gdb run
```

Then type in the emulator's terminal. Type `?` for help (mon2/mon3).

## Build

```
make          # build all programs
make clean    # remove build artifacts
```

Requires `cc65` (cl65/ca65/ld65).

## Memory Layout

Same as the main firmware:

```
$0000-$00FF  Zero Page
$0100-$01FF  Hardware Stack
$0200-$BEFF  RAM (BSS variables)
$C000-$CFFF  Frame Buffer (4KB)
$D000-$D7FF  Dev Bank (2KB RAM)
$D800-$DFFF  Device Registers (slots 0-7)
$E000-$FFF9  ROM (program code + data)
$FFFA-$FFFF  Vectors (NMI, RESET, IRQ)
```

## See Also

- [`../gdb_playground/`](../gdb_playground/) — GDB RSP stub validation tests
- [`../../bin/n8gdb/`](../../bin/n8gdb/) — n8gdb client
