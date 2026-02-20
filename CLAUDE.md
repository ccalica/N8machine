# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

N8machine is a 6502 homebrew computer emulator with a GUI debugger. It emulates a custom 6502 machine with 64KB address space, memory-mapped I/O (text display at $C000, TTY at $C100), and 12KB ROM at $D000. The GUI uses SDL2 + Dear ImGui + OpenGL3. A GDB RSP stub on TCP port 3333 enables remote debugging via the included `n8gdb` Node.js client.

## Build Commands

```bash
make              # build emulator → ./n8
make firmware     # build firmware via cc65 → N8firmware (copied to repo root)
make test         # build and run unit tests → ./n8_test
make clean        # clean emulator + firmware
make clean-test   # clean test artifacts only
```

Run a single test by name: `./n8_test -tc="test name"`

Dependencies: `libsdl2-dev`, OpenGL, `cc65` (firmware only), Node.js (n8gdb only).

Run the emulator from the repo root (it loads `N8firmware` and `N8firmware.sym` from CWD).

## Architecture

**CPU core:** `src/m6502.h` — vendored cycle-accurate 6502 from [floooh/chips](https://github.com/floooh/chips/). All bus interaction is via a 64-bit pin mask (`m6502_tick()`). Device decode uses `BUS_DECODE(bus, base, mask)` macro in `emulator.cpp`.

**Memory map:**
- `$0000-$00FF` Zero Page (cc65 runtime vars + firmware ZP at `$E0-$FF`)
- `$0100-$01FF` Hardware Stack
- `$0200-$BEFF` RAM
- `$C000-$C0FF` Text Display (frame_buffer[256])
- `$C100-$C10F` TTY device (maps to host stdin/stdout via `emu_tty.cpp`)
- `$D000-$FFF9` ROM (firmware binary)
- `$FFFA-$FFFF` Vectors (NMI/RESET/IRQ)

**GDB stub** (`src/gdb_stub.cpp`): Zero coupling to emulator — all access through `gdb_stub_callbacks_t` function pointers wired in `main.cpp`. TCP listener runs in a separate thread. Compile-time toggle: `ENABLE_GDB_STUB=0` makes all stub functions empty inlines.

**Main loop** (`src/main.cpp`): Each frame polls `gdb_stub_poll()` for GDB commands, then runs `emulator_step()` in a ~13ms time slice. Breakpoint/watchpoint hits call `gdb_stub_notify_stop()` to send async stop replies.

**IRQ mechanism:** `mem[0x00FF]` is the IRQ flag register. TTY asserts IRQ bit 1 when its ring buffer has data.

## Testing

Framework: doctest (vendored single-header in `test/doctest.h`). C++11.

Two test fixtures in `test/test_helpers.h`:
- `CpuFixture` — isolated CPU with its own memory; for instruction-level tests
- `EmulatorFixture` — uses real `emulator.cpp` globals; for bus/integration tests

`gdb_stub.cpp` is recompiled with `-DGDB_STUB_TESTING` for tests, exposing `gdb_stub_feed_byte()`, `gdb_stub_process_packet()`, etc. ImGui dependencies are satisfied by link-time stubs in `test/test_stubs.cpp`.

## n8gdb Client

`bin/n8gdb/n8gdb.mjs` — Node.js ESM, zero dependencies. Connects to port 3333.

**Important:** Each CLI invocation creates a separate TCP connection. Breakpoints are cleared on disconnect (`GDB_POLL_DETACHED` zeroes `bp_mask[]`). Use REPL mode for multi-step workflows:

```bash
node bin/n8gdb/n8gdb.mjs repl --sym firmware/gdb_playground/test_regs.sym
n8> load firmware/gdb_playground/test_regs 0xD000
n8> reset
n8> bp final_state
n8> run
```

Address syntax: `0x` or `$` prefix for hex, `#` prefix for decimal, bare hex, or label name (if `.sym` loaded). Env vars: `N8GDB_HOST`, `N8GDB_PORT`, `N8GDB_SYM`, `N8GDB_DEBUG=1`.

## Firmware

cc65 toolchain (`cl65 -t none --cpu 6502`). Linker config: `firmware/n8.cfg`. Custom runtime lib: `firmware/n8.lib`.

- `firmware/` — main firmware (boot, TTY driver, echo loop)
- `firmware/playground/` — experimental firmware programs
- `firmware/gdb_playground/` — GDB RSP stub validation tests (7 test programs)

Playground programs build to 12KB ROM binaries at $D000 with `.sym` files for n8gdb label resolution.

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | SDL2/ImGui event loop, GDB callback wiring, emulator control |
| `src/emulator.cpp` | CPU core, 64KB memory, bus decode, breakpoint/watchpoint logic |
| `src/gdb_stub.cpp` | GDB RSP protocol handler + TCP transport thread |
| `src/emu_tty.cpp` | TTY memory-mapped device (raw terminal I/O) |
| `src/m6502.h` | Vendored 6502 CPU emulator (do not edit) |
| `bin/n8gdb/rsp.mjs` | Low-level GDB RSP TCP client |
| `bin/n8gdb/n8gdb.mjs` | n8gdb CLI commands and REPL |
