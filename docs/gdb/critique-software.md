# Software Architecture & GDB Protocol Critique: GDB Stub Spec for N8Machine

**Reviewer perspective**: Software architect and GDB protocol expert focused on correctness, robustness, and maintainability.

---

## 1. GDB RSP Protocol Compliance

### Issue 1.1: Target XML `<architecture>` Tag [CRITICAL]
**Ref: Part 1 Section 5, D13**

The spec uses `<architecture>6502</architecture>`. GDB does NOT have a built-in "6502" architecture. When GDB encounters an unknown architecture string, it may refuse to connect or disable register/memory commands. The XML feature name `org.gnu.gdb.m6502.cpu` is also non-standard -- GDB does not know this feature name.

**Recommendation**: Use `<architecture>none</architecture>` or omit the architecture tag entirely. GDB will still use the register definitions from the `<feature>` block. The feature name should be changed to something that does not imply GDB recognition (e.g., `org.n8machine.cpu`). Test with actual GDB to verify it accepts the XML.

### Issue 1.2: Missing `qTStatus` Support [Minor]
GDB sends `qTStatus` early in the connection to check tracepoint support. The spec's "return empty for unknown q-commands" handles this, but it may cause GDB to print a warning.

### Issue 1.3: Missing `qOffsets` Support [Minor]
GDB may send `qOffsets` to determine text/data/bss section offsets. Empty response is fine.

### Issue 1.4: Missing `qSymbol` Support [Nitpick]
GDB sends `qSymbol::` at connect. Empty response is fine.

### Issue 1.5: `qXfer:features:read` Annex Name [CRITICAL]
**Ref: Part 1 Section 5.3**

GDB requests `qXfer:features:read:target.xml:offset,length`. The annex name is "target.xml". The spec correctly handles this. However, the checksum coverage for `qXfer` must be verified -- the colons in the command string are part of the data.

### Issue 1.6: Stop Reply Format -- Thread Field [IMPORTANT]
**Ref: Section 4, D13**

The spec uses `T05` for stop replies. Modern GDB expects the extended format `T05thread:1;` (with thread info). While bare `T05` works, some GDB versions emit warnings without thread info. Adding `thread:1;` is trivial and improves compatibility.

**Recommendation**: Use `T05thread:01;` for all stop replies.

### Issue 1.7: `k` (Kill) Response [Minor]
The spec says "no response expected." Correct per RSP spec. However, some GDB stubs send `OK` before closing. The spec is correct here.

### Issue 1.8: `?` Query After JAM -- Stop Reason Persistence [IMPORTANT]
**Ref: D21**

When GDB connects and sends `?`, it expects the current stop reason. If the CPU is in a JAM state (from a previous step that hit JAM), should `?` return `T04` (SIGILL)? The spec defines SIGILL for JAM during step, but does not specify that this stop reason persists for `?` queries. The `get_stop_reason()` callback must track the most recent stop reason across commands.

### Issue 1.9: `vCont?` and Step/Continue Compatibility [IMPORTANT]
**Ref: D11**

The spec says `vCont?` returns empty (unsupported). GDB then falls back to `s`/`c`. However, some GDB versions (particularly the gdb-multiarch builds) may not fall back gracefully. The safer approach is to implement `vCont?` returning `vCont;c;s;t` to advertise basic capabilities. Then implement `vCont;c` and `vCont;s` as aliases for `c` and `s`.

**Recommendation**: Consider minimal `vCont` support for wider GDB compatibility.

---

## 2. Threading Model Correctness

### Issue 2.1: `run_emulator` Is Block-Scoped Static [CRITICAL]
**Ref: Part 3 Section 3.2**

The spec says `run_emulator` and `step_emulator` should be "promoted from function-local static to externally visible variable." However, `run_emulator` and `step_emulator` are currently `static bool` locals inside the main loop function body (main.cpp lines 167-168). They are NOT accessible from outside that scope.

The spec underestimates the refactoring required. The variable needs to be moved to file scope and all references updated (Run/Pause button, breakpoint check, step logic).

**Recommendation**: Explicitly describe the refactoring: move to file-scope variables in main.cpp (or to an `emulator_state_t` struct). Document all references that must change.

### Issue 2.2: `bp_enable` Shadowing [IMPORTANT]

There are *two* `bp_enable` variables: a global in `emulator.cpp` line 36 and a `static bool bp_enable` local in `main.cpp` line 169. The main.cpp local drives the ImGui checkbox and calls `emulator_enablebp()` to sync. The spec's `set_breakpoint()` callback sets `bp_mask[addr] = true; bp_enable = true` -- but which `bp_enable`?

**Recommendation**: Address the dual-variable problem. Either the GDB callback should call `emulator_enablebp(true)`, or the bp_enable local should be eliminated in favor of the global.

### Issue 2.3: Race Condition -- Ctrl-C During Emulation Tick Loop [IMPORTANT]
**Ref: Section 2.5**

Ctrl-C INTERRUPT won't be processed until `gdb_stub_poll()` on the next frame. Latency could be up to one frame (~16ms). Acceptable but should be documented.

**Recommendation**: Add `std::atomic<bool> interrupt_requested` flag that the tick loop checks between instructions for faster response.

### Issue 2.4: Disconnect During Continue -- Signaling Gap [IMPORTANT]
**Ref: Section 2.4, T70**

The spec does not clearly define how the TCP thread signals disconnect to the main thread. If GDB disconnects after halting (without `c` or `D`), `gdb_halted=true` and emulator is stopped. TCP thread must enqueue something.

**Recommendation**: Add `DISCONNECT` command type to `GdbCommand::Type`.

### Issue 2.5: Shutdown While TCP Thread Blocked in `accept()` [IMPORTANT]
**Ref: Section 2.6**

Closing a fd from another thread while blocked in `accept()` is technically undefined by POSIX. Use `shutdown(server_fd, SHUT_RDWR)` before `close()`, or use self-pipe/eventfd for portable wakeup.

### Issue 2.6: Response Queue Blocking -- Latency Budget [IMPORTANT]
**Ref: D4, Section 2.3**

TCP thread blocks up to 500ms waiting for response. GDB's `remotetimeout` is 2 seconds default, so single-wait is fine. But document the worst-case latency budget.

### Issue 2.7: Ctrl-C Flow Ambiguity [IMPORTANT]
**Ref: Sections 2.5 vs 2.4**

Section 2.5 says INTERRUPT is "enqueued to command queue." Section 2.4 says TCP thread "checks socket" during wait. These describe different mechanisms. Clarify: use `std::atomic<bool>` for interrupt, not the command queue.

---

## 3. Callback Interface Design

### Issue 3.1: `step_instruction()` Ownership [IMPORTANT]
**Ref: D9, Part 2 Section 4.2**

The callback implementation must know about M6502_SYNC, guard counters, and JAM detection. The stub just calls it blindly. Acceptable but the contract must be documented: "step_instruction() executes one instruction, sets internal stop signal."

### Issue 3.2: Missing `is_halted()` Callback [Minor]
For Ctrl-C handling ("if emulator halted: ignored"), stub uses its own `gdb_halted` flag. Should be explicitly stated.

### Issue 3.3: Breakpoint Detection During Continue [IMPORTANT]
The spec says main loop calls `emulator_check_break()` then `gdb_stub_notify_stop(5)`, but does not show the modified main loop code. Important detail for implementer.

### Issue 3.4: `reset()` Callback Side Effects [Minor]
`emulator_reset()` sets M6502_RES pin but it's not processed until next `m6502_tick()`. Document: "Reset sets RES pin; actual reset executes on next step/continue."

### Issue 3.5: Register Read/Write Dispatch [Nitpick]
Both stub and callback have switch statements on register number. Simpler alternative: 6 named getters.

---

## 4. State Machine Completeness

### Issue 4.1: No Explicit State Machine [IMPORTANT]

States are implicit. Key states:
- NO_CLIENT → HALTED (on accept)
- HALTED → RUNNING (on `c`) → HALTED (on bp/Ctrl-C)
- HALTED → STEPPING (on `s`) → HALTED (step complete)
- Any → DISCONNECTING → NO_CLIENT

Missing: what if GDB sends `g` while RUNNING? (Protocol violation -- RSP is synchronous, GDB waits for stop reply.)

**Recommendation**: Add explicit state diagram.

---

## 5. Error Handling Gaps

### Issue 5.1: Non-Hex Characters [IMPORTANT]
No validation spec for non-hex chars in memory data. Should produce `E03`.

### Issue 5.2: `G` Packet Length Validation [IMPORTANT]
No handling for wrong-length data in `G` (should be exactly 14 hex chars). Should produce `E03`.

### Issue 5.3: Address > 0xFFFF [IMPORTANT]
Hex parsing could produce values > 16 bits. `uint16_t` truncation would silently wrap. Validate explicitly.

### Issue 5.4: `Z`/`z` Kind Field [Minor]
Accepted and ignored. Document this.

### Issue 5.5: Integer Overflow in Length Field [Minor]
Specify data type for parsed length field.

### Issue 5.6: Empty Packet Body [Minor]
`$#00` should be treated as unknown command → empty response.

---

## 6. Test Case Analysis

### Issue 6.1: Missing -- `?` After JAM [IMPORTANT]
No test verifying `?` returns `T04` when CPU is in JAM state at connect.

### Issue 6.2: Missing -- `tty_tick()` During Step [IMPORTANT]
`tty_tick()` calls `select()` on stdin during every step cycle. No test for this interaction.

### Issue 6.3: Missing -- `G` Wrong Length [IMPORTANT]
No negative test for `G` packet with incorrect data length.

### Issue 6.4: Missing -- `P` Value Too Large for Register [Minor]
`P0=FFFF` writing 16-bit value to 8-bit register — undefined behavior.

### Issue 6.5: Missing -- Breakpoint During Step [IMPORTANT]
Step lands on breakpoint address — should report T05 (same as normal step).

### Issue 6.6: Missing -- Memory Write Then Execute [Minor]
Write instruction via `M`, then step through it.

### Issue 6.7: T03 Escape Test Note [IMPORTANT]
Checksum is over raw bytes (including `}` and `0x03`), not decoded bytes. Test is correct but clarify.

### Issue 6.8: Missing Concurrency Tests [IMPORTANT]
No tests for: rapid commands, protocol violations, disconnect during step, rapid connect/disconnect.

### Issue 6.9: T48 Untestable Assertions [Minor]
"Single atomic load, no mutex operations" is a code review check, not automatable.

---

## 7. API Design

### Issue 7.1: `gdb_poll_result_t` Multiple Events [IMPORTANT]
Single enum return can't express multiple events per poll. Clarify: does poll process all commands? What if HALT + register read both queued?

**Recommendation**: Document poll processes all commands, returns highest-priority state change. Or require caller to loop until `GDB_POLL_NONE`.

### Issue 7.2: `gdb_stub_notify_stop()` No-Client Safety [IMPORTANT]
Should be documented as no-op when no client connected.

### Issue 7.3: `GDB_POLL_NONE` Naming [Minor]
Misleading when commands were processed but no state change. Consider `GDB_POLL_NO_STATE_CHANGE`.

---

## 8. Memory Safety

No significant issues. `std::string` handles 131KB. Consider `reserve(131072)` at init.

---

## 9. Build and Portability

### Issue 9.1: `-lpthread` vs `-pthread` [IMPORTANT]
Use `-pthread` (compiler+linker flag) instead of `-lpthread` (linker only). GCC requires `-pthread` for `std::thread`.

### Issue 9.2: Emscripten Compatibility [Minor]
Codebase supports Emscripten. GDB stub uses sockets/threads unavailable there. Note: "Emscripten builds must use `-DENABLE_GDB_STUB=0`."

---

## 10. Spec Completeness

### Issue 10.1: `pins` Global Clobbering [IMPORTANT]
PC write prefetch (`pins = M6502_SYNC`) clobbers all other pin state. Must be called only when halted.

### Issue 10.2: `tty_tick()` stdin Interaction [IMPORTANT]
During step, `tty_tick()` reads from stdin. Could consume user keystrokes. Recommend stdin redirect during GDB sessions.

### Issue 10.3: `emulator_check_break()` Clears `bp_hit` [IMPORTANT]
Side-effect clearing could cause race between GDB and ImGui handlers. Separate the checks.

### Issue 10.4: Hex Case Convention [Minor]
Specify lowercase hex (conventional).

### Issue 10.5: `qXfer` Offset/Length Are Hex [Minor]
Document that offset and length in `qXfer` are hex-encoded.

### Issue 10.6: `bp_enable` Global Switch vs GDB [IMPORTANT]
If user unchecks ImGui BP checkbox, GDB breakpoints silently stop working. Force `bp_enable=true` when GDB connected, or bypass it for GDB checks.

### Issue 10.7: Detach Breakpoint Cleanup [Minor]
Spec doesn't specify whether GDB-set breakpoints are cleared on disconnect.

---

## Summary

| Severity | Count | Key Issues |
|----------|-------|------------|
| Critical | 2 | Target XML architecture name (1.1), `run_emulator` scoping (2.1) |
| Important | 19 | Stop reply thread field, `?` after JAM, bp_enable shadowing, Ctrl-C race, disconnect signaling, shutdown accept(), Ctrl-C flow ambiguity, breakpoint during continue, state machine, non-hex validation, G packet length, address overflow, poll multiple events, notify_stop safety, pins clobbering, tty_tick stdin, bp_hit clearing, bp_enable global, -pthread flag |
| Minor | 12 | Various protocol queries, reset side effects, kind field, Emscripten, hex case, detach cleanup |
| Nitpick | 5 | qSymbol, register dispatch, allocations, buffer reuse |
