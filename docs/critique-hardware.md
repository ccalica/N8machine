# Hardware Portability Critique: GDB Stub Spec for N8Machine

**Reviewer perspective**: Hardware engineer / embedded systems developer planning to port this GDB stub to physical 6502 hardware over UART.

---

## Issue 1: The RDY Pin Exists and Is Not Used for Halt — Critical

**Decisions affected**: D40, D23, D39

The spec halts the CPU by simply stopping calls to `m6502_tick()` (setting `run_emulator = false`). This is an emulator-only mechanism with zero equivalent on real silicon.

The real 6502 has a **RDY pin** (pin 28) specifically designed for this purpose. When RDY is asserted during a read cycle, the CPU holds its state. The m6502.h emulator implements this faithfully at line 722:

```cpp
if ((pins & (M6502_RW|M6502_RDY)) == (M6502_RW|M6502_RDY)) {
    M6510_SET_PORT(pins, c->io_pins);
    c->PINS = pins;
    c->irq_pip <<= 1;
    return pins;
}
```

**The problem**: The spec's halt mechanism (`run_emulator = false`) cannot be ported to hardware. The spec should at minimum document this gap and consider an alternative `halt()` callback implementation that asserts M6502_RDY on the pins and continues ticking. This would make the emulator's behavior match what hardware debug probes actually do, and would validate the halt-state interaction with IRQ/NMI pipelines.

**Note on NMOS vs CMOS**: On NMOS 6502, RDY only halts on read cycles. On WDC 65C02, RDY halts on both read and write. D23 (initial halt alignment) could stop on a write cycle on NMOS hardware. The guard loop must account for this.

**Recommendation**: Add an alternate `halt()` callback implementation using RDY, or document that the hardware port will require a fundamentally different halt mechanism. Add a test case for halt during write cycle.

---

## Issue 2: Memory Access Model — Bypass Bus Decode Will Not Port — Critical

**Decisions affected**: D17, D18, D19

The spec's `read_mem(addr)` is `return mem[addr]` — direct array access bypassing all bus decode logic. On physical hardware, there is no "bypass bus decode" path. Every memory read goes through the address bus, and the bus decode logic is hardwired.

On hardware, reading TTY_IN_DATA ($C103) **will** pop from the FIFO (the UART chip does not know or care that the read is from a debugger). This is a fundamental hardware reality that cannot be worked around without additional hardware.

**The spec's T25 test case** (read device-mapped region without side effects) is impossible on bare hardware.

**Recommendations**:
1. Document that D17 is emulator-only
2. Add a `read_mem_mode` field to the callback struct (`MEM_BYPASS` vs `MEM_BUS`)
3. Implement `qXfer:memory-map:read` (GDB memory map reporting) so GDB knows which regions are I/O
4. Consider a `read_mem_safe(addr)` variant that returns error for I/O regions on hardware

---

## Issue 3: IRQ Freezing While Halted Is Incorrect for Hardware — Important

**Decisions affected**: D40

On real hardware, IRQ and NMI are continuous electrical signals that do not freeze when the CPU is halted. The emulator's approach of stopping `m6502_tick()` entirely freezes the `irq_pip` and `nmi_pip` shift registers. Note that m6502.h line 725: `c->irq_pip <<= 1` runs inside the RDY block — meaning on real hardware (and in the emulator when using RDY), the IRQ pipeline continues even while halted.

**Recommendation**: If the emulator used RDY-based halting (Issue 1), the irq_pip would continue to be shifted, matching hardware behavior. At minimum, document that halt/resume on hardware will have different IRQ timing characteristics. Add a test case for "IRQ arrives while halted, fires on resume."

---

## Issue 4: PC Write Prefetch Sequence (D16) — Important

**Decision affected**: D16

The prefetch sequence is correct for the emulator but nonsensical on hardware. On a physical 6502, you cannot "set pins" — the CPU owns the address bus. To redirect execution, you would:
1. Force a JMP instruction into the data bus (bus stuffing)
2. Provide the target address bytes on subsequent bus cycles

The callback-based architecture (D35) isolates this well, but test case T14 verifies implementation details rather than observable effects.

**Recommendation**: T14 should verify observable effects (PC reads back correctly, next instruction executes from new address) rather than the mechanism.

---

## Issue 5: Instruction Boundary Detection via SYNC (D20, D32) — Good, With Caveats — Minor

The spec correctly uses M6502_SYNC. On real hardware, SYNC is a physical output pin that goes high during opcode fetch — standard mechanism for ICE designs.

The breakpoint SYNC fix (D32) is well-aligned with hardware address comparator + SYNC gating.

**Caveat**: The existing `cur_instruction` tracking (emulator.cpp:81-84) uses address comparison, not SYNC. This should also be gated on SYNC for consistency.

---

## Issue 6: Signal Choices — Good — Nitpick

SIGTRAP (5), SIGILL (4), SIGINT (2) are all reasonable. One gap: no signal defined for NMI during step — should be documented.

---

## Issue 7: Missing Watchpoint Support (Z2/Z3/Z4) — Important

For hardware debugging of memory-mapped I/O, watchpoints are arguably more important than execution breakpoints. The N8 machine has devices at $C000-$C1FF. Hardware address comparator logic with R/W qualification is straightforward.

**Recommendation**: Add at minimum `Z2` (write watchpoint) support. Response signal: `T05watch:addr;`.

---

## Issue 8: No Memory Map Reporting — Important

The spec does not implement `qXfer:memory-map:read`. For hardware, this is important:
1. GDB uses memory maps to decide software vs hardware breakpoints
2. GDB avoids writing to ROM regions
3. On hardware, ROM writes are no-ops (D19 is emulator-only)

**Recommendation**: Implement `qXfer:memory-map:read` and advertise in `qSupported`.

---

## Issue 9: UART Transport Not Addressed — Important

**Decisions affected**: D1, D36

The entire transport layer is TCP-specific. For hardware, the transport is UART with interrupt-driven receive, no connection concept.

**Recommendation**: Explicitly separate "transport layer" from "protocol layer" in the spec. Note that Part 1 sections 1-2 are transport-specific and will be replaced. Sections 3-4 (packet framing, command parsing) port directly.

---

## Issue 10: Step Guard Counter May Be Too Low — Minor

**Decision affected**: D22

Guard of 16 is correct for the emulator. On hardware, bus contention, DMA, or clock stretching can add wait states. 65C02 WAI instruction waits indefinitely.

**Recommendation**: Make guard counter configurable via `gdb_stub_config_t`. Default 16 for emulator, 32+ for hardware.

---

## Issue 11: Missing Hardware-Relevant Test Cases — Important

**T-HW1: NMI during single step**
- NMI asserted between step command and completion
- PC should be at NMI handler, stop reply T05

**T-HW2: IRQ arrives while halted**
- CPU halted, IRQ becomes active, then continue
- CPU should vector to IRQ handler

**T-HW3: Reset during GDB session**
- External reset while GDB connected
- Breakpoints should survive, stub should send T05

**T-HW4: Breakpoint in IRQ handler while running**
- BP at IRQ entry, continue, IRQ fires
- BP fires, stack has correct return address

---

## Issue 12: Bus Decode Ordering Subtlety — Important

In `emulator.cpp`, writes to device regions go to both `mem[]` and the device. Reads first load from `mem[]` then get overwritten by device data. This means `mem[0xC103]` contains the last CPU-written value, not current device state. Acceptable for the emulator GDB stub but should be documented. On hardware, there is no `mem[]` backing store for device addresses.

---

## Issue 13: Prefetch Zeroes IRQ/NMI Pins — Minor

The PC write prefetch (D16) assigns `pins = M6502_SYNC`, zeroing all other pin state (IRQ, NMI). Should preserve existing IRQ/NMI state:

```cpp
pins = (pins & (M6502_IRQ | M6502_NMI)) | M6502_SYNC | M6502_RW;
```

Add test case verifying IRQ state survives PC write.

---

## Issue 14: No `qRcmd` (Monitor Command) Support — Minor

For hardware probes, `qRcmd` is essential for target-specific ops: `monitor reset`, `monitor ioread <addr>`, `monitor clock <freq>`.

**Recommendation**: Add `qRcmd` with at least `reset`. Route unknown commands through a callback for hardware extensibility.

---

## Summary Table

| # | Issue | Severity | Decisions |
|---|-------|----------|-----------|
| 1 | RDY pin not used for halt | Critical | D40, D23, D39 |
| 2 | Memory bypass won't port to hardware | Critical | D17, D18, D19 |
| 3 | IRQ freezing incorrect for hardware | Important | D40 |
| 4 | PC prefetch is emulator-only mechanism | Important | D16 |
| 5 | SYNC-based breakpoints well-aligned | Minor (good) | D20, D32 |
| 6 | Signal choices reasonable | Nitpick | D21 |
| 7 | No watchpoint support | Important | D10 |
| 8 | No memory map reporting | Important | — |
| 9 | UART transport not addressed | Important | D1, D36 |
| 10 | Step guard counter may be too low | Minor | D22 |
| 11 | Missing hardware test cases | Important | — |
| 12 | Bus decode ordering subtlety | Important | D17 |
| 13 | Prefetch zeroes IRQ/NMI pins | Minor | D16 |
| 14 | No qRcmd support | Minor | — |
