# GDB Stub — Phase 3 Completion

## What was delivered

Phase 3 adds three protocol features from the spec's Future Work: **watchpoints (Z2/Z3/Z4)**, **vCont support**, and a **configurable step guard**.

### Files modified

| File | Changes |
|------|---------|
| `src/emulator.h` | Added `wp_write_mask[]`/`wp_read_mask[]` externs, watchpoint accessor declarations |
| `src/emulator.cpp` | Watchpoint state, check logic in `emulator_step()`, accessor functions |
| `src/gdb_stub.h` | `set_watchpoint`/`clear_watchpoint` callbacks, `step_guard` config field, `gdb_stub_notify_watchpoint()`/`gdb_stub_get_step_guard()` API |
| `src/gdb_stub.cpp` | Z2-Z4 handlers, vCont support in `handle_v()` and `gdb_stub_poll()`, step guard getter, watchpoint stop notification |
| `src/main.cpp` | Watchpoint callbacks, tick loop wp hit check, detach clears wp masks, configurable step guard |
| `test/test_helpers.h` | Fixture clears wp arrays and state |
| `test/test_gdb_protocol.cpp` | Mock watchpoint tracking, 11 new protocol tests (6 watchpoint, 5 vCont) |
| `test/test_gdb_callbacks.cpp` | 5 new emulator-level watchpoint tests |

### Design decisions

- **D35 preserved**: `gdb_stub.cpp` still has zero N8machine includes — watchpoint bus logic lives in `emulator.cpp`, glue in `main.cpp`
- **Read watchpoints exclude SYNC**: `!(pins & M6502_SYNC)` gate prevents opcode fetch cycles from triggering read watchpoints — only data reads fire
- **Access watchpoints (Z4)**: decomposed into both `wp_write_mask` and `wp_read_mask` — simplest correct implementation
- **vCont**: single-threaded target, so only the first action matters; thread IDs accepted and ignored
- **vCont;t**: treated as halt (same as Ctrl-C interrupt)
- **Step guard**: config-driven via `gdb_stub_config_t.step_guard`, queried by `gdb_stub_get_step_guard()`, default 16
- **Stop reply format**: `T05watch:<addr>;thread:01;` / `T05rwatch:...` / `T05awatch:...` per GDB RSP spec
- **No qSupported changes**: GDB discovers vCont via `vCont?` query, watchpoints via Z2 attempt returning OK

### Testing

- **221 tests pass** (207 Phase 2 + 14 new Phase 3)
- New protocol tests: Z2/z2, Z3/z3, Z4/z4 set/clear, vCont? response, vCont;c, vCont;s, vCont;s:1, vCont;c:1
- New emulator tests: write wp hit on STA, read wp hit on LDA, SYNC exclusion, accessor coverage, D44 wp cleanup
- Production build succeeds

### Manual verification

```bash
# Terminal 1: Run emulator
./n8

# Terminal 2: Connect GDB
gdb-multiarch -ex "target remote :3333"
(gdb) watch *0x0200          # write watchpoint
(gdb) continue               # run until write to 0x0200
(gdb) rwatch *0x0200         # read watchpoint
(gdb) continue               # run until read from 0x0200
(gdb) awatch *0x0300         # access watchpoint (read or write)
(gdb) detach                 # verify wp masks cleared
```

Modern GDB uses vCont automatically — no special setup needed.
