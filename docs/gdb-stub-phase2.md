# GDB Stub — Phase 2 Completion

## What was delivered

Phase 2 adds the TCP transport layer, main loop integration, and async continue/step to the GDB RSP stub, making it usable with `gdb-multiarch`.

### Files modified

| File | Changes |
|------|---------|
| `src/gdb_stub.h` | Added `continue_exec`/`halt` callbacks, `gdb_interrupt_requested()` API |
| `src/gdb_stub.cpp` | TCP thread, command queue, response queue, `gdb_stub_poll()` implementation, `gdb_stub_notify_stop()`, `gdb_interrupt_requested()` |
| `src/main.cpp` | GDB callbacks, init/shutdown, poll switch, tick loop with bp/interrupt checks, ImGui status/disabled controls |
| `Makefile` | `-DENABLE_GDB_STUB=1` in production CXXFLAGS |
| `test/test_gdb_callbacks.cpp` | 6 new tests (step NOP/JAM, reset pin, bp set/clear, D44 disconnect) |

### Architecture

```
TCP Thread                    Main Thread
-----------                   -----------
socket/bind/listen
accept() → SENT_CONNECT ──→ cmd_queue
recv() → framing ──→ payload ──→ cmd_queue
                              gdb_stub_poll() drains cmd_queue
                              dispatch_command() / special handling
             resp_queue ←── response pushed
format_response() + send()

Async continue:
'c' → SENT_CONTINUE ──→ TCP enters async wait
                        Emulator runs...
                        bp_hit/interrupt → gdb_stub_notify_stop()
             resp_queue ←── stop reply
send stop reply to GDB
```

### Design decisions

- **D35**: `gdb_stub.cpp` has zero N8machine includes — all access through callbacks
- **D42**: `run_emulator` at file scope for callback access
- **D43**: GDB forces `bp_enable=true` on connect; BP checkbox disabled in ImGui
- **D44**: Detach/disconnect clears all GDB breakpoints via `memset(bp_mask, 0, ...)`
- **D45**: `std::atomic<bool>` for Ctrl-C interrupt, checked every tick in run loop
- **D47**: Reset callback uses `M6502_RES` pin + `tty_reset()`, NOT `emulator_reset()`

### Testing

- **207 tests pass** (201 Phase 1 + 6 new Phase 2)
- Production build succeeds with TCP thread and `ENABLE_GDB_STUB=1`
- TCP/socket tests (T43-T54, T80-T85) are manual with `gdb-multiarch`

### Manual verification

```bash
# Terminal 1: Run emulator
./n8

# Terminal 2: Connect GDB
gdb-multiarch -ex "target remote :3333"
(gdb) info registers
(gdb) x/16b 0xd000
(gdb) break *0xd010
(gdb) continue
(gdb) # Press Ctrl-C to interrupt
(gdb) detach
```

ImGui should show:
- "GDB: Connected (port 3333)" when client attached
- "Status: Halted (GDB)" when GDB has halted the target
- Run/Step buttons disabled when GDB is connected and halted
- BP checkbox disabled when GDB is connected
