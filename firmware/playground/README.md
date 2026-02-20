# playground

Firmware experiments for the N8machine emulator. Each program is a standalone bare-metal 6502 binary, debuggable with `n8gdb`.

## Quick Start

1. Start the N8machine emulator
2. Build and load a program:

```
make test_name
n8gdb repl --sym test_name.sym
n8> load test_name 0xD000
n8> reset
```

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
$C000-$C0FF  Text Display (memory-mapped I/O)
$C100-$C10F  TTY (memory-mapped I/O)
$D000-$FFF9  ROM (program code + data)
$FFFA-$FFFF  Vectors (NMI, RESET, IRQ)
```

## See Also

- [`../gdb_playground/`](../gdb_playground/) — GDB RSP stub validation tests
- [`../../bin/n8gdb/`](../../bin/n8gdb/) — n8gdb client
