# N8 Machine Migration Spec — Multi-Agent Analysis Report

**Date:** 2026-02-22
**Author:** Claude Opus 4.6 (synthesis pass)
**Subject:** Comparative analysis of three specialist specs and two independent reviews for the N8 machine memory map migration and video/keyboard implementation.

---

## 1. Process Overview

This report documents a structured multi-agent specification process for two major N8 Machine tasks:

1. **Migrate the emulator to the proposed memory map** (including firmware)
2. **Implement video text mode and keyboard input**

The process consisted of five rounds:

| Round | Agents | Output |
|-------|--------|--------|
| 1. Initial specs | 3 domain specialists | `spec_a_bus_architecture.md`, `spec_b_graphics_pipeline.md`, `spec_c_integration_testing.md` |
| 2. Cross-revision | 3 specialists (each reads peers) | `spec_a_revised.md`, `spec_b_revised.md`, `spec_c_revised.md` |
| 3. Independent review | 2 reviewers | `review_correctness.md`, `review_implementability.md` |
| 4. Synthesis | This report + final spec | `report.md`, `final_spec.md` |

Each specialist had a different domain focus:
- **Agent A** — Bus architecture, device registers, IRQ routing
- **Agent B** — Graphics pipeline, rendering, SDL2 input
- **Agent C** — Firmware toolchain, testing strategy, GDB stub

---

## 2. Individual Spec Summaries

### 2.1 Spec A — Bus Architecture

**Strengths:**
- Clear bus decode mask math with worked examples
- Best rationale for ROM migration timing (Phase 4, before video/keyboard) — clearing the device register address space of ROM overlap before implementing devices there
- Detailed scroll function implementations with `safe_rows()` guard
- Good operator-precedence fix for IRQ_SET macro
- Caught the GDB callback problem for frame buffer reads

**Weaknesses:**
- Deferred `frame_buffer[]` separation to Phase 7, creating a "big bang" phase that combines backing store separation + bus decode restructuring + rendering pipeline + GDB callbacks + scroll function rewrites
- Reversed its own position on the device router in reconciliation notes ("use BUS_DECODE, not a router") after having proposed the router in the original spec — creating internal inconsistency
- Omitted `kbd_tick()`, which the correctness review identified as a functional bug

**Phase count:** 10 phases, 276 final tests.

### 2.2 Spec B — Graphics Pipeline

**Strengths:**
- Most detailed rendering pipeline architecture with data flow diagrams
- Performance budget table quantifying rendering overhead (<1ms per frame)
- Complete shifted-symbol key translation table for SDL→N8 mapping
- Frame-counter-based cursor flash (deterministic, no timer dependency)
- `fb_dirty` comparison check optimization (skip rasterization when value unchanged)
- `!event.key.repeat` filter for SDL key events
- Phosphor color as a UI-selectable option

**Weaknesses:**
- Also omitted `kbd_tick()` (same bug as Spec A)
- Initially proposed a GPU font atlas texture that was unnecessary with CPU rasterization (correctly dropped in revision)
- Did not address `machine.h` disposition when creating `n8_memory_map.h`

**Phase count:** 10 phases (reduced from 12 in revision), 273 final tests.

### 2.3 Spec C — Integration & Testing

**Strengths:**
- Most comprehensive test strategy with 60 new tests (vs 55 and 52)
- Only spec to include `kbd_tick()` for IRQ reassertion — and showed its work (initially agreed with A/B, then caught the error and corrected)
- Disciplined test registry with sequential IDs (T110+) and running totals per phase
- Explicit per-phase rollback procedures
- Risk matrix with likelihood/impact ratings
- Complete file change inventory classifying every project file as new/modified/unchanged
- ROM write protection included (3 lines of code, prevents a class of bugs)
- `N8_LEGACY_*` defines for clean transition tracking
- Appendices documenting bus decode ordering constraints and cc65 segment budgets

**Weaknesses:**
- Originally deferred `frame_buffer[]` separation, then reversed (correctly) in revision
- Original four-region ROM linker config was over-engineered for ~1KB firmware (correctly simplified in revision)
- Rendering pipeline explicitly out of scope (reasonable for the testing specialist, but means the complete spec requires supplementing from Spec B)

**Phase count:** 11 phases (separating playground migration and E2E validation), 281 final tests.

---

## 3. Key Disagreements and Resolution

The cross-revision process resolved approximately 85% of disagreements. The remaining conflicts required the reviews and this synthesis to settle.

### 3.1 The `frame_buffer[]` Question (CRITICAL)

This was the central architectural dispute.

| | Spec A | Spec B | Spec C |
|---|--------|--------|--------|
| **When** | Phase 7 (with rendering) | Phase 3 (with FB expansion) | Phase 3 (with FB expansion) |
| **Rationale** | User said "use mem[]" | hardware.md says separate store | hardware.md says separate store |

**Context:** The user previously removed the old `frame_buffer[256]` in favor of `mem[]` to simplify the codebase. The hardware spec (`hardware.md`) simultaneously says "routed to a separate backing store (not backed by main RAM)."

**Resolution: Phase 3 separation (Specs B/C).**

The user's directive was made in a different context — simplifying away a redundant 256-byte array that duplicated `mem[]`. The new 4KB frame buffer with bus decode intercept is a different architectural element. Deferring to Phase 7 creates unnecessary churn: scroll functions written against `mem[]` in Phase 5 would need rewriting in Phase 7. Both reviews independently recommended Phase 3.

### 3.2 Device Router vs BUS_DECODE (HIGH)

| | Spec A (revised) | Spec B (revised) | Spec C (revised) |
|---|--------|--------|--------|
| **Approach** | Individual BUS_DECODE macros | Slot-based router | Slot-based router |

Spec A's revised reconciliation notes explicitly rejected its own original router design in favor of BUS_DECODE macros, while both B and C adopted the router from Spec A's original. This created a 2-vs-1 split.

**Resolution: Slot-based router (Specs B/C).**

The router is a single range check + switch statement. Adding a device is one `case` line. With 4 devices in a contiguous 2KB region, per-device BUS_DECODE blocks are repetitive and require each block to independently compute the register offset. The router computes it once. The implementability review also favored the router.

### 3.3 Constants Header Location (HIGH)

| | Spec A | Spec B | Spec C |
|---|--------|--------|--------|
| **File** | Expand `machine.h` | New `n8_memory_map.h` | New `n8_memory_map.h` |

**Resolution: New `n8_memory_map.h` (Specs B/C).**

`machine.h` currently contains a single constant (`total_memory`). Adding 60+ `#define` macros fundamentally changes its character. A dedicated file with a descriptive name is cleaner, and `machine.h` can remain undisturbed. The firmware parallel file is `firmware/n8_memory_map.inc` for naming consistency.

### 3.4 `kbd_tick()` — The Functional Bug (CRITICAL)

Only Spec C included `kbd_tick()`. The correctness review confirmed this is a **functional bug** without it:

1. SDL event fires between frames → `kbd_inject_key()` sets IRQ bit 2 in `mem[$D800]`
2. Next `emulator_step()` → `IRQ_CLR()` zeros `mem[$D800]`
3. `tty_tick()` reasserts TTY bit if needed. **Nothing reasserts keyboard bit.**
4. IRQ check sees `mem[$D800] == 0` → no IRQ → keyboard interrupt lost

The TTY already has `tty_tick()` for exactly this reason. The keyboard must follow the same pattern.

**Resolution: Include `kbd_tick()` (Spec C).**

### 3.5 ROM Migration Timing (HIGH)

| | Spec A | Spec B | Spec C |
|---|--------|--------|--------|
| **Phase** | 4 (before video/kbd) | 6 (after kbd) | 6 (after kbd) |

**Resolution: Phase 4 (Spec A).**

Spec A's reasoning is compelling: the device register space at `$D800-$DFFF` overlaps the current ROM region. Until ROM is moved, `emulator_loadrom()` writes firmware bytes into `mem[$D800-$DFFF]`. While the device router overrides reads, having stale firmware bytes in the device address range is confusing and could mask bus decode bugs during development. Moving ROM early clears the space cleanly before devices are implemented there. The implementability review also favored this ordering.

### 3.6 ROM Write Protection

| | Spec A | Spec B | Spec C |
|---|--------|--------|--------|
| **Decision** | Deferred | Optional | Included |

**Resolution: Include it (Spec C).** It's 3 lines of code: `if (addr < N8_ROM_BASE) { mem[addr] = ...; }` in the generic write path. Only affects CPU bus writes — direct `mem[]` writes (tests, ROM loader, GDB) are unaffected. Prevents firmware bugs with zero risk to tests.

---

## 4. Review Findings

### 4.1 Correctness Review Summary

The correctness reviewer found 4 CRITICAL, 5 HIGH, and 8 MEDIUM findings.

**Critical findings (resolved above):**
1. `frame_buffer[]` introduction phase disagreement → Phase 3
2. `kbd_tick()` missing from Specs A/B → Include it
3. These same issues under different finding numbers (4.1 = 1.3, 6.1 = 1.10)

**Notable HIGH findings:**
- Constants header name disagreement → `n8_memory_map.h`
- Device router disagreement → Router
- Phase ordering divergence → Resolved per Section 3
- `EmulatorFixture` not updated after `frame_buffer[]` separation — **none of the three specs explicitly addressed this**. The fixture constructor must `memset(frame_buffer, 0, N8_FB_SIZE)` and call `video_init()` / `kbd_init()` when those phases land.

**Notable MEDIUM findings:**
- Scroll with `width=0` causes `uint8_t` underflow in `memmove()` — Spec A's `safe_rows()` / `w < 2` guards handle this; Spec C's inline `active > N8_FB_SIZE` check does not catch all cases. Final spec uses Spec A's guards.
- GDB reads of device registers return stale `mem[]` values, not actual register state — known limitation, documented as acceptable for a debugger.
- `N8_RAM_START` disagreement (`0x0200` in A/B vs `0x0400` in C) — use `0x0400` per proposed memory map.

### 4.2 Implementability Review Summary

**Key findings:**
- Spec A Phase 7 is overloaded — combines 6+ concerns in one phase. Specs B/C correctly split them.
- `EmulatorFixture` initialization gap — fixture does not call `emulator_init()`, so `video_init()` and `kbd_init()` must be added explicitly to the constructor.
- Spec A's scroll functions use per-row `memcpy()` (correct for non-overlapping row copies); Spec C uses single `memmove()` (also correct). Both work; Spec C is more concise.
- The `emu_display.cpp` module correctly excluded from test build (OpenGL dependency).
- Current Makefile `SOURCES` uses explicit file lists, not wildcards — each new source file must be manually added.

**Effort estimate:** ~35 hours without rendering, ~43 hours with rendering. Roughly 5-6 working days.

**Recommended implementation path** (from the review): Cherry-pick Spec C for overall structure (constants, router, kbd_tick, test registry), Spec A for ROM migration timing and scroll code, Spec B for rendering pipeline and dirty flag optimization.

---

## 5. My Analysis

### 5.1 Process Effectiveness

The multi-agent process worked well. Key observations:

**What worked:**
- Three independent perspectives caught different issues. Spec C's `kbd_tick()` discovery is the standout — a functional bug that two other specialists missed because they focused on the event-driven SDL model without considering the per-tick IRQ clear mechanism.
- The cross-revision round resolved ~85% of disagreements. Each specialist adopted good ideas from peers: Spec A adopted Spec B's shifted-symbol table, Spec B adopted Spec A's GDB callback fix, Spec C adopted Spec A's router design.
- The two-reviewer structure (correctness vs implementability) provided complementary coverage. The correctness reviewer found architectural bugs; the implementability reviewer found practical gaps (EmulatorFixture, Makefile patterns).

**What didn't work as well:**
- Spec A reversed its own position on the device router during reconciliation, creating confusion. The revised notes said "no router" while the body still implied one. This suggests the reconciliation process can introduce inconsistencies when a spec tries to accommodate peer feedback without fully rewriting.
- The `frame_buffer[]` question remained unresolved across all three revised specs because the user's prior directive and the hardware spec pulled in opposite directions. Neither the specs nor the reviews could fully resolve this without understanding the user's intent behind the earlier directive.

### 5.2 Architectural Assessment

The proposed 10-phase migration is well-structured. The core insight — move one device at a time, test at each step — is sound incremental engineering.

**Strongest architectural decisions:**
- The device router with slot-based dispatch is clean and extensible
- Separating `frame_buffer[]` early (Phase 3) prevents cascade rewrites
- `kbd_tick()` following the same pattern as `tty_tick()` is consistent and correct
- The dual-path ROM loader for backward compatibility during transition is pragmatic

**Areas of concern:**
- The rendering pipeline (Phase 7 in the final spec, or a separate effort) is the riskiest single phase — it introduces OpenGL texture management, font integration, and cursor compositing. Keeping it as a separate effort after the bus migration lands is the right call.
- GDB visibility into device register state is limited. `gdb_read_mem($D840)` returns stale `mem[]` values, not the actual `vid_regs[]` contents. This is acceptable for now but should be documented and addressed in a future GDB enhancement.
- VID_OPER=$00 means "scroll up" but $00 is also the default register value. An accidental write of 0 to this register would trigger an unintended scroll. This is a hardware design consideration, not a spec bug, but worth noting for firmware developers.

### 5.3 Test Strategy Assessment

Spec C's test strategy is the strongest of the three:
- Sequential IDs (T110+) avoid collisions with existing tests (T01-T101a)
- Running totals per phase make it easy to verify completeness
- Tests like T121 (frame buffer isolation from mem[]) and T165-T166 (kbd_tick IRQ reassertion) test the most important correctness properties
- The per-phase gate structure ("all prior tests + new tests pass") provides clear stop/go criteria

The final spec adopts Spec C's test registry as the baseline and adds Spec A's T146 (scroll overflow guard) for completeness.

---

## 6. Final Decisions Summary

| Decision | Choice | Source | Rationale |
|----------|--------|--------|-----------|
| Constants header | `src/n8_memory_map.h` | B/C | Dedicated file, clear purpose |
| Assembly constants | `firmware/n8_memory_map.inc` | C | Mirrors C++ header name |
| Device routing | Slot-based router | B/C (from original A) | Clean, extensible, 1 case per device |
| `frame_buffer[]` timing | Phase 3 | B/C | hardware.md spec, avoids Phase 7 churn |
| `kbd_tick()` | Included | C | Required by IRQ clear-reassert architecture |
| ROM migration timing | Phase 4 (before video/kbd) | A | Clears device space before implementing devices |
| ROM write protection | Included | C | 3 lines, prevents firmware bugs |
| `N8_RAM_START` | `0x0400` | C | Matches proposed memory map |
| `N8_FB_SIZE` in Phase 0 | `0x1000` (target) | C | Avoids mid-migration constant changes |
| Keyboard file naming | `emu_kbd.*` | C | Consistent with `emu_tty.*` |
| Scroll fill character | `0x00` | B/C | Hardware-like (memory clear to zero) |
| Rendering pipeline | Separate effort | C's scoping, B's design | Reduces Phase 7 risk |
| Total new tests | 60 | C (superset) | Includes all critical correctness tests |
| Final test count | 281 + 10 E2E | C | Most comprehensive coverage |

---

## 7. Conclusion

The three-specialist, two-reviewer process produced a robust migration spec. The most valuable outcome was Spec C's discovery of the `kbd_tick()` requirement — a subtle functional bug that would have caused keyboard interrupts to be silently lost. This alone justified the multi-agent approach.

The final spec (in `final_spec.md`) synthesizes the best of all three perspectives: Spec A's ROM migration timing and scroll guards, Spec B's rendering architecture and keyboard translation, and Spec C's test strategy, device router, and `kbd_tick()` implementation. The result is a 10-phase incremental migration plan that takes the emulator from 221 to 281 automated tests, with the rendering pipeline as a follow-on effort.
