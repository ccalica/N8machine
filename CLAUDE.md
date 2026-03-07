# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

N8machine is a 6502 homebrew computer emulator with a GUI debugger. It emulates a custom 6502 machine with 64KB address space, slot-based device registers at `$D800-$DFFF`, 4KB frame buffer at `$C000`, and 8KB ROM at `$E000`. The GUI uses SDL2 + Dear ImGui + OpenGL3. A GDB RSP stub on TCP port 3333 enables remote debugging via the included `n8gdb` Node.js client.

## Build Commands

```bash
make              # build emulator → ./n8
make firmware     # build firmware via cc65 → N8firmware (copied to repo root)
make test         # build and run unit tests → ./n8_test
make clean        # clean emulator + firmware
make clean-test   # clean test artifacts only
```

Run a single test by name: `./n8_test -tc="test name"`

Dependencies: `libsdl2-dev`, OpenGL, `cc65` (firmware only), Node.js (n8gdb/n8mcp).

Run the emulator from the repo root (it loads `N8firmware` and `N8firmware.sym` from CWD).

## Architecture

**CPU core:** `src/m6502.h` — vendored cycle-accurate 6502 from [floooh/chips](https://github.com/floooh/chips/). All bus interaction is via a 64-bit pin mask (`m6502_tick()`).

**Memory map:** (all constants defined in `src/n8_memory_map.h`)
- `$0000-$00FF` Zero Page (cc65 runtime vars + firmware ZP at `$E0-$FF`)
- `$0100-$01FF` Hardware Stack
- `$0400-$BFFF` Program RAM
- `$C000-$CFFF` Frame Buffer (4KB, separate `frame_buffer[]` backing store)
- `$D000-$D7FF` Dev Bank (2KB RAM)
- `$D800-$D81F` System/IRQ (slot 0) — `mem[$D800]` = IRQ flags
- `$D820-$D83F` TTY (slot 1) — `emu_tty.cpp`
- `$D840-$D85F` Video Control (slot 2, 12 regs) — `emu_video.cpp`
- `$D860-$D87F` Keyboard (slot 3) — `emu_kbd.cpp`
- `$D880-$DFFF` Reserved device slots
- `$E000-$FFFF` ROM (8KB firmware binary)

**Device router:** Slot-based dispatch in `emulator_step()`: `slot = (addr - $D800) >> 5`, `reg = offset & 0x1F`. Bus decode priority: frame buffer → device router → generic mem[] (with ROM write protection).

**GDB stub** (`src/gdb_stub.cpp`): Zero coupling to emulator — all access through `gdb_stub_callbacks_t` function pointers wired in `main.cpp`. TCP listener runs in a separate thread. Compile-time toggle: `ENABLE_GDB_STUB=0` makes all stub functions empty inlines.

**Main loop** (`src/main.cpp`): Each frame polls `gdb_stub_poll()` for GDB commands, then runs `emulator_step()` in a ~13ms time slice. Breakpoint/watchpoint hits call `gdb_stub_notify_stop()` to send async stop replies. SDL keyboard events are translated to N8 keycodes via `sdl_to_n8_keycode()` and injected via `kbd_inject_key()`.

**IRQ mechanism:** `mem[$D800]` is the IRQ flag register. `IRQ_CLR()` zeros it every tick; devices reassert via their tick functions (`tty_tick()`). TTY asserts bit 1. Keyboard is polling-only (no IRQ).

## Testing

Framework: doctest (vendored single-header in `test/doctest.h`). C++11.

Two test fixtures in `test/test_helpers.h`:
- `CpuFixture` — isolated CPU with its own memory; for instruction-level tests
- `EmulatorFixture` — uses real `emulator.cpp` globals; for bus/integration tests

`gdb_stub.cpp` is recompiled with `-DGDB_STUB_TESTING` for tests, exposing `gdb_stub_feed_byte()`, `gdb_stub_process_packet()`, etc. ImGui dependencies are satisfied by link-time stubs in `test/test_stubs.cpp`.

## n8gdb Client

`bin/n8gdb/n8gdb.mjs` — Node.js ESM, zero dependencies. Connects to port 3333.

Each CLI invocation creates a separate TCP connection. Breakpoints and watchpoints persist across connections, so individual commands can be chained:

```bash
node bin/n8gdb/n8gdb.mjs --sym firmware/gdb_playground/test_regs.sym load firmware/gdb_playground/test_regs 0xE000
node bin/n8gdb/n8gdb.mjs reset
node bin/n8gdb/n8gdb.mjs --sym firmware/gdb_playground/test_regs.sym bp final_state
node bin/n8gdb/n8gdb.mjs run
```

Address syntax: `0x` or `$` prefix for hex, `#` prefix for decimal, bare hex, or label name (if `.sym` loaded). Env vars: `N8GDB_HOST`, `N8GDB_PORT`, `N8GDB_SYM`, `N8GDB_DEBUG=1`.

## Firmware

cc65 toolchain (`cl65 -t none --cpu 6502`). Linker config: `firmware/n8.cfg`. Custom runtime lib: `firmware/n8.lib`.

- `firmware/` — main firmware (boot, TTY driver, echo loop)
- `firmware/playground/` — experimental firmware programs
- `firmware/gdb_playground/` — GDB RSP stub validation tests (10 test programs)

Playground programs build to 8KB ROM binaries at $E000 with `.sym` files for n8gdb label resolution.

## n8mcp — MCP Server

`bin/n8mcp/n8mcp.mjs` — MCP server for AI-assisted debugging via Claude Code or other MCP clients. Connects to the GDB RSP stub over TCP, exposing 18 tools for emulator control.

**Setup:** Install dependencies once: `cd bin/n8mcp && npm install`

**Configuration** (`~/.claude/mcp.json`):
```json
{
  "mcpServers": {
    "n8machine": {
      "command": "node",
      "args": ["/path/to/N8machine/bin/n8mcp/n8mcp.mjs"],
      "env": { "N8_HOME": "/path/to/N8machine" }
    }
  }
}
```

**Tools:**

| Tool | Description |
|------|-------------|
| `n8_start` | Start n8 emulator (no-op if running) |
| `n8_stop` | Stop n8 emulator |
| `n8_restart` | Restart n8 emulator |
| `n8_regs` | Read all CPU registers with decoded flags |
| `n8_write_reg` | Write a register by name (a/x/y/s/p/pc) |
| `n8_status` | Show running/halted state + registers |
| `n8_read_memory` | Read memory (hex dump + ASCII) |
| `n8_write_memory` | Write hex bytes to memory |
| `n8_load_binary` | Load binary file at address, optionally load .sym |
| `n8_load_symbols` | Load cc65 .sym file for label resolution |
| `n8_run` | Continue execution, wait for stop |
| `n8_step` | Single-step N instructions |
| `n8_halt` | Interrupt running program |
| `n8_reset` | Reset CPU via reset vector |
| `n8_goto` | Set PC and continue |
| `n8_set_breakpoint` | Set breakpoint at address/label |
| `n8_clear_breakpoint` | Clear breakpoint |
| `n8_clear_all_breakpoints` | Clear all breakpoints/watchpoints |
| `n8_kbd_inject` | Inject keystrokes (e.g. `go north[enter]`) |
| `n8_console_text` | Read framebuffer as Unicode text |
| `n8_console_video` | Capture screen as PNG screenshot |

Read-only tools (`n8_regs`, `n8_read_memory`, `n8_status`, `n8_console_text`, `n8_console_video`, `n8_set_breakpoint`, `n8_clear_breakpoint`, `n8_clear_all_breakpoints`, `n8_kbd_inject`) auto-resume the CPU if it was running before the tool call.

Env vars: `N8_HOME` (repo root, required for `n8_start`), `N8GDB_HOST`, `N8GDB_PORT`, `N8GDB_DEBUG`.

Tests: `node bin/n8mcp/test.mjs` (131 tests, mock RSP server, no emulator needed).

## Shared Modules

`bin/shared/` — Reusable code imported by both n8gdb and n8mcp:

| Module | Contents |
|--------|----------|
| `address.mjs` | `parseAddr()` — hex/decimal/label address parsing |
| `symbols.mjs` | `loadSymbols()` — cc65 .sym file loader |
| `format.mjs` | `hexdump()`, `fmtRegs()`, `fmtStop()`, `hex8()`, `hex16()` |
| `charmap.mjs` | `N8_CHARMAP` — 256-entry byte-to-Unicode map |
| `keyboard.mjs` | `parseKeyInput()`, `NAMED_KEYS`, `charToKeycode()` |
| `console.mjs` | `readConsoleText()` — read video regs + framebuffer as Unicode text |

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | SDL2/ImGui event loop, GDB callback wiring, SDL key translation |
| `src/emulator.cpp` | CPU core, 64KB memory, device router, breakpoint/watchpoint logic |
| `src/n8_memory_map.h` | All hardware address constants and register definitions |
| `src/gdb_stub.cpp` | GDB RSP protocol handler + TCP transport thread |
| `src/gdb_bridge.cpp` | GDB-to-emulator callback bridge (zero coupling) |
| `src/emu_tty.cpp` | TTY memory-mapped device (raw terminal I/O) |
| `src/emu_video.cpp` | Video control registers, scroll, VID_DATA streaming |
| `src/emu_kbd.cpp` | Keyboard registers, FIFO buffer, key injection |
| `src/m6502.h` | Vendored 6502 CPU emulator (do not edit) |
| `bin/n8gdb/rsp.mjs` | Low-level GDB RSP TCP client (shared by n8gdb + n8mcp) |
| `bin/n8gdb/n8gdb.mjs` | n8gdb CLI commands and REPL |
| `bin/n8mcp/n8mcp.mjs` | MCP server for AI-assisted debugging |
| `bin/shared/` | Shared modules (address, symbols, format, charmap, keyboard) |
