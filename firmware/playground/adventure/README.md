A Zork-like text adventure set in William Gibson's Sprawl universe.

## Build

```bash
make          # produces: adventure (ROM) + adventure_data (RAM)
make clean    # remove build artifacts
```

## Two-File Architecture

The game is split into two binaries:
- `adventure` — engine code in 8KB ROM at $E000
- `adventure_data` — world data (rooms, items, text) in RAM at $0500

This gives ~47KB for game text instead of sharing 8KB ROM with engine code.

## Run

```bash
n8gdb load adventure_data 0x0500   # strings into RAM
n8gdb load adventure 0xE000        # engine into ROM
n8gdb reset
n8gdb run
```
