# GDB RSP Stub — Phase 1 Completion Report

**Branch**: `feature/gdb-stub` (from `feature/test-harness`)
**Spec**: `docs/gdb-stub-spec-v3.md`
**Date**: 2026-02-19

---

## Scope

Core protocol parser, callback interface, emulator accessor functions, bug fixes, and tests. No TCP thread, no `main.cpp` integration. Everything testable via direct function calls.

## Test Results

```
test cases: 201 | 201 passed | 0 failed | 0 skipped
assertions: 308 | 308 passed | 0 failed
```

- 109 existing tests (unchanged, all pass)
- 65 protocol tests (`test_gdb_protocol.cpp`)
- 27 callback/accessor tests (`test_gdb_callbacks.cpp`)

---

## Files Changed

| File | Action | Lines | Description |
|------|--------|-------|-------------|
| `src/gdb_stub.h` | Created | 76 | Callback struct, config, poll enum, lifecycle API, `#if !ENABLE_GDB_STUB` no-op stubs, `#ifdef GDB_STUB_TESTING` test API |
| `src/gdb_stub.cpp` | Created | 510 | Protocol parser, framing state machine, command handlers, hex utilities, embedded XML |
| `src/emulator.h` | Modified | +14 | Accessor declarations |
| `src/emulator.cpp` | Modified | +35 | D32 fix, accessor implementations, `emulator_write_pc()` with prefetch |
| `src/emu_tty.cpp` | Modified | +3 | BUG-1 fix: empty queue guard on reg 3 read |
| `test/test_gdb_protocol.cpp` | Created | 596 | 65 protocol test cases |
| `test/test_gdb_callbacks.cpp` | Created | 248 | 27 callback/accessor test cases |
| `Makefile` | Modified | +8 | gdb_stub in sources, `-pthread`, test-specific `GDB_STUB_TESTING` compilation |

---

## Bug Fixes

### D32 — SYNC gate on breakpoint check
`src/emulator.cpp:86` — breakpoints now only fire on instruction fetch (SYNC set), not data reads.

```cpp
// Before:
if(bp_enable && bp_mask[addr]) {
// After:
if(bp_enable && bp_mask[addr] && (pins & M6502_SYNC)) {
```

Validated by: `D32: breakpoint at data address does not fire on data read`

### BUG-1 — Empty queue guard
`src/emu_tty.cpp:84` — reading TTY register 3 (`$C103`) on empty queue now returns `0x00` instead of invoking UB (`front()` on empty `std::queue`).

Validated by: `BUG-1: tty_decode reg 3 on empty queue returns 0x00`, `BUG-1: tty_decode reg 3 on empty queue does not crash`

---

## Emulator Accessors Added

| Function | Description |
|----------|-------------|
| `emulator_read_a/x/y/s/p()` | Register getters |
| `emulator_write_a/x/y/s/p(uint8_t)` | Register setters |
| `emulator_write_pc(uint16_t)` | PC write with prefetch: preserves IRQ/NMI pins, sets SYNC+RW |
| `emulator_bp_hit()` | Non-clearing breakpoint check |
| `emulator_clear_bp_hit()` | Clear bp_hit flag |
| `emulator_bp_enabled()` | Read bp_enable state |

Existing `emulator_check_break()` preserved for backward compat.

---

## GDB RSP Commands Implemented

| Command | Status | Notes |
|---------|--------|-------|
| `?` | Done | `T<signal>thread:01;` |
| `g` / `G` | Done | 14-char hex, PC little-endian |
| `p` / `P` | Done | Single register read/write with validation |
| `m` / `M` | Done | Memory read/write with range validation |
| `s` | Done | Step, returns stop reply |
| `c` | Partial | Sets running state, no async execution (Phase 2) |
| `Z0`/`Z1` | Done | Set breakpoint (Z1 maps to Z0) |
| `z0`/`z1` | Done | Clear breakpoint |
| `Z2-Z4` | Done | Returns empty (unsupported) |
| `D` | Done | Detach |
| `k` | Done | Kill |
| `H` | Done | Thread select (always OK) |
| `qSupported` | Done | PacketSize, NoAck, qXfer features+memmap |
| `qXfer:features:read:target.xml` | Done | Chunked XML, `org.n8machine.cpu` |
| `qXfer:memory-map:read` | Done | v3 memory map with precise sub-regions |
| `qfThreadInfo` / `qsThreadInfo` | Done | Single thread |
| `qC` / `qAttached` | Done | |
| `qRcmd` | Done | `reset` calls callback, unknown returns error |
| `QStartNoAckMode` | Done | |
| `vMustReplyEmpty` / `vCont?` | Done | Empty responses |

### Framing
- Full state machine: IDLE → PACKET_DATA → CHECKSUM_1 → CHECKSUM_2
- Escape handling (`}` + byte XOR 0x20)
- `$` in PACKET_DATA restarts framing
- Ctrl-C (0x03) in IDLE sets interrupt flag
- ACK/NACK prefix in normal mode, suppressed in NoAck mode

### Error codes
- `E01` — bad address / overflow
- `E02` — invalid register number
- `E03` — malformed command / parse error

---

## Design Decisions Implemented

| ID | Decision |
|----|----------|
| D13 | Target XML with `org.n8machine.cpu`, no `<architecture>` tag |
| D16 | `emulator_write_pc()` prefetch: preserves IRQ/NMI, sets SYNC+RW |
| D17 | GDB memory reads use raw `mem[]`, bypass bus decode |
| D32 | Breakpoint check gated on SYNC pin |
| D35 | `gdb_stub.cpp` has zero N8machine includes — all access through callbacks |
| D47 | Reset via callback (which sets M6502_RES pin) |

---

## Phase 2 — Remaining Work

### Required for GDB connection
- TCP listener thread (accept on configured port)
- Socket I/O: read bytes → `feed_byte()`, responses → socket write
- Main loop integration: `gdb_stub_poll()` in `main.cpp` render loop
- Async `continue`: run emulator until breakpoint/interrupt, then send stop reply

### Deferred items
- `O<hex-output>` transport packets for `qRcmd` output
- `emulator_check_break()` refactor (replace with `bp_hit()`/`clear_bp_hit()`)
- Full `emulator_reset()` test (requires firmware file fixture)
- Test cases T43-T54, T80-T85 (TCP/main loop dependent)
