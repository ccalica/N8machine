# Test Harness Spec v1 -- Methodology Critique (Critic B)

**Reviewer perspective**: Testing methodology -- coverage completeness, test level alignment, behavioral vs implementation coupling, independence, and structural organization.

---

## Critical Issues

### 1. IRQ pipeline in `emulator_step()` is undertested and the existing test (T101) is under-prioritized

**Severity**: Critical

The IRQ logic in `emulator_step()` (lines 92-110) is one of the most intricate code paths in the emulator. Every tick: `IRQ_CLR()` wipes `mem[0x00FF]` to zero, then `tty_tick()` potentially re-sets it, then the value determines whether M6502_IRQ is asserted or de-asserted on the pin bus. This is a three-phase pipeline with subtle ordering dependencies.

The spec has exactly one integration test (T101, P1) for IRQ. That is grossly insufficient for what is effectively the emulator's interrupt subsystem.

**Missing tests**:
- IRQ assertion timing relative to `CLI` -- CPU must finish current instruction before vectoring
- IRQ with I flag set (should be masked)
- IRQ cleared mid-sequence (char consumed between ticks)
- Multiple IRQ sources (the design uses bit positions -- bit 0 vs bit 1) -- what happens with two bits set?
- IRQ register race: `IRQ_CLR()` at line 92 means `emu_set_irq()` calls from outside `tty_tick()` are obliterated every tick. This is an architectural concern that tests should document/verify

**Recommendation**: Promote T101 to P0. Add at minimum:
- `T101a` (P0): IRQ masked by I flag -- run program with `SEI`, assert IRQ, verify CPU does NOT vector
- `T101b` (P0): IRQ timing -- assert IRQ, verify CPU completes current instruction before vectoring
- `T101c` (P1): IRQ after buffer drain -- inject char, read it (clearing IRQ), verify IRQ pin de-asserts on next step
- `T101d` (P2): Multiple IRQ bits -- `emu_set_irq(0)` and `emu_set_irq(1)` both set, clear one, verify other persists through tick

### 2. `tty_decode()` reads from empty queue -- undefined behavior, no test

**Severity**: Critical

In `emu_tty.cpp` line 87-88, register 3 (In Data) executes `tty_buff.front()` followed by `tty_buff.pop()` unconditionally. If the queue is empty, `front()` is undefined behavior (likely segfault or garbage). There is no guard checking `tty_buff.size() > 0` before this path.

The spec has no test for reading register 3 when the buffer is empty. This is a crash-path bug that tests must cover.

**Recommendation**: Add test case:
- `T75a` (P0): Read In Data with empty buffer -- `tty_decode(read_pins, 3)` when buffer empty -- document expected behavior (crash? return 0? return 0xFF?). This will likely expose a bug that needs a production fix.

### 3. `emulator_reset()` is completely untested

**Severity**: Critical

`emulator_reset()` (line 259-264) is a real function called from the UI. It:
1. Sets the `M6502_RES` pin
2. Calls `tty_reset()`
3. Calls `emulator_loadrom()` (opens a file!)
4. Calls `emu_labels_init()` (opens another file!)

The spec mentions reset vector fetch (T23) but never tests the `emulator_reset()` function itself. Since it calls `emulator_loadrom()` and `emu_labels_init()` (both of which open files), it cannot be called in the current test harness without either (a) providing stub files, or (b) crashing with `fopen` returning NULL.

This is both a coverage gap and a design hazard: any integration test that accidentally triggers reset will crash the test process.

**Recommendation**:
- Add a test documenting that `emulator_reset()` is NOT safe to call in the test harness (or add file stubs)
- Consider whether `emulator_reset()` needs a testable variant that doesn't reload ROM/labels
- At minimum, add `T95a` (P0): Verify RES pin behavior through `emulator_step()` -- set `M6502_RES` on pins, tick forward, verify CPU re-fetches reset vector

### 4. Bus decode mask `0xFFF0` for TTY incorrectly selects 16 registers, not 4

**Severity**: Critical

`emulator.cpp` line 142: `BUS_DECODE(addr, 0xC100, 0xFFF0)` matches addresses `0xC100-0xC10F` (16 addresses). But the TTY device only has 4 registers (0-3). `dev_reg` is computed as `(addr - 0xC100) & 0x00FF`, so addresses `0xC104-0xC10F` will hit the `default` case in `tty_decode()`.

The spec does not test what happens at `0xC104` through `0xC10F`. These are phantom register addresses that hit the TTY decode path.

**Recommendation**: Add:
- `T78a` (P1): Access TTY at address 0xC104 through 0xC10F -- verify no side effects, read returns 0x00 (default case)
- `T67a` (P1): Access at 0xC110 -- verify it does NOT hit TTY decode (falls through to plain RAM)

### 5. `IRQ_SET` macro has operator precedence bug -- no test catches it

**Severity**: Critical

`emulator.cpp` line 24: `#define IRQ_SET(bit) mem[0x00FF] = (mem[0x00FF] | 0x01 << bit)`

Due to C operator precedence, `0x01 << bit` is evaluated before `|`, which happens to be correct. But `emu_clr_irq` (line 52) uses `~(0x01 << bit)` which is also correct. However, the spec notes this in the T69/T70 descriptions but only tests bit=1. No test exercises bit=0 or bit>1.

Since the IRQ register is designed as a bitmask (multiple bits for multiple interrupt sources), the bit=0 case and multi-bit case must be tested.

**Recommendation**:
- `T69a` (P0): `emu_set_irq(0)` -- verify `mem[0x00FF] & 0x01`
- `T69b` (P1): `emu_set_irq(0)` then `emu_set_irq(1)` -- verify `mem[0x00FF] == 0x03`
- `T70a` (P1): Set bits 0 and 1, clear bit 1 only -- verify bit 0 remains set

---

## Important Issues

### 6. `emulator_step()` calls `tty_tick()` which calls `tty_kbhit()` / `select()` on every tick

**Severity**: Important

Every call to `emulator_step()` invokes `tty_tick()`, which calls `tty_kbhit()`, which calls `select(1, &fds, NULL, NULL, &tv)` on file descriptor 0 (stdin). In a test environment (especially CI), this system call on every tick:
- Adds measurable overhead (thousands of syscalls for integration tests)
- Behavior may differ across environments (piped stdin, redirected input, no TTY)
- If stdin has unexpected data in CI, it gets consumed into `tty_buff`, corrupting test state

The spec acknowledges `tty_kbhit()` returns 0 with no input (Section 5.3) but does not address the risk of stdin having data in CI environments, nor the performance cost.

**Recommendation**:
- Document this risk explicitly in the spec
- Consider a `tty_tick` stub for integration tests that need deterministic behavior
- At minimum, add a test that verifies `tty_tick()` with no stdin data does not modify `tty_buff` or set IRQ

### 7. `emulator_setbp()` has no test for clearing breakpoints

**Severity**: Important

The spec tests T100 (parsing `"$D000 $D005 $D00A"` sets bp_mask). But there is no way to CLEAR individual breakpoints through `emulator_setbp()`. Looking at the code (line 181-203), the current `emulator_setbp()` only sets bits, never clears them. The old version (`emulator_setbp_old`, line 205) cleared all breakpoints before parsing.

The spec has no test for:
- Calling `emulator_setbp()` twice -- do old breakpoints persist? (Yes, they do)
- Clearing a specific breakpoint (no API exists for this)
- `emulator_setbp("")` -- empty string (returns immediately via `offset == 0`)
- `emulator_setbp("garbage")` -- no valid numbers (returns via `offset == 0`)

**Recommendation**:
- `T100a` (P1): Call `emulator_setbp("$D000")`, then `emulator_setbp("$D005")` -- verify both `bp_mask[0xD000]` and `bp_mask[0xD005]` are true (breakpoints accumulate)
- `T100b` (P1): `emulator_setbp("")` -- verify no crash, no breakpoints changed
- `T100c` (P2): `emulator_setbp("garbage")` -- verify no crash

### 8. `emulator_logbp()` and `emu_labels_console_list()` are untested

**Severity**: Important

Both functions produce output through `gui_con_printmsg()`, which is captured by the stub buffer. These are testable behaviors that verify the debug/inspection interface works correctly. Neither has any test coverage.

**Recommendation**:
- `T100d` (P1): Set breakpoints at 0xD000 and 0xD005, call `emulator_logbp()`, verify console buffer contains both addresses
- `T94a` (P1): Add labels, call `emu_labels_console_list()`, verify console buffer output contains label text

### 9. Frame buffer address range is 0xC000-0xC0FF (256 bytes) but address 0xC000 is also `TXT_CTRL` per devices.inc

**Severity**: Important

`devices.inc` defines `TXT_CTRL` at `$C000` and `TXT_BUFF` at `$C001`. But the emulator's bus decode at line 127 treats the entire `0xC000-0xC0FF` range as frame buffer with `BUS_DECODE(addr, 0xC000, 0xFF00)`. This means `0xC000` is frame_buffer[0], not a separate control register.

This discrepancy between firmware expectations (separate ctrl/buff) and emulator implementation (flat 256-byte buffer) should be tested to document the actual behavior. The spec tests T64-T66 cover the happy path but don't verify the firmware-emulator interface contract.

**Recommendation**:
- `T64a` (P1): Read/write at 0xC000 -- verify it hits frame_buffer[0] (documenting that TXT_CTRL is not a distinct register in the emulator)

### 10. `CpuFixture::run_until_sync()` has a hardcoded 100-tick timeout -- no test verifies timeout behavior

**Severity**: Important

If a test program causes an infinite loop (e.g., `JMP` to self), `run_until_sync()` silently returns after 100 ticks without any indication of failure. The returned tick count is 100, but tests may not check this.

**Recommendation**:
- Document the 100-tick safety limit in the spec
- Add a helper: `bool timed_out() { return !(pins & M6502_SYNC); }` -- tests can assert they did NOT time out
- `T40a` (P2): Intentional infinite loop (`JMP` to self) -- verify `run_until_sync()` returns 100 and sync is NOT set

### 11. No tests for `emu_dis6502_log()` -- the console disassembly command

**Severity**: Important

`emu_dis6502_log()` is the entry point for the console `d` command. It uses `range_helper()` to parse address ranges, calls `emu_dis6502_decode()` for each address, formats output, and pushes to `gui_con_printmsg()`. The stub captures this output. Zero test coverage.

**Recommendation**:
- `T89a` (P1): Load known bytes at an address range, call `emu_dis6502_log("$0400+$05")`, verify console buffer contains expected disassembly lines

### 12. No BCD (decimal mode) tests

**Severity**: Important

The 6502 has a decimal mode flag (`SED`/`CLD`). ADC and SBC behave differently in BCD mode. The `m6502_desc_t` struct has a `bcd_disabled` field. `CpuFixture` initializes `desc` with memset(0), meaning BCD is enabled by default.

No tests exercise decimal mode arithmetic. If the emulator ever uses BCD (unlikely but possible in firmware), there's zero coverage.

**Recommendation**:
- `T33a` (P2): SED; CLC; LDA #$15; ADC #$27 -- verify a()==0x42 (BCD result)
- `T34a` (P2): SED; SEC; LDA #$50; SBC #$18 -- verify a()==0x32 (BCD result)

---

## Minor Issues

### 13. P2 tests that should be P1: ROL and ROR (T51, T52)

**Severity**: Minor (priority misalignment)

ROL and ROR are used in multiplication/division routines and shift-based algorithms. They are common in 6502 firmware (e.g., address calculation, bit manipulation). Marking them P2 undervalues their importance relative to instructions like BNE (T43, P1), which is just the complement of BEQ (already covered at P0).

**Recommendation**: Promote T51, T52 to P1.

### 14. Missing addressing mode coverage

**Severity**: Minor

The CPU tests cover immediate, zero-page, absolute, (indirect,X), and (indirect),Y. Missing modes with no test at any priority:
- Zero-page,X (e.g., LDA $10,X -- opcode 0xB5)
- Zero-page,Y (e.g., LDX $10,Y -- opcode 0xB6)
- Absolute,X (e.g., LDA $1234,X -- opcode 0xBD)
- Absolute,Y (e.g., LDA $1234,Y -- opcode 0xB9)
- Absolute,X page-crossing (adds an extra cycle -- important for timing accuracy)

The disassembler tests cover zero-page,X formatting (T83) but no CPU execution test validates these modes.

**Recommendation**: Add P1 tests:
- `T29a` (P1): LDA $1234,X with X=2 -- verify a() reads from 0x1236
- `T29b` (P1): LDA $1234,Y with Y=3 -- verify a() reads from 0x1237
- `T28a` (P1): LDA $10,X with X=5 -- verify a() reads from 0x15
- `T29c` (P2): LDA $10FF,X with X=1 -- page boundary crossing (address wraps to 0x1100)

### 15. `EmulatorFixture` does not reset `tick_count` documentation mismatch

**Severity**: Minor

The fixture code in Section 6.2 resets `tick_count = 0` in the constructor. However, `tick_count` is incremented by `emulator_step()` (line 148). If a test relies on tick_count for timing verification, the fixture must ensure it starts at 0. This is actually correct in the spec -- calling it out as a verification that this was intentional.

No tests actually USE `tick_count`. Consider adding at least one test that verifies `tick_count` increments by 1 per `emulator_step()` call.

**Recommendation**:
- `T96a` (P2): After N calls to `emulator_step()`, verify `tick_count == N`

### 16. `cur_instruction` tracking is tested via `emulator_getci()` but the update logic is fragile

**Severity**: Minor

`emulator_step()` lines 81-84: `cur_instruction` is set to `m6502_pc(&cpu)` when `addr == m6502_pc(&cpu)`. This condition relies on the bus address matching PC, which only happens during opcode fetch. This is correct for most cases but could be wrong during interrupt vectoring or DMA.

The spec has no test that verifies `emulator_getci()` returns the correct address across instructions.

**Recommendation**:
- `T96b` (P1): Run a two-instruction program, verify `emulator_getci()` returns the address of the second instruction after it begins executing

### 17. Test independence: `EmulatorFixture` resets global state but `labels[]` array is NOT reset

**Severity**: Minor (test independence risk)

The `EmulatorFixture` constructor (Section 6.2) resets `mem`, `frame_buffer`, `bp_mask`, `tick_count`, `bp_enable`, and re-initializes CPU. It does NOT call `emu_labels_clear()`. If a label test runs before a disassembler test that calls `emu_dis6502_decode()` (which calls `emu_labels_get()`), residual labels could affect disassembly output.

**Recommendation**: Add `emu_labels_clear()` to `EmulatorFixture` constructor. Update the spec.

### 18. `tty_buff` is a `std::queue` but test access strategy is incomplete

**Severity**: Minor

Section 6.4 identifies that `tty_buff` is file-static and proposes two approaches: (a) add `tty_inject_char()` accessor, or (b) test through bus path only. The spec chooses (b) for v1.

However, tests T74, T75, T76 explicitly say "Push byte to buffer" -- this is impossible without option (a) or accessing through `tty_tick()` which requires stdin input. The spec contradicts itself.

**Recommendation**: Accept that option (a) -- adding `tty_inject_char()` and `tty_buff_count()` -- is required for T74-T76 to work. Update the spec to reflect this as a minimal production code change, or redesign these tests to work through `emulator_step()` with a stubbed `tty_tick()`.

### 19. No test for `emu_bus_read()` utility function

**Severity**: Minor

`emu_bus_read()` (line 45-47) is a public function declared in `emulator.h`. It returns the RW state of global `pins`. No test covers it.

**Recommendation**:
- `T62a` (P2): After a write step, verify `emu_bus_read()` returns false. After a read step, verify it returns true.

---

## Nitpicks

### 20. `htoi()` returns `int`, not `uint16_t` -- potential sign issues

**Severity**: Nitpick

`htoi()` in `utils.cpp` returns `int`. For values like 0xFFFF, the return value is 65535 as int (fine on 32-bit int). But the function is used in `emu_labels_add(htoi(addr), label)` where the first parameter is `uint16_t`. This works but is fragile. The test T21 verifies `htoi("D000")` returns `0xD000` -- good. But T22 could also check `htoi("FFFF")` to verify no sign truncation.

### 21. Test naming: T62-T70 are "bus" tests but T69-T70 test IRQ register functions directly

**Severity**: Nitpick

T69 (`emu_set_irq`) and T70 (`emu_clr_irq`) call functions directly, not through the bus. They're utility/register tests that happen to be in the bus suite. Consider moving to a separate "irq" sub-suite or renaming for clarity.

### 22. `my_itoa` size parameter is not bounds-checked

**Severity**: Nitpick

`my_itoa(buf, val, size)` writes `size` hex characters plus a null terminator. If `buf` is too small, it overflows. No test verifies behavior with `size=0` or large `size`. Low priority but worth a P2 test for robustness:
- `T04a` (P2): `my_itoa(buf, 0, 0)` -- verify no crash, buf[0] == '\0'

### 23. `range_helper` comma separator not tested

**Severity**: Nitpick

The memory dump window uses `"$022d+$25,$0+$10"` as a range string (line 274 of emulator.cpp). The comma acts as a separator between multiple ranges. `range_helper` returns the number of characters consumed, and the caller loops calling it. But no test verifies the comma-separated multi-range case works end to end.

**Recommendation**:
- `T18a` (P2): Parse `"$100-$1FF,$200-$2FF"` -- verify second call to `range_helper` on remaining string parses correctly

---

## Firmware Test Plan Gaps (Section 8)

### 24. Missing firmware test: `_tty_peekc` with carry flag not set

**Severity**: Important

`_tty_peekc` (tty.s lines 46-51) uses `SBC rb_start` without first executing `SEC`. This means the carry flag from whatever ran before affects the subtraction result. If carry is clear, the result is off by one. FW-10 and FW-11 test `_tty_peekc` but don't mention the carry flag precondition.

**Recommendation**: Add FW-10a: Call `_tty_peekc` with carry flag clear -- verify the count is wrong (documenting the bug), or verify the calling convention requires SEC before calling.

### 25. Missing firmware test: `tty_recv` boundary -- `CMP #$E1` magic number

**Severity**: Important

`tty_recv` (tty.s lines 67-88) has a check `CMP #$E1` (value -31 unsigned, or buffer length - 1 in two's complement). This is a "buffer full" guard. If `rb_len` changes from 32, this hardcoded `#$E1` breaks. FW-3 tests "full buffer discard" but the plan doesn't specify testing at the exact boundary where `CMP #$E1` fires. The magic constant should be validated.

**Recommendation**: Add FW-3a: Fill buffer to exactly 31 characters, add one more -- verify it's accepted. Add FW-3b: Fill to 32 -- verify next char is discarded. This validates the magic `#$E1` / `#$FF` boundary logic.

### 26. Missing firmware test: `_tty_getc` wrap-around

**Severity**: Minor

FW-8 tests "with data" and FW-9 tests "empty buffer". Neither tests `_tty_getc` when `rb_start` wraps past `rb_len`. The wrap logic (lines 58-60: `CPY rb_len / BNE @cont / LDY #$00`) needs a test where start reaches 31 and wraps to 0.

**Recommendation**: Add FW-8a: Fill and drain buffer until `rb_start` wraps past `rb_len` -- verify character returned correctly after wrap.

### 27. Missing firmware test: BRK instruction behavior in interrupt handler

**Severity**: Minor

FW-14 tests `BRK` traps to `brken`. But the 6502 BRK instruction pushes PC+2 (not PC+1) and sets the B flag. The firmware's IRQ handler at the shared `$FFFE` vector needs to distinguish BRK from IRQ by checking the B flag on the stacked status register. No firmware test validates this distinction.

**Recommendation**: Add FW-14a: Trigger IRQ and BRK in sequence, verify the handler correctly identifies each.

---

## Structural Observations

### 28. The `emulator_setbp_old()` function is dead code with no test coverage

The spec does not mention `emulator_setbp_old()` at all. It is dead code in production. If it is kept for reference, that's fine, but it should be explicitly excluded from test scope.

### 29. `pc_mask[]` and `label_mask[]` globals are declared but appear unused

`pc_mask[65536]` and `label_mask[65536]` exist in `emulator.cpp` (lines 39-40) with a TODO comment. They are never written to or read from anywhere in the codebase. The spec does not mention them. No action needed, but worth noting as technical debt.

### 30. Write-to-device path writes to BOTH `mem[]` AND device

The spec correctly notes this in T68. However, this means `mem[0xC000]` through `mem[0xC0FF]` contain stale copies of frame buffer data, and `mem[0xC100]` through `mem[0xC10F]` contain stale TTY register writes. Tests should be careful about which source of truth they verify. The spec handles this correctly for the tests listed, but future test authors should be warned.

---

## Summary

| Severity | Count | Key themes |
|----------|-------|------------|
| Critical | 5 | Empty-queue UB in TTY, IRQ pipeline undertested, reset untested, bus decode address range, IRQ bitmask |
| Important | 7 | stdin in CI, breakpoint clearing, console output functions, frame buffer mapping, BCD mode, `tty_buff` access contradiction |
| Minor | 7 | Priority misalignment (ROL/ROR), missing addressing modes, label state leakage, tick_count, cur_instruction, emu_bus_read |
| Nitpick | 4 | htoi return type, suite naming, my_itoa bounds, range_helper comma |
| Firmware plan | 4 | Carry flag bug in peekc, magic constant in recv, getc wrap, BRK vs IRQ |

The spec is well-organized and thorough for a v1. The most urgent gaps are the undefined behavior crash path in TTY register 3 (empty queue read), the complete absence of `emulator_reset()` testing, and the thin IRQ coverage relative to the complexity of the interrupt pipeline in `emulator_step()`.
