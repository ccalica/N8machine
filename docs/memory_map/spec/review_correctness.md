# Correctness & Risk Review: Revised Migration Specs A, B, C

Reviewer: Claude Opus 4.6
Date: 2026-02-22
Inputs: spec_a_revised.md, spec_b_revised.md, spec_c_revised.md, hardware.md, keycodes.md, README.md, and the actual codebase.

---

## 1. Contradictions Between Specs

### Finding 1.1 — Constants header: file name and approach still unresolved

**Severity:** HIGH

**Finding:** The three specs still disagree on where to put the constants header.

- **Spec A** (Reconciliation Notes): "Expand `src/machine.h` (Spec B's approach) but use `#define` macros (Spec A/C's approach)."
- **Spec B** (Reconciliation Note 3): "Adopt Spec A's name `src/n8_memory_map.h` with `#define` macros."
- **Spec C** (Reconciliation Note 1): "Use `src/n8_memory_map.h` with `N8_` prefixed `#define` macros."

**Evidence:** Spec A Phase 0 says "Expand existing file: `src/machine.h`" while Specs B and C Phase 0 both say "New file: `src/n8_memory_map.h`".

**Recommendation:** Pick one. The choice between expanding `machine.h` and creating `n8_memory_map.h` is cosmetic, but all three specs must agree. Spec A's reasoning (avoid adding a new file and `#include`) is pragmatic. Specs B and C's reasoning (dedicated file with clear purpose) is also valid. The implementer needs a single answer. Recommend `src/n8_memory_map.h` since two of three specs converge on it, and `machine.h` currently has only `const int total_memory = 65536;` which could be moved.

---

### Finding 1.2 — Device router vs BUS_DECODE: still unresolved

**Severity:** HIGH

**Finding:** The specs still disagree on whether to use a slot-based device router or individual `BUS_DECODE` macros.

- **Spec A** (Reconciliation Notes): "Use individual `BUS_DECODE` macros (Spec B/C approach), not a router."
- **Spec B** (Reconciliation Note 4): "Adopt Spec A's device router."
- **Spec C** (Reconciliation Note 3): "Adopt Spec A's slot-based device router."

**Evidence:** Spec A explicitly decided against the router, while Specs B and C explicitly decided for it. Further, Spec A Phase 2 uses `BUS_DECODE(addr, N8_TTY_BASE, N8_TTY_MASK)` while Specs B and C Phase 2 use `case N8_TTY_SLOT: tty_decode(pins, reg); break;` inside the router.

**Recommendation:** Pick one. The router is cleaner for 4+ devices. However, each spec's bus decode code is internally consistent; the issue is cross-spec consistency. Since two specs favor the router, adopt it.

---

### Finding 1.3 — frame_buffer[] timing: Phase 3 vs Phase 7

**Severity:** CRITICAL

**Finding:** The three specs introduce `frame_buffer[]` in different phases.

- **Spec A** (Reconciliation Notes): "Keep using `mem[]` for Phase 3. Introduce separate backing store later (Phase 5, alongside rendering pipeline)." The actual spec body then says Phase 7 introduces `frame_buffer[]`.
- **Spec B** (Reconciliation Note 1): "The separation happens in the same phase as the frame buffer expansion (Phase 3)."
- **Spec C** (Reconciliation Note 2): "Introduce `frame_buffer[]` as a separate backing store in Phase 3."

**Evidence:** Spec A Phase 3 says "No emulator code changes required for this phase" and "Continue using `mem[]` as the backing store." Spec B Phase 3 says `uint8_t frame_buffer[N8_FB_SIZE] = { };` and introduces the bus intercept. Spec C Phase 3 also introduces `frame_buffer[]` with bus intercept.

**Recommendation:** This is the central design question and all three specs must agree. See Finding 4 for detailed analysis of the consequences.

---

### Finding 1.4 — N8_RAM_START value disagrees

**Severity:** MEDIUM

**Finding:** Specs A and B define `N8_RAM_START` as `0x0200`, while Spec C defines it as `0x0400`.

**Evidence:**
- Spec A Phase 0: `#define N8_RAM_START 0x0200`
- Spec B Phase 0: `#define N8_RAM_START 0x0200`
- Spec C Phase 0: `#define N8_RAM_START 0x0400`

The proposed memory map (README.md) shows `$0200-$03FF` as "????" (reserved) and `$0400-$BFFF` as "Program RAM". All three specs agree that the linker config should use `$0400` for RAM start. The disagreement is only in the constants header.

**Recommendation:** Use `0x0400` in the header to match the linker config and proposed memory map. The value `0x0200` in Specs A and B is the current state, but Phase 0 should use the target values for constants that will be used by the linker config.

---

### Finding 1.5 — N8_FB_SIZE in Phase 0 constants disagrees

**Severity:** LOW

**Finding:** Specs A and B define `N8_FB_SIZE` as `0x0100` (current value) in Phase 0, while Spec C defines it as `0x1000` (target value).

**Evidence:**
- Spec A Phase 0: `#define N8_FB_SIZE 0x0100 // Phase 0: current 256 bytes`
- Spec B Phase 0: `#define N8_FB_SIZE 0x0100 // Phase 0: current 256 bytes`
- Spec C Phase 0: `#define N8_FB_SIZE 0x1000 // 4 KB target (current: 0x0100)`

**Recommendation:** Define target values in Phase 0 (Spec C's approach). If the constant starts at `0x0100` and is changed to `0x1000` in Phase 3, any code that uses `N8_FB_SIZE` before Phase 3 would be wrong after Phase 3. Better to define the target up front and note that not all 4KB is used until Phase 3.

---

### Finding 1.6 — Phase ordering and numbering diverge significantly

**Severity:** HIGH

**Finding:** After reconciliation, the three specs still have different phase orderings.

| Phase | Spec A | Spec B | Spec C |
|-------|--------|--------|--------|
| 3 | FB expand (mem[]) | FB expand (frame_buffer[]) | FB expand (frame_buffer[]) |
| 4 | ROM migration | Video regs | Video regs |
| 5 | Video regs | Keyboard | Keyboard |
| 6 | Keyboard | ROM migration | ROM migration |
| 7 | Rendering pipeline | Rendering pipeline | GDB stub |
| 8 | Firmware | Firmware | Firmware |
| 9 | GDB + cleanup | GDB + cleanup | Playground |
| 10 | -- | -- | E2E validation |

**Evidence:** Spec A places ROM migration at Phase 4 ("the device register space at $D800-$DFFF overlaps the current ROM region"), while Specs B and C place it after device registers (Phase 6).

**Recommendation:** The ordering disagreement is substantive. Spec A's argument for early ROM migration is that loading the 12KB firmware binary writes padding bytes into $D800-$DFFF which could mask bus decode bugs. Spec C's argument for late ROM migration is that $D000-$DFFF is understood as writable RAM/device space by the time you move ROM. Both arguments have merit. The implementer needs one ordering. Recommend Spec A's approach (ROM at Phase 4) because it eliminates the firmware-in-device-space confusion before device registers are implemented.

---

### Finding 1.7 — Firmware assembly constants file naming

**Severity:** LOW

**Finding:** Three different names proposed for the assembly-language constants file.

- Spec A: `firmware/hw_regs.inc`
- Spec B: "update the existing `devices.inc` / `devices.h`"
- Spec C: `firmware/n8_memory_map.inc`

**Evidence:** Spec A Phase 0: "New file: `firmware/hw_regs.inc`". Spec C Phase 0: "New file: `firmware/n8_memory_map.inc`". Spec B: "Rather than adding a new file, update the existing `devices.inc`".

**Recommendation:** Pick one. `firmware/n8_memory_map.inc` mirrors the C++ header name if `n8_memory_map.h` is chosen. The existing `devices.inc` is updated to `.include` it in Phase 8 across all specs, so the choice of the canonical name just needs to be consistent.

---

### Finding 1.8 — ROM write protection: deferred vs included

**Severity:** MEDIUM

**Finding:** Specs disagree on whether ROM write protection is included.

- **Spec A** (Reconciliation Notes): "Deferred. The current codebase has no ROM protection."
- **Spec B** (Phase 6): "ROM write protection (optional)" with code.
- **Spec C** (Phase 6): ROM write protection included, `addr < N8_ROM_BASE` guard in write path.

**Evidence:** Spec A defers it explicitly, Spec C includes it with code.

**Recommendation:** Include it in the ROM migration phase (Spec C's approach). The implementation is a single `if` statement. It only affects CPU bus writes, not direct `mem[]` access by tests or `emulator_loadrom()`. The risk is negligible.

---

### Finding 1.9 — Test count final totals disagree

**Severity:** MEDIUM

**Finding:** The three specs end with different test totals.

- Spec A: 276 automated + 10 manual
- Spec B: 273 automated + 10 manual
- Spec C: 281 automated + 10 manual

**Evidence:** Spec A adds 55 new tests. Spec B adds 52. Spec C adds 60. The differences come from:
- Phase 3: Spec A adds 4, Spec B adds 4, Spec C adds 7 (includes T121 fb/mem isolation, T122 reset clears, T123 fb_dirty)
- Phase 5 (Video): Spec A adds 16, Spec B adds 16, Spec C adds 17 (includes T146 scroll overflow guard)
- Phase 6 (Keyboard): Spec A adds 15, Spec B adds 15, Spec C adds 17 (includes T165-T166 kbd_tick tests)
- Phase 6/7 (ROM): Spec A adds 5, Spec B adds 5, Spec C adds 7 (includes T175 ROM protection, T176 dev bank)
- Phase 9 (GDB): Spec A adds 4, Spec B adds 5, Spec C adds 5

**Recommendation:** Adopt Spec C's test count as the superset. The extra tests (fb/mem isolation, fb_dirty, scroll overflow guard, kbd_tick reassertion, ROM protection) are all valuable.

---

### Finding 1.10 — kbd_tick() inclusion

**Severity:** CRITICAL

**Finding:** Specs A and B do not include `kbd_tick()` for per-tick IRQ reassertion. Spec C initially excluded it, then reversed itself in Reconciliation Note 11.

**Evidence:**
- Spec A Phase 6 keyboard code: no `kbd_tick()` function, no per-tick call. `keyboard_key_down()` calls `emu_set_irq(N8_IRQ_BIT_KBD)` but there is no reassertion after `IRQ_CLR()`.
- Spec B Phase 5 keyboard code: no `kbd_tick()`.
- Spec C Reconciliation Note 11: "Revised decision: Include `kbd_tick()` to reassert IRQ bit when `kbd_data_avail && kbd_ctrl & IRQ_EN`."

The current IRQ mechanism in `emulator.cpp` (line 110) calls `IRQ_CLR()` every tick, which zeros `mem[0x00FF]`. Then `tty_tick()` reasserts the TTY IRQ bit if its buffer has data. The keyboard would need the same pattern.

**Recommendation:** Spec C is correct. Without `kbd_tick()`, a keyboard IRQ is lost after one tick because `IRQ_CLR()` zeros the flags and nothing reasserts the keyboard bit. The CPU would see the IRQ pin for at most 1 tick -- likely not enough to complete the current instruction and vector to the handler. This is a functional bug. All specs must include `kbd_tick()`.

---

## 2. Correctness Against hardware.md

### Finding 2.1 — VID_CURSOR bit layout misinterpreted in Spec B rendering code

**Severity:** MEDIUM

**Finding:** Spec B's cursor rendering code reads bits incorrectly relative to `hardware.md`.

**Evidence:** `hardware.md` defines VID_CURSOR bits as:
```
Bits 0-1: MODE (00=off, 01=on steady, 10=flash, 11=reserved)
Bit 2: SHAPE (0=underline, 1=block)
Bits 3-4: RATE (00=off, 01=slow, 10=medium, 11=fast)
```

Spec B `rasterize_cursor()`:
```cpp
uint8_t mode  = cursor_style & 0x03;      // bits 0-1: correct
bool    block = (cursor_style >> 2) & 1;  // bit 2: correct
uint8_t rate  = (cursor_style >> 3) & 0x03; // bits 3-4: correct
```

The bit extraction is correct. However, the flash logic says:
```cpp
if (mode == 2) {  // flash mode
    switch (rate) {
        case 0: return;  // Rate off
```

Per hardware.md, when MODE=10 (flash) and RATE=00 (off), what should happen? The cursor mode says "flash" but the rate says "off". Spec B returns (cursor invisible). This edge case is not defined in hardware.md.

**Recommendation:** Document the behavior: MODE=flash + RATE=off = cursor invisible (or steady, either is defensible). The current Spec B behavior (invisible) is reasonable.

---

### Finding 2.2 — Scroll operation VID_OPER=$00 ambiguity

**Severity:** MEDIUM

**Finding:** `hardware.md` defines VID_OPER `$00` = Scroll up. But `$00` is also the default/reset value of registers. If firmware writes `VID_MODE=$00` and the mode handler does `video_apply_mode()`, no scroll is triggered because VID_OPER and VID_MODE are different registers. However, if firmware accidentally reads VID_OPER (which returns 0) and then writes 0 back to VID_OPER, it would trigger an unintended scroll up.

**Evidence:** All three specs implement `VID_OPER=$00` as scroll up. `hardware.md` VID_OPER values table: `$00 = Scroll up`.

**Recommendation:** This is a hardware design concern, not a spec bug. Note it as a potential pitfall for firmware developers. Consider reserving `$00` as NOP and starting scroll operations at `$01`.

---

### Finding 2.3 — VID_DEFAULT_STRIDE missing from Specs B and C

**Severity:** LOW

**Finding:** Spec A defines `N8_VID_DEFAULT_STRIDE 80` as a separate constant. Specs B and C do not define it; they hardcode `N8_VID_DEFAULT_WIDTH` for the stride reset value.

**Evidence:**
- Spec A: `#define N8_VID_DEFAULT_STRIDE 80` and `vid_regs[N8_VID_REG_STRIDE] = N8_VID_DEFAULT_STRIDE;`
- Spec B: No `N8_VID_DEFAULT_STRIDE` constant.
- Spec C: `vid_regs[N8_VID_STRIDE] = N8_VID_DEFAULT_WIDTH;`

`hardware.md`: "VID_STRIDE defaults to same as VID_WIDTH."

**Recommendation:** Spec C's approach is correct per `hardware.md` -- stride defaults to width. A separate `N8_VID_DEFAULT_STRIDE` constant is unnecessary and could diverge from the actual relationship. Use `N8_VID_DEFAULT_WIDTH` for the stride reset value.

---

## 3. Phase Ordering Risks

### Finding 3.1 — Spec A Phase 4 (ROM move) before video/keyboard creates a clean device space

**Severity:** LOW (informational)

**Finding:** Spec A moves ROM before implementing video/keyboard (Phase 4). This means that during Phases 5-6, the device register space at `$D800-$DFFF` is cleanly available -- the old ROM binary no longer writes padding into this region.

Specs B and C move ROM after device registers (Phase 6), which means during Phases 4-5, loading the 12KB firmware writes bytes into `$D800-$DFFF`. Since the device router is already in place (Phase 1), the router intercepts reads/writes for registered slots, and reserved slots return `$00`. The firmware bytes landing in `mem[$D800-$DFFF]` do not affect device behavior because device decode overrides the data bus.

**Evidence:** `emulator_loadrom()` currently does `mem[rom_ptr] = c` in a loop starting from `$D000`. After Phase 1 (device router), reads from `$D800-$DFFF` are handled by the router, not `mem[]`, so the firmware bytes in `mem[$D800-$DFFF]` are invisible to the CPU.

**Recommendation:** Both orderings work correctly. Spec A's early ROM migration is cleaner but not strictly necessary. The risk is LOW either way.

---

### Finding 3.2 — Phase 3 frame_buffer[] separation creates a structural change to emulator_step()

**Severity:** HIGH

**Finding:** Introducing `frame_buffer[]` in Phase 3 (Specs B and C) requires restructuring `emulator_step()` to intercept `$C000-$CFFF` before the generic `mem[]` handler. This is a high-risk structural change that affects all subsequent phases.

The current `emulator_step()` flow is:
1. `m6502_tick()` (CPU produces address)
2. Generic `mem[]` read/write (unconditional)
3. `BUS_DECODE` blocks override data bus for specific addresses

After Phase 3 (Specs B/C), the flow becomes:
1. `m6502_tick()`
2. If FB address: read/write `frame_buffer[]`, skip `mem[]`
3. Else: generic `mem[]` read/write
4. Device decode overrides data bus

**Evidence:** Current code (emulator.cpp:130-137):
```cpp
if (BUS_READ) {
    M6502_SET_DATA(pins, mem[addr]);
} else {
    mem[addr] = M6502_GET_DATA(pins);
}
```

After Phase 3 (per Spec C):
```cpp
bool fb_access = (addr >= N8_FB_BASE && addr <= N8_FB_END);
if (fb_access) {
    // frame_buffer[] access
} else {
    // generic mem[] access
}
```

**Recommendation:** If `frame_buffer[]` is introduced in Phase 3, the `emulator_step()` restructuring should be isolated to that phase with thorough regression testing. If deferred to Phase 7 (Spec A), the restructuring is combined with the rendering pipeline, making the phase larger but avoiding early risk. Recommend Phase 3 introduction (Specs B/C) because it forces the bus decode structure to be correct before device registers are added on top.

---

### Finding 3.3 — No phase leaves the system untestable

**Severity:** LOW (informational)

**Finding:** All three specs maintain the invariant that `make test` passes at the end of every phase. During the window between Phase 1 (IRQ move) and Phase 8 (firmware rebuild), the real firmware references wrong addresses, but tests use inline programs and do not depend on the firmware binary. The dual-path ROM loader ensures the emulator can still boot with the old firmware binary during the transition.

**Recommendation:** No action needed. The design is sound.

---

## 4. frame_buffer[] Controversy

### Finding 4.1 — Deferred vs immediate separation

**Severity:** CRITICAL (architectural decision)

**Finding:** The user said "remove frame_buffer[] and just use mem[]." `hardware.md` says "routed to a separate backing store (not backed by main RAM)." These two directives conflict.

**Analysis of consequences:**

**Option A (Spec A): Keep mem[] through Phase 6, introduce frame_buffer[] in Phase 7**

Pros:
- Minimal code changes in early phases
- Tests don't need to change until Phase 7
- Scroll operations in Phase 5 operate on `mem[]` which is simpler

Cons:
- Phase 7 becomes a large, risky phase (rendering pipeline + backing store separation + bus decode restructuring + GDB callback update all at once)
- Video scroll operations in Phase 5 operate on `mem[N8_FB_BASE..]`. When Phase 7 switches to `frame_buffer[]`, all scroll code must be updated
- GDB callbacks work correctly until Phase 7, then need updating

**Option B (Specs B/C): Introduce frame_buffer[] in Phase 3**

Pros:
- Bus decode restructuring is isolated to one small phase
- All subsequent phases (video scroll, keyboard) can assume `frame_buffer[]` exists
- Matches `hardware.md` spec
- GDB callbacks fixed early

Cons:
- More tests change in Phase 3 (T64-T67, T99 update from `mem[]` to `frame_buffer[]`)
- Contradicts user's explicit "use mem[]" directive
- Earlier structural change to `emulator_step()`

**Recommendation:** Adopt Option B (Phase 3 separation). Rationale:
1. The user's "use mem[]" directive was given in the context of removing the _existing_ `frame_buffer[256]` to simplify the code. The expanded 4KB buffer with bus decode intercept is a different architectural element.
2. Deferring to Phase 7 creates a "big bang" phase that combines too many changes.
3. All scroll operations in Phase 5 (video registers) would target `mem[N8_FB_BASE..]` and then need rewriting in Phase 7. This is wasted work.

---

## 5. Bus Decode Correctness

### Finding 5.1 — BUS_DECODE mask for TTY in Spec A is wrong for 32-byte window

**Severity:** HIGH

**Finding:** Spec A Phase 2 uses `BUS_DECODE(addr, N8_TTY_BASE, N8_TTY_MASK)` where `N8_TTY_MASK = 0xFFE0`. The `BUS_DECODE` macro is `if((bus & mask) == base)`. For `base=0xD820` and `mask=0xFFE0`: `0xD820 & 0xFFE0 = 0xD820`. This matches addresses `$D820-$D83F`. Correct.

But the existing `BUS_DECODE` macro at `emulator.cpp:19` is:
```cpp
#define BUS_DECODE(bus, base, mask) if((bus & mask) == base)
```

For `addr=0xD83F`: `0xD83F & 0xFFE0 = 0xD820`. Matches. Correct.
For `addr=0xD840`: `0xD840 & 0xFFE0 = 0xD840 != 0xD820`. Does not match. Correct (this is the video device).

**Evidence:** The mask math is correct for 32-byte windows. No issue.

**Recommendation:** No action needed. The mask math checks out.

---

### Finding 5.2 — Device router slot computation is correct

**Severity:** LOW (informational)

**Finding:** The slot-based router (Specs B/C) computes `slot = (addr - N8_DEV_BASE) >> 5`. For `$D800`: slot=0 (IRQ). For `$D820`: slot=1 (TTY). For `$D840`: slot=2 (Video). For `$D860`: slot=3 (Keyboard). For `$D880-$DFFF`: slots 4-63 (reserved).

`(0xD820 - 0xD800) >> 5 = 0x20 >> 5 = 1`. Correct.
`(0xDFFF - 0xD800) >> 5 = 0x7FF >> 5 = 63`. Correct (last valid slot).

**Evidence:** Math is correct. The router handles 64 possible slots in the 2KB device space.

**Recommendation:** No action needed. Verify `N8_DEV_SIZE = 0x0800` which gives `addr < N8_DEV_BASE + N8_DEV_SIZE` = `addr < 0xE000`. This correctly excludes ROM addresses.

---

### Finding 5.3 — No overlap or gap between regions

**Severity:** LOW (informational)

**Finding:** Verifying the full address space coverage:

| Range | Owner | Size |
|-------|-------|------|
| `$0000-$00FF` | Zero Page | 256 |
| `$0100-$01FF` | Stack | 256 |
| `$0200-$03FF` | Reserved | 512 |
| `$0400-$BFFF` | Program RAM | 48,128 |
| `$C000-$CFFF` | Frame Buffer | 4,096 |
| `$D000-$D7FF` | Dev Bank | 2,048 |
| `$D800-$DFFF` | Device Registers | 2,048 |
| `$E000-$FFFF` | ROM | 8,192 |
| **Total** | | **65,536** |

`256 + 256 + 512 + 48128 + 4096 + 2048 + 2048 + 8192 = 65536`. No gaps, no overlaps.

**Recommendation:** No action needed.

---

### Finding 5.4 — Frame buffer range check uses inclusive end address

**Severity:** LOW

**Finding:** All specs use `addr <= N8_FB_END` where `N8_FB_END = 0xCFFF`. This is correct for an inclusive range `$C000-$CFFF`. However, the device router uses `addr < (N8_DEV_BASE + N8_DEV_SIZE)` which is exclusive. This inconsistency is harmless but worth noting for the implementer.

**Recommendation:** Be consistent. Either use `addr <= N8_FB_END` and `addr <= N8_DEV_END` (inclusive) or use `addr < N8_FB_BASE + N8_FB_SIZE` and `addr < N8_DEV_BASE + N8_DEV_SIZE` (exclusive). Both are correct.

---

## 6. IRQ Mechanism

### Finding 6.1 — IRQ_CLR() every tick clears $D800, requiring all devices to reassert

**Severity:** CRITICAL

**Finding:** The current IRQ mechanism calls `IRQ_CLR()` (which zeros `mem[0x00FF]`) every tick, then `tty_tick()` reasserts the TTY bit if its buffer has data. After migration, `IRQ_CLR()` zeros `mem[$D800]` and `tty_tick()` reasserts.

The keyboard device needs the same reassertion pattern. Without `kbd_tick()`, a keyboard IRQ set by `kbd_inject_key()` (called from the SDL event loop between frames) survives at most until the next CPU tick, when `IRQ_CLR()` zeros it.

**Evidence:** `emulator.cpp:110`: `IRQ_CLR();` then `emulator.cpp:113`: `tty_tick(pins);` reasserts.

Spec A and Spec B keyboard implementations do NOT include `kbd_tick()`. Spec C includes it (after self-correction in Reconciliation Note 11).

**Timeline of a keyboard IRQ without `kbd_tick()`:**
1. SDL event fires between frames. `kbd_inject_key()` calls `emu_set_irq(2)` which sets bit 2 of `mem[$D800]`.
2. Next `emulator_step()`: `IRQ_CLR()` zeros `mem[$D800]`.
3. `tty_tick()` may reassert bit 1 if TTY has data. Keyboard bit 2 is NOT reasserted.
4. IRQ check: `if(mem[$D800] == 0)` -> no IRQ (unless TTY also has data).
5. Keyboard IRQ is lost.

**Recommendation:** This is a functional bug in Specs A and B. All specs MUST include `kbd_tick()` that reasserts `IRQ_BIT_KBD` when `DATA_AVAIL && IRQ_EN`. Add it to `emulator_step()` alongside `tty_tick()`.

---

### Finding 6.2 — IRQ flags at $D800 still backed by mem[]

**Severity:** LOW

**Finding:** All three specs store IRQ flags in `mem[$D800]`. The `IRQ_CLR()` and `IRQ_SET()` macros write to `mem[$D800]`. The device router for slot 0 reads from `mem[$D800]` and returns it on the data bus. This is consistent because `$D800` is within the generic `mem[]` address space (not intercepted like the frame buffer).

However, when `frame_buffer[]` is separated (Phase 3 or Phase 7), the bus decode structure changes. The frame buffer intercept must NOT catch `$D800` (it doesn't -- `$D800` > `$CFFF`). The device router must still run after the generic `mem[]` handler (so that writes to `mem[$D800]` land before the device decode reads them back).

**Evidence:** Spec C Appendix B documents the ordering:
```
a. Frame buffer ($C000-$CFFF): intercept, skip mem[]
b. Generic mem[] read/write (for all other addresses)
c. Device router ($D800-$DFFF): overrides data bus
```

This ordering is correct. The device router runs after `mem[]` writes, so `IRQ_CLR()` writing to `mem[$D800]` is visible to the router's read path.

**Recommendation:** No action needed. The ordering is correct across all specs. Document it clearly in the implementer's notes.

---

### Finding 6.3 — keyboard ACK deasserts IRQ via emu_clr_irq()

**Severity:** MEDIUM

**Finding:** When the keyboard ACK register is written, all three specs call `emu_clr_irq(N8_IRQ_BIT_KBD)`. The current `emu_clr_irq()` implementation (emulator.cpp:57) is:
```cpp
void emu_clr_irq(int bit) {
    mem[0x00FF] = (mem[0x00FF] & ~(0x01 << bit));
}
```

After migration, this clears the keyboard bit in `mem[$D800]`. But `IRQ_CLR()` already zeros ALL bits every tick. So `emu_clr_irq()` is only meaningful if called during the same tick (between `IRQ_CLR()` and the IRQ check). In practice, `kbd_decode()` is called during the device decode phase of `emulator_step()`, which is after `IRQ_CLR()` and after `kbd_tick()` reasserts. So the ACK write clears the bit, and since `DATA_AVAIL` is also cleared, the next tick's `kbd_tick()` will NOT reassert it. The sequence is correct.

**Recommendation:** No action needed. The ACK -> clear AVAIL -> next tick kbd_tick() sees no AVAIL -> no reassertion chain is correct.

---

## 7. cc65 Linker Config

### Finding 7.1 — ROM size math is correct

**Severity:** LOW (informational)

**Finding:** `ROM: start=$E000, size=$2000`. `$E000 + $2000 = $10000`. The ROM occupies `$E000-$FFFF` inclusive. The VECTORS segment is pinned at `$FFFA` which is within the ROM region. Correct.

**Recommendation:** No action needed.

---

### Finding 7.2 — RAM size calculation

**Severity:** LOW

**Finding:** All specs use `RAM: start=$400, size=$BC00`. `$0400 + $BC00 = $C000`. The RAM ends at `$BFFF` inclusive. This matches the proposed memory map. Correct.

The current config has `RAM: start=$200, size=$BD00`. `$0200 + $BD00 = $BF00`. Wait -- this should be `$0200 + $BD00 = $BF00`? No: `$0200 + $BD00 = $BF00`. But the current RAM end should be `$BEFF` per the current memory map. Let me recheck: `$0200 + $BD00 = $BF00 - 1 = $BEFF` for the last accessible byte. Actually, cc65's `size` is the number of bytes: `start=$200`, `size=$BD00` means addresses `$0200` through `$0200+$BD00-1 = $BEFF`. This matches the current map (`$0200-$BEFF`).

New config: `start=$400`, `size=$BC00`. Addresses `$0400` through `$0400+$BC00-1 = $BFFF`. This matches the proposed map (`$0400-$BFFF`). Correct.

**Recommendation:** No action needed. The math is correct.

---

### Finding 7.3 — VECTORS segment pinned at $FFFA inside ROM

**Severity:** LOW (informational)

**Finding:** The VECTORS segment uses `start = $FFFA`. Since ROM is `$E000-$FFFF`, `$FFFA` is within the ROM region. cc65 will place the 6-byte vector table (NMI, RESET, IRQ) at `$FFFA-$FFFF`. The linker will pad the space between the end of CODE/RODATA and `$FFFA` with fill bytes. Correct.

**Recommendation:** No action needed.

---

### Finding 7.4 — Stack pointer overlap with reserved region

**Severity:** LOW

**Finding:** All specs define `__STACKSIZE__ = $0200` (512 bytes). The cc65 runtime initializes the software stack pointer to the end of the BSS/HEAP area and grows downward. The hardware stack is at `$0100-$01FF` (6502 stack page). The cc65 software stack is separate and lives in RAM. No overlap.

**Recommendation:** No action needed. The `__STACKSIZE__` symbol is for cc65's software stack, not the 6502 hardware stack.

---

## 8. Test Coverage Gaps

### Finding 8.1 — No test for writing to ROM addresses via CPU

**Severity:** MEDIUM

**Finding:** Only Spec C includes a test (T175) for ROM write protection ("CPU write to `$E000` is silently ignored"). Specs A and B defer ROM write protection entirely and have no test for it.

**Evidence:** Spec A Reconciliation Notes: "ROM write protection... Deferred." Spec B Phase 6 includes optional ROM write protection code but no specific test ID for it. Spec C Phase 6 includes T175.

**Recommendation:** Include T175 regardless of whether ROM write protection is implemented. If protection is deferred, the test verifies that the CPU can write to ROM addresses (current behavior). If protection is added, the test verifies writes are ignored. Either way, the behavior is documented.

---

### Finding 8.2 — No test for reading unimplemented device registers via CPU program

**Severity:** MEDIUM

**Finding:** All specs test phantom/reserved registers within a device slot (e.g., T143: "Phantom registers $D848-$D85F read 0"). But none test accessing a completely unimplemented device slot (e.g., slot 4 at `$D880` or slot 10 at `$D940`).

**Evidence:** The device router default case returns `$00` on read and ignores writes. But no test exercises this path via a CPU program (only the `video_decode()` and `kbd_decode()` internal functions are tested for phantom registers).

**Recommendation:** Add a test: "CPU read from `$D880` (unimplemented device slot) returns `$00`." This exercises the router's default case through the full bus decode path.

---

### Finding 8.3 — No test for device register write side effects via GDB

**Severity:** LOW

**Finding:** The specs test GDB reads of device registers (T183: "GDB read at $D840 returns value"). But no test verifies that GDB writes to device registers do NOT trigger side effects (e.g., GDB writing `$D844` should not trigger a scroll operation).

**Evidence:** The GDB `gdb_write_mem()` callback writes to `mem[addr]`, not through the device router. So GDB writes to `$D844` (VID_OPER) write to `mem[$D844]` which is not the video register file. The scroll trigger is only in `video_decode()`, which only runs during CPU ticks. This is the correct behavior but it is not tested.

Spec B adds T184: "GDB write at $C000 modifies frame_buffer[0]." But no test for device register GDB writes.

**Recommendation:** Add a test: "GDB write to $D844 does NOT trigger scroll (mem[$D844] changes but frame_buffer[] is unchanged)."

---

### Finding 8.4 — No test for bus contention between frame buffer and device space

**Severity:** LOW

**Finding:** The frame buffer (`$C000-$CFFF`) and device registers (`$D800-$DFFF`) are in non-overlapping regions, so bus contention is not possible. The dev bank (`$D000-$D7FF`) is plain RAM. No contention scenarios exist.

**Recommendation:** No action needed. The address map has no overlapping regions.

---

### Finding 8.5 — Scroll operations with width=0 or height=0

**Severity:** MEDIUM

**Finding:** Spec A's `safe_rows()` guard handles `stride=0` by returning 0 and `h < 2` by skipping the scroll. But what about `width=0` for horizontal scroll? Spec A checks `w < 2` before horizontal scroll. Spec C uses `uint8_t w = vid_regs[N8_VID_WIDTH]` as a `uint8_t`, and `w - 1` would underflow to 255 if `w=0`, passed to `memmove(..., w - 1)`. Spec C checks `active > N8_FB_SIZE` which would catch many edge cases but not all.

**Evidence:** Spec C `video_scroll_left()`:
```cpp
uint8_t w = vid_regs[N8_VID_WIDTH];
// ...
memmove(line, line + 1, w - 1);
```
If `w=0`, then `w - 1 = 255` (uint8_t underflow), and `memmove` copies 255 bytes from `line+1`. If stride*height <= N8_FB_SIZE, the safety clamp passes, but the memmove still reads 255 bytes which could be within bounds but is clearly wrong behavior.

**Recommendation:** All scroll functions should guard against `w < 2` (horizontal) or `h < 2` (vertical). Spec A does this; Specs B and C do not. Add guards. Add a test: "Scroll left with width=0 does not corrupt memory."

---

### Finding 8.6 — No test for emulator_reset() clearing device state

**Severity:** LOW

**Finding:** The specs call `video_reset()` and `keyboard_reset()` from `emulator_reset()`. T193 tests that `emulator_reset()` clears `frame_buffer[]`. But no test verifies that `emulator_reset()` also resets video registers to defaults or clears keyboard state.

**Recommendation:** Add tests: "After emulator_reset(), VID_MODE=0, VID_WIDTH=80" and "After emulator_reset(), KBD_DATA=0, KBD_STATUS=0."

---

## 9. Missing Details

### Finding 9.1 — emu_tty.cpp should include the constants header

**Severity:** LOW

**Finding:** Spec A Phase 0 says to add `#include "machine.h"` to `emu_tty.cpp`. But `emu_tty.cpp` already includes `emulator.h`, and `emu_tty.cpp` does not reference any memory-mapped addresses directly (it uses register offsets 0-3 passed by the bus decode). The only reference to a magic number is `emu_set_irq(1)` / `emu_clr_irq(1)`, which should become `emu_set_irq(N8_IRQ_BIT_TTY)`.

**Evidence:** emu_tty.cpp:53: `emu_set_irq(1);` and emu_tty.cpp:63: `emu_set_irq(1);` and emu_tty.cpp:92: `emu_clr_irq(1);`.

**Recommendation:** Include the constants header in `emu_tty.cpp` and replace the hardcoded `1` with `N8_IRQ_BIT_TTY`. All specs agree on this.

---

### Finding 9.2 — EmulatorFixture does not clear frame_buffer[]

**Severity:** HIGH

**Finding:** When `frame_buffer[]` is introduced as a separate array, the `EmulatorFixture` constructor in `test/test_helpers.h` must be updated to clear it. Currently it only clears `mem[]`:

```cpp
EmulatorFixture() {
    memset(mem, 0, sizeof(uint8_t) * 65536);
    // ...
}
```

After `frame_buffer[]` is introduced, `EmulatorFixture` must also:
```cpp
memset(frame_buffer, 0, N8_FB_SIZE);
fb_dirty = true;
```

None of the specs explicitly mention this update to `EmulatorFixture`.

**Evidence:** test_helpers.h:148: `memset(mem, 0, sizeof(uint8_t) * 65536);` -- no mention of `frame_buffer[]`.

**Recommendation:** Add `frame_buffer[]` clearing to `EmulatorFixture` constructor. Also add `video_reset()` and `keyboard_reset()` calls (after those modules are introduced). Document this in the relevant phase spec.

---

### Finding 9.3 — emulator_show_status_window() still references mem[0x00FF]

**Severity:** LOW

**Finding:** `emulator.cpp:386` displays IRQ status:
```cpp
ImGui::Text(" IRQ: %2d %2d Last PC: %4.4x",
    (pins & M6502_IRQ) == M6502_IRQ,
    (int) (mem[0x00FF] != 0), cur_instruction);
```

After IRQ migration, this should reference `mem[N8_IRQ_ADDR]` (or `mem[N8_IRQ_FLAGS]`). Spec A mentions this in Phase 0, but Specs B and C do not explicitly call it out.

**Evidence:** emulator.cpp line 386: `(int) (mem[0x00FF] != 0)`.

**Recommendation:** All specs should list this as a Phase 0/1 code change. It is easy to miss.

---

### Finding 9.4 — gdb_write_mem for device registers has side-effect risk

**Severity:** MEDIUM

**Finding:** After the frame buffer separation, `gdb_write_mem()` redirects `$C000-$CFFF` to `frame_buffer[]`. But for device registers (`$D800-$DFFF`), `gdb_write_mem()` writes to `mem[addr]`. This means a GDB write to `$D800` (IRQ_FLAGS) modifies `mem[$D800]` directly. On the next tick, `IRQ_CLR()` will zero it anyway. But between the GDB write and the next tick, if the emulator is halted (single-stepping), the IRQ state in `mem[$D800]` may not match what the devices have asserted.

For video registers at `$D840-$D847`, GDB writes to `mem[$D840]` do not update the `vid_regs[]` array. The CPU would read the vid_regs value (via device decode), not `mem[$D840]`. This means GDB cannot inspect or modify video register state via memory reads/writes.

**Evidence:** All three specs note that GDB reads of device registers return `mem[addr]` to avoid side effects. But `mem[$D840]` was never written by the device decode (video registers are in `vid_regs[]`, not `mem[]`). So GDB reads of `$D840` return whatever was last written to `mem[$D840]` by the generic handler (likely stale or zero).

**Recommendation:** Document this limitation. For a debugger, reading video registers via `gdb_read_mem($D840)` will return stale data, not the actual register value. To properly support GDB inspection of device registers, the GDB callbacks would need to call the device decode. This is a future enhancement, not a migration blocker. Add it to the "Open Questions" section.

---

### Finding 9.5 — Makefile changes not specified in detail

**Severity:** LOW

**Finding:** All specs mention adding new `.cpp` files to the Makefile's `SOURCES` and `TEST_SRC_OBJS`, but none provide the exact Makefile diff. The implementer needs to know the variable names and the correct build rules.

**Recommendation:** The implementer should check the existing Makefile patterns. Low risk since compilation errors will surface immediately.

---

### Finding 9.6 — SDL key event processing happens AFTER emulator_step() time slice

**Severity:** MEDIUM

**Finding:** In `main.cpp`, the SDL event loop runs AFTER the emulator time slice (line 383). This means `kbd_inject_key()` is called after the emulator has already run ~13ms worth of ticks. The injected key will be processed in the NEXT frame's emulator time slice. This introduces up to one frame (~13ms) of input latency.

**Evidence:** main.cpp structure:
```
while (!done) {
    gdb_stub_poll();
    // Run emulator for ~13ms  <-- CPU ticks here
    while (SDL_PollEvent(&event)) {
        // Key events processed here <-- kbd_inject_key() here
    }
    // ImGui render
}
```

**Recommendation:** This is inherent to the frame-based architecture and acceptable for a retro emulator. The latency is imperceptible. No action needed, but document it.

---

## 10. GDB Stub Consistency

### Finding 10.1 — GDB memory map XML matches the proposed layout

**Severity:** LOW (informational)

**Finding:** All three specs converge on the same memory map XML:
```xml
<memory type="ram"  start="0x0000" length="0xC000"/>
<memory type="ram"  start="0xC000" length="0x1000"/>
<memory type="ram"  start="0xD000" length="0x0800"/>
<memory type="ram"  start="0xD800" length="0x0800"/>
<memory type="rom"  start="0xE000" length="0x2000"/>
```

Verification:
- `0x0000 + 0xC000 = 0xC000` -> next region starts at `0xC000`. Correct.
- `0xC000 + 0x1000 = 0xD000` -> next region starts at `0xD000`. Correct.
- `0xD000 + 0x0800 = 0xD800` -> next region starts at `0xD800`. Correct.
- `0xD800 + 0x0800 = 0xE000` -> next region starts at `0xE000`. Correct.
- `0xE000 + 0x2000 = 0x10000` -> end of address space. Correct.

Total: `0xC000 + 0x1000 + 0x0800 + 0x0800 + 0x2000 = 0x10000 = 65536`. No gaps, no overlaps.

**Recommendation:** No action needed. The XML is correct.

---

### Finding 10.2 — Current GDB XML has a gap

**Severity:** LOW (informational)

**Finding:** The current `memory_map_xml` in `gdb_stub.cpp` (lines 122-131) has:
```xml
<memory type="ram"  start="0x0000" length="0xC000"/>   <!-- $0000-$BFFF -->
<memory type="ram"  start="0xC000" length="0x0100"/>   <!-- $C000-$C0FF -->
<memory type="ram"  start="0xC100" length="0x0010"/>   <!-- $C100-$C10F -->
<memory type="ram"  start="0xC110" length="0x0EF0"/>   <!-- $C110-$CFFF -->
<memory type="rom"  start="0xD000" length="0x3000"/>   <!-- $D000-$FFFF -->
```

Total: `0xC000 + 0x0100 + 0x0010 + 0x0EF0 + 0x3000 = 0x10000`. This is correct (no gaps). The new XML consolidates the `$C000-$CFFF` range into a single region and splits the `$D000-$FFFF` range into three regions.

**Recommendation:** No action needed. Both current and proposed XML are correct.

---

## Summary of Critical and High Findings

| # | Severity | Finding |
|---|----------|---------|
| 1.1 | HIGH | Constants header file name not agreed |
| 1.2 | HIGH | Device router vs BUS_DECODE not agreed |
| 1.3 | CRITICAL | frame_buffer[] introduction phase not agreed |
| 1.6 | HIGH | Phase ordering not agreed |
| 1.10 | CRITICAL | kbd_tick() missing from Specs A and B (keyboard IRQ lost every tick) |
| 3.2 | HIGH | Phase 3 frame_buffer[] restructures emulator_step() |
| 4.1 | CRITICAL | frame_buffer[] timing is the central architectural decision |
| 6.1 | CRITICAL | Keyboard IRQ lost without kbd_tick() (same as 1.10) |
| 8.5 | MEDIUM | Scroll with width=0 causes uint8_t underflow in memmove |
| 9.2 | HIGH | EmulatorFixture does not clear frame_buffer[] after separation |

**Bottom line:** The three specs are ~85% aligned after revision. The critical unresolved issues are: (1) when to introduce `frame_buffer[]`, (2) whether to use a device router or BUS_DECODE macros, and (3) the missing `kbd_tick()` in Specs A and B. Issue (3) is a functional bug that will cause keyboard IRQs to be lost. These must be resolved before implementation begins.
